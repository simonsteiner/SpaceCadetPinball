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

std::vector<render_sprite*> render::sprite_list, render::ball_list;
zmap_header_type* render::background_zmap;
int render::zmap_offsetX, render::zmap_offsetY, render::offset_x, render::offset_y;
rectangle_type render::vscreen_rect;
gdrv_bitmap8 *render::vscreen, *render::background_bitmap, *render::ball_bitmap[20];
zmap_header_type* render::zscreen;
SDL_Rect render::DestinationRect{}, render::BoardRect{}, render::SidebarRect{};
SDL_Texture* render::BackdropTexture = nullptr;

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
	if (bmp)
		gdrv::copy_bitmap(vscreen, width, height, 0, 0, bmp, 0, 0);
	else
		gdrv::fill_bitmap(vscreen, vscreen->Width, vscreen->Height, 0, 0, 0);

	recreate_screen_texture();
}

void render::uninit()
{
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
}

void render::recreate_screen_texture()
{
	vscreen->CreateTexture(options::Options.LinearFiltering ? SDL_SCALEMODE_LINEAR : UnfilteredScaleMode,
						   SDL_TEXTUREACCESS_STREAMING);
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
		}
	}

	// Paint dirty rectangles of dirty sprites
	for (auto sprite : sprite_list)
	{
		if (!sprite->DirtyFlag)
			continue;

		repaint(*sprite);
		sprite->DirtyFlag = false;
		sprite->DirtyRectPrev = sprite->DirtyRect;
		if (sprite->DeleteFlag)
			delete sprite;
	}

	paint_balls();
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
		}
	}
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
}

void render::unpaint_balls()
{
	// Restore portions of vScreen saved during previous paint_balls call.
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
		ImGui::PopStyleColor();
		ImGui::PopStyleVar();
	}
	ImGui::End();
}

void render::PresentVScreen()
{
	vscreen->BlitToTexture();

	DrawBackdrop();
	DrawSplit(vscreen->Texture, static_cast<float>(offset_x), static_cast<float>(offset_y), true);

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

void render::DrawSplit(SDL_Texture* texture, float offsetX, float offsetY, bool postEffect)
{
	// Table and panel are drawn separately: the nudge shifts only the table, and the stacked layout moves the panel
	auto boardWidth = static_cast<float>(BoardWidth());
	auto height = static_cast<float>(vscreen->Height);
	auto srcBoardRect = SDL_FRect{0, 0, boardWidth, height};
	auto srcSidebarRect = SDL_FRect{boardWidth, 0, vscreen->Width - boardWidth, height};

	SDL_FRect dstBoardRect, dstSidebarRect;
	SDL_RectToFRect(&BoardRect, &dstBoardRect);
	SDL_RectToFRect(&SidebarRect, &dstSidebarRect);
	dstBoardRect.x += offsetX * fullscrn::ScaleX;
	dstBoardRect.y += offsetY * fullscrn::ScaleY;

	if (postEffect)
		PostEffect::Begin(static_cast<float>(vscreen->Width), height, fullscrn::ScaleX, fullscrn::ScaleY);
	SDL_RenderTexture(winmain::Renderer, texture, &srcBoardRect, &dstBoardRect);
	if (srcSidebarRect.w > 0)
		SDL_RenderTexture(winmain::Renderer, texture, &srcSidebarRect, &dstSidebarRect);
	if (postEffect)
		PostEffect::End();
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
	// A blurred, darkened copy of the table as it is first drawn, made from the player's own data.
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
