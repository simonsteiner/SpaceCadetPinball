#include "pch.h"
#include "render.h"

#include "fullscrn.h"
#include "GroupData.h"
#include "options.h"
#include "pb.h"
#include "score.h"
#include "TPinballTable.h"
#include "winmain.h"
#include "DebugOverlay.h"
#include "proj.h"
#include "PostEffect.h"
#include "Compositor.h"
#include "Upscale.h"

std::vector<render_sprite*> render::sprite_list, render::ball_list;
zmap_header_type* render::background_zmap;
int render::zmap_offsetX, render::zmap_offsetY, render::offset_x, render::offset_y;
rectangle_type render::vscreen_rect;
gdrv_bitmap8 *render::vscreen, *render::background_bitmap, *render::ball_bitmap[20];
zmap_header_type* render::zscreen;
SDL_Rect render::BoardRect{}, render::SidebarRect{};
SDL_Texture* render::BackdropTexture = nullptr;
bool render::BallsInterpolated = false;
gdrv_bitmap8* render::InitialBackground = nullptr;
gdrv_bitmap8 *render::vscreenHi = nullptr, *render::ball_bitmap_hi[20]{};
zmap_header_type* render::zscreenHi = nullptr;
int render::HiScale = 0;
std::vector<ColorRgba> render::SidebarCopy{};
SDL_Texture* render::DepthTexture = nullptr;
rectangle_type render::DepthChanged{};
bool render::DepthChangedAll = true;
std::vector<ColorRgba> render::DepthRows{};

#if SDL_VERSION_ATLEAST(3, 4, 0)
// The same as nearest at whole multiples; at other scales it keeps every table pixel the same size
static constexpr SDL_ScaleMode UnfilteredScaleMode = SDL_SCALEMODE_PIXELART;
#else
static constexpr SDL_ScaleMode UnfilteredScaleMode = SDL_SCALEMODE_NEAREST;
#endif

render_sprite::render_sprite(VisualTypes visualType, gdrv_bitmap8* bmp, zmap_header_type* zMap,
	int xPosition, int yPosition, rectangle_type* boundingRect)
{
	Bmp = bmp;
	ZMap = zMap;
	VisualType = visualType;
	DeleteFlag = false;
	OccludedSprites = nullptr;
	DirtyRect = rectangle_type{};
	DirtyFlag = visualType != VisualTypes::Ball;
	ZMapOffestX = 0;
	ZMapOffestY = 0;
	Depth = 0xffFF;

	if (boundingRect)
	{
		BoundingRect = *boundingRect;
	}
	else
	{
		BoundingRect.Width = -1;
		BoundingRect.Height = -1;
		BoundingRect.XPosition = 0;
		BoundingRect.YPosition = 0;
	}

	BmpRect.XPosition = xPosition;
	BmpRect.YPosition = yPosition;
	if (bmp)
	{
		BmpRect.Width = bmp->Width;
		BmpRect.Height = bmp->Height;
	}
	else
	{
		BmpRect.Width = 0;
		BmpRect.Height = 0;
	}
	DirtyRectPrev = BmpRect;

	if (!ZMap && VisualType != VisualTypes::Ball)
	{
		assertm(false, "Background zMap should not be used");
		ZMap = render::background_zmap;
		ZMapOffestY = xPosition - render::zmap_offsetX;
		ZMapOffestX = yPosition - render::zmap_offsetY;
	}

	render::AddSprite(*this);
}

render_sprite::~render_sprite()
{
	render::RemoveSprite(*this);
	delete OccludedSprites;
}

void render_sprite::set(gdrv_bitmap8* bmp, zmap_header_type* zMap, int xPos, int yPos)
{
	if (Bmp == bmp && ZMap == zMap && BmpRect.XPosition == xPos && BmpRect.YPosition == yPos)
		return;

	Bmp = bmp;
	ZMap = zMap;
	DirtyFlag = VisualType != VisualTypes::Ball;
	BmpRect.XPosition = xPos;
	BmpRect.YPosition = yPos;
	if (bmp) 
	{
		BmpRect.Width = bmp->Width;
		BmpRect.Height = bmp->Height;
	}
}

