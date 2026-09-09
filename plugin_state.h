// SPDX-License-Identifier: MIT
//
// The plugin's state, gathered by owner: the SDL functions bound at load, the frame counters,
// the recorder, the settings the console sets, and what the render hook reads and writes each
// frame. The plugin file holds the one instance; the console commands and the render hook
// are handed it. Nothing here reads the game: what a member needs from it is passed in.

#pragma once

#include "df/graphic_viewportst.h"

#include "frame_recorder.h"
#include "frame_stats.h"
#include "free_camera.h"
#include "plugin_settings.h"
#include "tile_repaint.h"
#include "view_context.h"
#include "visual_animation.h"

#include <SDL_render.h>

#include <atomic>
#include <cstdint>
#include <set>
#include <utility>

// The SDL functions the plugin binds at load; the game's own SDL, looked up by name.
struct sdl_apist
{
	decltype(&SDL_RenderCopyF) render_copy_f=nullptr;
	decltype(&SDL_RenderCopyExF) render_copy_ex_f=nullptr;
	decltype(&SDL_RenderFillRect) render_fill_rect=nullptr;
	decltype(&SDL_RenderSetClipRect) render_set_clip_rect=nullptr;
	decltype(&SDL_GetRenderDrawColor) get_render_draw_color=nullptr;
	decltype(&SDL_SetRenderDrawColor) set_render_draw_color=nullptr;

	// Back to unbound: a fresh value, so a function added above needs no line here.
	void clear()
		{
		*this=sdl_apist();
		}
};

// When the game last filled the viewports' per-tile arrays. The simulation thread fills them
// inside the map screens' render and runs on while the render thread paints them, so the
// simulation's frame counter is read there, not at paint time when it may already have
// moved on. Draw serial in the high half, tick in the low half, in one word so the paint side
// reads both as they were stored.
struct drawn_buffersst
{
	std::atomic<uint64_t> drawn{0};
	uint32_t draw_serial=0;         // simulation thread
	uint32_t painted_draw_serial=0; // render thread
	// Tick of the arrays about to be painted, or -1 when no fill was seen since the last
	// frame; the frame's animation input. A visual reset leaves it alone: the arrays on
	// screen keep the tick they were filled at, and the frame that resets is painting them.
	int64_t frame_simulation_tick=-1;

	// Simulation thread: the arrays were just filled at this frame counter.
	void note_drawn(int32_t tick)
		{
		++draw_serial;
		drawn.store((uint64_t(draw_serial)<<32)|uint32_t(tick),std::memory_order_release);
		}

	// Render thread: the tick of a fill not yet painted, or -1.
	int64_t take_tick()
		{
		const uint64_t value=drawn.load(std::memory_order_acquire);
		const uint32_t serial=uint32_t(value>>32);
		if(serial==painted_draw_serial)return -1;
		painted_draw_serial=serial;
		return int64_t(int32_t(uint32_t(value)));
		}
};

// The state the render hook reads and writes each frame.
struct render_statest
{
	visual_animation_managerst animation_manager;
	std::set<std::pair<int32_t,int32_t>> previous_coverage;
	view_context_trackerst view_context;

	// The camera itself lives in free_camera.h; the plugin file hands it what it reads from
	// the game each frame and writes the window position for it.
	free_camerast camera;
	bool camera_was_offset=false;             // edge-detects offset->0 for one cleanup redraw
	int32_t native_follow_id=-1;

	// The camera glide's per-tile blank summary, filled for the glide's frame and cleared
	// after.
	blank_summariest<df::graphic_viewportst> blank_summaries;

	drawn_buffersst drawn_buffers;

	// What the hook did on the frame in progress, for the recorder. Render thread only, so
	// a `stats reset` from the console cannot skew them.
	uint32_t hook_repaints=0;
	bool hook_painted=false;
};

struct plugin_statest
{
	sdl_apist sdl;
	frame_statsst stats;
	frame_recorderst recorder;
	bool flip_enabled=false;
	bool hauled_enabled=false;
	render_statest render;

	// The animation and camera state a freshly enabled plugin starts from, keeping the
	// settings; the recorder's first frame resets to it so the replay, which starts from
	// plugin_enable, sees the same start.
	void reset_visual()
		{
		const interpolationst &interpolation=render.animation_manager.get_interpolation();
		const uint32_t step_ms=render.animation_manager.step_duration_ms();
		const walk_bob_settingst bob=render.animation_manager.bob;
		render.animation_manager=visual_animation_managerst();
		render.animation_manager.set_interpolation(interpolation);
		render.animation_manager.set_step_duration_ms(step_ms);
		render.animation_manager.bob=bob;
		render.previous_coverage.clear();
		render.view_context=view_context_trackerst();
		render.camera.restart();
		render.camera_was_offset=false;
		render.native_follow_id=-1;
		}

	// The settings as one value, for the recorder to store with the frame.
	plugin_settingsst settings() const
		{
		plugin_settingsst s;
		s.flip=flip_enabled;
		s.hauled=hauled_enabled;
		s.camera=render.camera.is_enabled();
		s.rest_x=render.camera.rest_offset_x();
		s.rest_y=render.camera.rest_offset_y();
		s.interpolation=&render.animation_manager.get_interpolation();
		s.step_ms=render.animation_manager.step_duration_ms();
		s.bob=render.animation_manager.bob;
		return s;
		}

	// Every setting to the value given, where its owner reads it; the harness restores a
	// recorded frame's settings with it. The camera's rest offset goes after its switch,
	// which zeroes the offset when it changes.
	void apply_settings(const plugin_settingsst &s)
		{
		flip_enabled=s.flip;
		hauled_enabled=s.hauled;
		render.camera.set_enabled(s.camera);
		render.camera.set_rest(s.rest_x,s.rest_y);
		render.animation_manager.set_interpolation(*s.interpolation);
		render.animation_manager.set_step_duration_ms(s.step_ms);
		render.animation_manager.bob=s.bob;
		}

	// Everything back to how a freshly enabled plugin starts: the visual state, the settings
	// at their defaults, the counters cleared and a running recording stopped. `linear` is
	// the one setting kept: the plugin has kept linear easing across disable and enable
	// since that setting was added, and the walk bob has always come back off.
	void reset()
		{
		reset_visual();
		plugin_settingsst defaults;
		const interpolationst *linear=find_interpolation("linear");
		if(&render.animation_manager.get_interpolation()==linear)defaults.interpolation=linear;
		apply_settings(defaults);
		stats.enabled=false;
		stats.clear();
		recorder.stop();
		}
};
