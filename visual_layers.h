// SPDX-License-Identifier: MIT

#ifndef VISUAL_LAYERS_H
#define VISUAL_LAYERS_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

enum class viewport_visual_layer : uint8_t
{
	right,
	center,
	left,
	upright,
	up,
	upleft,
	vehicle,
	item,
	designation,
	count
};

enum class visual_render_groupst : uint8_t
{
	item,
	vehicle,
	main,
	upper,
	designation,
	count
};

struct visual_layer_descriptorst
{
	viewport_visual_layer layer;
	visual_render_groupst render_group;
	bool moves_independently;
	bool matches_any_previous;
	uint8_t draw_order;
	int8_t center_x;
	int8_t center_y;
};

constexpr std::array visual_layer_descriptors=
	{
	visual_layer_descriptorst{viewport_visual_layer::right,
		visual_render_groupst::main,false,false,3,-1,0},
	visual_layer_descriptorst{viewport_visual_layer::center,
		visual_render_groupst::main,true,false,0,0,0},
	visual_layer_descriptorst{viewport_visual_layer::left,
		visual_render_groupst::main,false,false,4,1,0},
	visual_layer_descriptorst{viewport_visual_layer::upright,
		visual_render_groupst::upper,false,false,5,-1,1},
	visual_layer_descriptorst{viewport_visual_layer::up,
		visual_render_groupst::upper,false,false,6,0,1},
	visual_layer_descriptorst{viewport_visual_layer::upleft,
		visual_render_groupst::upper,false,false,7,1,1},
	visual_layer_descriptorst{viewport_visual_layer::vehicle,
		visual_render_groupst::vehicle,true,true,2,0,0},
	visual_layer_descriptorst{viewport_visual_layer::item,
		visual_render_groupst::item,true,false,1,0,0},
	visual_layer_descriptorst{viewport_visual_layer::designation,
		visual_render_groupst::designation,false,true,8,0,0}
	};

constexpr bool valid_visual_layer_descriptors()
{
	uint16_t draw_orders=0;
	for(size_t i=0;i<visual_layer_descriptors.size();++i)
		{
		const auto &descriptor=visual_layer_descriptors[i];
		if(static_cast<size_t>(descriptor.layer)!=i||
			descriptor.draw_order>=visual_layer_descriptors.size()||
			(draw_orders&(1U<<descriptor.draw_order)))return false;
		draw_orders|=uint16_t(1U<<descriptor.draw_order);
		}
	return true;
}

static_assert(valid_visual_layer_descriptors());

constexpr const visual_layer_descriptorst &visual_layer_descriptor(
	viewport_visual_layer layer)
{
	return visual_layer_descriptors[static_cast<size_t>(layer)];
}

constexpr viewport_visual_layer visual_layer_at_draw_order(uint8_t draw_order)
{
	for(const auto &descriptor:visual_layer_descriptors)
		if(descriptor.draw_order==draw_order)return descriptor.layer;
	return viewport_visual_layer::count;
}

constexpr visual_render_groupst visual_render_group(viewport_visual_layer layer)
{
	return visual_layer_descriptor(layer).render_group;
}

constexpr bool visual_layer_moves_independently(viewport_visual_layer layer)
{
	return visual_layer_descriptor(layer).moves_independently;
}

constexpr bool visual_layer_tracks_own_movement(viewport_visual_layer layer)
{
	const auto &descriptor=visual_layer_descriptor(layer);
	return descriptor.moves_independently||
		descriptor.render_group==visual_render_groupst::designation;
}

constexpr bool visual_layer_matches(
	viewport_visual_layer layer,
	int32_t current,
	int32_t previous)
{
	return visual_layer_descriptor(layer).matches_any_previous?
		previous!=0:
		previous==current;
}

constexpr size_t visual_layer_count=static_cast<size_t>(viewport_visual_layer::count);

