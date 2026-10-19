#pragma once
#include "gdrv.h"
#include "maths.h"
#include "zdrv.h"

enum class VisualTypes : char
{
	Background = 0,
	Sprite = 1,
	Ball = 2
};

struct render_sprite
{
	rectangle_type BmpRect{};
	gdrv_bitmap8* Bmp;
	zmap_header_type* ZMap;
	bool DeleteFlag;
	VisualTypes VisualType;
	uint16_t Depth;
	rectangle_type DirtyRectPrev{};
	int ZMapOffestY;
	int ZMapOffestX;
	rectangle_type DirtyRect{};
	std::vector<render_sprite*>* OccludedSprites;
	rectangle_type BoundingRect{};
	bool DirtyFlag{};
	// Balls only: where the last two updates painted the ball, for drawing frames in between
	struct
	{
		rectangle_type PaintedRect{}, PrevPaintedRect{}, SavedBmpRect{};
		bool Painted{}, PrevPainted{};
	} Interp{};
	// Balls only: the part of the upscaled frame the ball covers
	rectangle_type DirtyRectHi{};

	render_sprite(VisualTypes visualType, gdrv_bitmap8* bmp, zmap_header_type* zMap,
	              int xPosition, int yPosition, rectangle_type* boundingRect);
	~render_sprite();
	void set(gdrv_bitmap8* bmp, zmap_header_type* zMap, int xPos, int yPos);
	void ball_set(gdrv_bitmap8* bmp, float depth, int xPos, int yPos);
};


class render
{
public:
	static gdrv_bitmap8 *vscreen, *background_bitmap;
	static zmap_header_type* background_zmap;
	static int zmap_offsetX, zmap_offsetY;
	// Where the table and its score panel are drawn, in pixels
	static SDL_Rect BoardRect, SidebarRect;

	static void init(gdrv_bitmap8* bmp, int width, int height);
	static void uninit();
	static void recreate_screen_texture();
	static void update();
	static void AddSprite(render_sprite& sprite);
	static void RemoveSprite(render_sprite& sprite);
	static void set_background_zmap(zmap_header_type* zMap, int offsetX, int offsetY);
	static void shift(int offsetX, int offsetY);
	static void build_occlude_list();
	static void SpriteViewer(bool* show);
	static void PresentVScreen();
	static int BoardWidth();
	static void InterpolateBalls(float alpha);
	static void RestoreBalls();
	// The texture is the frame at textureScale times its size
	static void DrawSplit(SDL_Texture* texture, float offsetX, float offsetY, int textureScale = 1);
	// Upscaled art: 0 for the original sprites, or 2 or 4 to draw an upscaled copy of the frame
	static void SetUpscale(int factor);
	static int GetUpscale()
	{
		return HiScale;
	}
	// The table's background as pb::init first copied it into the frame
	static gdrv_bitmap8* InitialBackground;

private:
	static std::vector<render_sprite*> sprite_list, ball_list;
	static int offset_x, offset_y;
	static rectangle_type vscreen_rect;
	static gdrv_bitmap8* ball_bitmap[20];
	static zmap_header_type* zscreen;
	static SDL_Texture* BackdropTexture;
	static bool BallsInterpolated;
	static gdrv_bitmap8 *vscreenHi, *ball_bitmap_hi[20];
	static zmap_header_type* zscreenHi;
	static int HiScale;
	static std::vector<ColorRgba> SidebarCopy;

	static void repaint(const render_sprite& sprite);
	static void paint_balls();
	static void unpaint_balls();
	static void paint_balls_hi();
	static void PaintHi(const render_sprite& sprite, const rectangle_type& clipRect);
	static void RefreshSidebarHi();
	static void DrawBackdrop();
	static void CreateBackdrop();
};
