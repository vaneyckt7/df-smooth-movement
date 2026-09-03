// SPDX-License-Identifier: MIT

#ifndef SPRITE_PROXIES_H
#define SPRITE_PROXIES_H

#include "visual_layers.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include <vector>

// Everything the sprite pass reads from one viewport, without the engine's type.
struct viewport_viewst
{
	const void *id=nullptr;
	visual_gridst grid;
	int32_t clip_x0=0;
	int32_t clip_x1=-1;
	int32_t clip_y0=0;
	int32_t clip_y1=-1;
	visual_layer_pointerst current{};
	visual_layer_pointerst previous{};
	// Per-tile spatter flags; the fire bits mark tiles the engine animates itself.
	const uint32_t *spatter_flags=nullptr;

	static constexpr uint32_t fire_bits=0x70000000U;

	bool inside_clip(int32_t x,int32_t y) const
		{
		return x>=clip_x0&&x<=clip_x1&&y>=clip_y0&&y<=clip_y1;
		}

	bool burning(int32_t x,int32_t y) const
		{
		return spatter_flags!=nullptr&&
			(spatter_flags[grid.index(x,y)]&fire_bits)!=0;
		}

};

// An inclusive tile rectangle.
struct tile_boxst
{
	int32_t x0=0;
	int32_t x1=-1;
	int32_t y0=0;
	int32_t y1=-1;
};

// One sprite the plugin paints itself this frame, in place of the engine's copy.
struct sprite_proxyst
{
	viewport_visual_layer layer;
	float source_x;
	float source_y;
	int32_t target_x;
	int32_t target_y;
	int32_t texpos;
	float progress;
	const void *texture=nullptr;
	bool mirrored=false;
	int32_t mirror_shift=0;
	// The walk bob is decided per creature: fragments, status icons and carried items point
	// at their centre proxy (an index into the proxy list, -1 for none) and follow its bob.
	bool bob=false;
	int32_t anchor=-1;

	// The tiles the sprite crosses between source and target.
	tile_boxst path() const
		{
		return {
			int32_t(std::floor(std::min(source_x,float(target_x)))),
			int32_t(std::ceil(std::max(source_x,float(target_x)))),
			int32_t(std::floor(std::min(source_y,float(target_y)))),
			int32_t(std::ceil(std::max(source_y,float(target_y))))};
		}

	// The row the bob lifts the sprite into.
	int32_t bob_row() const
		{
		return path().y0-1;
		}

	// Every tile this proxy may paint over: its path, the mirrored path when flipped, and
	// the bob row above both when bobbing. Tiles may repeat; callers mark, not count.
	template<typename Callback>
	void for_each_covered_tile(const Callback &callback) const
		{
		const tile_boxst box=path();
		for(int32_t x=box.x0;x<=box.x1;++x)
			{
			for(int32_t y=box.y0;y<=box.y1;++y)
				{
				callback(x,y);
				if(mirror_shift!=0)callback(x+mirror_shift,y);
				}
			if(bob)
				{
				callback(x,box.y0-1);
				callback(x+mirror_shift,box.y0-1);
				}
			}
		}
};

struct sprite_settingst
{
	bool flip=false;
	bool bob=false;
};

// Builds the proxy list for one viewport from the animation manager's movements and facings.
// Owns its scratch so a frame allocates nothing once warmed up.
class sprite_collectorst
{
	std::vector<sprite_proxyst> proxies;
	std::vector<int32_t> candidates;
	std::vector<int32_t> mirrored;
	std::vector<int32_t> anchors;
	std::vector<bool> bobs;
	// Per tile: the index of the centre proxy targeting it, and the layers already proxied.
	std::vector<int32_t> center_proxy_at;
	std::vector<uint16_t> proxied_layers;
	std::vector<int32_t> touched;

	void reset_scratch(const visual_gridst &grid)
		{
		proxies.clear();
		if(center_proxy_at.size()!=grid.tile_count())
			{
			center_proxy_at.assign(grid.tile_count(),-1);
			proxied_layers.assign(grid.tile_count(),0);
			}
		else
			{
			for(const int32_t index:touched)
				{
				center_proxy_at[size_t(index)]=-1;
				proxied_layers[size_t(index)]=0;
				}
			}
		touched.clear();
		}

	void push(const visual_gridst &grid,const sprite_proxyst &proxy)
		{
		const int32_t index=grid.index(proxy.target_x,proxy.target_y);
		if(proxied_layers[size_t(index)]==0)touched.push_back(index);
		proxied_layers[size_t(index)]|=visual_layer_bit(proxy.layer);
		if(proxy.layer==viewport_visual_layer::center)
			center_proxy_at[size_t(index)]=int32_t(proxies.size());
		proxies.push_back(proxy);
		}

