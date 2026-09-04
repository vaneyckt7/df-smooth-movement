// Oracle: the pre-redesign proxy collection, ported mechanically onto a generic view.
#pragma once
#include "visual_animation.h"
#include <set>
#include <unordered_map>
#include <cmath>
#include <cstdlib>
#include "frame_render.h"
namespace oracle {
struct render_proxyst
{
	viewport_visual_layer layer;
	float source_x;
	float source_y;
	int32_t target_x;
	int32_t target_y;
	int32_t texpos;
	float progress;
	const void *texture;
	bool mirrored=false;
	int32_t mirror_shift=0;
	bool bob=false;
	int32_t anchor=-1;
	std::set<std::pair<int32_t,int32_t>> coverage;
};
using tile_coveragest=std::set<std::pair<int32_t,int32_t>>;
struct render_coveragest
{
	tile_coveragest all;
	std::array<tile_coveragest,static_cast<size_t>(visual_render_groupst::count)> groups;
	std::unordered_map<int32_t,uint16_t> selected;
};
template<typename View,typename Manager,typename Texture>
std::vector<render_proxyst> old_collect_proxies(
	const View *vp,const Manager &animation_manager,bool flip_enabled,bool bob_enabled,
	const Texture &texture_of)
{
	std::vector<render_proxyst> proxies;
	const auto &layers=vp->current;
	const auto &previous_layers=vp->previous;
	// Only tiles near a movement can hold a moving proxy, so visit those rather than the grid.
	// Scratch vectors persist across frames to avoid reallocating; the render hook is serial.
	static std::vector<int32_t> candidates;
	animation_manager.movement_candidate_tiles(vp->id,candidates);
	// Same visiting order as the former grid sweep: row by row, left to right. Anchor lookups
	// below rely on a centre proxy preceding the tiles that ride on it.
	const int32_t dim_y=vp->grid.dim_y;
	std::sort(candidates.begin(),candidates.end(),
		[dim_y](int32_t a,int32_t b)
			{
			return std::make_pair(a%dim_y,a/dim_y)<std::make_pair(b%dim_y,b/dim_y);
			});
	for(uint8_t draw_order=0;draw_order<visual_layer_count;++draw_order)
		{
		const viewport_visual_layer visual_layer=visual_layer_at_draw_order(draw_order);
		const size_t layer=static_cast<size_t>(visual_layer);
		for(const int32_t index:candidates)
			{
				{
				const int32_t x=index/dim_y;
				const int32_t y=index%dim_y;
				const int32_t texpos=layers[layer][index];
				if(texpos==0)continue;
				const auto movement=animation_manager.get_movement(
					vp->id,static_cast<viewport_visual_layer>(layer),x,y);
				if(!movement.active)continue;
				const int32_t inherited_source_x=inherited_visual_source_tile(
					x,movement.source_x,x);
				const int32_t inherited_source_y=inherited_visual_source_tile(
					y,movement.source_y,y);
				const bool inherited_source_in_bounds=
					inherited_source_x>=0&&inherited_source_x<vp->grid.dim_x&&
					inherited_source_y>=0&&inherited_source_y<vp->grid.dim_y;
				// The centre proxy this tile rides on, if any: a creature fragment or an icon
				// or item sharing the creature's motion. Used for the anchoring checks below
				// and for the creature-level bob decision. A centre is always its own root,
				// and a vehicle takes no part, so neither couples to a neighbour that happens
				// to move in lockstep (a dwarf and the wheelbarrow it pushes, a squad column).
				int32_t anchor_index=-1;
				const bool bob_rider=visual_layer!=viewport_visual_layer::center&&
					visual_layer!=viewport_visual_layer::vehicle;
				for(size_t i=0;i<proxies.size()&&
					(bob_rider||!visual_layer_moves_independently(visual_layer));++i)
					{
					const render_proxyst &anchor=proxies[i];
					if(anchor.layer==viewport_visual_layer::center&&
						std::abs(anchor.target_x-x)<=1&&
						std::abs(anchor.target_y-y)<=1&&
						anchor.source_x-anchor.target_x==movement.source_x-x&&
						anchor.source_y-anchor.target_y==movement.source_y-y&&
						anchor.progress==movement.progress)
						{
						anchor_index=int32_t(i);
						break;
						}
					}
				if(!visual_layer_moves_independently(visual_layer)&&anchor_index<0)continue;
					if((visual_layer==viewport_visual_layer::item||
						visual_layer==viewport_visual_layer::designation)&&
						movement.inherited)
					{
					if(visual_layer==viewport_visual_layer::item&&
						previous_layers[static_cast<size_t>(viewport_visual_layer::center)][index]!=0)continue;
					if(!inherited_source_in_bounds)continue;
					const int32_t source=
						inherited_source_x*vp->grid.dim_y+inherited_source_y;
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
							inherited_source_x*vp->grid.dim_y+inherited_source_y,index);
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
							anchor.progress==movement.progress)
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
						vp->id,
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
					// Creatures bob; whatever rides on a creature (fragments, status icons,
					// carried items) bobs with it. Vehicles never do.
					bob_enabled&&
						(visual_layer==viewport_visual_layer::center||anchor_index>=0),
					bob_rider?anchor_index:-1,
					{}
					};
				// The bob lifts the sprite into the row above its path, so that row (and its
				// mirrored image) must be erasable and repaintable too. If it is outside the
				// clip or burning, this tile cannot bob; the whole creature then glides
				// without the bob (resolved below), because the bob is optional and the glide
				// is not. Only the candidate is decided here; the coverage is added after the
				// creature-level decision.
				if(proxy.bob)
					{
					const int32_t bob_row=int32_t(std::floor(
						std::min(proxy.source_y,float(y))))-1;
					for(int32_t bob_x=int32_t(std::floor(
							std::min(proxy.source_x,float(x))));
						bob_x<=int32_t(std::ceil(
							std::max(proxy.source_x,float(x))))&&proxy.bob;++bob_x)
						for(const int32_t shifted_x:{bob_x,bob_x+proxy.mirror_shift})
							if(!vp->inside_clip(shifted_x,bob_row)||
								vp->burning(shifted_x,bob_row))
								{
								proxy.bob=false;
								break;
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
						if(!vp->inside_clip(coverage_x,coverage_y))
							{
							blocked=true;
							break;
							}
						if(visual_render_group(proxy.layer)==visual_render_groupst::main&&
							vp->burning(coverage_x,coverage_y))
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
						if(!vp->inside_clip(tile.first,tile.second))
							{
							blocked=true;
							break;
							}
						if(visual_render_group(proxy.layer)==visual_render_groupst::main&&
							vp->burning(tile.first,tile.second))
							{
							blocked=true;
							break;
							}
						proxy.coverage.insert(tile);
						}
					if(blocked)continue;
					}

				proxy.texture=texture_of(texpos);
				if(proxy.texture==nullptr)continue;
				proxies.push_back(std::move(proxy));
				}
			}
		}

	// One bob per creature: if any tile riding on a centre cannot bob, none of them do, so a
	// multi-tile creature never tears and an icon never detaches from its creature. Then the
	// row above every bobbing tile joins its coverage; the candidate check above already
	// established that row is inside the clip and not burning.
	if(bob_enabled)
		{
		std::vector<int32_t> anchors;
		std::vector<bool> bobs;
		anchors.reserve(proxies.size());
		bobs.reserve(proxies.size());
		for(const render_proxyst &proxy:proxies)
			{
			anchors.push_back(proxy.anchor);
			bobs.push_back(proxy.bob);
			}
		resolve_creature_bob(anchors,bobs);
		for(size_t i=0;i<proxies.size();++i)
			{
			render_proxyst &proxy=proxies[i];
			proxy.bob=bobs[i];
			if(!proxy.bob)continue;
			const int32_t bob_row=int32_t(std::floor(
				std::min(proxy.source_y,float(proxy.target_y))))-1;
			for(int32_t bob_x=int32_t(std::floor(
					std::min(proxy.source_x,float(proxy.target_x))));
				bob_x<=int32_t(std::ceil(
					std::max(proxy.source_x,float(proxy.target_x))));++bob_x)
				{
				proxy.coverage.emplace(bob_x,bob_row);
				proxy.coverage.emplace(bob_x+proxy.mirror_shift,bob_row);
				}
			}
		}

	// A creature that has stopped still needs its mirrored sprite painted each frame.
	// Otherwise the engine repaints it natively and the two orientations alternate between steps.
	// A fragment's tile is its anchor minus the layer's centre offset, inverting the moving path.
	if(flip_enabled)
		{
		for(const int32_t anchor_index:animation_manager.mirrored_tiles(vp->id))
			{
				{
				const int32_t anchor_x=anchor_index/vp->grid.dim_y;
				const int32_t anchor_y=anchor_index%vp->grid.dim_y;
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
					if(x<0||x>=vp->grid.dim_x||y<0||y>=vp->grid.dim_y)continue;
					const size_t layer=static_cast<size_t>(visual_layer);
					const int32_t texpos=layers[layer][x*vp->grid.dim_y+y];
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
						false,
						-1,
						{}
						};
					// The sprite lands on x+mirror_shift, so that interval must be repaintable.
					// The shift has either sign, so order the interval ends first.
					bool blocked=false;
					for(const int32_t coverage_x:{x,x+proxy.mirror_shift})
						{
						if(!vp->inside_clip(coverage_x,y)||
							(group==visual_render_groupst::main&&
							vp->burning(coverage_x,y)))
							{
							blocked=true;
							break;
							}
						proxy.coverage.emplace(coverage_x,y);
						}
					if(blocked)continue;

					proxy.texture=texture_of(texpos);
					if(proxy.texture==nullptr)continue;
					proxies.push_back(std::move(proxy));
					}
				}
			}
		}
	return proxies;
}

render_coveragest old_collect_coverage(
	const std::vector<render_proxyst> &proxies,
	int32_t dim_y)
{
	render_coveragest coverage;
	for(const render_proxyst &proxy:proxies)
		{
		coverage.all.insert(proxy.coverage.begin(),proxy.coverage.end());
		coverage.selected[proxy.target_x*dim_y+proxy.target_y]|=
			visual_layer_bit(proxy.layer);
		auto &group=coverage.groups[static_cast<size_t>(visual_render_group(proxy.layer))];
		group.insert(proxy.coverage.begin(),proxy.coverage.end());
		}
	return coverage;
}


} // namespace oracle