// One bit per layer, for masks of layers to suppress or of layers a tile has proxies on.
constexpr uint16_t visual_layer_bit(viewport_visual_layer layer)
{
	return uint16_t(1U<<static_cast<uint8_t>(layer));
}

// The layers a redraw through `group` has already painted as proxies, and so must hide.
// Designations are drawn last and stand alone.
constexpr uint16_t compute_visual_layers_through_group(visual_render_groupst group)
{
	uint16_t mask=0;
	for(const auto &descriptor:visual_layer_descriptors)
		if(descriptor.render_group!=visual_render_groupst::designation&&
			static_cast<uint8_t>(descriptor.render_group)<=static_cast<uint8_t>(group))
			mask|=visual_layer_bit(descriptor.layer);
	return mask;
}

constexpr std::array<uint16_t,static_cast<size_t>(visual_render_groupst::count)>
	visual_layers_through_group_table=[]
{
	std::array<uint16_t,static_cast<size_t>(visual_render_groupst::count)> table{};
	for(size_t i=0;i<table.size();++i)
		table[i]=compute_visual_layers_through_group(static_cast<visual_render_groupst>(i));
	return table;
}();

constexpr uint16_t visual_layers_through_group(visual_render_groupst group)
{
	return visual_layers_through_group_table[static_cast<size_t>(group)];
}

constexpr uint16_t all_visual_layers_mask=uint16_t((1U<<visual_layer_count)-1);
using visual_layer_pointerst=std::array<const int32_t *,visual_layer_count>;

// A viewport's tile grid. The buffers are column-major: index = x*dim_y+y.
struct visual_gridst
{
	int32_t dim_x=0;
	int32_t dim_y=0;

	constexpr bool contains(int32_t x,int32_t y) const
		{
		return x>=0&&x<dim_x&&y>=0&&y<dim_y;
		}
	constexpr int32_t index(int32_t x,int32_t y) const
		{
		return x*dim_y+y;
		}
	constexpr size_t tile_count() const
		{
		return size_t(dim_x)*size_t(dim_y);
		}
	constexpr bool operator==(const visual_gridst &other) const
		{
		return dim_x==other.dim_x&&dim_y==other.dim_y;
		}
	constexpr bool operator!=(const visual_gridst &other) const
		{
		return !(*this==other);
		}
};

// out[x,y] = in[x+dx,y+dy] where that tile exists, `fill` elsewhere: the grid as it looks
// after the view scrolled by (dx,dy).
template<typename T>
void translate_grid(
	const visual_gridst &grid,
	const T *in,
	T *out,
	int32_t dx,
	int32_t dy,
	T fill)
{
	for(int32_t x=0;x<grid.dim_x;++x)
		{
		for(int32_t y=0;y<grid.dim_y;++y)
			{
			const int32_t sx=x+dx;
			const int32_t sy=y+dy;
			out[grid.index(x,y)]=grid.contains(sx,sy)?in[grid.index(sx,sy)]:fill;
			}
		}
}

struct viewport_visual_animation_inputst
{
	const void *viewport=nullptr;
	int32_t dim_x=0;
	int32_t dim_y=0;
	uint64_t context_revision=0;
	visual_layer_pointerst current{};
	visual_layer_pointerst previous{};
	// Current map-scroll offset (window_x/window_y). A pure pan does not bump context_revision.
	// Only a hint: it changes at input time, the buffers shift on a later render frame.
	int32_t pan_x=0;
	int32_t pan_y=0;

	visual_gridst grid() const
		{
		return {dim_x,dim_y};
		}

	bool valid() const
		{
		if(viewport==nullptr||dim_x<=0||dim_y<=0)return false;
		for(size_t layer=0;layer<current.size();++layer)
			{
			if(current[layer]==nullptr||previous[layer]==nullptr)return false;
			}
		return true;
		}
};

struct visual_movement_renderst
{
	bool active=false;
	float source_x=0.0f;
	float source_y=0.0f;
	float progress=1.0f;
	bool inherited=false;
};

