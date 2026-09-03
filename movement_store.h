// SPDX-License-Identifier: MIT

#ifndef MOVEMENT_STORE_H
#define MOVEMENT_STORE_H

#include "visual_layers.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// One sprite in flight: it was last drawn at (source_x,source_y), possibly mid-step, and the
// buffers now show it on the target tile.
struct visual_movementst
{
	viewport_visual_layer layer;
	int32_t texpos;
	float source_x;
	float source_y;
	int32_t target_x;
	int32_t target_y;
	uint32_t start_time_ms;
	uint32_t duration_ms;

	float progress(uint32_t now_ms) const
		{
		return animation_progress(now_ms,start_time_ms,duration_ms);
		}
	bool finished(uint32_t now_ms) const
		{
		return now_ms-start_time_ms>=duration_ms;
		}
	// Where the sprite is drawn at `now_ms`, in tiles.
	float visual_x(uint32_t now_ms) const
		{
		return source_x+(target_x-source_x)*progress(now_ms);
		}
	float visual_y(uint32_t now_ms) const
		{
		return source_y+(target_y-source_y)*progress(now_ms);
		}
};

// The movements of one viewport, indexed by (layer, target tile) so the renderer's lookups
// are a binary search rather than a scan of every movement.
class movement_storest
{
	std::vector<visual_movementst> movements;
	std::vector<std::pair<uint64_t,int32_t>> index;

	static uint64_t key(viewport_visual_layer layer,int32_t target_x,int32_t target_y)
		{
		// Targets are viewport tile coordinates, so both fit in 16 bits with room to spare.
		return (uint64_t(static_cast<uint8_t>(layer))<<32)|
			(uint64_t(uint16_t(target_x))<<16)|uint64_t(uint16_t(target_y));
		}

	void rebuild_index()
		{
		index.clear();
		index.reserve(movements.size());
		for(size_t i=0;i<movements.size();++i)
			{
			const visual_movementst &movement=movements[i];
			index.emplace_back(
				key(movement.layer,movement.target_x,movement.target_y),int32_t(i));
			}
		// Ordered by position for equal keys, so the first registered wins.
		std::sort(index.begin(),index.end());
		}

	public:
		bool empty() const
			{
			return movements.empty();
			}

		const std::vector<visual_movementst> &all() const
			{
			return movements;
			}

		void clear()
			{
			movements.clear();
			index.clear();
			}

		void append(const std::vector<visual_movementst> &added)
			{
			movements.insert(movements.end(),added.begin(),added.end());
			rebuild_index();
			}

		const visual_movementst *find(
			viewport_visual_layer layer,
			int32_t target_x,
			int32_t target_y) const
			{
			const uint64_t wanted=key(layer,target_x,target_y);
			const auto it=std::lower_bound(
				index.begin(),index.end(),std::make_pair(wanted,int32_t(-1)));
			if(it==index.end()||it->first!=wanted)return nullptr;
			return &movements[size_t(it->second)];
			}

		template<typename Predicate>
		void erase_if(const Predicate &predicate)
			{
			movements.erase(
				std::remove_if(movements.begin(),movements.end(),predicate),
				movements.end());
			rebuild_index();
			}

		// The view scrolled by (dx,dy): re-anchor every movement and drop what left the grid.
		void translate(const visual_gridst &grid,int32_t dx,int32_t dy)
			{
			erase_if([&](visual_movementst &movement)
				{
				movement.source_x-=dx;
				movement.source_y-=dy;
				movement.target_x-=dx;
				movement.target_y-=dy;
				return !grid.contains(movement.target_x,movement.target_y);
				});
			}

		// Tiles that can carry a moving sprite, as grid indices, ascending and unique: every
		// target, plus the 3x3 around each centre target, since an icon, fragment or item
		// inherits the motion of a centre next to it.
		void candidate_tiles(const visual_gridst &grid,std::vector<int32_t> &tiles) const
			{
			tiles.clear();
			for(const visual_movementst &movement:movements)
				{
				const int32_t spread=movement.layer==viewport_visual_layer::center?1:0;
				for(int32_t dx=-spread;dx<=spread;++dx)
					{
					for(int32_t dy=-spread;dy<=spread;++dy)
						{
						const int32_t x=movement.target_x+dx;
						const int32_t y=movement.target_y+dy;
						if(grid.contains(x,y))tiles.push_back(grid.index(x,y));
						}
					}
				}
			std::sort(tiles.begin(),tiles.end());
			tiles.erase(std::unique(tiles.begin(),tiles.end()),tiles.end());
			}
};

#endif