void render_sprite::ball_set(gdrv_bitmap8* bmp, float depth, int xPos, int yPos)
{
	set(bmp, ZMap,xPos, yPos);
	Depth = proj::NormalizeDepth(depth);
}

void render::init(gdrv_bitmap8* bmp, int width, int height)
{
	vscreen = new gdrv_bitmap8(width, height, false);
	zscreen = new zmap_header_type(width, height, width);
	zdrv::fill(zscreen, zscreen->Width, zscreen->Height, 0, 0, 0xFFFF);
	vscreen_rect.YPosition = 0;
	vscreen_rect.XPosition = 0;
	vscreen_rect.Width = width;
	vscreen_rect.Height = height;
	vscreen->YPosition = 0;
	vscreen->XPosition = 0;
	for (auto& ballBmp : ball_bitmap)
		ballBmp = new gdrv_bitmap8(64, 64, false);

	background_bitmap = bmp;
	DepthChangedAll = true;
	if (bmp)
		gdrv::copy_bitmap(vscreen, width, height, 0, 0, bmp, 0, 0);
	else
		gdrv::fill_bitmap(vscreen, vscreen->Width, vscreen->Height, 0, 0, 0);

	recreate_screen_texture();
}

void render::uninit()
{
	SetUpscale(0);
	Upscale::Clear();
	InitialBackground = nullptr;
	delete vscreen;
	delete zscreen;

	// Sprite destructor removes it from the list.
	while (!sprite_list.empty())
		delete sprite_list[0];
	while (!ball_list.empty())
		delete ball_list[0];
	for (auto& ballBmp : ball_bitmap)
		delete ballBmp;
	DebugOverlay::UnInit();
	if (BackdropTexture)
		SDL_DestroyTexture(BackdropTexture);
	BackdropTexture = nullptr;
	if (DepthTexture)
		SDL_DestroyTexture(DepthTexture);
	DepthTexture = nullptr;
	DepthRows.clear();
}

void render::recreate_screen_texture()
{
	auto scaleMode = options::Options.LinearFiltering ? SDL_SCALEMODE_LINEAR : UnfilteredScaleMode;
	vscreen->CreateTexture(scaleMode, SDL_TEXTUREACCESS_STREAMING);
	if (vscreenHi)
		vscreenHi->CreateTexture(scaleMode, SDL_TEXTUREACCESS_STREAMING);
}

void render::update()
{
	unpaint_balls();

	// Clip dirty sprites with vScreen, clear clipping (dirty) rectangles
	for (const auto sprite : sprite_list)
	{
		if (!sprite->DirtyFlag)
			continue;

		bool clearSprite = false;
		switch (sprite->VisualType)
		{
		case VisualTypes::Sprite:
			if (sprite->DirtyRectPrev.Width > 0)
				maths::enclosing_box(sprite->DirtyRectPrev, sprite->BmpRect, sprite->DirtyRect);

			if (maths::rectangle_clip(sprite->DirtyRect, vscreen_rect, &sprite->DirtyRect))
				clearSprite = true;
			else
				sprite->DirtyRect.Width = -1;
			break;
		case VisualTypes::Background:
			if (maths::rectangle_clip(sprite->BmpRect, vscreen_rect, &sprite->DirtyRect))
				clearSprite = !sprite->Bmp;
			else
				sprite->DirtyRect.Width = -1;
			break;
		default: break;
		}

		if (clearSprite)
		{
			auto yPos = sprite->DirtyRect.YPosition;
			auto width = sprite->DirtyRect.Width;
			auto xPos = sprite->DirtyRect.XPosition;
			auto height = sprite->DirtyRect.Height;
			zdrv::fill(zscreen, width, height, xPos, yPos, 0xFFFF);
			if (background_bitmap)
				gdrv::copy_bitmap(vscreen, width, height, xPos, yPos, background_bitmap, xPos, yPos);
			else
				gdrv::fill_bitmap(vscreen, width, height, xPos, yPos, 0);
			if (HiScale)
			{
				auto s = HiScale;
				zdrv::fill(zscreenHi, width * s, height * s, xPos * s, yPos * s, 0xFFFF);
				if (background_bitmap)
					gdrv::copy_bitmap(vscreenHi, width * s, height * s, xPos * s, yPos * s,
									  Upscale::Bitmap(background_bitmap), xPos * s, yPos * s);
				else
					gdrv::fill_bitmap(vscreenHi, width * s, height * s, xPos * s, yPos * s, 0);
			}
		}
	}

	// Paint dirty rectangles of dirty sprites
	for (auto sprite : sprite_list)
	{
		if (!sprite->DirtyFlag)
			continue;

		repaint(*sprite);
		if (sprite->DirtyRect.Width > 0)
			DepthChange(sprite->DirtyRect);
		sprite->DirtyFlag = false;
		sprite->DirtyRectPrev = sprite->DirtyRect;
		if (sprite->DeleteFlag)
			delete sprite;
	}

	paint_balls();

	for (auto ball : ball_list)
	{
		auto& interp = ball->Interp;
		interp.PrevPaintedRect = interp.PaintedRect;
		interp.PrevPainted = interp.Painted;
		interp.PaintedRect = ball->BmpRect;
		interp.Painted = ball->Bmp != nullptr;
	}
}

