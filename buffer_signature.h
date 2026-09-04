// SPDX-License-Identifier: MIT

#ifndef BUFFER_SIGNATURE_H
#define BUFFER_SIGNATURE_H

#include "visual_layers.h"

#include <array>
#include <cstdint>
#include <cstring>

// Identifies the contents of the tracked layers, to tell a redrawn viewport from a repeated
// one: the render hook runs every frame, while the viewport is recomputed only when it
// changes, and while paused hardly at all. Re-reading a landed scroll would step every sprite
// by a tile.
//
// FNV-1a over 64-bit words (two tiles per step) in four independent lanes, since one chain is
// a serial multiply per element over every tracked buffer of every viewport each frame. The
// value is only ever compared with the previous frame's, never stored.
//
// Measured against the alternative of an exact comparison with a retained copy (memcmp and a
// vectorisable word loop): in the game, with nine 38x25 viewports, hashing took 12.6 µs a
// frame and either comparison 16 to 17 µs, because comparing reads two streams where hashing
// reads one, and this pass is bound by memory traffic.
inline uint64_t tracked_buffer_signature(
	const visual_gridst &grid,
	const visual_layer_pointerst &buffers)
{
	constexpr uint64_t fnv_offset_basis=0xcbf29ce484222325ULL;
	constexpr uint64_t fnv_prime=0x100000001b3ULL;
	constexpr size_t lanes=4;
	std::array<uint64_t,lanes> hash;
	for(size_t lane=0;lane<lanes;++lane)hash[lane]=fnv_offset_basis+lane;
	const size_t tile_count=grid.tile_count();
	const size_t word_count=tile_count/2;
	for(size_t layer=0;layer<buffers.size();++layer)
		{
		if(!visual_layer_tracks_own_movement(
			static_cast<viewport_visual_layer>(layer)))continue;
		const int32_t *current=buffers[layer];
		size_t word=0;
		for(;word+lanes<=word_count;word+=lanes)
			{
			for(size_t lane=0;lane<lanes;++lane)
				{
				uint64_t value;
				std::memcpy(&value,current+2*(word+lane),sizeof value);
				hash[lane]=(hash[lane]^value)*fnv_prime;
				}
			}
		for(;word<word_count;++word)
			{
			uint64_t value;
			std::memcpy(&value,current+2*word,sizeof value);
			hash[0]=(hash[0]^value)*fnv_prime;
			}
		if(tile_count%2)
			hash[0]=(hash[0]^uint64_t(uint32_t(current[tile_count-1])))*fnv_prime;
		}
	uint64_t folded=fnv_offset_basis;
	for(const uint64_t lane:hash)folded=(folded^lane)*fnv_prime;
	return folded;
}

#endif
