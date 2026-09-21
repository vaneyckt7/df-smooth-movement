// SPDX-License-Identifier: MIT
//
// Drawing a frame's movement in stages, so that a sprite between tiles ends up behind what
// the game draws above it and in front of what it draws below. The game paints a tile in
// one go, bottom layer to top; the plugin cannot, because the sprite it is moving sits
// between two of those layers and has to be painted with the layers below it already down
// and the layers above it not yet. So a stage paints every sprite of one render group and
// then asks the game to repaint, above that group only, the tiles those sprites cover --
// tile_repaint.h decides what "above that group" hides, tile_redraw.h does the asking. The
// designation group is the last one and has nothing above it, so it skips the repaint.
// Stacked viewports repeat the whole thing lowest first, since a lower z-level's sprite
// must end up under the next viewport's fog, and the hauled item icons go on last, over the
// topmost viewport. Generic over the renderer type, and takes the plugin's state as an
// argument rather than reading the plugin's own, which is what tile_redraw.h does and for
// the same reason: a test can hand it a state of its own and read the counters back.

#pragma once

#include "df/graphic_viewportst.h"

#include "plugin_state.h"
#include "sprite_drawing.h"
#include "sprite_proxies.h"
#include "tile_coverage.h"
#include "tile_redraw.h"
#include "visual_animation.h"

#include <cstddef>
#include <cstdint>
#include <vector>

template<typename Renderer>
void draw_movement_stages(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	const std::vector<render_proxyst> &proxies,
	const render_coveragest &coverage)
{
	for(size_t index=0;index<coverage.groups.size();++index)
		{
		const auto group=static_cast<visual_render_groupst>(index);
		for(const render_proxyst &proxy:proxies)
			if(visual_render_group(proxy.layer)==group)draw_proxy(state.sdl,renderer,proxy);
		if(group==visual_render_groupst::designation)continue;
		for(const auto &[x,y]:coverage.groups[index])
			redraw_above(state,renderer,vp,x,y,group,coverage.selected);
		}
}

template<typename Renderer>
void draw_viewport_movement_stages(
	plugin_statest &state,
	Renderer *renderer,
	const std::vector<viewport_renderst> &viewports,
	const tile_coveragest &coverage,
	const std::vector<carried_item_proxyst> &carried_items)
{
	for(size_t index=0;index<viewports.size();++index)
		{
		// A lower z-level's proxy must be covered by the next viewport's fog and terrain.
		// Reapply that viewport before its own proxies, matching DF's lower-to-main draw order.
		if(index>0)redraw_viewport_tiles(state,renderer,viewports[index],coverage);
		const viewport_renderst &viewport=viewports[index];
		draw_movement_stages(
			state,renderer,viewport.viewport,viewport.proxies,viewport.coverage);
		if(index+1==viewports.size())
			for(const carried_item_proxyst &proxy:carried_items)
				draw_carried_item_proxy(state.sdl,renderer,proxy);
		// A viewport shades everything drawn beneath it, so this covers every staged tile.
		// Restricting it to the tiles this viewport has sprites on would not deepen with distance.
		for(const auto &[x,y]:coverage)
			{
			if(paintable_tile(viewport.viewport,x,y))
				draw_interface_only(state,renderer,viewport.viewport,x,y);
			}
		}
}