void render::InterpolateBalls(float alpha)
{
	// Moves the balls on the frame to where they were between the last two updates, alpha of the way to the last.
	// Only the picture changes: the table's own ball positions are untouched, and RestoreBalls puts the frame back.
	if (BallsInterpolated || ball_list.empty())
		return;

	unpaint_balls();
	for (auto ball : ball_list)
	{
		auto& interp = ball->Interp;
		interp.SavedBmpRect = ball->BmpRect;
		if (!interp.Painted || !interp.PrevPainted)
			continue;

		// A ball that jumped, such as into a hole or back to the plunger, isn't drawn in between
		auto& from = interp.PrevPaintedRect;
		auto& to = interp.PaintedRect;
		constexpr int MaxStep = 24;
		if (std::abs(to.XPosition - from.XPosition) > MaxStep || std::abs(to.YPosition - from.YPosition) > MaxStep)
			continue;
		ball->BmpRect.XPosition =
			static_cast<int>(std::lround(from.XPosition + (to.XPosition - from.XPosition) * alpha));
		ball->BmpRect.YPosition =
			static_cast<int>(std::lround(from.YPosition + (to.YPosition - from.YPosition) * alpha));
	}
	paint_balls();
	BallsInterpolated = true;
}

void render::RestoreBalls()
{
	if (!BallsInterpolated)
		return;

	unpaint_balls();
	for (auto ball : ball_list)
		ball->BmpRect = ball->Interp.SavedBmpRect;
	paint_balls();
	BallsInterpolated = false;
}

void render::AddSprite(render_sprite& sprite)
{
	auto& list = sprite.VisualType == VisualTypes::Ball ? ball_list : sprite_list;
	list.push_back(&sprite);
}

void render::RemoveSprite(render_sprite& sprite)
{
	auto& list = sprite.VisualType == VisualTypes::Ball ? ball_list : sprite_list;
	auto it = std::find(list.begin(), list.end(), &sprite);
	if (it != list.end())
		list.erase(it);
}

void render::set_background_zmap(zmap_header_type* zMap, int offsetX, int offsetY)
{
	background_zmap = zMap;
	zmap_offsetX = offsetX;
	zmap_offsetY = offsetY;
}

void render::repaint(const render_sprite& sprite)
{
	rectangle_type clipRect{};
	if (!sprite.OccludedSprites || sprite.VisualType == VisualTypes::Ball || sprite.DirtyRect.Width <= 0)
		return;

	for (auto refSprite : *sprite.OccludedSprites)
	{
		if (!refSprite->DeleteFlag && refSprite->Bmp)
		{
			if (maths::rectangle_clip(refSprite->BmpRect, sprite.DirtyRect, &clipRect))
				zdrv::paint(
					clipRect.Width,
					clipRect.Height,
					vscreen,
					clipRect.XPosition,
					clipRect.YPosition,
					zscreen,
					clipRect.XPosition,
					clipRect.YPosition,
					refSprite->Bmp,
					clipRect.XPosition - refSprite->BmpRect.XPosition,
					clipRect.YPosition - refSprite->BmpRect.YPosition,
					refSprite->ZMap,
					clipRect.XPosition + refSprite->ZMapOffestY - refSprite->BmpRect.XPosition,
					clipRect.YPosition + refSprite->ZMapOffestX - refSprite->BmpRect.YPosition);
			if (HiScale && maths::rectangle_clip(refSprite->BmpRect, sprite.DirtyRect, &clipRect))
				PaintHi(*refSprite, clipRect);
		}
	}
}

