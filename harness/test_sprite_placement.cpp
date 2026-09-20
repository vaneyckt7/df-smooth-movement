// SPDX-License-Identifier: MIT
// Checks sprite_placement.h: the tile size and tile edge in pixels at each zoom, and where a
// gliding sprite's top left corner lands. Nothing here draws; this is the arithmetic that
// decides what rectangle the plugin hands SDL. Every recording in recordings/ was made at
// zoom 192, where a tile is exactly 48 pixels, so a replay runs this at that one zoom and
// never at the default zoom or at a zoom whose tile size is not a whole number of pixels.

#include "df/renderer_2d_base.h"

#include "sprite_placement.h"

#include <cmath>
#include <cstdio>

namespace {

int failures=0;

// What place_sprite reads off a proxy. The real ones (render_proxyst and carried_item_proxyst
// in sprite_proxies.h) carry a texture and a layer as well, which placement never looks at.
struct proxyst
{
	float source_x_tiles;
	float source_y_tiles;
	int32_t target_x;
	int32_t target_y;
	float offset_x_tiles;
	float offset_y_tiles;
};

void expect_int(const char *name,int32_t got,int32_t expected)
{
	if(got!=expected)printf("%s: %d, expected %d\n",name,got,expected),++failures;
}

// The placement is in floats. Most of the values here are sums of tile sizes and whole or
// half tiles, which are exact in binary, but the hop case lifts by a tenth of a tile, which
// is not. An eighth of a pixel covers that rounding and is far below any wrong answer the
// arithmetic can produce, all of which are off by a whole pixel or more.
void expect_px(const char *name,float got,float expected)
{
	if(std::fabs(got-expected)>0.125f)
		printf("%s: %.3f px, expected %.3f px\n",name,double(got),double(expected)),++failures;
}

void expect_placement(
	const char *name,
	const df::renderer_2d_base &renderer,
	const proxyst &proxy,
	float x_px,
	float y_px,
	float tile_px)
{
	const sprite_placementst placement=place_sprite(&renderer,proxy);
	expect_px(name,placement.x_px,x_px);
	char axis[128];
	snprintf(axis,sizeof axis,"%s, y",name);
	expect_px(axis,placement.y_px,y_px);
	snprintf(axis,sizeof axis,"%s, tile size",name);
	expect_px(axis,placement.tile_size_px,tile_px);
}

void test_tile_size()
{
	// The default zoom is a whole 32 pixels, and the formula scales from there. Both
	// functions take a shortcut at 128 rather than computing it, which gives the same answer
	// the general formula would, so these cases pin the value rather than telling the two
	// apart: deleting either shortcut leaves every test here passing.
	expect_int("default zoom",tile_size_px(128),32);
	expect_int("half zoom",tile_size_px(64),16);
	expect_int("double zoom",tile_size_px(256),64);
	expect_int("quarter zoom",tile_size_px(32),8);
	// Zoom 50 is 12.5 pixels a tile, which truncates rather than rounds.
	expect_int("zoom 50",tile_size_px(50),12);
	expect_int("zoom 51",tile_size_px(51),12);
	expect_int("zoom 52",tile_size_px(52),13);
	// A tile of zero pixels would be an invisible viewport and an empty SDL rectangle, so
	// every zoom below four is one pixel rather than none.
	expect_int("zoom 4",tile_size_px(4),1);
	expect_int("zoom 3",tile_size_px(3),1);
	expect_int("zoom 1",tile_size_px(1),1);
	expect_int("zoom 0",tile_size_px(0),1);
	df::renderer_2d_base renderer;
	renderer.viewport_zoom_factor=64;
	if(renderer_tile_px(&renderer)!=16.0)
		printf("renderer tile size: %.3f, expected 16\n",renderer_tile_px(&renderer)),++failures;
}

void test_tile_pixel()
{
	// At the default zoom a tile is 32 pixels from the one before it, from the origin on.
	expect_int("first tile",tile_pixel(0,0,128),0);
	expect_int("second tile",tile_pixel(1,0,128),32);
	expect_int("tenth tile",tile_pixel(9,0,128),288);
	// The origin belongs to the renderer, not to one viewport: every viewport on screen is
	// drawn against it, and the glide moves it for all of them. It shifts every tile with it.
	expect_int("first tile from an origin",tile_pixel(0,7,128),7);
	expect_int("second tile from an origin",tile_pixel(1,7,128),39);
	// A negative tile is off the left or top of the screen. No call the plugin makes reaches
	// one: the per-tile path passes tiles that have passed paintable_tile, and the glide path
	// passes clip bounds. It is pinned anyway because the function is total and the value is
	// not obvious -- a sprite that starts outside the viewport arrives through the source
	// term in place_sprite instead, which is a float and is tested below.
	expect_int("tile before the origin",tile_pixel(-1,0,128),-32);
	expect_int("tile before a shifted origin",tile_pixel(-2,64,128),0);
	// The multiply before the divide is what keeps the edges evenly spaced. At zoom 50 a
	// tile rounds down to 12 pixels, so tile*tile_size_px would put the fourth tile at 36
	// and the twentieth at 228, against the 37 and 237 the tiles are really drawn at.
	expect_int("zoom 50 tile size",tile_size_px(50),12);
	expect_int("zoom 50, fourth tile",tile_pixel(3,0,50),37);
	expect_int("zoom 50, twentieth tile",tile_pixel(19,0,50),237);
	// A rounded tile size drifts from the drawn grid, and never comes back: at zoom 50 the
	// two forms agree on the first two tiles and on no tile after them, while at the default
	// zoom they agree everywhere because 32 pixels a tile is exact.
	for(int32_t tile=0;tile<200;++tile)
		{
		if(tile_pixel(tile,0,128)!=tile*tile_size_px(128))
			printf("zoom 128 drifts at tile %d\n",tile),++failures;
		if((tile>=2)==(tile_pixel(tile,0,50)==tile*tile_size_px(50)))
			printf("zoom 50 drift wrong at tile %d\n",tile),++failures;
		}
}

void test_placement()
{
	df::renderer_2d_base renderer;
	renderer.origin_x=10;
	renderer.origin_y=20;
	{
	// A sprite that has not moved sits exactly on its tile, at the tile's own pixel corner.
	const proxyst still={4.0f,3.0f,4,3,0.0f,0.0f};
	expect_placement("still",renderer,still,10.0f+4*32,20.0f+3*32,32.0f);
	}
	{
	// Halfway through a step east: the source is the tile behind the target, and the
	// movement has carried the sprite half a tile from it, so it sits half a tile short.
	const proxyst halfway={3.0f,3.0f,4,3,0.5f,0.0f};
	expect_placement("halfway east",renderer,halfway,10.0f+3*32+16,20.0f+3*32,32.0f);
	}
	{
	// The whole of a step north, which is a smaller y: offset and source agree with the
	// target, so the sprite is on the target tile.
	const proxyst arrived={4.0f,4.0f,4,3,0.0f,-1.0f};
	expect_placement("arrived north",renderer,arrived,10.0f+4*32,20.0f+3*32,32.0f);
	}
	{
	// A source that is not a whole tile, which is what a creature that retargets mid-step
	// leaves behind: the sprite starts a quarter tile east of tile 3 and has gone half the
	// remaining way, so it sits three quarters of a tile east of it.
	const proxyst fractional={3.25f,3.0f,4,3,0.5f,0.0f};
	expect_placement("fractional source",renderer,fractional,10.0f+3*32+8+16,20.0f+3*32,32.0f);
	}
	{
	// The hop lifts the sprite off its row, a negative y offset being up the screen. Moving
	// the rectangle is the whole of what placement does with the lift; which tiles have to be
	// erased under a sprite that leaves its row is the proxies' business, and sprite_proxies.h
	// adds those rows to the coverage itself.
	const proxyst hopping={3.0f,3.0f,4,3,0.5f,-0.1f};
	expect_placement("hop lift",renderer,hopping,10.0f+3*32+16,20.0f+3*32-3.2f,32.0f);
	}
	{
	// Everything scales with the zoom, and the target's corner comes from tile_pixel, so a
	// zoom whose tile size is not exact places the sprite on the drawn grid rather than on
	// a grid of rounded tiles. At zoom 50 tile 4 begins at 50 pixels, not at 4*12=48.
	df::renderer_2d_base zoomed;
	zoomed.viewport_zoom_factor=50;
	const proxyst halfway={3.0f,3.0f,4,3,0.5f,0.0f};
	expect_placement("halfway at zoom 50",zoomed,halfway,50.0f-12.0f+6.0f,37.0f,12.0f);
	}
	{
	// A sprite can be part way in from off the edge: here the source is a tile left of column
	// zero, which is a creature stepping into the viewport. Placement keeps the negative
	// pixels rather than clamping them, and what cuts the sprite off is the clip rectangle
	// the glide path sets from the viewport's bounds, or the render target's own edge.
	const proxyst offscreen={-1.0f,0.0f,0,0,0.5f,0.0f};
	expect_placement("target at the corner",renderer,offscreen,10.0f-32+16,20.0f,32.0f);
	}
}

} // namespace

int main()
{
	test_tile_size();
	test_tile_pixel();
	test_placement();
	if(failures!=0){printf("sprite placement tests: %d failures\n",failures);return 1;}
	printf("sprite placement tests: OK\n");
	return 0;
}
