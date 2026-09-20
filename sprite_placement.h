// SPDX-License-Identifier: MIT
//
// Where a sprite belongs on screen, in pixels. The game draws each viewport tile at a whole
// number of pixels from the renderer's origin, and a sprite the plugin glides sits between
// two of those tiles, so the plugin works the corner out itself and hands SDL a rectangle in
// floats. Generic over the renderer type: the game's renderer_2d_base, or a test double with
// the same viewport_zoom_factor, origin_x and origin_y, so this header needs nothing from
// DFHack.

#pragma once

#include <algorithm>
#include <cstdint>

// The side of a tile on screen in pixels at the renderer's zoom: 32 at the default zoom of
// 128, scaled with the zoom otherwise and never below one pixel.
inline int32_t tile_size_px(int32_t zoom)
{
	return zoom==128?32:std::max(1,zoom*32/128);
}

template<typename Renderer>
double renderer_tile_px(const Renderer *renderer)
{
	return double(tile_size_px(renderer->viewport_zoom_factor));
}

// The left or top edge of a tile in pixels. The multiply comes before the divide, which is
// not the same as `origin+tile*tile_size_px(zoom)`: at a zoom whose tile size does not divide
// exactly the rounded size would accumulate across a viewport, while this keeps the edges
// evenly spaced. At zoom 50 the tile size rounds to 12 pixels, and the fourth tile begins at
// 37 pixels here against 36 there. The test pins both forms against each other.
inline int32_t tile_pixel(int32_t tile,int32_t origin,int32_t zoom)
{
	return zoom==128?32*tile+origin:(zoom*32*tile)/128+origin;
}

// Where a sprite gliding from its source tile to its target tile sits on screen this frame:
// the top left corner in pixels and the tile size. The movement gives the sprite's offset
// from the source tile (movement.h); a proxy that fell back takes the default movement's.
struct sprite_placementst
{
	float x_px;
	float y_px;
	float tile_size_px;
};

// A proxy names its target in whole tiles, which is a tile of the viewport, and its source in
// fractions of a tile, which need not be one. So the target's pixel corner is the fixed point
// the rest is measured from: the source in tile sizes away from it, and the movement's offset
// in tile sizes from the source.
template<typename Renderer,typename Proxy>
sprite_placementst place_sprite(const Renderer *renderer,const Proxy &proxy)
{
	const int32_t zoom=renderer->viewport_zoom_factor;
	const float target_x_px=float(tile_pixel(proxy.target_x,renderer->origin_x,zoom));
	const float target_y_px=float(tile_pixel(proxy.target_y,renderer->origin_y,zoom));
	const float tile_px=float(tile_size_px(zoom));
	const float source_x_px=target_x_px+(proxy.source_x_tiles-proxy.target_x)*tile_px;
	const float source_y_px=target_y_px+(proxy.source_y_tiles-proxy.target_y)*tile_px;
	return {source_x_px+proxy.offset_x_tiles*tile_px,
		source_y_px+proxy.offset_y_tiles*tile_px,tile_px};
}
