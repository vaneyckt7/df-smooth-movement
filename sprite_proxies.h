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
	// Where the sprite is drawn, in tiles from the source tile: `offset` by the movement,
	// `fallback` on the straight path at the movement's travelled fraction, which the
	// sprite takes when `fell_back` is set (see collect_proxies).
	float offset_x_tiles;
	float offset_y_tiles;
	float fallback_x_tiles;
	float fallback_y_tiles;
	SDL_Texture *texture;
	bool mirrored=false;
	int32_t mirror_shift=0;
	// The fallback is decided per creature: fragments, status icons and carried items point
	// at their centre proxy (an index into the proxy list, -1 for none) and follow its
	// decision.
	bool fell_back=false;
	int32_t anchor=-1;
	std::set<std::pair<int32_t,int32_t>> coverage;
};

// The icon of an item a creature hauls, drawn on top of the last viewport.
struct carried_item_proxyst
{
	float source_x;
	float source_y;
	int32_t target_x;
	int32_t target_y;
	float offset_x_tiles;
	float offset_y_tiles;
	float fallback_x_tiles;
	float fallback_y_tiles;
	SDL_Texture *texture;
	// Set when the creature carrying it fell back to the default movement: the icon then
	// rides the same fallback.
	bool fell_back=false;
	std::set<std::pair<int32_t,int32_t>> coverage;
};

