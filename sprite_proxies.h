// SPDX-License-Identifier: MIT

#ifndef SPRITE_PROXIES_H
#define SPRITE_PROXIES_H

#include "tile_coverage.h"
#include "tile_repaint.h"
#include "visual_animation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <set>
#include <type_traits>
#include <utility>
#include <vector>

struct SDL_Texture;

template<typename Viewport>
bool inside_clip(const Viewport *vp,int32_t x,int32_t y)
{
	return x>=vp->clipx[0]&&x<=vp->clipx[1]&&
		y>=vp->clipy[0]&&y<=vp->clipy[1];
}

template<typename Flag>
bool fire_frame(const Flag &flag)
{
	if constexpr(std::is_same_v<std::remove_cv_t<Flag>,uint32_t>)
		return (flag&0x70000000U)!=0;
	else
		return flag.bits.fire_frame_type!=0;
}

template<typename Viewport>
bool has_fire(const Viewport *vp,int32_t x,int32_t y)
{
	return vp->screentexpos_spatter_flag!=nullptr&&
		fire_frame(vp->screentexpos_spatter_flag[x*vp->dim_y+y]);
}

// One sprite the plugin paints itself this frame in place of the game's copy, between its
// source and target tiles.
struct render_proxyst
{
	viewport_visual_layer layer;
	float source_x;
	float source_y;
	int32_t target_x;
	int32_t target_y;
	int32_t texpos;
	float progress;
	SDL_Texture *texture;
	bool mirrored=false;
	int32_t mirror_shift=0;
	std::set<std::pair<int32_t,int32_t>> coverage;
};

// The icon of an item a creature hauls, drawn on top of the last viewport.
struct carried_item_proxyst
{
	float source_x;
	float source_y;
	int32_t target_x;
	int32_t target_y;
	float progress;
	SDL_Texture *texture;
	std::set<std::pair<int32_t,int32_t>> coverage;
};

