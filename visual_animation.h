// SPDX-License-Identifier: MIT

#ifndef VISUAL_ANIMATION_H
#define VISUAL_ANIMATION_H

#include "buffer_signature.h"
#include "facing_grid.h"
#include "movement_detector.h"
#include "movement_store.h"
#include "scroll_tracker.h"
#include "visual_layers.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

// Fraction of tracked sprites consistent with the buffers having shifted by (dx,dy):
// current[x]==previous[x+dx]. Negative when there is nothing to compare.
inline double shift_match_ratio(
	const viewport_visual_animation_inputst &input,
	int32_t dx,
	int32_t dy)
{
	const visual_gridst grid=input.grid();
	shift_match_tallyst tally;
	for(size_t layer=0;layer<visual_layer_count;++layer)
		{
		const auto id=static_cast<viewport_visual_layer>(layer);
		if(!visual_layer_tracks_own_movement(id))continue;
		// A layer matching any non-zero previous carries no position, so it would vote for
		// every hypothesis and carry an unapplied scroll over the bar.
		if(visual_layer_descriptor(id).matches_any_previous)continue;
		tally_shift_matches(
			grid,
			input.current[layer],
			input.previous[layer],
			dx,
			dy,
			[id](int32_t texpos,int32_t previous){return visual_layer_matches(id,texpos,previous);},
			tally);
		}
	return tally.ratio();
}

class visual_animation_managerst
{
	struct viewport_animationst
	{
		const void *viewport=nullptr;
		visual_gridst grid;
		uint64_t context_revision=0;
		bool has_context=false;
		bool seen=false;
		int32_t pan_x=0;
		int32_t pan_y=0;
		bool has_pan=false;
		movement_storest movements;
		facing_gridst facing;
		scroll_trackerst scroll;
		// Buffer contents last seen, to recognize a repeat of them.
		uint64_t buffer_signature=0;
		bool has_buffer_signature=false;
		// Set while the previous buffer still belongs to a view that has been left behind,
		// with that buffer's signature: the crossing shows there when the current buffers
		// already held the new view.
		bool previous_view_stale=false;
		uint64_t previous_buffer_signature=0;
		// Simulation tick the buffers last advanced at, to tell a step from a repaint.
		int64_t buffer_tick=-1;
	};

	uint32_t frame_time_ms=0;
	uint32_t frame_delta_ms=0;
	bool has_frame=false;
	bool force_full_redraw=false;
	std::vector<viewport_animationst> viewports;
	movement_detectorst detector;
	// Scratch for the landing frame's rebased previous buffers.
	std::array<std::vector<int32_t>,visual_layer_count> rebased_previous;

	// Base time for one tile step; settable at runtime ('timestep <ms>').
	uint32_t movement_duration_ms=100;

	viewport_animationst &get_viewport(const void *viewport)
		{
		for(viewport_animationst &state:viewports)
			if(state.viewport==viewport)return state;
		viewports.emplace_back();
		viewports.back().viewport=viewport;
		return viewports.back();
		}

	const viewport_animationst *find_viewport(const void *viewport) const
		{
		for(const viewport_animationst &state:viewports)
			if(state.viewport==viewport)return &state;
		return nullptr;
		}

	// Whether the buffers hold something not seen on the last redraw. The hook runs every
	// frame; the viewport is recomputed only when it changes, and while paused hardly at all.
	static bool observe_buffers(
		viewport_animationst &state,
		const viewport_visual_animation_inputst &input)
		{
		const uint64_t signature=tracked_buffer_signature(input.grid(),input.current);
		bool advanced=!state.has_buffer_signature||state.buffer_signature!=signature;
		state.buffer_signature=signature;
		state.has_buffer_signature=true;
		// After a view switch the buffers may already show the new view on the input frame,
		// so the crossing is visible only in `previous` catching up a frame later. Hash it
		// for just that window; the rest of the time it is the last frame's current.
		if(state.previous_view_stale)
			{
			const uint64_t previous_signature=
				tracked_buffer_signature(input.grid(),input.previous);
			advanced=advanced||state.previous_buffer_signature!=previous_signature;
			state.previous_buffer_signature=previous_signature;
			}
		return advanced;
		}

