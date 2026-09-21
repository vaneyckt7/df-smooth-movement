// SPDX-License-Identifier: MIT

#include "Core.h"
#include "MemAccess.h"
#include "PluginManager.h"
#include "VTableInterpose.h"

#include "modules/DFSDL.h"
#include "modules/Units.h"

#include "df/enabler.h"
#include "df/graphic.h"
#include "df/graphic_viewportst.h"
#include "df/plotinfost.h"
#include "df/renderer_2d_base.h"
#include "df/unit.h"
#include "df/viewport_spatter_flag.h"
#include "df/viewscreen_dungeonmodest.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

#include "frame_record.h"
#include "frame_recorder.h"
#include "frame_stats.h"
#include "free_camera.h"
#include "map_painting.h"
#include "plugin_commands.h"
#include "plugin_state.h"
#include "sprite_placement.h"
#include "sprite_proxies.h"
#include "texture_cache.h"
#include "tile_coverage.h"
#include "tile_redraw.h"
#include "tile_repaint.h"
#include "view_context.h"
#include "viewport_collection.h"
#include "visual_animation.h"

#include <SDL_render.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace DFHack;

DFHACK_PLUGIN("smooth-movement");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(window_x);
REQUIRE_GLOBAL(window_y);
REQUIRE_GLOBAL(window_z);
REQUIRE_GLOBAL(world);