	// The centre proxy that steps in lockstep with (x,y): same delta, same progress. Within
	// the 3x3 around the tile, or exactly at the fragment's centre when `exact` is set. Row by
	// row so the earliest-listed candidate wins, as the proxies are listed row by row.
	int32_t lockstep_center(
		const visual_gridst &grid,
		int32_t x,
		int32_t y,
		const visual_movement_renderst &movement,
		int32_t exact_dx,
		int32_t exact_dy,
		bool exact) const
		{
		const auto matches=[&](int32_t cx,int32_t cy)
			{
			if(!grid.contains(cx,cy))return -1;
			const int32_t index=center_proxy_at[size_t(grid.index(cx,cy))];
			if(index<0)return -1;
			const sprite_proxyst &anchor=proxies[size_t(index)];
			return anchor.source_x-anchor.target_x==movement.source_x-x&&
				anchor.source_y-anchor.target_y==movement.source_y-y&&
				anchor.progress==movement.progress?index:-1;
			};
		if(exact)return matches(x+exact_dx,y+exact_dy);
		for(int32_t dy=-1;dy<=1;++dy)
			for(int32_t dx=-1;dx<=1;++dx)
				if(const int32_t index=matches(x+dx,y+dy);index>=0)return index;
		return -1;
		}

	// A tile is paintable when it is inside the clip and, for the main group, not burning:
	// the engine animates fire itself and a proxy would fight it.
	static bool paintable(
		const viewport_viewst &view,
		viewport_visual_layer layer,
		int32_t x,
		int32_t y)
		{
		return view.inside_clip(x,y)&&
			!(visual_render_group(layer)==visual_render_groupst::main&&view.burning(x,y));
		}

	static bool path_paintable(const viewport_viewst &view,const sprite_proxyst &proxy)
		{
		const tile_boxst box=proxy.path();
		for(int32_t x=box.x0;x<=box.x1;++x)
			for(int32_t y=box.y0;y<=box.y1;++y)
				if(!paintable(view,proxy.layer,x,y)||
					(proxy.mirror_shift!=0&&
					!paintable(view,proxy.layer,x+proxy.mirror_shift,y)))return false;
		return true;
		}

	// The bob lifts the sprite into the row above its path, so that row (and its mirrored
	// image) must be repaintable too. Fire there is never paintable, whatever the layer.
	static bool bob_row_paintable(const viewport_viewst &view,const sprite_proxyst &proxy)
		{
		const tile_boxst box=proxy.path();
		const int32_t row=box.y0-1;
		for(int32_t x=box.x0;x<=box.x1;++x)
			for(const int32_t shifted_x:{x,x+proxy.mirror_shift})
				if(!view.inside_clip(shifted_x,row)||view.burning(shifted_x,row))return false;
		return true;
		}

	template<typename Manager,typename Texture>
	void collect_moving(
		const viewport_viewst &view,
		const Manager &manager,
		const sprite_settingst &settings,
		const Texture &texture_of)
		{
		const visual_gridst &grid=view.grid;
		manager.movement_candidate_tiles(view.id,candidates);
		// Row by row, left to right: a centre proxy must precede the tiles that ride on it.
		for(int32_t &index:candidates)index=(index%grid.dim_y)*grid.dim_x+index/grid.dim_y;
		std::sort(candidates.begin(),candidates.end());
		for(int32_t &index:candidates)index=(index%grid.dim_x)*grid.dim_y+index/grid.dim_x;
		for(uint8_t draw_order=0;draw_order<visual_layer_count;++draw_order)
			{
			const viewport_visual_layer layer=visual_layer_at_draw_order(draw_order);
			const auto &descriptor=visual_layer_descriptor(layer);
			const bool independent=visual_layer_moves_independently(layer);
			// Creatures bob and whatever rides on one bobs with it; vehicles never do.
			const bool rider=layer!=viewport_visual_layer::center&&
				layer!=viewport_visual_layer::vehicle;
			const visual_render_groupst group=visual_render_group(layer);
			const bool mirror_eligible=settings.flip&&
				(group==visual_render_groupst::main||group==visual_render_groupst::upper);
			for(const int32_t index:candidates)
				{
				const int32_t x=index/grid.dim_y;
				const int32_t y=index%grid.dim_y;
				const int32_t texpos=view.current[static_cast<size_t>(layer)][index];
				if(texpos==0)continue;
				const visual_movement_renderst movement=manager.get_movement(view.id,layer,x,y);
				if(!movement.active)continue;
				const int32_t inherited_x=inherited_visual_source_tile(x,movement.source_x,x);
				const int32_t inherited_y=inherited_visual_source_tile(y,movement.source_y,y);
				const bool inherited_in_bounds=grid.contains(inherited_x,inherited_y);
				// The centre proxy this tile rides on: a fragment, icon or item sharing the
				// creature's motion. A centre is its own root and a vehicle takes no part, so
				// neither couples to a neighbour moving in lockstep (a dwarf and its wheelbarrow).
				int32_t anchor=rider||!independent?
					lockstep_center(grid,x,y,movement,0,0,false):-1;
				if(!independent&&anchor<0)continue;
				if((layer==viewport_visual_layer::item||
					layer==viewport_visual_layer::designation)&&movement.inherited)
					{
					// An item on a tile a creature just left is the one it carried.
					if(layer==viewport_visual_layer::item&&
						view.previous[static_cast<size_t>(viewport_visual_layer::center)][index]!=0)
						continue;
					if(!inherited_in_bounds||!visual_moved_between_tiles(
						layer,
						view.current[static_cast<size_t>(layer)],
						view.previous[static_cast<size_t>(layer)],
						grid.index(inherited_x,inherited_y),
						index))continue;
					}
				if(!independent&&layer!=viewport_visual_layer::designation&&movement.inherited)
					{
					// A fragment that did not step itself belongs to the creature whose centre
					// sits at the layer's offset, or it is a different creature's.
					const bool fragment_moved=inherited_in_bounds&&visual_moved_between_tiles(
						layer,
						view.current[static_cast<size_t>(layer)],
						view.previous[static_cast<size_t>(layer)],
						grid.index(inherited_x,inherited_y),
						index);
					if(!fragment_moved)
						{
						const int32_t owner=lockstep_center(
							grid,x,y,movement,descriptor.center_x,descriptor.center_y,true);
						if(owner<0)continue;
						anchor=owner;
						}
					}
				// Facing is read from the anchor tile so every fragment of one creature
				// agrees; the anchor's own layer has a zero offset, so it flips in place.
				// Items, vehicles and designations keep their vanilla orientation.
				const bool mirrored=mirror_eligible&&manager.get_facing(
					view.id,x+descriptor.center_x,y+descriptor.center_y)!=native_sprite_facing;
				sprite_proxyst proxy=
					{
					layer,
					movement.source_x,
					movement.source_y,
					x,
					y,
					texpos,
					movement.progress,
					nullptr,
					mirrored,
					mirrored?mirrored_tile_x(x,x+descriptor.center_x)-x:0,
					settings.bob&&(layer==viewport_visual_layer::center||anchor>=0),
					rider?anchor:-1
					};
				// Only the bob candidate is decided here; whether the creature bobs is
				// resolved once all its tiles are known.
				if(proxy.bob&&!bob_row_paintable(view,proxy))proxy.bob=false;
				if(!path_paintable(view,proxy))continue;
				proxy.texture=texture_of(texpos);
				if(proxy.texture==nullptr)continue;
				push(grid,proxy);
				}
			}
	}