// Lists the sprites the plugin paints itself on one viewport this frame: every moving sprite
// the animation manager tracks whose path stays inside the clip and off burning tiles, and,
// with flipping on, every resting sprite that faces the mirrored way. `cached_texture(texpos)`
// returns the renderer's texture for a tile, or null when it has none, in which case the
// sprite is left to the game.
//
// The movement the manager follows (movement.h) can take a sprite beyond the straight path
// between its tiles, by its overshoot: tiles around the path that must be erasable and
// repaintable too. A creature's walk takes the movement: a centre proxy and whatever rides
// on it (fragments, status icons, carried items); anything else, a vehicle or an item moving
// on its own, keeps to the path. When a tile of the overshoot zone is outside the clip or
// burning, or the sprite is not a creature's, the proxy falls back to the default movement,
// which never leaves the path; the fallback is decided per creature (see
// resolve_creature_fallback). The overshoot tiles of a proxy that did not fall back join its
// coverage.
template<typename Viewport,typename TextureLookup>
std::vector<render_proxyst> collect_proxies(
	Viewport *vp,
	visual_animation_managerst &animation_manager,
	bool flip_enabled,
	const TextureLookup &cached_texture)
{
	std::vector<render_proxyst> proxies;
	const movement_overshootst overshoot=animation_manager.movement().overshoot();
	const int32_t rows_above=int32_t(std::ceil(overshoot.above_tiles));
	const int32_t rows_below=int32_t(std::ceil(overshoot.below_tiles));
	const int32_t cols_left=int32_t(std::ceil(overshoot.left_tiles));
	const int32_t cols_right=int32_t(std::ceil(overshoot.right_tiles));
	const bool overshoots=rows_above>0||rows_below>0||cols_left>0||cols_right>0;
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
			// A fragment, icon or item rides on a centre proxy moving the same way: the
			// same step, at the same point of it. Two looks at the centres collected so far:
			// `anchored`, whether any within a tile moves this way, which is what lets a
			// fragment through at all; and `anchor_index`, the centre at this layer's own
			// offset, the one the tile follows the movement with. That takes only the
			// owner, since centres come in tile order and a neighbour stepping in lockstep
			// (a squad column, a dwarf beside the barrow it pushes) can come first: riding
			// on it would leave a fragment on the path while its own creature leaves it, or
			// take one off the path whose creature cannot. A centre is its own root and a
			// vehicle takes no part.
			int32_t anchor_index=-1;
			bool anchored=false;
			const auto &owner=visual_layer_descriptor(visual_layer);
			const bool rider=visual_layer!=viewport_visual_layer::center&&
				visual_layer!=viewport_visual_layer::vehicle;
			for(size_t i=0;i<proxies.size()&&
				(rider||!visual_layer_moves_independently(visual_layer));++i)
				{
				const render_proxyst &anchor=proxies[i];
				if(anchor.layer!=viewport_visual_layer::center||
					anchor.source_x-anchor.target_x!=movement.source_x-x||
					anchor.source_y-anchor.target_y!=movement.source_y-y||
					anchor.offset_x_tiles!=movement.offset_x_tiles||
					anchor.offset_y_tiles!=movement.offset_y_tiles)continue;
				if(std::abs(anchor.target_x-x)<=1&&std::abs(anchor.target_y-y)<=1)
					anchored=true;
				if(rider&&anchor.target_x==x+owner.center_x&&
					anchor.target_y==y+owner.center_y)
					{
					anchor_index=int32_t(i);
					break;
					}
				}
			if(!visual_layer_moves_independently(visual_layer)&&!anchored)continue;
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
				for(size_t i=0;i<proxies.size();++i)
					{
					const render_proxyst &anchor=proxies[i];
					if(anchor.layer==viewport_visual_layer::center&&
						anchor.target_x==x+descriptor.center_x&&
						anchor.target_y==y+descriptor.center_y&&
						anchor.source_x-anchor.target_x==movement.source_x-x&&
						anchor.source_y-anchor.target_y==movement.source_y-y&&
						anchor.offset_x_tiles==movement.offset_x_tiles&&
						anchor.offset_y_tiles==movement.offset_y_tiles)
						{
						owns_fragment=true;
						anchor_index=int32_t(i);
						break;
						}
					}
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
				movement.offset_x_tiles,
				movement.offset_y_tiles,
				movement.fallback_x_tiles,
				movement.fallback_y_tiles,
				nullptr,
				mirrored,
				mirror_shift,
				// A creature's walk takes the movement: a centre and whatever rides on it
				// (fragments, status icons, carried items). Anything else, a vehicle, an
				// item on its own, keeps to the path: with a movement that overshoots
				// beyond it, it takes the default movement instead.
				overshoots&&visual_layer!=viewport_visual_layer::center&&anchor_index<0,
				rider?anchor_index:-1,
				{}
				};
			// The tiles the movement overshoots into beyond the path (and their mirrored
			// image) must be erasable and repaintable too. If any tile of them is outside
			// the clip or burning, this tile cannot follow the movement there; the whole
			// creature then takes the default movement (resolved below), because the
			// overshoot is optional and the glide is not. Only the candidate is decided
			// here; the coverage is added after the creature-level decision.
			if(overshoots&&!proxy.fell_back)
				{
				const int32_t first_row=int32_t(std::floor(
					std::min(proxy.source_y,float(y))));
				const int32_t last_row=int32_t(std::ceil(
					std::max(proxy.source_y,float(y))));
				const int32_t first_col=int32_t(std::floor(
					std::min(proxy.source_x,float(x))));
				const int32_t last_col=int32_t(std::ceil(
					std::max(proxy.source_x,float(x))));
				for(int32_t extra_x=first_col-cols_left;
					extra_x<=last_col+cols_right&&!proxy.fell_back;++extra_x)
					for(int32_t extra_y=first_row-rows_above;
						extra_y<=last_row+rows_below&&!proxy.fell_back;++extra_y)
						{
						if(extra_x>=first_col&&extra_x<=last_col&&
							extra_y>=first_row&&extra_y<=last_row)continue;
						for(const int32_t shifted_x:{extra_x,extra_x+proxy.mirror_shift})
							if(!inside_clip(vp,shifted_x,extra_y)||
								has_fire(vp,shifted_x,extra_y))
								{
								proxy.fell_back=true;
								break;
								}
						}
				}
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

	// One movement per creature: if any tile riding on a centre must fall back, all of them
	// do, so a multi-tile creature never tears and an icon never detaches from its creature.
	// Then the overshoot tiles of every proxy that follows the movement join its coverage; the
	// candidate check above already established they are inside the clip and not burning.
	if(overshoots)
		{
		std::vector<int32_t> anchors;
		std::vector<bool> fallbacks;
		anchors.reserve(proxies.size());
		fallbacks.reserve(proxies.size());
		for(const render_proxyst &proxy:proxies)
			{
			anchors.push_back(proxy.anchor);
			fallbacks.push_back(proxy.fell_back);
			}
		resolve_creature_fallback(anchors,fallbacks);
		for(size_t i=0;i<proxies.size();++i)
			{
			render_proxyst &proxy=proxies[i];
			proxy.fell_back=fallbacks[i];
			if(proxy.fell_back)continue;
			const int32_t first_row=int32_t(std::floor(
				std::min(proxy.source_y,float(proxy.target_y))));
			const int32_t last_row=int32_t(std::ceil(
				std::max(proxy.source_y,float(proxy.target_y))));
			const int32_t first_col=int32_t(std::floor(
				std::min(proxy.source_x,float(proxy.target_x))));
			const int32_t last_col=int32_t(std::ceil(
				std::max(proxy.source_x,float(proxy.target_x))));
			for(int32_t extra_x=first_col-cols_left;
				extra_x<=last_col+cols_right;++extra_x)
				for(int32_t extra_y=first_row-rows_above;
					extra_y<=last_row+rows_below;++extra_y)
					{
					if(extra_x>=first_col&&extra_x<=last_col&&
						extra_y>=first_row&&extra_y<=last_row)continue;
					proxy.coverage.emplace(extra_x,extra_y);
					proxy.coverage.emplace(extra_x+proxy.mirror_shift,extra_y);
					}
			}
		}

	// A creature that has stopped still needs its mirrored sprite painted each frame.
	// Otherwise the game repaints it natively and the two orientations alternate between
	// steps. A fragment's tile is its anchor minus the layer's centre offset, inverting the
	// moving path.
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

				// source == target with no offset draws in place, moved only by mirror_shift.
				render_proxyst proxy=
					{
					visual_layer,
					float(x),
					float(y),
					x,
					y,
					texpos,
					0.0f,
					0.0f,
					0.0f,
					0.0f,
					nullptr,
					true,
					mirrored_tile_x(x,anchor_x)-x,
					false,
					-1,
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

// Marks each carried item whose carrier follows the movement. The item rides on the centre
// proxy of the viewport it is drawn over, the one on its own tile at the same point of the
// same step, matched the way a fragment finds its anchor above: same target, same source,
// same offset. The proxy's coverage already holds the tiles the movement overshoots into, so
// the item adds nothing to it. An item comes in fallen back whenever the movement overshoots
// beyond the path (it keeps to the path on its own, like a vehicle); a carrier that follows
// the movement takes it along.
inline void mark_carried_item_movements(
	std::vector<carried_item_proxyst> &items,
	const std::vector<render_proxyst> &proxies)
{
	for(carried_item_proxyst &item:items)
		{
		if(!item.fell_back)continue;
		for(const render_proxyst &proxy:proxies)
			if(!proxy.fell_back&&proxy.layer==viewport_visual_layer::center&&
				proxy.target_x==item.target_x&&proxy.target_y==item.target_y&&
				proxy.source_x==item.source_x&&proxy.source_y==item.source_y&&
				proxy.offset_x_tiles==item.offset_x_tiles&&
				proxy.offset_y_tiles==item.offset_y_tiles)
				{
				item.fell_back=false;
				break;
				}
		}
}
#endif