// Lists the sprites the plugin paints itself on one viewport this frame: every moving sprite
// the animation manager tracks whose path stays inside the clip and off burning tiles, and,
// with flipping on, every resting sprite that faces the mirrored way. `cached_texture(texpos)`
// returns the renderer's texture for a tile, or null when it has none, in which case the
// sprite is left to the game.
template<typename Viewport,typename TextureLookup>
std::vector<render_proxyst> collect_proxies(
	Viewport *vp,
	visual_animation_managerst &animation_manager,
	bool flip_enabled,
	const TextureLookup &cached_texture)
{
	std::vector<render_proxyst> proxies;
	// Only a tile the movement tracker lists can have a movement, so the walk over every
	// layer visits those instead of every tile of the viewport; a viewport with none is
	// skipped before the resting-mirrored sweep, which does not depend on movements.
	std::vector<std::array<int32_t,2>> tiles;
	animation_manager.collect_movement_tiles(vp,vp->dim_x,vp->dim_y,tiles);
	auto layers=visual_layers(vp);
	auto previous_layers=visual_layers(vp,true);
	for(uint8_t draw_order=0;draw_order<visual_layer_count&&!tiles.empty();++draw_order)
		{
		const viewport_visual_layer visual_layer=visual_layer_at_draw_order(draw_order);
		const size_t layer=static_cast<size_t>(visual_layer);
		for(const std::array<int32_t,2> &tile:tiles)
			{
			const int32_t x=tile[0];
			const int32_t y=tile[1];
			const int32_t index=x*vp->dim_y+y;
			const int32_t texpos=layers[layer][index];
			if(texpos==0)continue;
			const auto movement=animation_manager.get_movement(
				vp,static_cast<viewport_visual_layer>(layer),x,y);
			if(!movement.active)continue;
			const int32_t inherited_source_x=inherited_visual_source_tile(
				x,movement.source_x,x);
			const int32_t inherited_source_y=inherited_visual_source_tile(
				y,movement.source_y,y);
			const bool inherited_source_in_bounds=
				inherited_source_x>=0&&inherited_source_x<vp->dim_x&&
				inherited_source_y>=0&&inherited_source_y<vp->dim_y;
			if(!visual_layer_moves_independently(visual_layer))
				{
				bool anchored=false;
				for(const render_proxyst &anchor:proxies)
					{
					if(anchor.layer==viewport_visual_layer::center&&
						std::abs(anchor.target_x-x)<=1&&
						std::abs(anchor.target_y-y)<=1&&
						anchor.source_x-anchor.target_x==movement.source_x-x&&
						anchor.source_y-anchor.target_y==movement.source_y-y&&
						anchor.progress==movement.progress)anchored=true;
					}
				if(!anchored)continue;
				}
				if((visual_layer==viewport_visual_layer::item||
					visual_layer==viewport_visual_layer::designation)&&
					movement.inherited)
				{
				if(visual_layer==viewport_visual_layer::item&&
					vp->screentexpos_old[index]!=0)continue;
				if(!inherited_source_in_bounds)continue;
				const int32_t source=
					inherited_source_x*vp->dim_y+inherited_source_y;
				if(!visual_moved_between_tiles(
					visual_layer,
						layers[layer],
						previous_layers[layer],
						source,
						index))continue;
				}
			if(!visual_layer_moves_independently(visual_layer)&&
				visual_layer!=viewport_visual_layer::designation&&movement.inherited)
				{
				const bool fragment_moved=inherited_source_in_bounds&&
					visual_moved_between_tiles(
						visual_layer,layers[layer],previous_layers[layer],
						inherited_source_x*vp->dim_y+inherited_source_y,index);
				if(!fragment_moved)
					{
				const auto &descriptor=visual_layer_descriptor(visual_layer);
				bool owns_fragment=false;
				for(const render_proxyst &anchor:proxies)
					if(anchor.layer==viewport_visual_layer::center&&
						anchor.target_x==x+descriptor.center_x&&
						anchor.target_y==y+descriptor.center_y&&
						anchor.source_x-anchor.target_x==movement.source_x-x&&
						anchor.source_y-anchor.target_y==movement.source_y-y&&
						anchor.progress==movement.progress)owns_fragment=true;
				if(!owns_fragment)continue;
					}
				}

			// Items, vehicles and designations keep their vanilla orientation.
			const auto &mirror_descriptor=
				visual_layer_descriptor(visual_layer);
			const visual_render_groupst group=
				visual_render_group(visual_layer);
			const bool mirror_eligible=flip_enabled&&
				(group==visual_render_groupst::main||
				group==visual_render_groupst::upper);
			// Facing is read from the anchor tile so every fragment of one creature agrees.
			const bool mirrored=mirror_eligible&&
				animation_manager.get_facing(
					vp,
					x+mirror_descriptor.center_x,
					y+mirror_descriptor.center_y)!=native_sprite_facing;
			// The anchor's own layer has center_x 0, so it flips in place.
			const int32_t mirror_shift=
				mirrored?
				mirrored_tile_x(x,x+mirror_descriptor.center_x)-x:
				0;
			render_proxyst proxy=
				{
				static_cast<viewport_visual_layer>(layer),
				movement.source_x,
				movement.source_y,
				x,
				y,
				texpos,
				movement.progress,
				nullptr,
				mirrored,
				mirror_shift,
				{}
				};
			bool blocked=false;
			for(int32_t coverage_x=int32_t(std::floor(
					std::min(proxy.source_x,float(x))));
				coverage_x<=int32_t(std::ceil(
					std::max(proxy.source_x,float(x))));++coverage_x)
				{
				for(int32_t coverage_y=int32_t(std::floor(
						std::min(proxy.source_y,float(y))));
					coverage_y<=int32_t(std::ceil(
						std::max(proxy.source_y,float(y))));++coverage_y)
					{
					if(!inside_clip(vp,coverage_x,coverage_y))
						{
						blocked=true;
						break;
						}
					if(visual_render_group(proxy.layer)==visual_render_groupst::main&&
						has_fire(vp,coverage_x,coverage_y))
						{
						blocked=true;
						break;
						}
					proxy.coverage.emplace(coverage_x,coverage_y);
					}
				if(blocked)break;
				}
			if(blocked)continue;
			if(proxy.mirror_shift!=0)
				{
				std::set<std::pair<int32_t,int32_t>> mirrored_coverage;
				for(const auto &tile:proxy.coverage)
					mirrored_coverage.emplace(
						tile.first+proxy.mirror_shift,tile.second);
				for(const auto &tile:mirrored_coverage)
					{
					if(!inside_clip(vp,tile.first,tile.second))
						{
						blocked=true;
						break;
						}
					if(visual_render_group(proxy.layer)==visual_render_groupst::main&&
						has_fire(vp,tile.first,tile.second))
						{
						blocked=true;
						break;
						}
					proxy.coverage.insert(tile);
					}
				if(blocked)continue;
				}

			proxy.texture=cached_texture(texpos);
			if(proxy.texture==nullptr)continue;
			proxies.push_back(std::move(proxy));
			}
		}

	// A creature that has stopped still needs its mirrored sprite painted each frame.
	// Otherwise the engine repaints it natively and the two orientations alternate between steps.
	// A fragment's tile is its anchor minus the layer's centre offset, inverting the moving path.
	// The movement tracker lists the tiles of the viewport that face the mirrored way, so the
	// sweep visits those alone; a viewport with none, such as a level below the camera, lists
	// nothing.
	if(flip_enabled)
		{
		animation_manager.collect_mirrored_tiles(vp,vp->dim_x,vp->dim_y,tiles);
		for(const std::array<int32_t,2> &anchor:tiles)
			{
			const int32_t anchor_x=anchor[0];
			const int32_t anchor_y=anchor[1];
			for(uint8_t draw_order=0;draw_order<visual_layer_count;++draw_order)
				{
				const viewport_visual_layer visual_layer=
					visual_layer_at_draw_order(draw_order);
				const visual_render_groupst group=
					visual_render_group(visual_layer);
				if(group!=visual_render_groupst::main&&
					group!=visual_render_groupst::upper)continue;
				const auto &descriptor=visual_layer_descriptor(visual_layer);
				const int32_t x=anchor_x-descriptor.center_x;
				const int32_t y=anchor_y-descriptor.center_y;
				if(x<0||x>=vp->dim_x||y<0||y>=vp->dim_y)continue;
				const size_t layer=static_cast<size_t>(visual_layer);
				const int32_t texpos=layers[layer][x*vp->dim_y+y];
				if(texpos==0)continue;
				bool already_drawn=false;
				for(const render_proxyst &existing:proxies)
					if(existing.layer==visual_layer&&
						existing.target_x==x&&existing.target_y==y)
						already_drawn=true;
				if(already_drawn)continue;

				// source == target at progress 1.0 draws in place, moved only by mirror_shift.
				render_proxyst proxy=
					{
					visual_layer,
					float(x),
					float(y),
					x,
					y,
					texpos,
					1.0f,
					nullptr,
					true,
					mirrored_tile_x(x,anchor_x)-x,
					{}
					};
				// The sprite lands on x+mirror_shift, so that interval must be repaintable.
				// The shift has either sign, so order the interval ends first.
				const int32_t coverage_first=std::min(x,x+proxy.mirror_shift);
				const int32_t coverage_last=std::max(x,x+proxy.mirror_shift);
				bool blocked=false;
				for(int32_t coverage_x=coverage_first;
					coverage_x<=coverage_last;++coverage_x)
					{
					if(!inside_clip(vp,coverage_x,y)||
						(group==visual_render_groupst::main&&
						has_fire(vp,coverage_x,y)))
						{
						blocked=true;
						break;
						}
					proxy.coverage.emplace(coverage_x,y);
					}
				if(blocked)continue;

				proxy.texture=cached_texture(texpos);
				if(proxy.texture==nullptr)continue;
				proxies.push_back(std::move(proxy));
				}
			}
		}
	return proxies;
}
#endif
