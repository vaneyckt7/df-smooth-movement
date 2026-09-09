// SPDX-License-Identifier: MIT

#ifndef VISUAL_ANIMATION_H
#define VISUAL_ANIMATION_H

#include "interpolation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>
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

struct viewport_visual_animation_inputst
{
	const void *viewport=nullptr;
	int32_t dim_x=0;
	int32_t dim_y=0;
	uint64_t context_revision=0;
	std::array<const int32_t *,static_cast<size_t>(viewport_visual_layer::count)> current{};
	std::array<const int32_t *,static_cast<size_t>(viewport_visual_layer::count)> previous{};
	const int32_t *current_background=nullptr;
	const int32_t *previous_background=nullptr;
	// Current map-scroll offset (window_x/window_y). A pure pan does not bump context_revision.
	// Only a hint: it changes at input time, the buffers shift on a later render frame.
	int32_t pan_x=0;
	int32_t pan_y=0;
	// The simulation's frame counter, or -1 when unknown. Creatures only step when it
	// advances; a change in the per-tile arrays at a standing counter is presentation:
	// the units sharing a tile shown in turn, blinking markers, cursor highlights.
	int64_t simulation_tick=-1;

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
	// How far above the line between the tiles the sprite is, in tiles (see interpolation.h).
	float lift=0.0f;
	bool inherited=false;
	uint64_t movement_id=0;
};

using visual_movement_idst=uint64_t;
constexpr visual_movement_idst no_visual_movement=0;

struct visual_scroll_renderst
{
	bool pending=false;
	bool landed=false;
	bool abandoned=false;
	int32_t landed_x=0;
	int32_t landed_y=0;
	int32_t pending_x=0;
	int32_t pending_y=0;
	visual_movement_idst follow_candidate=no_visual_movement;
};

struct visual_follow_renderst
{
	bool active=false;
	visual_movement_idst movement_id=no_visual_movement;
	float offset_x=0.0f;
	float offset_y=0.0f;
};

struct visual_icon_rectst
{
	float x;
	float y;
	float width;
	float height;
};

constexpr visual_icon_rectst carried_item_icon_rect(
	float tile_x,
	float tile_y,
	float tile_size)
{
	return {
		tile_x+tile_size*0.05f,
		tile_y+tile_size*0.2f,
		tile_size*0.7f,
		tile_size*0.7f
		};
}

constexpr bool camera_glide_enabled(bool camera_enabled,bool native_follow_active)
{
	return camera_enabled||native_follow_active;
}

constexpr bool native_follow_changed(int32_t previous_id,int32_t current_id)
{
	return previous_id!=current_id;
}