void render::PaintHi(const render_sprite& sprite, const rectangle_type& clipRect)
{
	// The same paint as at 1x, with every coordinate and the art HiScale times larger
	auto s = HiScale;
	zdrv::paint(clipRect.Width * s, clipRect.Height * s, vscreenHi, clipRect.XPosition * s, clipRect.YPosition * s,
				zscreenHi, clipRect.XPosition * s, clipRect.YPosition * s, Upscale::Bitmap(sprite.Bmp),
				(clipRect.XPosition - sprite.BmpRect.XPosition) * s,
				(clipRect.YPosition - sprite.BmpRect.YPosition) * s, Upscale::ZMap(sprite.ZMap),
				(clipRect.XPosition + sprite.ZMapOffestY - sprite.BmpRect.XPosition) * s,
				(clipRect.YPosition + sprite.ZMapOffestX - sprite.BmpRect.YPosition) * s);
}


void render::paint_balls()
{
	// Sort ball sprites by ascending depth
	std::sort(ball_list.begin(), ball_list.end(), [](const render_sprite* lhs, const render_sprite* rhs)
	{
		return lhs->Depth < rhs->Depth;
	});

	// For balls that clip vScreen: save original vScreen contents and paint ball bitmap.
	for (auto index = 0u; index < ball_list.size(); ++index)
	{
		auto ball = ball_list[index];
		auto dirty = &ball->DirtyRect;
		if (ball->Bmp && maths::rectangle_clip(ball->BmpRect, vscreen_rect, &ball->DirtyRect))
		{
			int xPos = dirty->XPosition;
			int yPos = dirty->YPosition;
			gdrv::copy_bitmap(ball_bitmap[index], dirty->Width, dirty->Height, 0, 0, vscreen, xPos, yPos);
			zdrv::paint_flat(
				dirty->Width,
				dirty->Height,
				vscreen,
				xPos,
				yPos,
				zscreen,
				xPos,
				yPos,
				ball->Bmp,
				xPos - ball->BmpRect.XPosition,
				yPos - ball->BmpRect.YPosition,
				ball->Depth);
		}
		else
		{
			dirty->Width = -1;
		}
	}

	if (HiScale)
		paint_balls_hi();
}

void render::paint_balls_hi()
{
	// After paint_balls, so the list is sorted the same way
	auto s = HiScale;
	for (auto index = 0u; index < ball_list.size(); ++index)
	{
		auto ball = ball_list[index];
		auto& dirty = ball->DirtyRectHi;
		if (ball->Bmp && ball->DirtyRect.Width > 0)
		{
			auto& lowDirty = ball->DirtyRect;
			dirty = {lowDirty.XPosition * s, lowDirty.YPosition * s, lowDirty.Width * s, lowDirty.Height * s};
			gdrv::copy_bitmap(ball_bitmap_hi[index], dirty.Width, dirty.Height, 0, 0, vscreenHi, dirty.XPosition,
							  dirty.YPosition);
			zdrv::paint_flat(dirty.Width, dirty.Height, vscreenHi, dirty.XPosition, dirty.YPosition, zscreenHi,
							 dirty.XPosition, dirty.YPosition, Upscale::Bitmap(ball->Bmp),
							 dirty.XPosition - ball->BmpRect.XPosition * s,
							 dirty.YPosition - ball->BmpRect.YPosition * s, ball->Depth);
		}
		else
		{
			dirty.Width = -1;
		}
	}
}