	// On the landing frame `previous` is still framed on the pre-scroll view. Rebasing it by
	// the landed delta keeps a creature that walked during the scroll.
	visual_layer_pointerst rebase_previous(
		const viewport_visual_animation_inputst &input,
		const scroll_trackerst::shiftst &shift)
		{
		visual_layer_pointerst previous=input.previous;
		const visual_gridst grid=input.grid();
		for(size_t layer=0;layer<visual_layer_count;++layer)
			{
			if(!visual_layer_tracks_own_movement(
				static_cast<viewport_visual_layer>(layer)))continue;
			std::vector<int32_t> &buffer=rebased_previous[layer];
			buffer.resize(grid.tile_count());
			translate_grid(
				grid,input.previous[layer],buffer.data(),shift[0],shift[1],int32_t(0));
			previous[layer]=buffer.data();
			}
		return previous;
		}

	// Movements that ended, or whose sprite is no longer where they were heading.
	void expire_movements(
		viewport_animationst &state,
		const viewport_visual_animation_inputst &input) const
		{
		const visual_gridst grid=input.grid();
		state.movements.erase_if([&](const visual_movementst &movement)
			{
			const int32_t current=input.current[static_cast<size_t>(movement.layer)]
				[grid.index(movement.target_x,movement.target_y)];
			return movement.finished(frame_time_ms)||current==0||
				!visual_layer_matches(movement.layer,current,movement.texpos);
			});
		}

	public:
		uint32_t base_duration_ms() const
			{
			return movement_duration_ms;
			}
		void set_base_duration_ms(uint32_t ms)
			{
			movement_duration_ms=std::max<uint32_t>(1,ms);
			}

