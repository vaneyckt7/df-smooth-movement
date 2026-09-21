// SPDX-License-Identifier: MIT
//
// Deciding what a frame draws before any of it goes down: which of the game's viewports can
// be read at all, what the animation manager is told about each, which sprites each viewport
// contributes and which creatures carry an icon. The game keeps nine viewports -- eight
// lower z-levels and the main one -- and frees a viewport's layer arrays without clearing
// its active flag, so "active" is not the same question as "readable"; everything here works
// on the readable ones, lowest level first, the order the game draws them in. Generic over
// the renderer type, the game's renderer_2d_base or a test double with a tile cache and an
// update_viewport_tile of the same shape. It takes the plugin's state as an argument rather
// than reading the plugin's own, and the game's window position and viewport table as
// arguments rather than naming the globals, which is what the other drawing headers do and
// for the same reason: the harness builds against stub headers with no DFHack in them, so a
// test can hand these functions a state, a renderer and a viewport table of its own.

#pragma once

#include "df/graphic.h"
#include "df/graphic_viewportst.h"
#include "df/unit.h"

#include "plugin_state.h"
#include "sprite_proxies.h"
#include "texture_cache.h"
#include "tile_coverage.h"
#include "tile_redraw.h"
#include "tile_repaint.h"
#include "visual_animation.h"

#include <SDL_render.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

inline viewport_visual_animation_inputst animation_input(
	plugin_statest &state,
	df::graphic_viewportst *vp,
	int32_t window_x,
	int32_t window_y)
{
	const df::graphic_viewportst *const_viewport=vp;
	return {
		vp,
		vp->dim_x,
		vp->dim_y,
		state.render.view_context.revision(),
		visual_layers(const_viewport),
		visual_layers(const_viewport,true),
		vp->screentexpos_background,
		vp->screentexpos_background_old,
		window_x,
		window_y,
		state.render.drawn_arrays.frame_simulation_tick
		};
}

// The layer arrays are freed and nulled without clearing the active flag.
inline bool viewport_readable(
	plugin_statest &state,
	df::graphic_viewportst *vp,
	int32_t window_x,
	int32_t window_y)
{
	return vp!=nullptr&&vp->flag.bits.active&&
		animation_input(state,vp,window_x,window_y).valid();
}

inline std::vector<df::graphic_viewportst *> active_viewports(
	plugin_statest &state,
	df::graphic *gps,
	int32_t window_x,
	int32_t window_y)
{
	std::vector<df::graphic_viewportst *> viewports;
	if(gps==nullptr)return viewports;
	for(int32_t lower=7;lower>=0;--lower)
		{
		df::graphic_viewportst *vp=gps->lower_viewport[lower];
		if(viewport_readable(state,vp,window_x,window_y))viewports.push_back(vp);
		}
	if(viewport_readable(state,gps->main_viewport,window_x,window_y))
		viewports.push_back(gps->main_viewport);
	return viewports;
}

template<typename Renderer>
std::vector<carried_item_proxyst> collect_carried_item_proxies(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	const std::vector<df::unit *> &units,
	int32_t window_x,
	int32_t window_y)
{
	std::vector<carried_item_proxyst> proxies;
	// An icon keeps to the path on its own; its carrier takes it along the movement (see
	// mark_carried_item_movements).
	const bool overshoots=state.render.animation_manager.movement().overshoot().any();
	for(const df::unit *unit:units)
		{
		const int32_t x=unit->pos.x-window_x;
		const int32_t y=unit->pos.y-window_y;
		if(!paintable_tile(vp,x,y))continue;
		const int32_t index=x*vp->dim_y+y;
		if(vp->screentexpos[index]==0)continue;
		const int32_t texpos=item_texpos(hauled_item(unit));
		SDL_Texture *texture=cached_viewport_texture(state,renderer,vp,index,texpos);
		if(texture==nullptr)continue;
		const auto movement=state.render.animation_manager.get_movement(
			vp,viewport_visual_layer::center,x,y);
		const float source_x_tiles=movement.active?movement.source_x_tiles:float(x);
		const float source_y_tiles=movement.active?movement.source_y_tiles:float(y);
		carried_item_proxyst proxy={
			source_x_tiles,source_y_tiles,x,y,
			movement.active?movement.offset_x_tiles:0.0f,
			movement.active?movement.offset_y_tiles:0.0f,
			movement.active?movement.travelled_pct:1.0f,
			texture,overshoots,{}};
		for(int32_t coverage_x=int32_t(std::floor(std::min(source_x_tiles,float(x))));
			coverage_x<=int32_t(std::ceil(std::max(source_x_tiles,float(x))));++coverage_x)
			for(int32_t coverage_y=int32_t(std::floor(std::min(source_y_tiles,float(y))));
				coverage_y<=int32_t(std::ceil(std::max(source_y_tiles,float(y))));++coverage_y)
				if(paintable_tile(vp,coverage_x,coverage_y))
					proxy.coverage.emplace(coverage_x,coverage_y);
		proxies.push_back(std::move(proxy));
		}
	return proxies;
}

template<typename Renderer>
std::vector<viewport_renderst> collect_viewport_renders(
	plugin_statest &state,
	Renderer *renderer,
	const std::vector<df::graphic_viewportst *> &viewports)
{
	std::vector<viewport_renderst> renders;
	renders.reserve(viewports.size());
	for(df::graphic_viewportst *vp:viewports)
		{
		viewport_renderst render=
			{
			vp,
			collect_proxies(
				vp,state.render.animation_manager,state.flip_enabled,
				[renderer](int32_t texpos){return cached_texture(renderer,texpos);}),
			{}
			};
		render.coverage=collect_coverage(render.proxies,vp->dim_y);
		renders.push_back(std::move(render));
		}
	return renders;
}

inline bool has_mirrored_viewport_facing(
	plugin_statest &state,
	const std::vector<df::graphic_viewportst *> &viewports)
{
	for(const df::graphic_viewportst *vp:viewports)
		if(state.render.animation_manager.has_mirrored_facing(vp))return true;
	return false;
}
