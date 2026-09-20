// SPDX-License-Identifier: MIT
//
// Asking the game to repaint a viewport tile, and the gating around the ask.
// tile_repaint.h decides which per-tile entries a repaint pass hides and whether what is
// left has anything to paint; this is what actually calls the game's renderer, counts the
// call so `stats` can report what a frame cost, and skips a tile the camera glide's blank
// summary already knows is empty. Generic over the renderer type, the game's
// renderer_2d_base or a test double with an update_viewport_tile of the same shape. It
// takes the plugin's state as an argument rather than reading the plugin's own, which is
// what plugin_commands.h does and for the same reason: a test can hand it a state of its
// own and read the counters back.

#pragma once

#include "df/graphic_viewportst.h"

#include "plugin_state.h"
#include "sprite_proxies.h"
#include "tile_coverage.h"
#include "tile_repaint.h"
#include "visual_animation.h"

#include <atomic>
#include <cstdint>
#include <unordered_map>
#include <vector>

// One viewport of a frame: the viewport itself, the sprites the plugin is painting on it,
// and which of its tiles those sprites cover.
struct viewport_renderst
{
	df::graphic_viewportst *viewport;
	std::vector<render_proxyst> proxies;
	render_coveragest coverage;
};

// Every tile repaint the plugin asks the game for goes through here so `stats` can count
// them. The frame counter is the recorder's, the render thread's alone; the stats counter is
// read from the console, so it is atomic.
template<typename Renderer>
void game_repaint(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	int32_t x,
	int32_t y)
{
	state.stats.repaints.fetch_add(1,std::memory_order_relaxed);
	++state.render.hook_repaints;
	renderer->update_viewport_tile(vp,x,y);
}

// A staged repaint: skipped when the tile, with the stage's layers hidden, has nothing to
// paint. The repaint passes of tile_repaint.h call it as repaint(vp,x,y).
template<typename Renderer>
auto staged_repainter(plugin_statest &state,Renderer *renderer)
{
	return [&state,renderer](df::graphic_viewportst *vp,int32_t x,int32_t y)
		{
		if(tile_paints_nothing(vp,x*vp->dim_y+y))return;
		game_repaint(state,renderer,vp,x,y);
		};
}

template<typename Renderer>
void redraw_viewport_tile(
	plugin_statest &state,
	Renderer *renderer,
	const viewport_renderst &viewport,
	int32_t x,
	int32_t y,
	bool defer_interface)
{
	df::graphic_viewportst *vp=viewport.viewport;
	const int32_t index=x*vp->dim_y+y;
	if(state.render.blank_summaries.known_blank(vp,index))return;
	repaint_staged(
		vp,x,y,selected_mask(viewport.coverage.selected,index),defer_interface,
		staged_repainter(state,renderer));
}

// Runs after the proxies so the shading covers them rather than sitting underneath.
template<typename Renderer>
void draw_interface_only(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	int32_t x,
	int32_t y)
{
	if(!interface_pass_readable(vp))return;
	if(state.render.blank_summaries.known_blank(vp,x*vp->dim_y+y))return;
	repaint_interface_only(vp,x,y,staged_repainter(state,renderer));
}

template<typename Renderer>
void redraw_world_tile(
	plugin_statest &state,
	Renderer *renderer,
	const std::vector<viewport_renderst> &viewports,
	const tile_coveragest &staged,
	int32_t x,
	int32_t y)
{
	// The stage pass repaints everything above the lowest across the staged tiles, after the
	// proxies.
	const bool staged_tile=staged.count({x,y})!=0;
	for(const viewport_renderst &viewport:viewports)
		{
		if(paintable_tile(viewport.viewport,x,y))
			redraw_viewport_tile(state,renderer,viewport,x,y,staged_tile);
		if(staged_tile)break;
		}
}

template<typename Renderer>
void redraw_above(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	int32_t x,
	int32_t y,
	visual_render_groupst group,
	const std::unordered_map<int32_t,uint16_t> &selected)
{
	const int32_t index=x*vp->dim_y+y;
	if(state.render.blank_summaries.known_blank(vp,index))return;
	repaint_above(
		vp,x,y,group,selected_mask(selected,index),
		staged_repainter(state,renderer));
}

template<typename Renderer>
void redraw_viewport_tiles(
	plugin_statest &state,
	Renderer *renderer,
	const viewport_renderst &viewport,
	const tile_coveragest &coverage)
{
	df::graphic_viewportst *vp=viewport.viewport;
	for(const auto &[x,y]:coverage)
		{
		if(!paintable_tile(vp,x,y))continue;
		redraw_viewport_tile(state,renderer,viewport,x,y,true);
		}
}
