// SPDX-License-Identifier: MIT

#ifndef FACING_GRID_H
#define FACING_GRID_H

#include "visual_layers.h"

#include <algorithm>
#include <cstdint>
#include <vector>

// Which way the creature on each tile faces. One facing per tile, not per unit: the viewport
// exposes one creature texpos per tile. Facing is sticky, set by horizontal steps, and lost
// when the tile empties.
class facing_gridst
{
	std::vector<int8_t> facing;
	// Stationary mirrored creatures are repainted every frame; this is the cheap pre-check,
	// recomputed by settle() rather than maintained at every write.
	bool mirrored=false;

	public:
		void resize(const visual_gridst &grid)
			{
			facing.assign(grid.tile_count(),int8_t(native_sprite_facing));
			mirrored=false;
			}

		void reset()
			{
			std::fill(facing.begin(),facing.end(),int8_t(native_sprite_facing));
			mirrored=false;
			}

		bool has_mirrored() const
			{
			return mirrored;
			}

		size_t size() const
			{
			return facing.size();
			}

		visual_facingst at(size_t index) const
			{
			return index<facing.size()?
				static_cast<visual_facingst>(facing[index]):
				native_sprite_facing;
			}

		void set(size_t index,visual_facingst value)
			{
			facing[index]=int8_t(value);
			}

		// A snapshot, for reading the frame's starting facings while they are rewritten.
		std::vector<int8_t> snapshot() const
			{
			return facing;
			}

		static visual_facingst read(const std::vector<int8_t> &values,size_t index)
			{
			return static_cast<visual_facingst>(values[index]);
			}

		// The view scrolled by (dx,dy): facing describes creatures still on screen, so move it
		// with them rather than drop it.
		void translate(const visual_gridst &grid,int32_t dx,int32_t dy)
			{
			if(facing.size()!=grid.tile_count())return;
			std::vector<int8_t> shifted(facing.size());
			translate_grid(grid,facing.data(),shifted.data(),dx,dy,int8_t(native_sprite_facing));
			facing.swap(shifted);
			}

		// End of a redraw: an empty tile has no creature to face anywhere.
		void settle(const int32_t *center_current)
			{
			bool any_mirrored=false;
			for(size_t i=0;i<facing.size();++i)
				{
				if(center_current[i]==0)facing[i]=int8_t(native_sprite_facing);
				else if(facing[i]!=int8_t(native_sprite_facing))any_mirrored=true;
				}
			mirrored=any_mirrored;
			}

		// Tiles whose creature faces away from the sprite's native side, ascending indices.
		void mirrored_tiles(std::vector<int32_t> &tiles) const
			{
			tiles.clear();
			if(!mirrored)return;
			for(size_t i=0;i<facing.size();++i)
				if(facing[i]!=int8_t(native_sprite_facing))tiles.push_back(int32_t(i));
			}
};

#endif