// Where a step is at 'now_ms': its time progress, capped at the end, through the
// interpolation, for a step in a direction, given the walk bob settings.
inline sprite_positionst animation_position(
	uint32_t now_ms,
	uint32_t start_time_ms,
	uint32_t duration_ms,
	const interpolationst &interpolation=default_interpolation(),
	step_directionst direction=step_directionst::horizontal,
	const walk_bob_settingst &bob=walk_bob_settingst{})
{
	const float progress=std::min(
		1.0f,float(now_ms-start_time_ms)/duration_ms);
	return interpolation.at(progress,direction,bob);
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

// The plugin erases and repaints exactly one row above a bobbing sprite's path, so the
// tallest possible lift must stay under a tile or the apex leaves stale pixels behind.
constexpr float max_walk_bob_lift=0.9f;

inline bool walk_bob_lift_fits(
	float amplitude,float horizontal,float diagonal,float vertical)
{
	// A little slack so a product landing on the cap (0.3 x 3.0) is not rejected by rounding.
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

class visual_animation_managerst
{
	struct movementst
	{
		visual_movement_idst id=no_visual_movement;
		viewport_visual_layer layer;
		int32_t texpos;
		float source_x;
		float source_y;
		int32_t target_x;
		int32_t target_y;
		uint32_t start_time_ms;
		uint32_t duration_ms;
		bool historical=false;
	};

	struct viewport_animationst
	{
		const void *viewport=nullptr;
		int32_t dim_x=0;
		int32_t dim_y=0;
		uint64_t context_revision=0;
		bool has_context=false;
		bool seen=false;
		std::vector<movementst> movements;
		// One facing per tile, not per unit: the viewport exposes one creature texpos per tile.
		std::vector<int8_t> facing;
		// Stationary mirrored creatures are repainted every frame; this is the cheap pre-check.
		bool has_mirrored=false;
		int32_t pan_x=0;
		int32_t pan_y=0;
		bool has_pan=false;
		// Window scrolls not yet observed in the buffers, oldest first.
		// A signed total would cancel on a reversing drag while both shifts are still owed.
		std::vector<std::array<int32_t,2>> pending;
		// Redraws no prefix has matched.
		int32_t pending_frames=0;
		// Redraws spent waiting for the buffers to move at all.
		int32_t pending_age=0;
		// Redraws left in which new-movement detection stays suppressed after scroll activity.
		int32_t suppress_frames=0;
		// Simulation tick the per-tile arrays last changed at, to tell a step from a repaint.
		int64_t buffer_tick=-1;
		// Buffer contents last seen, to recognize a repeat of them.
		uint64_t buffer_signature=0;
		bool has_buffer_signature=false;
		// Set while the previous buffer still belongs to a view that has been left behind.
		bool previous_view_stale=false;
		bool landed_this_frame=false;
		bool abandoned_this_frame=false;
		std::array<int32_t,2> landed_shift{};
		visual_movement_idst follow_candidate=no_visual_movement;
	};

	uint32_t frame_time_ms=0;
	uint32_t frame_delta_ms=0;
	bool has_frame=false;
	bool force_full_redraw=false;
	// The interpolation every step follows (its pacing decides the cadence rules below), and
	// the walk bob settings, the parameters of the ones that lift (`bob` below).
	const interpolationst *interpolation=&default_interpolation();
	visual_movement_idst next_movement_id=1;
	std::vector<viewport_animationst> viewports;

	// How long a one-tile step takes, a runtime setting ('timestep <ms>'). A movement keeps
	// the duration it started with, so a change applies to the movements that start after it.
	uint32_t movement_duration_ms=default_step_duration_ms;
	// Scrolling faster than detection keeps up: give up rather than test ever more prefixes.
	static constexpr size_t max_pending_shifts=8;
	static constexpr int32_t max_pending_shift_debt=6;
	// Bounds the wait on a scroll that never lands, so suppression cannot stick forever.
	static constexpr int32_t max_pending_age_frames=120;

	static void clear_pending(viewport_animationst &state)
		{
		state.pending.clear();
		state.pending_frames=0;
		state.pending_age=0;
		}

	static std::array<int32_t,2> pending_total(const viewport_animationst &state)
		{
		std::array<int64_t,2> total{};
		for(const auto &shift:state.pending)
			{
			total[0]+=shift[0];
			total[1]+=shift[1];
			}
		return {
			int32_t(std::clamp(total[0],int64_t(INT32_MIN),int64_t(INT32_MAX))),
			int32_t(std::clamp(total[1],int64_t(INT32_MIN),int64_t(INT32_MAX)))
			};
		}

	static int32_t saturated_pan_delta(int32_t current,int32_t previous)
		{
		return int32_t(std::clamp(
			int64_t(current)-previous,int64_t(INT32_MIN),int64_t(INT32_MAX)));
		}

	static void abandon_pending(viewport_animationst &state)
		{
		state.movements.clear();
		clear_pending(state);
		}

	static void reset_facing(viewport_animationst &state)
		{
		std::fill(
			state.facing.begin(),
			state.facing.end(),
			int8_t(native_sprite_facing));
		state.has_mirrored=false;
		}

	static void reset_tracking(viewport_animationst &state)
		{
		abandon_pending(state);
		state.suppress_frames=0;
		}

	// The signature hashes every tracked per-tile array each frame, so its speed sets the
	// floor of a frame. One FNV-1a chain is a serial multiply per entry; eight independent
	// chains, each over every eighth entry, let the CPU overlap them.
	static constexpr size_t signature_lanes=8;

	static void hash_signature_lanes(
		uint64_t (&lanes)[signature_lanes],
		const int32_t *values,
		int32_t count)
		{
		constexpr uint64_t fnv_prime=0x100000001b3ULL;
		int32_t i=0;
		for(;i+int32_t(signature_lanes)<=count;i+=int32_t(signature_lanes))
			for(size_t lane=0;lane<signature_lanes;++lane)
				lanes[lane]=(lanes[lane]^uint64_t(uint32_t(values[i+int32_t(lane)])))*fnv_prime;
		for(;i<count;++i)
			lanes[0]=(lanes[0]^uint64_t(uint32_t(values[i])))*fnv_prime;
		}

	// Identifies the buffer contents this frame, to tell a redrawn viewport from a repeated one.
	static uint64_t compute_buffer_signature(const viewport_visual_animation_inputst &input)
		{
		// FNV-1a in lanes. Only ever compared against the previous frame's value, never stored.
		constexpr uint64_t fnv_offset_basis=0xcbf29ce484222325ULL;
		constexpr uint64_t fnv_prime=0x100000001b3ULL;
		uint64_t lanes[signature_lanes];
		for(size_t lane=0;lane<signature_lanes;++lane)
			lanes[lane]=fnv_offset_basis^uint64_t(lane);
		const int32_t tile_count=input.dim_x*input.dim_y;
		if(input.current_background!=nullptr&&input.previous_background!=nullptr)
			{
			hash_signature_lanes(lanes,input.current_background,tile_count);
			hash_signature_lanes(lanes,input.previous_background,tile_count);
			}
		for(size_t layer=0;layer<input.current.size();++layer)
			{
			if(!visual_layer_tracks_own_movement(
				static_cast<viewport_visual_layer>(layer)))continue;
			hash_signature_lanes(lanes,input.current[layer],tile_count);
			hash_signature_lanes(lanes,input.previous[layer],tile_count);
			}
		uint64_t hash=lanes[0];
		for(size_t lane=1;lane<signature_lanes;++lane)
			hash=(hash*fnv_prime)^lanes[lane];
		return hash;
		}

	// Counts, over one per-tile array, the non-zero entries whose source tile `dwx,dwy`
	// away lies inside the viewport (`considered`) and how many of those `matches` the
	// previous frame's entry at that source (`matched`): the votes an array casts for the
	// hypothesis that the view shifted by that much.
	template<typename Matches>
	static void count_shift_matches(
		const viewport_visual_animation_inputst &input,
		const int32_t *current,
		const int32_t *previous,
		int32_t dwx,
		int32_t dwy,
		const Matches &matches,
		int32_t &considered,
		int32_t &matched)
		{
		for(int32_t x=0;x<input.dim_x;++x)
			{
			const int32_t sx=x+dwx;
			if(sx<0||sx>=input.dim_x)continue;
			for(int32_t y=0;y<input.dim_y;++y)
				{
				const int32_t sy=y+dwy;
				if(sy<0||sy>=input.dim_y)continue;
				const int32_t value=current[x*input.dim_y+y];
				if(value==0)continue;
				++considered;
				if(matches(value,previous[sx*input.dim_y+sy]))++matched;
				}
			}
		}

	// Fraction of tracked sprites consistent with a buffer shift: current[x]==previous[x+dwx].
	// Negative when there is nothing to compare.
	static double background_shift_match_ratio(
		const viewport_visual_animation_inputst &input,
		int32_t dwx,
		int32_t dwy)
		{
		if(input.current_background==nullptr||input.previous_background==nullptr)return -1.0;
		int32_t considered=0;
		int32_t matches=0;
		count_shift_matches(
			input,input.current_background,input.previous_background,dwx,dwy,
			[](int32_t value,int32_t previous){return previous==value;},
			considered,matches);
		return considered?double(matches)/considered:-1.0;
		}

	static double visual_shift_match_ratio(
		const viewport_visual_animation_inputst &input,
		int32_t dwx,
		int32_t dwy)
		{
		int32_t considered=0;
		int32_t matches=0;
		for(size_t layer=0;layer<input.current.size();++layer)
			{
			const auto id=static_cast<viewport_visual_layer>(layer);
			if(!visual_layer_tracks_own_movement(id))continue;
			// A layer matching any non-zero previous carries no position, so it would vote for
			// every hypothesis and carry an unapplied scroll over the bar.
			if(visual_layer_descriptor(id).matches_any_previous)continue;
			count_shift_matches(
				input,input.current[layer],input.previous[layer],dwx,dwy,
				[id](int32_t texpos,int32_t previous)
					{return visual_layer_matches(id,texpos,previous);},
				considered,matches);
			}
		return considered?double(matches)/considered:-1.0;
		}

	static double scroll_shift_match_ratio(
		const viewport_visual_animation_inputst &input,
		int32_t dwx,
		int32_t dwy,
		bool &used_background)
		{
		const double background=background_shift_match_ratio(input,dwx,dwy);
		used_background=background>=0.0;
		return used_background?background:visual_shift_match_ratio(input,dwx,dwy);
		}

	static std::array<int32_t,2> shared_movement_delta(
		const int32_t *current,
		const int32_t *previous,
		int32_t dim_x,
		int32_t dim_y)
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
				for(int32_t x=0;x<dim_x;++x)
					{
					const int32_t source_x=x+dx;
					if(source_x<0||source_x>=dim_x)continue;
					for(int32_t y=0;y<dim_y;++y)
						{
						const int32_t source_y=y+dy;
						if(source_y<0||source_y>=dim_y)continue;
						const int32_t target=x*dim_y+y;
						const int32_t texpos=current[target];
						if(texpos!=0&&previous[target]!=texpos&&
							previous[source_x*dim_y+source_y]==texpos)++count;
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

	viewport_animationst &get_viewport(const viewport_visual_animation_inputst &input)
		{
		for(viewport_animationst &state:viewports)
			{
			if(state.viewport==input.viewport)return state;
			}
		viewports.emplace_back();
		viewports.back().viewport=input.viewport;
		return viewports.back();
		}

	// A movement's position this frame, for the direction of its own step (a rider's step
	// is its anchor's, shifted, so the direction is the same).
	sprite_positionst movement_position(const movementst &movement) const
		{
		return animation_position(
			frame_time_ms,movement.start_time_ms,movement.duration_ms,*interpolation,
			step_direction(movement.source_x,movement.source_y,movement.target_x,
				movement.target_y),
			bob);
		}

	float movement_progress(const movementst &movement) const
		{
		return movement_position(movement).along;
		}

	float movement_lift(const movementst &movement) const
		{
		return movement_position(movement).lift;
		}

	bool movement_active(const movementst &movement) const
		{
		return !movement.historical&&
			frame_time_ms-movement.start_time_ms<movement.duration_ms;
		}

	// The longest a paced movement lasts and the oldest predecessor its cadence follows:
	// 500 ms, or the step time when that is longer, so a long step is never cut short.
	uint32_t linear_limit_ms() const
		{
		return std::max(500U,movement_duration_ms);
		}

	// The same limit for a movement already in flight: its own duration is kept as the
	// floor, so lowering the step time never cuts a longer movement short.
	uint32_t linear_limit_ms(const movementst &movement) const
		{
		return std::max(linear_limit_ms(),movement.duration_ms);
		}

	visual_movement_idst allocate_movement_id()
		{
		const visual_movement_idst id=next_movement_id++;
		if(next_movement_id==no_visual_movement)next_movement_id=1;
		return id;
		}

	public:
		static constexpr uint32_t default_step_duration_ms=150;

		visual_animation_managerst()=default;

		walk_bob_settingst bob;

		uint32_t step_duration_ms() const
			{
			return movement_duration_ms;
			}

		void set_step_duration_ms(uint32_t ms)
			{
			movement_duration_ms=std::max(1U,ms);
			}

		void set_interpolation(const interpolationst &selected)
			{
			interpolation=&selected;
			}

		const interpolationst &get_interpolation() const
			{
			return *interpolation;
			}

		void begin_frame(uint32_t now_ms)
			{
			frame_delta_ms=has_frame?now_ms-frame_time_ms:0;
			frame_time_ms=now_ms;
			has_frame=true;
			force_full_redraw=false;
			// Keep one final full redraw when the last movement expires.
			for(viewport_animationst &state:viewports)
				{
				state.seen=false;
				state.landed_this_frame=false;
				state.abandoned_this_frame=false;
				state.landed_shift={};
				state.follow_candidate=no_visual_movement;
				for(movementst &movement:state.movements)
					{
					if(movement.historical)continue;
					force_full_redraw=true;
					if(interpolation->paced&&frame_time_ms-movement.start_time_ms>=movement.duration_ms)
						movement.historical=true;
					}
				}
			}

		void synchronize_viewport(const viewport_visual_animation_inputst &input)
			{
			if(input.viewport==nullptr)return;
			viewport_animationst &state=get_viewport(input);
			state.seen=true;

			if(!input.valid())
				{
				reset_tracking(state);
				state.has_context=false;
				state.has_mirrored=false;
				return;
				}

			// Only a replaced view leaves a previous buffer belonging somewhere else; a first
			// sighting does not.
			const bool view_switched=state.has_context&&
				(state.context_revision!=input.context_revision||
				state.dim_x!=input.dim_x||state.dim_y!=input.dim_y);
			const bool context_changed=!state.has_context||view_switched;
			// The scroll delta is queued here as a hint; the buffers are hypothesis-tested each
			// frame to find where it lands. Detection stays suppressed until then: a shifted
			// buffer makes every panned creature look like a real move.
			if(state.has_pan&&(state.pan_x!=input.pan_x||state.pan_y!=input.pan_y))
				{
				if(state.pending.size()>=max_pending_shifts)
					{
					// Same give-up as the other two sites: the owed shifts are unknowable now.
					abandon_pending(state);
					reset_facing(state);
					state.abandoned_this_frame=true;
					}
				state.pending.push_back(
					{saturated_pan_delta(input.pan_x,state.pan_x),
						saturated_pan_delta(input.pan_y,state.pan_y)});
				const auto debt=pending_total(state);
				if(std::abs(int64_t(debt[0]))>max_pending_shift_debt||
					std::abs(int64_t(debt[1]))>max_pending_shift_debt)
					{
					abandon_pending(state);
					reset_facing(state);
					state.abandoned_this_frame=true;
					}
				state.pending_frames=0;
				state.suppress_frames=2;
				}
			state.context_revision=input.context_revision;
			state.dim_x=input.dim_x;
			state.dim_y=input.dim_y;
			state.has_context=true;
			state.pan_x=input.pan_x;
			state.pan_y=input.pan_y;
			state.has_pan=true;
			if(context_changed)
				{
				state.facing.assign(
					size_t(input.dim_x)*size_t(input.dim_y),
					int8_t(native_sprite_facing));
				state.has_mirrored=false;
				}
			// This hook runs per frame; the viewport is recomputed only when it changes, and while
			// paused hardly at all. Re-reading a landed scroll steps every sprite by a tile.
			const uint64_t signature=compute_buffer_signature(input);
			const bool buffers_advanced=!state.has_buffer_signature||
				state.buffer_signature!=signature;
			state.buffer_signature=signature;
			state.has_buffer_signature=true;
			// A redraw at the tick the arrays last changed at shows the same world: whatever
			// differs is presentation, not a step, and a paused game is nothing but such redraws.
			const bool world_advanced=input.simulation_tick<0||
				state.buffer_tick!=input.simulation_tick;
			if(buffers_advanced)state.buffer_tick=input.simulation_tick;

			if(context_changed)
				{
				// Skips the recompute sweep, so clear has_mirrored here or a stale true survives.
				state.has_mirrored=false;
				reset_tracking(state);
				// window_z, zoom and resize change at input time; the buffers cross later.
				// This reset covers only the input frame, not the crossing itself.
				if(view_switched)state.previous_view_stale=true;
				return;
				}

			// On the crossing frame `current` is the new view and `previous` the old one, so a
			// sprite on each side, a tile apart, reads as one that moved between them.
			const bool crossed_views=buffers_advanced&&state.previous_view_stale;
			if(buffers_advanced)state.previous_view_stale=false;
			// The new view is drawn at the current window, so a queued scroll is already in it.
			// Left queued it would never match, and suppress everything until it aged out.
			if(crossed_views)clear_pending(state);

			bool translated=false;
			std::array<int32_t,2> landed_shift{};
			bool pending_testable=buffers_advanced;
			bool repeated_landing_allowed=false;
			if(!pending_testable&&!state.pending.empty())
				{
				bool used_background=false;
				const double ratio=scroll_shift_match_ratio(input,0,0,used_background);
				pending_testable=ratio<0.0||ratio>=(used_background?0.6:0.5);
				repeated_landing_allowed=used_background&&ratio>=0.6&&
					visual_shift_match_ratio(input,0,0)<0.0;
				}
			if(pending_testable&&!crossed_views&&!state.pending.empty())
				{
				struct landing_candidatest
					{
					std::array<int32_t,2> shift{};
					size_t full_events=0;
					std::array<int32_t,2> partial{};
					int32_t applied_components=0;
					double score=-1.0;
					bool valid=false;
					};
				landing_candidatest best;
				bool any_data=false;
				std::array<int32_t,2> base{};
				int32_t base_components=0;
				for(size_t event_index=0;event_index<state.pending.size();++event_index)
					{
					const auto event=state.pending[event_index];
					const int32_t step_x=(event[0]>0)-(event[0]<0);
					const int32_t step_y=(event[1]>0)-(event[1]<0);
					for(int32_t ix=0;ix<=std::abs(event[0]);++ix)
						for(int32_t iy=0;iy<=std::abs(event[1]);++iy)
						{
						if(ix==0&&iy==0)continue;
						const std::array<int32_t,2> partial={ix*step_x,iy*step_y};
						const std::array<int32_t,2> shift=
							{base[0]+partial[0],base[1]+partial[1]};
						if(shift[0]==0&&shift[1]==0)continue;
						bool used_background=false;
						const double ratio=scroll_shift_match_ratio(
							input,shift[0],shift[1],used_background);
						if(ratio<0.0)continue;
						any_data=true;
						const int32_t applied=base_components+ix+iy;
						if(ratio<(used_background?0.6:0.5)||
							(!buffers_advanced&&!repeated_landing_allowed)||
							(best.valid&&ratio<best.score)||
							(best.valid&&ratio==best.score&&applied>=best.applied_components))
							continue;
						const bool complete=ix==std::abs(event[0])&&iy==std::abs(event[1]);
						best={shift,event_index+(complete?1:0),
							complete?std::array<int32_t,2>{}:partial,applied,ratio,true};
						}
					base[0]+=event[0];
					base[1]+=event[1];
					base_components+=std::abs(event[0])+std::abs(event[1]);
					}
				if(!any_data)
					{
					// Nothing visible to anchor the test on: nothing to animate either.
					abandon_pending(state);
					reset_facing(state);
					state.abandoned_this_frame=true;
					}
				else if(best.valid)
					{
					// Re-anchor in-flight movements and drop anything scrolled off-screen.
					const int32_t dwx=best.shift[0];
					const int32_t dwy=best.shift[1];
					state.movements.erase(
						std::remove_if(
							state.movements.begin(),
							state.movements.end(),
							[&](movementst &movement)
								{
								movement.source_x-=dwx;
								movement.source_y-=dwy;
								movement.target_x-=dwx;
								movement.target_y-=dwy;
								return movement.target_x<0||movement.target_x>=input.dim_x||
									movement.target_y<0||movement.target_y>=input.dim_y;
								}),
						state.movements.end());
					// Facing describes creatures still on screen, so translate it rather than drop it.
					if(state.facing.size()==
						size_t(input.dim_x)*size_t(input.dim_y))
						{
						std::vector<int8_t> shifted(
							state.facing.size(),int8_t(native_sprite_facing));
						for(int32_t x=0;x<input.dim_x;++x)
							{
							const int32_t sx=x+dwx;
							if(sx<0||sx>=input.dim_x)continue;
							for(int32_t y=0;y<input.dim_y;++y)
								{
								const int32_t sy=y+dwy;
								if(sy<0||sy>=input.dim_y)continue;
								shifted[x*input.dim_y+y]=
									state.facing[sx*input.dim_y+sy];
								}
							}
						state.facing.swap(shifted);
						}
					state.pending.erase(state.pending.begin(),
						state.pending.begin()+std::ptrdiff_t(best.full_events));
					if(best.partial[0]!=0||best.partial[1]!=0)
						{
						state.pending.front()[0]-=best.partial[0];
						state.pending.front()[1]-=best.partial[1];
						if(state.pending.front()[0]==0&&state.pending.front()[1]==0)
							state.pending.erase(state.pending.begin());
						}
					state.pending_frames=0;
					state.pending_age=0;
					// The scroll is accounted for; the settle window must not block the rebased pass.
					state.suppress_frames=0;
					landed_shift=best.shift;
					translated=true;
					state.landed_this_frame=true;
					state.landed_shift=best.shift;
					}
				else
					{
					bool used_background=false;
					const double ratio=scroll_shift_match_ratio(input,0,0,used_background);
					if(ratio>=(used_background?0.6:0.5)&&
						++state.pending_age<=max_pending_age_frames)
						state.pending_frames=0;
					else if(++state.pending_frames>4||state.pending_age>max_pending_age_frames)
						{
						abandon_pending(state);
						reset_facing(state);
						state.suppress_frames=2;
						state.abandoned_this_frame=true;
						}
					}
				}

			const bool suppress=(!buffers_advanced&&!translated)||crossed_views||
				(!translated&&!state.pending.empty())||state.suppress_frames>0||
				!world_advanced;
			// The countdown measures redraws, not frames, so a repeated viewport must not spend it.
			if(buffers_advanced&&state.suppress_frames>0)--state.suppress_frames;

			// On the landing frame `previous` is still framed on the pre-scroll view.
			// Rebasing it by the landed delta keeps a creature that walked during the scroll.
			auto previous_layers=input.previous;
			std::vector<std::vector<int32_t>> rebased_previous;
			if(translated&&!suppress)
				{
				rebased_previous.resize(input.previous.size());
				for(size_t layer=0;layer<input.previous.size();++layer)
					{
					if(!visual_layer_tracks_own_movement(
						static_cast<viewport_visual_layer>(layer)))continue;
					rebased_previous[layer].assign(
						size_t(input.dim_x)*size_t(input.dim_y),0);
					for(int32_t x=0;x<input.dim_x;++x)
						{
						const int32_t sx=x+landed_shift[0];
						if(sx<0||sx>=input.dim_x)continue;
						for(int32_t y=0;y<input.dim_y;++y)
							{
							const int32_t sy=y+landed_shift[1];
							if(sy<0||sy>=input.dim_y)continue;
							rebased_previous[layer][x*input.dim_y+y]=
								input.previous[layer][sx*input.dim_y+sy];
							}
						}
					previous_layers[layer]=rebased_previous[layer].data();
					}
				}
			if(!suppress)
				{
				const int32_t tile_count=input.dim_x*input.dim_y;
				std::vector<uint8_t> claimed_sources(tile_count);
				const size_t existing_movement_count=state.movements.size();
				// A chained movement's source may already have been rewritten this frame.
				const std::vector<int8_t> facing_at_frame_start=state.facing;
				// Source clears are deferred until every movement this frame is registered.
				// A source can be another movement's target in the same frame -- a chain.
				std::vector<uint8_t> facing_target_written;
				std::vector<int32_t> pending_facing_source_clears;
				long double best_follow_distance=std::numeric_limits<long double>::max();
				if(state.facing.size()==size_t(input.dim_x)*size_t(input.dim_y))
					facing_target_written.assign(state.facing.size(),0);
				for(size_t layer=0;layer<input.current.size();++layer)
					{
					if(!visual_layer_tracks_own_movement(
						static_cast<viewport_visual_layer>(layer)))continue;
					std::fill(claimed_sources.begin(),claimed_sources.end(),0);
					const int32_t *current=input.current[layer];
					const int32_t *previous=previous_layers[layer];
					const auto shared_delta=
						static_cast<viewport_visual_layer>(layer)==viewport_visual_layer::center?
						shared_movement_delta(
							current,previous,input.dim_x,input.dim_y):
						std::array<int32_t,2>{};
					for(int32_t x=0;x<input.dim_x;++x)
						{
						for(int32_t y=0;y<input.dim_y;++y)
							{
							const int32_t target=x*input.dim_y+y;
							const int32_t texpos=current[target];
							if(texpos==0)continue;
							if(static_cast<viewport_visual_layer>(layer)==
								viewport_visual_layer::item&&
								previous_layers[static_cast<size_t>(
									viewport_visual_layer::center)][target]!=0)continue;

							int32_t source=-1;
							int32_t candidate_count=0;
							if(shared_delta[0]!=0||shared_delta[1]!=0)
								{
								const int32_t source_x=x+shared_delta[0];
								const int32_t source_y=y+shared_delta[1];
								if(source_x>=0&&source_x<input.dim_x&&
									source_y>=0&&source_y<input.dim_y)
									{
									const int32_t candidate=source_x*input.dim_y+source_y;
									if(!claimed_sources[candidate]&&previous[target]!=texpos&&
										previous[candidate]==texpos)
										{
										source=candidate;
										candidate_count=1;
										}
									}
								}
							// Otherwise require a unique same-sprite move between empty cells.
							if(candidate_count==0&&previous[target]==0)
								{
								for(int32_t dx=-1;dx<=1;++dx)
									{
									for(int32_t dy=-1;dy<=1;++dy)
										{
										if(dx==0&&dy==0)continue;
										const int32_t source_x=x+dx;
										const int32_t source_y=y+dy;
										if(source_x<0||source_x>=input.dim_x||
											source_y<0||source_y>=input.dim_y)continue;
										const int32_t candidate=source_x*input.dim_y+source_y;
										if(!claimed_sources[candidate]&&
											visual_layer_matches(
												static_cast<viewport_visual_layer>(layer),
												texpos,
												previous[candidate])&&
											current[candidate]==0)
											{
											source=candidate;
											++candidate_count;
											}
										}
									}
								}
							if(candidate_count!=1)continue;

							claimed_sources[source]=1;
							float visual_source_x=float(source/input.dim_y);
							float visual_source_y=float(source%input.dim_y);
							const movementst *predecessor=nullptr;
							for(size_t i=0;i<existing_movement_count;++i)
								{
								const movementst &movement=state.movements[i];
								if(movement.layer!=
									static_cast<viewport_visual_layer>(layer)||
									movement.target_x!=visual_source_x||
									movement.target_y!=visual_source_y||
									(interpolation->paced&&frame_time_ms-movement.start_time_ms>
										linear_limit_ms(movement)))continue;
								if(predecessor==nullptr||
									frame_time_ms-movement.start_time_ms<
									frame_time_ms-predecessor->start_time_ms)
									predecessor=&movement;
								}
							uint32_t duration_ms=movement_duration_ms;
							if(predecessor!=nullptr)
								{
								if(movement_active(*predecessor))
									{
									const float progress=movement_progress(*predecessor);
									visual_source_x=predecessor->source_x+
										(predecessor->target_x-predecessor->source_x)*progress;
									visual_source_y=predecessor->source_y+
										(predecessor->target_y-predecessor->source_y)*progress;
									}
								if(interpolation->paced)duration_ms=std::clamp(
									frame_time_ms-predecessor->start_time_ms,
									movement_duration_ms,linear_limit_ms());
								}
							const visual_movement_idst movement_id=allocate_movement_id();
							state.movements.push_back(
								{
								movement_id,
								static_cast<viewport_visual_layer>(layer),
								texpos,
								visual_source_x,
								visual_source_y,
								x,
								y,
								frame_time_ms,
								duration_ms
								});
							const int32_t source_x=source/input.dim_y;
							const int32_t source_y=source%input.dim_y;
							if(translated&&static_cast<viewport_visual_layer>(layer)==
								viewport_visual_layer::center&&
								x-source_x==landed_shift[0]&&y-source_y==landed_shift[1])
								{
								const long double centered_x=
									static_cast<long double>(2)*x-(input.dim_x-1);
								const long double centered_y=
									static_cast<long double>(2)*y-(input.dim_y-1);
								const long double distance=
									centered_x*centered_x+centered_y*centered_y;
								if(distance<best_follow_distance)
									{
									best_follow_distance=distance;
									state.follow_candidate=movement_id;
									}
								}
							if(static_cast<viewport_visual_layer>(layer)==
									viewport_visual_layer::center&&
								state.facing.size()==
									size_t(input.dim_x)*size_t(input.dim_y)&&
								!facing_at_frame_start.empty()&&
								facing_at_frame_start.size()==state.facing.size())
								{
								const int32_t target_index=x*input.dim_y+y;
								state.facing[target_index]=int8_t(
									facing_after_move(
										x-source_x,
										static_cast<visual_facingst>(
											facing_at_frame_start[source])));
								facing_target_written[size_t(target_index)]=1;
								pending_facing_source_clears.push_back(source);
								}
							}
						}
						}
				// A source vacates its tile only if no movement this frame claimed it as a target.
				if(!facing_target_written.empty())
					{
					for(int32_t pending_source:pending_facing_source_clears)
						{
						if(!facing_target_written[size_t(pending_source)])
							state.facing[size_t(pending_source)]=
								int8_t(native_sprite_facing);
						}
					}
				}
			state.movements.erase(
				std::remove_if(
					state.movements.begin(),
					state.movements.end(),
					[&](movementst &movement)
						{
						const size_t layer=static_cast<size_t>(movement.layer);
						const int32_t target=movement.target_x*input.dim_y+movement.target_y;
						const int32_t current=input.current[layer][target];
						const bool invalid=current==0||
							!visual_layer_matches(movement.layer,current,movement.texpos);
						if(interpolation->paced)
							{
							if(invalid||frame_time_ms-movement.start_time_ms>=movement.duration_ms)
								movement.historical=true;
							return frame_time_ms-movement.start_time_ms>linear_limit_ms(movement);
							}
						return frame_time_ms-movement.start_time_ms>=movement.duration_ms||invalid;
						}),
				state.movements.end());
			// has_mirrored is recomputed here rather than maintained at every write site.
			if(state.facing.size()==size_t(input.dim_x)*size_t(input.dim_y))
				{
				const int32_t *center_current=
					input.current[static_cast<size_t>(
						viewport_visual_layer::center)];
				bool any_mirrored=false;
				for(size_t i=0;i<state.facing.size();++i)
					{
					if(center_current[i]==0)
						state.facing[i]=int8_t(native_sprite_facing);
					else if(state.facing[i]!=int8_t(native_sprite_facing))
						any_mirrored=true;
					}
				state.has_mirrored=any_mirrored;
				}
			for(const movementst &movement:state.movements)
				if(movement_active(movement))force_full_redraw=true;
			}

		void end_frame()
			{
			viewports.erase(
				std::remove_if(
					viewports.begin(),
					viewports.end(),
					[](const viewport_animationst &state){return !state.seen;}),
				viewports.end());
			for(const viewport_animationst &state:viewports)
				{
				for(const movementst &movement:state.movements)
					if(movement_active(movement))force_full_redraw=true;
				}
			}

		// Pausing keeps persistent facing, but drops every in-flight visual transition.
		void cancel_transitions()
			{
			for(viewport_animationst &state:viewports)reset_tracking(state);
			force_full_redraw=false;
			}

		uint32_t get_frame_time_ms() const
			{
			return frame_time_ms;
			}

		uint32_t get_frame_delta_ms() const
			{
			return frame_delta_ms;
			}

		visual_scroll_renderst get_scroll(const void *viewport) const
			{
			for(const viewport_animationst &state:viewports)
				if(state.viewport==viewport)
					{
					const auto pending=pending_total(state);
					return {!state.pending.empty(),state.landed_this_frame,
						state.abandoned_this_frame,state.landed_shift[0],
						state.landed_shift[1],pending[0],pending[1],state.follow_candidate};
					}
			return {};
			}

		visual_follow_renderst get_follow(
			const void *viewport,
			visual_movement_idst movement_id) const
			{
			if(movement_id==no_visual_movement)return {};
			for(const viewport_animationst &state:viewports)
				{
				if(state.viewport!=viewport)continue;
				for(const movementst &movement:state.movements)
					if(movement.id==movement_id&&movement_active(movement))
						{
						const float remaining=1.0f-movement_progress(movement);
						return {true,movement.id,
							(movement.target_x-movement.source_x)*remaining,
							(movement.target_y-movement.source_y)*remaining};
						}
				break;
				}
			return {};
			}

		visual_facingst get_facing(
			const void *viewport,
			int32_t x,
			int32_t y) const
			{
			for(const viewport_animationst &state:viewports)
				{
				if(state.viewport!=viewport)continue;
				if(x<0||x>=state.dim_x||y<0||y>=state.dim_y)break;
				const size_t index=size_t(x)*size_t(state.dim_y)+size_t(y);
				if(index>=state.facing.size())break;
				return static_cast<visual_facingst>(state.facing[index]);
				}
			return native_sprite_facing;
			}

		// The tiles of this viewport whose facing is not the native one, in the order the
		// render code sweeps a viewport in: column by column, top to bottom within a column.
		// The sweep visits only these instead of asking for the facing of every tile.
		void collect_mirrored_tiles(
			const void *viewport,
			int32_t dim_x,
			int32_t dim_y,
			std::vector<std::array<int32_t,2>> &tiles) const
			{
			tiles.clear();
			for(const viewport_animationst &state:viewports)
				{
				if(state.viewport!=viewport)continue;
				if(!state.has_mirrored)break;
				const int32_t last_x=std::min(dim_x,state.dim_x);
				const int32_t last_y=std::min(dim_y,state.dim_y);
				for(int32_t x=0;x<last_x;++x)
					for(int32_t y=0;y<last_y;++y)
						{
						const size_t index=size_t(x)*size_t(state.dim_y)+size_t(y);
						if(index>=state.facing.size())break;
						if(state.facing[index]!=int8_t(native_sprite_facing))
							tiles.push_back({x,y});
						}
				break;
				}
			}

		bool has_mirrored_facing(const void *viewport) const
			{
			for(const viewport_animationst &state:viewports)
				{
				if(state.viewport!=viewport)continue;
				return state.has_mirrored;
				}
			return false;
			}

		bool requires_full_redraw() const
			{
			return force_full_redraw;
			}

		// The tiles get_movement can report active on this viewport: the target of every
		// active movement and, around a center movement's target, the eight tiles whose other
		// layers may inherit it. Listed once each, top row first and left to right, the order
		// the render code walks a viewport in, so it visits only these instead of every tile.
		void collect_movement_tiles(
			const void *viewport,
			int32_t dim_x,
			int32_t dim_y,
			std::vector<std::array<int32_t,2>> &tiles) const
			{
			tiles.clear();
			for(const viewport_animationst &state:viewports)
				{
				if(state.viewport!=viewport)continue;
				for(const movementst &movement:state.movements)
					{
					if(!movement_active(movement))continue;
					const int32_t reach=movement.layer==viewport_visual_layer::center?1:0;
					for(int32_t y=movement.target_y-reach;y<=movement.target_y+reach;++y)
						for(int32_t x=movement.target_x-reach;x<=movement.target_x+reach;++x)
							if(x>=0&&x<dim_x&&y>=0&&y<dim_y)tiles.push_back({x,y});
					}
				break;
				}
			std::sort(tiles.begin(),tiles.end(),
				[](const std::array<int32_t,2> &a,const std::array<int32_t,2> &b)
					{return a[1]!=b[1]?a[1]<b[1]:a[0]<b[0];});
			tiles.erase(std::unique(tiles.begin(),tiles.end()),tiles.end());
			}

		visual_movement_renderst get_movement(
			const void *viewport,
			viewport_visual_layer layer,
			int32_t target_x,
			int32_t target_y) const
			{
			for(const viewport_animationst &state:viewports)
				{
				if(state.viewport!=viewport)continue;
				const movementst *companion=nullptr;
				bool ambiguous=false;
				for(const movementst &movement:state.movements)
					{
					if(!movement_active(movement))continue;
					if(movement.layer==layer&&movement.target_x==target_x&&
						movement.target_y==target_y)
						{
						return {
							true,
							movement.source_x,
							movement.source_y,
							movement_progress(movement),
							movement_lift(movement),
							false,
							movement.id
							};
						}
					if(layer==viewport_visual_layer::vehicle||
						layer==viewport_visual_layer::center||
						movement.layer!=viewport_visual_layer::center||
						std::abs(movement.target_x-target_x)>1||
						std::abs(movement.target_y-target_y)>1)continue;
					if(companion!=nullptr&&
						(companion->source_x-companion->target_x!=
							movement.source_x-movement.target_x||
						companion->source_y-companion->target_y!=
							movement.source_y-movement.target_y||
						companion->start_time_ms!=movement.start_time_ms||
						companion->duration_ms!=movement.duration_ms))
						ambiguous=true;
					else if(companion==nullptr)
						companion=&movement;
					}
				if(ambiguous)return {};
				if(companion!=nullptr)
					return {
						true,
						target_x+companion->source_x-companion->target_x,
						target_y+companion->source_y-companion->target_y,
						movement_progress(*companion),
						movement_lift(*companion),
						true,
						companion->id
						};
				break;
				}
			return {};
			}
};

#endif