namespace {

constexpr const char *plugin_version="0.5.0";

// Runs a callback when the scope ends, whichever return path is taken.
template<typename Callback>
struct scope_guardst
{
	Callback callback;
	explicit scope_guardst(Callback c):callback(std::move(c)){}
	~scope_guardst(){callback();}
	scope_guardst(const scope_guardst &)=delete;
	scope_guardst &operator=(const scope_guardst &)=delete;
};

plugin_statest state;

// Scrolls the game's window position by whole tiles for the camera and says which axes it
// applied: an axis without a window, or that would go negative, is left alone.
std::array<bool,2> scroll_window(int32_t kx,int32_t ky)
{
	std::array<bool,2> applied={false,false};
	if(kx!=0&&window_x!=nullptr&&*window_x+kx>=0)
		{
		*window_x+=kx;
		applied[0]=true;
		}
	if(ky!=0&&window_y!=nullptr&&*window_y+ky>=0)
		{
		*window_y+=ky;
		applied[1]=true;
		}
	return applied;
}

// What the camera observes on this frame.
camera_framest camera_frame(
	const df::graphic_viewportst *vp,
	double tile,
	uint32_t delta_ms,
	bool native_follow_active)
{
	return {
		vp,
		window_x?*window_x:0,
		window_y?*window_y:0,
		enabler!=nullptr&&enabler->mouse_mbut,
		gps?gps->precise_mouse_x:0,
		gps?gps->precise_mouse_y:0,
		tile,
		delta_ms,
		native_follow_active
		};
}

view_signaturest view_signature(
	const df::renderer_2d_base *renderer,
	const df::graphic_viewportst *vp)
{
	return {
		window_z?*window_z:0,
		vp->dim_x,
		vp->dim_y,
		vp->clipx[0],
		vp->clipx[1],
		vp->clipy[0],
		vp->clipy[1],
		renderer->viewport_zoom_factor,
		renderer->origin_x,
		renderer->origin_y,
		gps->dimx,
		gps->dimy
		};
}

void update_visual_context(
	const df::renderer_2d_base *renderer,
	const df::graphic_viewportst *vp)
{
	const view_context_changest change=state.render.view_context.observe(
		vp,view_signature(renderer,vp),window_x?*window_x:0,window_y?*window_y:0);
	if(change.reset)state.render.camera.cancel_transients();
	if(change.panned)state.render.previous_coverage.clear();
}

// The active viewports with their recording slots, for the recorder.
frame_recorderst::viewport_listst recorded_viewports()
{
	frame_recorderst::viewport_listst viewports;
	if(gps==nullptr)return viewports;
	for(int slot=0;slot<frame_record::main_slot;++slot)
		{
		const df::graphic_viewportst *vp=gps->lower_viewport[slot];
		if(vp!=nullptr&&vp->flag.bits.active)viewports.emplace_back(slot,vp);
		}
	const df::graphic_viewportst *vp=gps->main_viewport;
	if(vp!=nullptr&&vp->flag.bits.active)viewports.emplace_back(frame_record::main_slot,vp);
	return viewports;
}

// Asks the game to repaint everything on its next frame. The plugin asks for it whenever
// the screen must change without anything the game knows changing: when a recording starts
// from fresh visual state, when the camera re-joins the grid, when a setting turns off what
// the plugin painted last frame and when the plugin is disabled.
void full_redraw()
{
	if(gps!=nullptr)++gps->force_full_display_count;
}

// Hands the recorder what the hook reads at the start of a frame.
void record_frame_start(df::renderer_2d_base *renderer,uint32_t now_ms)
{
	state.recorder.capture([&](bool first)
		{
		if(first)
			{
			state.reset_visual();
			full_redraw();
			}
		frame_recorderst::frame_inputst input;
		frame_record::frame_headerst &header=input.header;
		header.settings=state.settings();
		header.simulation_tick=state.render.drawn_arrays.frame_simulation_tick;
		header.tick_ms=now_ms;
		header.window_x=window_x?*window_x:0;
		header.window_y=window_y?*window_y:0;
		header.window_z=window_z?*window_z:0;
		header.paused=pause_state&&*pause_state;
		header.follow_unit=plotinfo?plotinfo->follow_unit:-1;
		header.mouse_x=gps?gps->precise_mouse_x:0;
		header.mouse_y=gps?gps->precise_mouse_y:0;
		header.mouse_mbut=enabler!=nullptr&&enabler->mouse_mbut;
		header.zoom=renderer->viewport_zoom_factor;
		header.origin_x=renderer->origin_x;
		header.origin_y=renderer->origin_y;
		header.dimx=gps?gps->dimx:0;
		header.dimy=gps?gps->dimy:0;
		input.viewports=recorded_viewports();
		return input;
		});
}

// The units that are not hidden on the level the window shows, within the viewport's tiles
// from the window's corner; empty when the game has no window position.
std::vector<df::unit *> units_in_view(const df::graphic_viewportst *vp)
{
	std::vector<df::unit *> units;
	if(window_x==nullptr||window_y==nullptr||window_z==nullptr)return units;
	Units::getUnitsInBox(
		units,
		*window_x,*window_y,*window_z,
		*window_x+vp->dim_x-1,*window_y+vp->dim_y-1,*window_z,
		[](df::unit *unit){return !Units::isHidden(unit);});
	return units;
}

// Hands the recorder the units in view, where the hook looks them up.
void record_frame_units(df::renderer_2d_base *renderer)
{
	state.recorder.capture_units([&]
		{
		std::vector<frame_record::unit_recordst> records;
		const df::graphic_viewportst *vp=gps?gps->main_viewport:nullptr;
		if(vp!=nullptr)
			{
			for(const df::unit *unit:units_in_view(vp))
				{
				const int32_t texpos=item_texpos(hauled_item(unit));
				records.push_back({unit->pos.x,unit->pos.y,unit->pos.z,texpos,
					texpos!=0&&cached_texture(renderer,texpos)!=nullptr});
				}
			}
		return records;
		});
}

void render_world_with_movement(df::renderer_2d_base *renderer)
{
	state.stats.frames.fetch_add(1,std::memory_order_relaxed);
	// Read once: if the console flipped the flag on mid-frame, the guard would subtract a
	// start time of zero.
	const bool timing_enabled=state.stats.enabled.load(std::memory_order_relaxed);
	// The frame's clock, read once so that a recording carries the value the frame used.
	const uint32_t now_ms=Core::getInstance().p->getTickCount();
	state.render.drawn_arrays.frame_simulation_tick=state.render.drawn_arrays.take_tick();
	record_frame_start(renderer,now_ms);
	state.render.hook_repaints=0;
	state.render.hook_painted=false;
	const uint64_t frame_start_us=timing_enabled?frame_statsst::now_us():0;
	uint64_t sync_end_us=frame_start_us;
	// Runs on every exit, including the early return for frames with nothing to draw.
	const scope_guardst timing([&]
		{
		state.recorder.finish(
			state.render.hook_repaints,state.render.hook_painted,recorded_viewports);
		if(!timing_enabled)return;
		const uint64_t end_us=frame_statsst::now_us();
		state.stats.timed.fetch_add(1,std::memory_order_relaxed);
		state.stats.add_sync(sync_end_us-frame_start_us);
		state.stats.add_render(end_us-sync_end_us);
		});
	// The game's tile camera, read once for the viewport questions this frame asks below. A
	// null global reads as the origin, which is the answer the code below worked out for
	// itself before it moved into viewport_collection.h. Everything reading these runs before
	// the camera update, which can scroll the window itself; the carried-item icons run after
	// it and read the globals again.
	const int32_t window_x_tiles=window_x?*window_x:0;
	const int32_t window_y_tiles=window_y?*window_y:0;
	df::graphic_viewportst *vp=gps?gps->main_viewport:nullptr;
	const std::vector<df::graphic_viewportst *> viewports=
		active_viewports(state,gps,window_x_tiles,window_y_tiles);

	if(vp!=nullptr)update_visual_context(renderer,vp);
	const int32_t follow_id=plotinfo?plotinfo->follow_unit:-1;
	if(native_follow_changed(state.render.native_follow_id,follow_id))
		{
		state.render.native_follow_id=follow_id;
		state.render.view_context.bump();
		state.render.previous_coverage.clear();
		state.render.camera.restart();
		}
	state.render.animation_manager.begin_frame(now_ms);
	for(df::graphic_viewportst *viewport:viewports)
		state.render.animation_manager.synchronize_viewport(
			animation_input(state,viewport,window_x_tiles,window_y_tiles));
	state.render.animation_manager.end_frame();
	if(timing_enabled)sync_end_us=frame_statsst::now_us();

	if(!viewport_readable(state,vp,window_x_tiles,window_y_tiles)||
		renderer->sdl_renderer==nullptr)
		return;
	const bool paused=pause_state&&*pause_state;
	if(paused)state.render.camera.restart();
	const bool native_follow_active=follow_id>=0;
	const double cam_tile=renderer_tile_px(renderer);
	if(!paused)
		state.render.camera.update(
			camera_frame(vp,cam_tile,state.render.animation_manager.get_frame_delta_ms(),
				native_follow_active),
			state.render.animation_manager,scroll_window);
	const int32_t glide_x=state.render.camera.glide_x(cam_tile);
	const int32_t glide_y=state.render.camera.glide_y(cam_tile);
	const bool glide=glide_x!=0||glide_y!=0;
	if(!glide&&state.render.camera_was_offset)
		{
		// The camera just re-joined the grid: one redraw by the game replaces the last shifted
		// frame.
		state.render.camera_was_offset=false;
		full_redraw();
		}
	if(glide)state.render.camera_was_offset=true;
	record_frame_units(renderer);
	// The window again, not the locals read at the top of the frame: the camera update above
	// scrolls it by whole tiles when it normalizes a finished drag, and an icon's tile has to
	// come from the same window units_in_view puts its box around.
	std::vector<carried_item_proxyst> carried_items=
		state.hauled_enabled?
		collect_carried_item_proxies(
			state,renderer,vp,units_in_view(vp),window_x?*window_x:0,window_y?*window_y:0):
		std::vector<carried_item_proxyst>{};
	if(!glide&&!state.render.animation_manager.requires_full_redraw()&&
		(!state.flip_enabled||!has_mirrored_viewport_facing(state,viewports))&&
		carried_items.empty()&&state.render.previous_coverage.empty())
		return;
	state.stats.painted.fetch_add(1,std::memory_order_relaxed);
	state.render.hook_painted=true;

	std::vector<viewport_renderst> viewport_renders=
		collect_viewport_renders(state,renderer,viewports);
	tile_coveragest coverage=collect_viewport_coverage(viewport_renders);
	// A hauled icon follows the movement with the creature under it, found among the main
	// viewport's proxies; the icons are drawn over that viewport, the last one collected.
	if(!viewport_renders.empty())
		mark_carried_item_movements(carried_items,viewport_renders.back().proxies);
	for(const carried_item_proxyst &proxy:carried_items)
		coverage.insert(proxy.coverage.begin(),proxy.coverage.end());

	if(glide)
		{
		paint_shifted_map(
			state,renderer,vp,viewport_renders,coverage,carried_items,glide_x,glide_y);
		// Everything was repainted; per-tile coverage bookkeeping restarts after the glide.
		state.render.previous_coverage.clear();
		return;
		}

	paint_covered_tiles(state,renderer,vp,viewport_renders,coverage,carried_items);

	state.render.previous_coverage=std::move(coverage);
}

// The render thread runs update_all, and it is the one thread DFHack never lets take the
// core suspend: Core.h asserts a CoreSuspender is never constructed on it, and the vmethod
// interpose adds no lock of its own. `disable` reaches the teardown below with the suspend
// held, and `unload`/`reload` reach it with no suspend at all, because Plugin::unload closes
// its CoreSuspender scope before it calls plugin_shutdown. On either path a hook body can
// still be running. These two gates close that window. The teardown clears hook_live and
// then waits for in_flight to fall to zero; a body raises in_flight and only then reads
// hook_live. Both pairs are sequentially consistent, which is what makes the two orders
// exclusive: a body the wait did not count cannot yet have read hook_live, so it is bound
// to see the cleared flag and leave the state alone. The hooks stay installed across the
// wait on purpose -- removing them first would not help, because a body that had already
// read the vtable slot is free to start afterwards. That body is the one window this does
// not close: it enters after remove_hooks() has nulled the interpose chain, so the
// INTERPOSE_NEXT below calls through a null pointer-to-member. That follows from patching a
// live vtable and is the same for every DFHack interpose, so it is not this plugin's to fix.
std::atomic<bool> hook_live{false};
std::atomic<int> in_flight{0};

struct renderer_hook : df::renderer_2d_base
{
	typedef df::renderer_2d_base interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,update_all,());
};