void render::unpaint_balls()
{
	// Restore portions of vScreen saved during previous paint_balls call.
	for (int index = static_cast<int>(ball_list.size()) - 1; index >= 0 && HiScale; index--)
	{
		auto& dirty = ball_list[index]->DirtyRectHi;
		if (dirty.Width > 0)
			gdrv::copy_bitmap(vscreenHi, dirty.Width, dirty.Height, dirty.XPosition, dirty.YPosition,
							  ball_bitmap_hi[index], 0, 0);
	}
	for (int index = static_cast<int>(ball_list.size()) - 1; index >= 0; index--)
	{
		auto curBall = ball_list[index];
		if (curBall->DirtyRect.Width > 0)
			gdrv::copy_bitmap(
				vscreen,
				curBall->DirtyRect.Width,
				curBall->DirtyRect.Height,
				curBall->DirtyRect.XPosition,
				curBall->DirtyRect.YPosition,
				ball_bitmap[index],
				0,
				0);

		curBall->DirtyRectPrev = curBall->DirtyRect;
	}
}

void render::shift(int offsetX, int offsetY)
{
	offset_x += offsetX;
	offset_y += offsetY;
}

void render::build_occlude_list()
{
	std::vector<render_sprite*>* spriteArr = nullptr;
	for (auto mainSprite : sprite_list)
	{
		if (mainSprite->OccludedSprites)
		{
			delete mainSprite->OccludedSprites;
			mainSprite->OccludedSprites = nullptr;
		}

		if (!mainSprite->DeleteFlag && mainSprite->BoundingRect.Width != -1)
		{
			if (!spriteArr)
				spriteArr = new std::vector<render_sprite*>();

			for (auto refSprite : sprite_list)
			{
				if (!refSprite->DeleteFlag
					&& refSprite->BoundingRect.Width != -1
					&& maths::rectangle_clip(mainSprite->BoundingRect, refSprite->BoundingRect, nullptr))
				{
					spriteArr->push_back(refSprite);
				}
			}

			if (mainSprite->Bmp && spriteArr->size() < 2)
				spriteArr->clear();
			if (!spriteArr->empty())
			{
				mainSprite->OccludedSprites = spriteArr;
				spriteArr = nullptr;
			}
		}
	}

	delete spriteArr;
}

void render::SpriteViewer(bool* show)
{
	static const char* BitmapTypes[] =
	{
		"None",
		"RawBitmap",
		"DibBitmap",
		"Spliced",
	};
	static float scale = 1.0f;
	auto uv_min = ImVec2(0.0f, 0.0f); // Top-left
	auto uv_max = ImVec2(1.0f, 1.0f); // Lower-right

	if (ImGui::Begin("Sprite viewer", show, ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar))
	{
		// Images get a 50% opaque white border
		ImGui::PushStyleVar(ImGuiStyleVar_ImageBorderSize, 1.0f);
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0f, 1.0f, 1.0f, 0.5f));
		if (ImGui::BeginMenuBar())
		{
			ImGui::SliderFloat("Sprite scale", &scale, 0.1f, 10.0f, "scale = %.3f");
			ImGui::EndMenuBar();
		}

		// The renderer backend samples linearly by default; keep the previews' own nearest scale mode
		auto drawList = ImGui::GetWindowDrawList();
		drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_SetSamplerFromTex, nullptr);
		for (const auto group : pb::record_table->Groups)
		{
			bool emptyGroup = true;
			for (int i = 0; i <= 2; i++)
			{
				auto bmp = group->GetBitmap(i);
				if (bmp)
				{
					emptyGroup = false;
					break;
				}
			}
			if (emptyGroup)
				continue;

			ImGui::Text("Group: %d, name:%s", group->GroupId, group->GroupName.c_str());
			for (int i = 0; i <= 2; i++)
			{
				auto bmp = group->GetBitmap(i);
				if (!bmp)
					continue;

				auto type = BitmapTypes[static_cast<uint8_t>(bmp->BitmapType)];
				ImGui::Text("type:%s, size:%d, resolution: %dx%d, offset:%dx%d", type,
				            bmp->Resolution,
				            bmp->Width, bmp->Height, bmp->XPosition, bmp->YPosition);
			}

			for (int same = 0, i = 0; i <= 2; i++)
			{
				auto bmp = group->GetBitmap(i);
				if (!bmp)
					continue;

				gdrv::CreatePreview(*bmp);
				if (bmp->Texture)
				{
					if (!same)
						same = true;
					else
						ImGui::SameLine();

					ImGui::Image(reinterpret_cast<ImTextureID>(bmp->Texture),
					             ImVec2(bmp->Width * scale, bmp->Height * scale), uv_min, uv_max);
				}
			}

			for (int same = 0, i = 0; i <= 2; i++)
			{
				auto zMap = group->GetZMap(i);
				if (!zMap)
					continue;

				zdrv::CreatePreview(*zMap);
				if (zMap->Texture)
				{
					if (!same)
						same = true;
					else
						ImGui::SameLine();
					ImGui::Image(reinterpret_cast<ImTextureID>(zMap->Texture),
					             ImVec2(zMap->Width * scale, zMap->Height * scale), uv_min, uv_max);
				}
			}
		}
		drawList->AddCallback(ImGui::GetPlatformIO().DrawCallback_ResetRenderState, nullptr);
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
	ImGui::End();
}

