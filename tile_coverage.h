// SPDX-License-Identifier: MIT

#ifndef TILE_COVERAGE_H
#define TILE_COVERAGE_H

#include "visual_layers.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// The tiles a frame repaints, as one byte per tile: bit 0 for any coverage and one bit per
// render group above it. Plus, per tile, the layers that have a proxy targeting it, which the
// engine's redraw must then leave blank. Iteration is column by column, top to bottom.
class tile_coveragest
{
	visual_gridst grid;
	std::vector<uint8_t> marks;
	std::vector<uint16_t> proxied;
	std::vector<int32_t> touched;
	int32_t min_x=0;
	int32_t max_x=-1;
	int32_t min_y=0;
	int32_t max_y=-1;

	static constexpr uint8_t any_bit=1;

	static constexpr uint8_t group_bit(visual_render_groupst group)
		{
		return uint8_t(2U<<static_cast<uint8_t>(group));
		}

	void touch(int32_t x,int32_t y,uint8_t bits)
		{
		const int32_t index=grid.index(x,y);
		if(marks[size_t(index)]==0)
			{
			touched.push_back(index);
			if(touched.size()==1)
				{
				min_x=max_x=x;
				min_y=max_y=y;
				}
			else
				{
				min_x=std::min(min_x,x);
				max_x=std::max(max_x,x);
				min_y=std::min(min_y,y);
				max_y=std::max(max_y,y);
				}
			}
		marks[size_t(index)]|=bits;
		}

	public:
		void reset(const visual_gridst &new_grid)
			{
			if(grid!=new_grid||marks.size()!=new_grid.tile_count())
				{
				grid=new_grid;
				marks.assign(grid.tile_count(),0);
				proxied.assign(grid.tile_count(),0);
				}
			else
				{
				for(const int32_t index:touched)
					{
					marks[size_t(index)]=0;
					proxied[size_t(index)]=0;
					}
				}
			touched.clear();
			max_x=-1;
			max_y=-1;
			}

		void clear()
			{
			reset(grid);
			}

		const visual_gridst &get_grid() const
			{
			return grid;
			}

		bool empty() const
			{
			return touched.empty();
			}

		// Marks a tile a proxy of `group` repaints. Tiles off the grid cannot be repainted.
		void mark(int32_t x,int32_t y,visual_render_groupst group)
			{
			if(grid.contains(x,y))touch(x,y,uint8_t(any_bit|group_bit(group)));
			}

		void mark(int32_t x,int32_t y)
			{
			if(grid.contains(x,y))touch(x,y,any_bit);
			}

		void mark_proxied(int32_t x,int32_t y,viewport_visual_layer layer)
			{
			proxied[size_t(grid.index(x,y))]|=visual_layer_bit(layer);
			}

		bool covers(int32_t x,int32_t y) const
			{
			return grid.contains(x,y)&&(marks[size_t(grid.index(x,y))]&any_bit)!=0;
			}

		uint16_t proxied_layers(int32_t index) const
			{
			return proxied[size_t(index)];
			}

		// Every marked tile of another coverage, wherever the two grids overlap.
		void merge(const tile_coveragest &other)
			{
			for(const int32_t index:other.touched)
				{
				const int32_t x=index/other.grid.dim_y;
				const int32_t y=index%other.grid.dim_y;
				if(grid.contains(x,y))touch(x,y,any_bit);
				}
			}

		template<typename Callback>
		void for_each(const Callback &callback) const
			{
			for_each_marked(any_bit,callback);
			}

		template<typename Callback>
		void for_each_in_group(visual_render_groupst group,const Callback &callback) const
			{
			for_each_marked(group_bit(group),callback);
			}

	private:
		template<typename Callback>
		void for_each_marked(uint8_t bits,const Callback &callback) const
			{
			for(int32_t x=min_x;x<=max_x;++x)
				{
				const uint8_t *column=marks.data()+size_t(grid.index(x,0));
				for(int32_t y=min_y;y<=max_y;++y)
					if(column[y]&bits)callback(x,y);
				}
			}
};

#endif