		visual_animation_managerst()=default;

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
				if(!state.movements.empty())force_full_redraw=true;
				}
			}

		void synchronize_viewport(const viewport_visual_animation_inputst &input)
			{
			if(input.viewport==nullptr)return;
			viewport_animationst &state=get_viewport(input.viewport);
			state.seen=true;

			if(!input.valid())
				{
				state.movements.clear();
				state.scroll.reset();
				state.has_context=false;
				state.facing.reset();
				return;
				}

			const visual_gridst grid=input.grid();
			// Only a replaced view leaves a previous buffer belonging somewhere else; a first
			// sighting does not.
			const bool view_switched=state.has_context&&
				(state.context_revision!=input.context_revision||state.grid!=grid);
			const bool context_changed=!state.has_context||view_switched;
			if(state.has_pan&&(state.pan_x!=input.pan_x||state.pan_y!=input.pan_y))
				{
				if(state.scroll.queue(input.pan_x-state.pan_x,input.pan_y-state.pan_y))
					{
					// The owed shifts are unknowable now: nothing anchored on them survives.
					state.movements.clear();
					state.facing.reset();
					}
				}
			state.context_revision=input.context_revision;
			state.grid=grid;
			state.has_context=true;
			state.pan_x=input.pan_x;
			state.pan_y=input.pan_y;
			state.has_pan=true;
			if(context_changed)state.facing.resize(grid);

			const bool buffers_advanced=observe_buffers(state,input);
			// A redraw at the tick the buffers were last drawn at shows the same world: whatever
			// differs is presentation, not a step, and a paused game is nothing but such redraws.
			const bool world_advanced=input.simulation_tick<0||
				state.buffer_tick!=input.simulation_tick;
			if(buffers_advanced)state.buffer_tick=input.simulation_tick;

			if(context_changed)
				{
				state.movements.clear();
				state.scroll.reset();
				// window_z, zoom and resize change at input time; the buffers cross later.
				// This reset covers only the input frame, not the crossing itself.
				if(view_switched)
					{
					state.previous_view_stale=true;
					state.previous_buffer_signature=
						tracked_buffer_signature(grid,input.previous);
					}
				return;
				}

			// On the crossing frame `current` is the new view and `previous` the old one, so a
			// sprite on each side, a tile apart, reads as one that moved between them.
			const bool crossed_views=buffers_advanced&&state.previous_view_stale;
			if(buffers_advanced)state.previous_view_stale=false;
			// The new view is drawn at the current window, so a queued scroll is already in it.
			// Left queued it would never match, and suppress everything until it aged out.
			if(crossed_views)state.scroll.forget();

			scroll_trackerst::resultst scroll;
			if(buffers_advanced&&!crossed_views)
				{
				scroll=state.scroll.observe([&](int32_t dx,int32_t dy)
					{
					return shift_match_ratio(input,dx,dy);
					});
				if(scroll.outcome==scroll_trackerst::outcomest::landed)
					{
					// Everything that describes tiles moves with them.
					state.movements.translate(grid,scroll.shift[0],scroll.shift[1]);
					state.facing.translate(grid,scroll.shift[0],scroll.shift[1]);
					}
				else if(scroll.outcome==scroll_trackerst::outcomest::abandoned)
					{
					// The delta was never identified, so nothing can be translated.
					state.movements.clear();
					state.facing.reset();
					}
				}

			const bool suppress=!buffers_advanced||crossed_views||state.scroll.suppresses()||
				!world_advanced;
			if(buffers_advanced)state.scroll.spend_settle_frame();
			if(!suppress)
				{
				const bool landed=scroll.outcome==scroll_trackerst::outcomest::landed;
				detector.detect(
					grid,
					input.current,
					landed?rebase_previous(input,scroll.shift):input.previous,
					state.movements,
					state.facing,
					frame_time_ms,
					movement_duration_ms);
				}
			expire_movements(state,input);
			// Facing only changes with the buffers; expiry also runs on time.
			if(buffers_advanced)
				state.facing.settle(
					input.current[static_cast<size_t>(viewport_visual_layer::center)]);
			if(!state.movements.empty())force_full_redraw=true;
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
				if(!state.movements.empty())force_full_redraw=true;
			}

		uint32_t get_frame_time_ms() const
			{
			return frame_time_ms;
			}

		uint32_t get_frame_delta_ms() const
			{
			return frame_delta_ms;
			}

		visual_facingst get_facing(const void *viewport,int32_t x,int32_t y) const
			{
			const viewport_animationst *state=find_viewport(viewport);
			if(state==nullptr||!state->grid.contains(x,y))return native_sprite_facing;
			return state->facing.at(size_t(state->grid.index(x,y)));
			}

		// Every movement in flight on a viewport, for diagnostics.
		const std::vector<visual_movementst> &movements(const void *viewport) const
			{
			static const std::vector<visual_movementst> none;
			const viewport_animationst *state=find_viewport(viewport);
			return state!=nullptr?state->movements.all():none;
			}

		bool has_mirrored_facing(const void *viewport) const
			{
			const viewport_animationst *state=find_viewport(viewport);
			return state!=nullptr&&state->facing.has_mirrored();
			}

		bool requires_full_redraw() const
			{
			return force_full_redraw;
			}

		// Tiles that can carry a moving proxy this frame, as x*dim_y+y indices, ascending and
		// unique. The renderer visits only these instead of sweeping the whole grid.
		void movement_candidate_tiles(const void *viewport,std::vector<int32_t> &tiles) const
			{
			tiles.clear();
			if(const viewport_animationst *state=find_viewport(viewport))
				state->movements.candidate_tiles(state->grid,tiles);
			}

		// Tiles whose creature faces away from the sprite's native side, ascending indices.
		const std::vector<int32_t> &mirrored_tiles(const void *viewport) const
			{
			static const std::vector<int32_t> none;
			const viewport_animationst *state=find_viewport(viewport);
			return state!=nullptr?state->facing.mirrored_tiles():none;
			}

		visual_movement_renderst get_movement(
			const void *viewport,
			viewport_visual_layer layer,
			int32_t target_x,
			int32_t target_y) const
			{
			const viewport_animationst *state=find_viewport(viewport);
			if(state==nullptr)return {};
			if(const visual_movementst *movement=
				state->movements.find(layer,target_x,target_y))
				{
				return {
					true,
					movement->source_x,
					movement->source_y,
					movement->progress(frame_time_ms)
					};
				}
			if(layer==viewport_visual_layer::vehicle||
				layer==viewport_visual_layer::center)return {};
			// An icon, fragment or item inherits the motion of a centre within one tile,
			// unless two such centres disagree on the step.
			const visual_movementst *companion=nullptr;
			for(int32_t dx=-1;dx<=1;++dx)
				{
				for(int32_t dy=-1;dy<=1;++dy)
					{
					const visual_movementst *movement=state->movements.find(
						viewport_visual_layer::center,target_x+dx,target_y+dy);
					if(movement==nullptr)continue;
					if(companion!=nullptr&&
						(companion->source_x-companion->target_x!=
							movement->source_x-movement->target_x||
						companion->source_y-companion->target_y!=
							movement->source_y-movement->target_y||
						companion->start_time_ms!=movement->start_time_ms))return {};
					if(companion==nullptr)companion=movement;
					}
				}
			if(companion==nullptr)return {};
			return {
				true,
				target_x+companion->source_x-companion->target_x,
				target_y+companion->source_y-companion->target_y,
				companion->progress(frame_time_ms),
				true
				};
			}
};

#endif