void render::PresentVScreen()
{
	auto screen = vscreen;
	if (HiScale)
	{
		RefreshSidebarHi();
		screen = vscreenHi;
	}
	screen->BlitToTexture();

	DrawBackdrop();
	// Space Cadet 2026 composes the table on the GPU when it can, into a texture drawn as the frame would be
	auto scale = HiScale ? HiScale : 1;
	auto table = screen->Texture;
	if (options::GetEdition() == Editions::Definitive && Compositor::Available())
	{
		if (auto composed = Compositor::Compose(screen->Texture, UpdateDepthTexture(), scale))
			table = composed;
	}
	// Only the table goes through the post-processing effect, not the backdrop or the overlays
	PostEffect::Begin(static_cast<float>(screen->Width), static_cast<float>(screen->Height),
					  fullscrn::ScaleX / static_cast<float>(scale), fullscrn::ScaleY / static_cast<float>(scale));
	DrawSplit(table, static_cast<float>(offset_x), static_cast<float>(offset_y), scale);
	PostEffect::End();

	if (options::Options.DebugOverlay)
	{
		DebugOverlay::DrawOverlay();
	}
}

int render::BoardWidth()
{
	// The table is the left part of the frame, the score panel the rest
	return pb::MainTable ? pb::MainTable->Width : vscreen ? vscreen->Width : 0;
}

void render::DrawSplit(SDL_Texture* texture, float offsetX, float offsetY, int textureScale)
{
	// Table and panel are drawn separately: the nudge shifts only the table, and the stacked layout moves the panel.
	// The texture is the frame at textureScale times its size.
	auto scale = static_cast<float>(textureScale);
	auto boardWidth = static_cast<float>(BoardWidth()) * scale;
	auto height = static_cast<float>(vscreen->Height) * scale;
	auto width = static_cast<float>(vscreen->Width) * scale;
	auto srcBoardRect = SDL_FRect{0, 0, boardWidth, height};
	auto srcSidebarRect = SDL_FRect{boardWidth, 0, width - boardWidth, height};

	SDL_FRect dstBoardRect, dstSidebarRect;
	SDL_RectToFRect(&BoardRect, &dstBoardRect);
	SDL_RectToFRect(&SidebarRect, &dstSidebarRect);
	dstBoardRect.x += offsetX * fullscrn::ScaleX;
	dstBoardRect.y += offsetY * fullscrn::ScaleY;

	SDL_RenderTexture(winmain::Renderer, texture, &srcBoardRect, &dstBoardRect);
	// Space Cadet 2026 draws its own score panel in the panel's place
	if (srcSidebarRect.w > 0 && options::GetEdition() != Editions::Definitive)
		SDL_RenderTexture(winmain::Renderer, texture, &srcSidebarRect, &dstSidebarRect);
}