inline float animation_progress(
	uint32_t now_ms,
	uint32_t start_time_ms,
	uint32_t duration_ms)
{
	const float linear=std::min(
		1.0f,float(now_ms-start_time_ms)/duration_ms);
	return linear*linear*(3.0f-2.0f*linear);
}

// Walk bob: how far, in tiles, a moving sprite is lifted at 'progress' through a step.
// |sin(pi*hops*progress)| rises and falls once per hop and is zero at both ends, so the
// sprite always lands on the grid. 'multiplier' is the per-direction factor.
inline float walk_bob_lift(float progress,int hops,float amplitude,float multiplier)
{
	return std::fabs(std::sin(progress*3.14159265f*float(hops)))*amplitude*multiplier;
}

// Which multiplier a step takes. The source can be fractional when a step retargets from an
// in-flight position, so it is rounded back to the tile the creature was last seen on; the
// direction is that of the whole tile step, not of the remaining fraction.
enum class walk_bob_directionst{horizontal,diagonal,vertical};

inline walk_bob_directionst walk_bob_direction(
	float source_x,float source_y,int32_t target_x,int32_t target_y)
{
	const bool same_x=std::lround(source_x)==target_x;
	const bool same_y=std::lround(source_y)==target_y;
	if(same_y)return walk_bob_directionst::horizontal;
	if(same_x)return walk_bob_directionst::vertical;
	return walk_bob_directionst::diagonal;
}

// One bob per creature. anchors[i] is the index of the proxy that i rides on (-1 for a root:
// a creature's centre tile), bob[i] whether i could bob on its own. Afterwards every proxy
// carries its root's decision, which is the AND over everything riding on that root. Anchors
// are followed to the root, so the depth of the chain does not matter.
inline void resolve_creature_bob(const std::vector<int32_t> &anchors,std::vector<bool> &bob)
{
	const size_t count=anchors.size();
	auto root_of=[&](size_t i)
		{
		for(size_t hops=0;anchors[i]>=0&&hops<count;++hops)i=size_t(anchors[i]);
		return i;
		};
	for(size_t i=0;i<count;++i)
		if(!bob[i])bob[root_of(i)]=false;
	for(size_t i=0;i<count;++i)
		bob[i]=bob[root_of(i)];
}

// The renderer erases and repaints exactly one row above a bobbing sprite's path, so the
// tallest possible lift must stay under a tile or the apex leaves stale pixels behind.
constexpr float max_walk_bob_lift=0.9f;

inline bool walk_bob_lift_fits(
	float amplitude,float horizontal,float diagonal,float vertical)
{
	// A little slack so a product that lands on the cap (0.3 x 3.0) is not rejected by rounding.
	return amplitude*std::max({horizontal,diagonal,vertical})<=max_walk_bob_lift+1e-4f;
}

inline bool visual_moved_between_tiles(
	viewport_visual_layer layer,
	const int32_t *current,
	const int32_t *previous,
	int32_t source,
	int32_t target)
{
	return previous[target]==0&&current[source]==0&&
		(layer==viewport_visual_layer::designation||previous[source]!=0);
}

inline int32_t inherited_visual_source_tile(
	int32_t overlay_target,
	float center_source,
	float center_target)
{
	return overlay_target+int32_t(std::lround(center_source-center_target));
}

enum class visual_facingst : int8_t
{
	east=0,
	west=1
};

// DF creature art faces west, so only east needs flipping. Also the default and cleared value.
constexpr visual_facingst native_sprite_facing=visual_facingst::west;

// Sticky facing: only a horizontal component changes it.
constexpr visual_facingst facing_after_move(
	int32_t dx,
	visual_facingst previous)
{
	if(dx>0)return visual_facingst::east;
	if(dx<0)return visual_facingst::west;
	return previous;
}

// center_x is only -1, 0 or +1, so a creature is at most three columns wide here.
constexpr int32_t mirrored_tile_x(int32_t piece_x,int32_t anchor_x)
{
	return anchor_x-(piece_x-anchor_x);
}

#endif
