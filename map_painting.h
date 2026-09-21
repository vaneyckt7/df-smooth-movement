// SPDX-License-Identifier: MIT
//
// Painting the map over the frame the game has already finished. The game draws the whole
// screen and the plugin then overdraws the part of it that has to move, which it does one of
// two ways. While the camera is mid-glide every tile on the map is off by a fraction of a
// tile, so the whole map rectangle is painted again at the shifted origin, clipped to that
// rectangle so nothing spills onto the interface around it. Otherwise only the tiles a
// sprite covers this frame are painted again, together with the ones it covered last frame
// and has since left. Either way the pixels are filled with black first, because the tile
// the game drew is still on screen underneath and a sprite caught between two tiles does not
// cover all of it. tile_redraw.h is what paints a tile's world layers and movement_drawing.h
// is what puts the sprites down in stages over them; this is what decides which tiles those
// two are handed. Generic over the renderer type, and takes the plugin's state as an
// argument rather than reading the plugin's own, which is what tile_redraw.h does and for
// the same reason: a test can hand it a state of its own and read the counters back.

#pragma once

#include "df/graphic_viewportst.h"

#include "movement_drawing.h"
#include "plugin_state.h"
#include "sprite_placement.h"
#include "sprite_proxies.h"
#include "tile_coverage.h"
#include "tile_redraw.h"

#include <SDL_render.h>

#include <cstdint>
#include <vector>

// Paints black under the rectangles, keeping the game's draw colour.
inline void fill_black(
	const sdl_apist &sdl,
	SDL_Renderer *sdl_renderer,
	const std::vector<SDL_Rect> &rects)
{
	Uint8 old_r=0,old_g=0,old_b=0,old_a=255;
	sdl.get_render_draw_color(sdl_renderer,&old_r,&old_g,&old_b,&old_a);
	sdl.set_render_draw_color(sdl_renderer,0,0,0,255);
	for(const SDL_Rect &rect:rects)sdl.render_fill_rect(sdl_renderer,&rect);
	sdl.set_render_draw_color(sdl_renderer,old_r,old_g,old_b,old_a);
}

template<typename Renderer>
void paint_shifted_map(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	const std::vector<viewport_renderst> &viewport_renders,
	const tile_coveragest &coverage,
	const std::vector<carried_item_proxyst> &carried_items,
	int32_t glide_x,
	int32_t glide_y)
{
	SDL_Renderer *sdl_renderer=static_cast<SDL_Renderer *>(renderer->sdl_renderer);
	const int32_t zoom=renderer->viewport_zoom_factor;
	// Camera mid-glide: repaint the WHOLE map rect at the shifted origin so the world (and
	// the creature proxies, which read origin at draw time) renders between tiles. The game
	// already drew this frame at the snapped position; everything here overdraws it, clipped
	// to the map rect so shifted tiles never spill over the UI. The uncovered strip on the
	// trailing edge stays black until the glide lands.
	const SDL_Rect map_rect=
		{
		tile_pixel(vp->clipx[0],renderer->origin_x,zoom),
		tile_pixel(vp->clipy[0],renderer->origin_y,zoom),
		tile_pixel(vp->clipx[1]+1,renderer->origin_x,zoom)-
			tile_pixel(vp->clipx[0],renderer->origin_x,zoom),
		tile_pixel(vp->clipy[1]+1,renderer->origin_y,zoom)-
			tile_pixel(vp->clipy[0],renderer->origin_y,zoom)
		};
	state.sdl.render_set_clip_rect(sdl_renderer,&map_rect);
	fill_black(state.sdl,sdl_renderer,{map_rect});
	const int32_t saved_origin_x=renderer->origin_x;
	const int32_t saved_origin_y=renderer->origin_y;
	renderer->origin_x+=glide_x;
	renderer->origin_y+=glide_y;
	state.render.blank_summaries.summarize(
		viewport_renders,[](const viewport_renderst &v){return v.viewport;});
	for(int32_t x=vp->clipx[0];x<=vp->clipx[1];++x)
		{
		for(int32_t y=vp->clipy[0];y<=vp->clipy[1];++y)
			redraw_world_tile(state,renderer,viewport_renders,coverage,x,y);
		}
	draw_viewport_movement_stages(
		state,renderer,viewport_renders,coverage,carried_items);
	renderer->origin_x=saved_origin_x;
	renderer->origin_y=saved_origin_y;
	state.render.blank_summaries.clear();
	state.sdl.render_set_clip_rect(sdl_renderer,nullptr);
}

template<typename Renderer>
void paint_covered_tiles(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	const std::vector<viewport_renderst> &viewport_renders,
	const tile_coveragest &coverage,
	const std::vector<carried_item_proxyst> &carried_items)
{
	SDL_Renderer *sdl_renderer=static_cast<SDL_Renderer *>(renderer->sdl_renderer);
	const int32_t zoom=renderer->viewport_zoom_factor;
	const int32_t tile_px=tile_size_px(zoom);
	tile_coveragest redraw_coverage=coverage;
	redraw_coverage.insert(
		state.render.previous_coverage.begin(),state.render.previous_coverage.end());
	std::vector<SDL_Rect> tile_rects;
	for(const auto &[x,y]:redraw_coverage)
		{
		if(!paintable_tile(vp,x,y))continue;
		tile_rects.push_back(
			{
			tile_pixel(x,renderer->origin_x,zoom),
			tile_pixel(y,renderer->origin_y,zoom),
			tile_px,
			tile_px
			});
		}
	fill_black(state.sdl,sdl_renderer,tile_rects);

	for(const auto &[x,y]:redraw_coverage)
		{
		if(paintable_tile(vp,x,y))
			redraw_world_tile(state,renderer,viewport_renders,coverage,x,y);
		}
	draw_viewport_movement_stages(
		state,renderer,viewport_renders,coverage,carried_items);
}