void render::DrawBackdrop()
{
	if (!options::Options.Backdrop)
		return;
	if (!BackdropTexture)
		CreateBackdrop();
	if (!BackdropTexture)
		return;

	// Cover the whole window, keeping the art's proportions
	int width, height;
	float textureWidth, textureHeight;
	SDL_GetRenderOutputSize(winmain::Renderer, &width, &height);
	SDL_GetTextureSize(BackdropTexture, &textureWidth, &textureHeight);
	auto scale = std::max(width / textureWidth, height / textureHeight);
	auto dstRect = SDL_FRect{(width - textureWidth * scale) / 2, (height - textureHeight * scale) / 2,
							 textureWidth * scale, textureHeight * scale};
	SDL_RenderTexture(winmain::Renderer, BackdropTexture, nullptr, &dstRect);
}

void render::CreateBackdrop()
{
	// A blurred, darkened copy of the table as drawn the first time the backdrop is shown, from the player's own data.
	// Averaging blocks of pixels and scaling the result up with linear filtering blurs it.
	constexpr int Block = 16;
	constexpr unsigned Brightness = 90; // Out of 255
	auto boardWidth = BoardWidth();
	auto width = std::max(1, boardWidth / Block), height = std::max(1, vscreen->Height / Block);
	std::vector<ColorRgba> pixels(width * height);
	for (int y = 0; y < height; y++)
	{
		for (int x = 0; x < width; x++)
		{
			unsigned red = 0, green = 0, blue = 0;
			for (int j = 0; j < Block; j++)
			{
				auto src = &vscreen->BmpBufPtr1[(y * Block + j) * vscreen->Stride + x * Block];
				for (int i = 0; i < Block; i++, src++)
				{
					red += src->GetRed();
					green += src->GetGreen();
					blue += src->GetBlue();
				}
			}
			constexpr unsigned divisor = Block * Block * 255;
			pixels[y * width + x] = ColorRgba{static_cast<uint8_t>(red * Brightness / divisor),
											  static_cast<uint8_t>(green * Brightness / divisor),
											  static_cast<uint8_t>(blue * Brightness / divisor), 255};
		}
	}

	BackdropTexture =
		SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC, width, height);
	if (!BackdropTexture)
		return;
	SDL_SetTextureScaleMode(BackdropTexture, SDL_SCALEMODE_LINEAR);
	SDL_SetTextureBlendMode(BackdropTexture, SDL_BLENDMODE_NONE);
	SDL_UpdateTexture(BackdropTexture, nullptr, pixels.data(), width * static_cast<int>(sizeof(ColorRgba)));
}

void render::SetUpscale(int factor)
{
	// factor 0 turns the upscaled frame off; the 1x frame is always kept, because the panel's text is drawn into it
	if (factor == HiScale)
		return;

	delete vscreenHi;
	delete zscreenHi;
	for (auto& ballBmp : ball_bitmap_hi)
	{
		delete ballBmp;
		ballBmp = nullptr;
	}
	vscreenHi = nullptr;
	zscreenHi = nullptr;
	HiScale = 0;
	SidebarCopy.clear();
	if (factor < Upscale::MinFactor || !vscreen)
	{
		// Off, the upscaled art is freed too; turning it back on makes it again
		Upscale::Clear();
		return;
	}

	Upscale::Init(factor);
	HiScale = factor;
	vscreenHi = new gdrv_bitmap8(vscreen->Width * factor, vscreen->Height * factor, false);
	zscreenHi = new zmap_header_type(vscreenHi->Width, vscreenHi->Height, vscreenHi->Width);
	for (auto& ballBmp : ball_bitmap_hi)
		ballBmp = new gdrv_bitmap8(64 * factor, 64 * factor, false);

	// Compose the whole frame once: the background the table started with, then every sprite and the balls
	zdrv::fill(zscreenHi, zscreenHi->Width, zscreenHi->Height, 0, 0, 0xFFFF);
	if (InitialBackground)
	{
		auto bmp = Upscale::Bitmap(InitialBackground);
		gdrv::copy_bitmap(vscreenHi, bmp->Width, bmp->Height, InitialBackground->XPosition * factor,
						  InitialBackground->YPosition * factor, bmp, 0, 0);
	}
	else
		gdrv::fill_bitmap(vscreenHi, vscreenHi->Width, vscreenHi->Height, 0, 0, 0);
	rectangle_type clipRect{};
	for (auto sprite : sprite_list)
	{
		if (!sprite->DeleteFlag && sprite->Bmp && sprite->VisualType != VisualTypes::Ball &&
			maths::rectangle_clip(sprite->BmpRect, vscreen_rect, &clipRect))
			PaintHi(*sprite, clipRect);
	}
	paint_balls_hi();
	DepthChangedAll = true;
	recreate_screen_texture();
}

