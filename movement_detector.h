// SPDX-License-Identifier: MIT

#ifndef MOVEMENT_DETECTOR_H
#define MOVEMENT_DETECTOR_H

#include "facing_grid.h"
#include "movement_store.h"
#include "visual_layers.h"

#include <array>
#include <cstdint>
#include <vector>

// The one step most creatures took this redraw, when a clear majority agree on it: a squad
// column or a crowd marching in lockstep. Zero when nothing moved or the vote is split.
inline std::array<int32_t,2> shared_movement_delta(
	const visual_gridst &grid,
	const int32_t *current,
	const int32_t *previous)
{
	std::array<int32_t,2> best{};
	int32_t best_count=1;
	bool ambiguous=false;
	for(int32_t dx=-1;dx<=1;++dx)
		{
		for(int32_t dy=-1;dy<=1;++dy)
			{
			if(dx==0&&dy==0)continue;
			int32_t count=0;
			for(int32_t x=0;x<grid.dim_x;++x)
				{
				for(int32_t y=0;y<grid.dim_y;++y)
					{
					if(!grid.contains(x+dx,y+dy))continue;
					const int32_t target=grid.index(x,y);
					const int32_t texpos=current[target];
					if(texpos!=0&&previous[target]!=texpos&&
						previous[grid.index(x+dx,y+dy)]==texpos)++count;
					}
				}
			if(count>best_count)
				{
				best_count=count;
				best={dx,dy};
				ambiguous=false;
				}
			else if(count==best_count&&count>1)ambiguous=true;
			}
		}
	return ambiguous?std::array<int32_t,2>{}:best;
}

// Reads the sprites that stepped between the previous and the current buffers, registers a
// movement for each and turns the horizontal ones into facings.
class movement_detectorst
{
	// Scratch that persists across frames so a redraw allocates nothing.
	std::vector<uint8_t> claimed_sources;
	std::vector<visual_movementst> added;
	std::vector<int8_t> facing_at_frame_start;
	std::vector<uint8_t> facing_target_written;
	std::vector<int32_t> facing_source_clears;

	// The tile the sprite now on `target` came from, or -1: the shared step if it fits, else
	// a unique same-sprite move between empty cells. A source serves one sprite per redraw.
	int32_t find_source(
		const visual_gridst &grid,
		viewport_visual_layer layer,
		const int32_t *current,
		const int32_t *previous,
		int32_t x,
		int32_t y,
		const std::array<int32_t,2> &shared_delta) const
		{
		const int32_t target=grid.index(x,y);
		const int32_t texpos=current[target];
		if(shared_delta[0]!=0||shared_delta[1]!=0)
			{
			const int32_t sx=x+shared_delta[0];
			const int32_t sy=y+shared_delta[1];
			if(grid.contains(sx,sy))
				{
				const int32_t candidate=grid.index(sx,sy);
				if(!claimed_sources[candidate]&&previous[target]!=texpos&&
					previous[candidate]==texpos)return candidate;
				}
			}
		if(previous[target]!=0)return -1;
		int32_t source=-1;
		int32_t candidate_count=0;
		for(int32_t dx=-1;dx<=1;++dx)
			{
			for(int32_t dy=-1;dy<=1;++dy)
				{
				if(dx==0&&dy==0)continue;
				if(!grid.contains(x+dx,y+dy))continue;
				const int32_t candidate=grid.index(x+dx,y+dy);
				if(!claimed_sources[candidate]&&
					visual_layer_matches(layer,texpos,previous[candidate])&&
					current[candidate]==0)
					{
					source=candidate;
					++candidate_count;
					}
				}
			}
		return candidate_count==1?source:-1;
		}

	public:
		void detect(
			const visual_gridst &grid,
			const visual_layer_pointerst &current_layers,
			const visual_layer_pointerst &previous_layers,
			movement_storest &store,
			facing_gridst &facing,
			uint32_t now_ms,
			uint32_t duration_ms)
			{
			const size_t tile_count=grid.tile_count();
			claimed_sources.assign(tile_count,0);
			added.clear();
			// A creature can vacate a tile another creature steps onto in the same redraw, a
			// chain, so source clears wait until every movement this frame is registered; and
			// a chained movement's source facing may already have been rewritten, so the
			// frame's starting facings are read from a snapshot.
			const bool track_facing=facing.size()==tile_count;
			if(track_facing)
				{
				facing_at_frame_start=facing.snapshot();
				facing_target_written.assign(tile_count,0);
				facing_source_clears.clear();
				}
			const int32_t *previous_center=
				previous_layers[static_cast<size_t>(viewport_visual_layer::center)];
			for(size_t layer_index=0;layer_index<visual_layer_count;++layer_index)
				{
				const auto layer=static_cast<viewport_visual_layer>(layer_index);
				if(!visual_layer_tracks_own_movement(layer))continue;
				std::fill(claimed_sources.begin(),claimed_sources.end(),0);
				const int32_t *current=current_layers[layer_index];
				const int32_t *previous=previous_layers[layer_index];
				const std::array<int32_t,2> shared_delta=
					layer==viewport_visual_layer::center?
					shared_movement_delta(grid,current,previous):
					std::array<int32_t,2>{};
				for(int32_t x=0;x<grid.dim_x;++x)
					{
					for(int32_t y=0;y<grid.dim_y;++y)
						{
						const int32_t target=grid.index(x,y);
						const int32_t texpos=current[target];
						if(texpos==0)continue;
						// An item on a tile a creature just left is the one it carried.
						if(layer==viewport_visual_layer::item&&
							previous_center[target]!=0)continue;
						const int32_t source=find_source(
							grid,layer,current,previous,x,y,shared_delta);
						if(source<0)continue;
						claimed_sources[source]=1;
						const int32_t source_x=source/grid.dim_y;
						const int32_t source_y=source%grid.dim_y;
						// A sprite still in flight to this source retargets from where it
						// is drawn now, not from the tile it never quite reached.
						float visual_source_x=float(source_x);
						float visual_source_y=float(source_y);
						if(const visual_movementst *chained=
							store.find(layer,source_x,source_y))
							{
							visual_source_x=chained->visual_x(now_ms);
							visual_source_y=chained->visual_y(now_ms);
							}
						added.push_back(
							{layer,texpos,visual_source_x,visual_source_y,x,y,now_ms,duration_ms});
						if(layer==viewport_visual_layer::center&&track_facing)
							{
							facing.set(size_t(target),facing_after_move(
								x-source_x,
								facing_gridst::read(facing_at_frame_start,size_t(source))));
							facing_target_written[size_t(target)]=1;
							facing_source_clears.push_back(source);
							}
						}
					}
				}
			if(track_facing)
				{
				// A source vacates its tile only if no movement this frame claimed it as a target.
				for(const int32_t source:facing_source_clears)
					if(!facing_target_written[size_t(source)])
						facing.set(size_t(source),native_sprite_facing);
				}
			store.append(added);
			}
};

#endif