IMPLEMENT_VMETHOD_INTERPOSE(renderer_hook,update_all);

void renderer_hook::interpose_fn_update_all()
{
	in_flight.fetch_add(1);
	// Runs on every exit, so a teardown cannot wait for ever on a body that returned early
	// or threw.
	const scope_guardst leave([]{in_flight.fetch_sub(1);});
	// update_all is the existing UI stage, so world correction must run first.
	if(hook_live.load())render_world_with_movement(this);
	// Counted as well, rather than left until after the guard: INTERPOSE_NEXT reads the
	// interpose link's chain pointer where it stands, and remove() ends by nulling that
	// pointer, so a body that had already left the counted region would call through null
	// as soon as the teardown reached remove_hooks().
	INTERPOSE_NEXT(update_all)();
}

// Both map screens fill the viewports' per-tile arrays in their render, on the simulation
// thread; the interposes note the simulation tick they were filled at.
void note_map_render()
{
	if(world!=nullptr)state.render.drawn_arrays.note_drawn(world->frame_counter);
}

struct dwarfmode_hook : df::viewscreen_dwarfmodest
{
	typedef df::viewscreen_dwarfmodest interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,render,(uint32_t curtick))
		{
		note_map_render();
		INTERPOSE_NEXT(render)(curtick);
		}
};