void render::RefreshSidebarHi()
{
	// Text boxes and score digits draw straight into the 1x frame, so the panel is magnified from there when it changes
	auto boardWidth = BoardWidth();
	auto width = vscreen->Width - boardWidth;
	if (width <= 0)
		return;

	auto size = static_cast<size_t>(width) * vscreen->Height;
	auto changed = SidebarCopy.size() != size;
	SidebarCopy.resize(size);
	for (int y = 0; y < vscreen->Height; y++)
	{
		auto src = &vscreen->BmpBufPtr1[y * vscreen->Stride + boardWidth];
		auto dst = &SidebarCopy[y * width];
		if (!changed && memcmp(src, dst, width * sizeof(ColorRgba)) == 0)
			continue;
		changed = true;
		std::copy_n(src, width, dst);
	}
	if (changed)
		Upscale::Region(*vscreen, boardWidth, 0, width, vscreen->Height, *vscreenHi);
}

void render::DepthChange(const rectangle_type& rect)
{
	if (DepthChanged.Width > 0)
		maths::enclosing_box(DepthChanged, rect, DepthChanged);
	else
		DepthChanged = rect;
}

SDL_Texture* render::UpdateDepthTexture()
{
	// The depth map of the frame on show, upscaled or not; only what changed since the last call goes up
	auto zmap = HiScale ? zscreenHi : zscreen;
	auto scale = HiScale ? HiScale : 1;
	if (!zmap)
		return nullptr;

	float width = 0, height = 0;
	if (DepthTexture)
		SDL_GetTextureSize(DepthTexture, &width, &height);
	if (!DepthTexture || static_cast<int>(width) != zmap->Width || static_cast<int>(height) != zmap->Height)
	{
		if (DepthTexture)
			SDL_DestroyTexture(DepthTexture);
		DepthTexture = SDL_CreateTexture(winmain::Renderer, SDL_PIXELFORMAT_BGRA32, SDL_TEXTUREACCESS_STATIC,
										 zmap->Width, zmap->Height);
		if (!DepthTexture)
			return nullptr;
		SDL_SetTextureScaleMode(DepthTexture, SDL_SCALEMODE_NEAREST);
		SDL_SetTextureBlendMode(DepthTexture, SDL_BLENDMODE_NONE);
		DepthChangedAll = true;
	}

	SDL_Rect rect;
	if (DepthChangedAll)
		rect = {0, 0, zmap->Width, zmap->Height};
	else if (DepthChanged.Width > 0)
	{
		rect = {DepthChanged.XPosition * scale, DepthChanged.YPosition * scale, DepthChanged.Width * scale,
				DepthChanged.Height * scale};
		SDL_Rect bounds{0, 0, zmap->Width, zmap->Height};
		if (!SDL_GetRectIntersection(&rect, &bounds, &rect))
			rect = {};
	}
	else
		rect = {};
	DepthChangedAll = false;
	DepthChanged = {};
	if (rect.w <= 0 || rect.h <= 0)
		return DepthTexture;

	// Red holds the high byte of each depth and green the low byte
	DepthRows.resize(static_cast<size_t>(rect.w) * rect.h);
	for (int y = 0; y < rect.h; y++)
	{
		auto src = &zmap->ZPtr1[(rect.y + y) * zmap->Stride + rect.x];
		auto dst = &DepthRows[static_cast<size_t>(y) * rect.w];
		for (int x = 0; x < rect.w; x++)
		{
			auto depth = static_cast<unsigned>(src[x]);
			dst[x] = ColorRgba{static_cast<uint8_t>(depth >> 8u), static_cast<uint8_t>(depth & 0xFFu), 0, 255};
		}
	}
	SDL_UpdateTexture(DepthTexture, &rect, DepthRows.data(), rect.w * static_cast<int>(sizeof(ColorRgba)));
	return DepthTexture;
}