	// One bob per creature: if any tile riding on a centre cannot bob, none of them do, so a
	// multi-tile creature never tears and an icon never detaches from its creature.
	void resolve_bob()
		{
		anchors.clear();
		bobs.clear();
		for(const sprite_proxyst &proxy:proxies)
			{
			anchors.push_back(proxy.anchor);
			bobs.push_back(proxy.bob);
			}
		resolve_creature_bob(anchors,bobs);
		for(size_t i=0;i<proxies.size();++i)proxies[i].bob=bobs[i];
		}

	// A creature that has stopped still needs its mirrored sprite painted each frame, or the
	// engine's native copy and the flipped one alternate between steps. A fragment's tile is
	// its anchor minus the layer's centre offset.
	template<typename Manager,typename Texture>
	void collect_mirrored_stationary(
		const viewport_viewst &view,
		const Manager &manager,
		const Texture &texture_of)
		{
		const visual_gridst &grid=view.grid;
		manager.mirrored_tiles(view.id,mirrored);
		for(const int32_t anchor_index:mirrored)
			{
			const int32_t anchor_x=anchor_index/grid.dim_y;
			const int32_t anchor_y=anchor_index%grid.dim_y;
			for(uint8_t draw_order=0;draw_order<visual_layer_count;++draw_order)
				{
				const viewport_visual_layer layer=visual_layer_at_draw_order(draw_order);
				const visual_render_groupst group=visual_render_group(layer);
				if(group!=visual_render_groupst::main&&
					group!=visual_render_groupst::upper)continue;
				const auto &descriptor=visual_layer_descriptor(layer);
				const int32_t x=anchor_x-descriptor.center_x;
				const int32_t y=anchor_y-descriptor.center_y;
				if(!grid.contains(x,y))continue;
				const int32_t index=grid.index(x,y);
				const int32_t texpos=view.current[static_cast<size_t>(layer)][index];
				if(texpos==0)continue;
				if(proxied_layers[size_t(index)]&visual_layer_bit(layer))continue;
				// source == target at progress 1.0 draws in place, moved only by the shift.
				sprite_proxyst proxy=
					{
					layer,
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
					-1
					};
				if(!path_paintable(view,proxy))continue;
				proxy.texture=texture_of(texpos);
				if(proxy.texture==nullptr)continue;
				push(grid,proxy);
				}
			}
		}

	public:
		// `texture_of(texpos)` resolves a sprite to something drawable, or null to skip it.
		// The list lands in `out`, whose old storage becomes scratch for the next call.
		template<typename Manager,typename Texture>
		void collect(
			const viewport_viewst &view,
			const Manager &manager,
			const sprite_settingst &settings,
			const Texture &texture_of,
			std::vector<sprite_proxyst> &out)
			{
			reset_scratch(view.grid);
			collect_moving(view,manager,settings,texture_of);
			if(settings.bob)resolve_bob();
			if(settings.flip)collect_mirrored_stationary(view,manager,texture_of);
			out.swap(proxies);
			}
};

#endif