struct dungeonmode_hook : df::viewscreen_dungeonmodest
{
	typedef df::viewscreen_dungeonmodest interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,render,(uint32_t curtick))
		{
		note_map_render();
		INTERPOSE_NEXT(render)(curtick);
		}
};

IMPLEMENT_VMETHOD_INTERPOSE(dwarfmode_hook,render);
IMPLEMENT_VMETHOD_INTERPOSE(dungeonmode_hook,render);

// The three interposes the plugin runs on: both map screens' render and the 2D renderer's
// update_all. Applying stops at the first that fails; removing one that is not applied is
// a no-op, so a failed apply and a disable remove all three alike.
bool apply_hooks()
{
	return INTERPOSE_HOOK(dwarfmode_hook,render).apply()&&
		INTERPOSE_HOOK(dungeonmode_hook,render).apply()&&
		INTERPOSE_HOOK(renderer_hook,update_all).apply();
}

void remove_hooks()
{
	INTERPOSE_HOOK(renderer_hook,update_all).remove();
	INTERPOSE_HOOK(dwarfmode_hook,render).remove();
	INTERPOSE_HOOK(dungeonmode_hook,render).remove();
}

bool load_sdl(color_ostream &out)
{
	state.sdl.clear();
	DFLibrary *sdl_handle=DFSDL::obtain_library_handle();
	#define bind(name,target) \
		target=reinterpret_cast<decltype(target)>(LookupPlugin(sdl_handle,#name)); \
		if(target==nullptr) { \
			out.printerr("smooth-movement: SDL2 function unavailable: " #name "\n"); \
			state.sdl.clear(); \
			return false; \
		}
	bind(SDL_RenderCopyF,state.sdl.render_copy_f);
	bind(SDL_RenderCopyExF,state.sdl.render_copy_ex_f);
	bind(SDL_RenderFillRect,state.sdl.render_fill_rect);
	bind(SDL_RenderSetClipRect,state.sdl.render_set_clip_rect);
	bind(SDL_GetRenderDrawColor,state.sdl.get_render_draw_color);
	bind(SDL_SetRenderDrawColor,state.sdl.set_render_draw_color);
	#undef bind
	return true;
}

command_result status_command(
	color_ostream &out,
	std::vector<std::string> &parameters)
{
	const command_hostst host={plugin_version,is_enabled,full_redraw,scroll_window};
	switch(run_command(out,parameters,state,host))
		{
		case command_outcomest::ok:return CR_OK;
		case command_outcomest::failed:return CR_FAILURE;
		case command_outcomest::wrong_usage:break;
		}
	return CR_WRONG_USAGE;
}

} // namespace

DFhackCExport command_result
plugin_init(color_ostream &,std::vector<PluginCommand> &commands)
{
	commands.emplace_back(
		"smooth-movement",
		"Smooth movement: status; free camera: camera on|off|reset|<fx> <fy>; "
		"flip and hauled together: all on|off; "
		"sprite flipping: flip on|off; movement: movement <name> [setting [value]]; "
		"one-tile step time: timestep <ms> (20-2000); "
		"hauled item icons: hauled on|off; "
		"frame timing: stats [on|off|reset]; "
		"frame recording: record <file> [frames] | record stop | record status.",
		status_command);
	return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out,bool enable)
{
	if(is_enabled==enable)return CR_OK;
	if(enable)
		{
		state.reset();
		if(!load_sdl(out))return CR_FAILURE;
		if(!apply_hooks())
			{
			out.printerr("smooth-movement: could not hook the map screens and 2D renderer\n");
			remove_hooks();
			state.sdl.clear();
			return CR_FAILURE;
			}
		// Last, so that no body runs against state the lines above were still building.
		hook_live.store(true);
		}
	else
		{
		// First, so that bodies starting from here on leave the state alone, then wait for
		// the one that may already be running on the render thread. See hook_live.
		hook_live.store(false);
		while(in_flight.load()>0)std::this_thread::yield();
		remove_hooks();
		state.reset();
		state.sdl.clear();
		full_redraw();
		}
	is_enabled=enable;
	out.print("smooth-movement: {}\n",enable?"enabled":"disabled");
	return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out)
{
	return plugin_enable(out,false);
}
