// SPDX-License-Identifier: MIT

// Plugin glue: binds the engine (DF globals, the 2D renderer hook, SDL entry points) to the
// headers that hold the behaviour, and parses the console commands. Nothing here decides what
// to paint; see frame_render.h, visual_animation.h, free_camera.h, view_context.h, sdl_canvas.h.

#include "Core.h"
#include "MemAccess.h"
#include "PluginManager.h"
#include "VTableInterpose.h"

#include "modules/DFSDL.h"

#include "df/enabler.h"
#include "df/graphic.h"
#include "df/graphic_viewportst.h"
#include "df/renderer_2d_base.h"
#include "df/texture_fullid.h"
#include "df/viewscreen_dungeonmodest.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

#include "frame_render.h"
#include "frame_stats.h"
#include "free_camera.h"
#include "sdl_canvas.h"
#include "view_context.h"
#include "visual_animation.h"

#include <SDL_render.h>
#include <SDL_rwops.h>
#include <SDL_surface.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using namespace DFHack;

DFHACK_PLUGIN("smooth-movement");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(window_x);
REQUIRE_GLOBAL(window_y);
REQUIRE_GLOBAL(window_z);
using df::global::pause_state;
using df::global::world;

namespace {

constexpr const char *plugin_version="0.3.0";

// The SDL2 entry points, looked up in the game's own SDL at enable time. Only the engine-owned
// visual state is touched; gameplay data is never read.
struct sdl_apist
{
	using renderer_type=SDL_Renderer;
	using texture_type=SDL_Texture;
	using rect_type=SDL_Rect;
	using frect_type=SDL_FRect;
	using color_type=Uint8;
	static constexpr SDL_RendererFlip flip_horizontal=SDL_FLIP_HORIZONTAL;

	decltype(&SDL_RenderCopyF) render_copy_f=nullptr;
	decltype(&SDL_RenderCopyExF) render_copy_ex_f=nullptr;
	decltype(&SDL_RenderFillRects) render_fill_rects=nullptr;
	decltype(&SDL_RenderSetClipRect) render_set_clip_rect=nullptr;
	decltype(&SDL_GetRenderDrawColor) get_render_draw_color=nullptr;
	decltype(&SDL_SetRenderDrawColor) set_render_draw_color=nullptr;
	// Frame capture (the `snapshot` command); optional, the pass runs without them.
	decltype(&SDL_GetRendererOutputSize) get_renderer_output_size=nullptr;
	decltype(&SDL_CreateRGBSurfaceWithFormat) create_rgb_surface_with_format=nullptr;
	decltype(&SDL_RenderReadPixels) render_read_pixels=nullptr;
	decltype(&SDL_RWFromFile) rw_from_file=nullptr;
	decltype(&SDL_SaveBMP_RW) save_bmp_rw=nullptr;
	decltype(&SDL_FreeSurface) free_surface=nullptr;

	static SDL_Texture *texture_of(
		df::renderer_2d_base *renderer,
		int32_t texpos,
		bool transparent_background=true)
		{
		if(texpos==0)return nullptr;
		df::texture_fullid texture_id;
		texture_id.texpos=texpos;
		texture_id.r=texture_id.g=texture_id.b=1.0f;
		texture_id.br=texture_id.bg=texture_id.bb=0.0f;
		texture_id.flag=transparent_background?
			df::texture_fullid_flag::mask_transparent_background:
			0;
		const auto texture=renderer->tile_cache.tile_cache.find(texture_id);
		return texture==renderer->tile_cache.tile_cache.end()?
			nullptr:
			static_cast<SDL_Texture *>(texture->second);
		}

	bool can_snapshot() const
		{
		return get_renderer_output_size&&create_rgb_surface_with_format&&render_read_pixels&&
			rw_from_file&&save_bmp_rw&&free_surface;
		}
};

// The console arms a capture; the render thread saves and disarms it. The console refuses a
// new request while one is armed, so the path is never written while it is read.
struct snapshot_requestst
{
	std::atomic<bool> armed{false};
	std::string path;
	int32_t frames=1;
	// Diagnostics: capture after the engine's UI stage instead of before it.
	bool after_ui=false;
};

// The simulation thread fills the viewport buffers inside the map viewscreens' render, then
// runs on while the render thread paints them, so the frame counter it shows is read there,
// not at paint time when the counter may already have moved on. Draw serial in the high half,
// tick in the low half, in one word so the paint side reads both as they were stored.
struct buffer_drawst
{
	std::atomic<uint64_t> drawn{0};
	// Set while a map viewscreen's render, which fills the buffers, is running.
	std::atomic<bool> drawing{false};
	uint32_t serial=0;   // simulation thread only

	void note_drawn()
		{
		if(world==nullptr)return;
		++serial;
		drawn.store((uint64_t(serial)<<32)|uint32_t(world->frame_counter),std::memory_order_release);
		}
	uint32_t drawn_serial() const
		{
		return uint32_t(drawn.load(std::memory_order_acquire)>>32);
		}
};

// The render thread's state, advanced by the update_all hook every frame and replaced whole
// by reset_state. Console commands never write it while the hook runs: they post to the
// mailbox below and the hook applies them at the top of the next frame. The console's status
// reads are unsynchronised single-field reads, worth at most a frame of staleness.
struct render_statest
{
	visual_animation_managerst animation_manager;
	frame_rendererst<df::graphic_viewportst> frame_renderer;
	view_context_trackerst view_context;
	free_camerast camera;
	std::vector<SDL_Rect> fill_scratch;
	uint32_t painted_draw_serial=0;
	// Tick of the buffers about to be painted, or -1 when no draw was seen since the last paint.
	int64_t simulation_tick=-1;
	int32_t snapshot_frames_left=0;
	int32_t snapshot_frame_index=0;

	// The tick of a draw not yet painted, or -1.
	int64_t take_drawn_tick(const buffer_drawst &draws)
		{
		const uint64_t drawn=draws.drawn.load(std::memory_order_acquire);
		const uint32_t serial=uint32_t(drawn>>32);
		if(serial==painted_draw_serial)return -1;
		painted_draw_serial=serial;
		return int64_t(int32_t(uint32_t(drawn)));
		}
};

// The plugin's state, by owner. Console commands run on the core thread, which suspends the
// simulation thread but not the render thread.
// The console's copy of every setting it can change: validated and reported here, pushed
// whole to the render thread through the mailbox, and the seed of every reset render state,
// so settings outlive disable/enable. The camera's position stays with the render thread.
struct console_settingst
{
	render_settingst render;
	uint32_t time_step_ms=visual_animation_managerst().base_duration_ms();
	bool camera=false;

	void apply_to(render_statest &r) const
		{
		r.frame_renderer.get_settings()=render;
		r.animation_manager.set_base_duration_ms(time_step_ms);
		if(r.camera.is_enabled()!=camera)r.camera.set_enabled(camera);
		}
};

// Console-to-render handoff: the core thread posts a change, the render thread applies it
// before painting. While the plugin is disabled the posts wait for the reset at enable.
struct command_mailboxst
{
	using commandt=std::function<void(render_statest &)>;
	std::mutex lock;
	std::vector<commandt> pending;

	void post(commandt command)
		{
		std::lock_guard<std::mutex> guard(lock);
		pending.push_back(std::move(command));
		}
	void drain(render_statest &render)
		{
		std::vector<commandt> commands;
			{
			std::lock_guard<std::mutex> guard(lock);
			commands.swap(pending);
			}
		for(const commandt &command:commands)command(render);
		}
};

struct plugin_statest
{
	// Bound at enable time before the hooks go in; read by the render thread after.
	sdl_apist sdl;
	// Counted by the render thread; switched, read and reset by the console.
	frame_statisticst stats;
	// Console-armed, render-served.
	snapshot_requestst snapshot;
	int32_t trace_budget=0;   // console-set; movements left to log before the trace stops
	console_settingst settings;   // console's own
	// Simulation thread writes, render thread reads.
	buffer_drawst draws;
	// Console posts, render thread applies; survives reset_state so nothing posted is lost.
	command_mailboxst mailbox;
	// Frames of the update_all hook in flight, so disable can wait for the last one.
	std::atomic<int32_t> hooks_in_flight{0};
	// Render thread's own. Written elsewhere only by the reset at enable, before the hooks
	// go in; the console reads the camera's offset from it for display.
	render_statest render;
};

plugin_statest state;

using canvasst=sdl_canvasst<df::renderer_2d_base,df::graphic_viewportst,sdl_apist>;

double tile_px(const df::renderer_2d_base *renderer)
{
	const int32_t zoom=renderer->viewport_zoom_factor;
	return double(zoom==128?32:std::max(1,zoom*32/128));
}

// Match ratio of "buffers shifted by (dwx,dwy)" on the background layer: 0..1, or -1 when there
// is nothing to compare (empty background).
double background_match_ratio(const df::graphic_viewportst *vp,int32_t dwx,int32_t dwy)
{
	int32_t considered=0;
	int32_t matches=0;
	for(int32_t x=0;x<vp->dim_x;++x)
		{
		const int32_t sx=x+dwx;
		if(sx<0||sx>=vp->dim_x)continue;
		for(int32_t y=0;y<vp->dim_y;++y)
			{
			const int32_t sy=y+dwy;
			if(sy<0||sy>=vp->dim_y)continue;
			const int32_t cur=vp->screentexpos_background[x*vp->dim_y+y];
			if(cur==0)continue;
			++considered;
			if(vp->screentexpos_background_old[sx*vp->dim_y+sy]==cur)++matches;
			}
		}
	if(considered==0)return -1.0;
	return double(matches)/double(considered);
}

// The camera's window writes: a plain UI scroll, never below zero.
std::array<bool,2> scroll_window(int32_t dx,int32_t dy)
{
	std::array<bool,2> applied{false,false};
	if(dx!=0&&window_x!=nullptr&&*window_x+dx>=0)
		{
		*window_x+=dx;
		applied[0]=true;
		}
	if(dy!=0&&window_y!=nullptr&&*window_y+dy>=0)
		{
		*window_y+=dy;
		applied[1]=true;
		}
	return applied;
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

viewport_visual_animation_inputst animation_input(const df::graphic_viewportst *vp)
{
	using table=viewport_layer_tablest<df::graphic_viewportst>;
	return {
		vp,
		vp->dim_x,
		vp->dim_y,
		state.render.view_context.revision(),
		table::current(vp),
		table::previous(vp),
		window_x?*window_x:0,
		window_y?*window_y:0,
		state.render.simulation_tick
		};
}

// The layer buffers are freed and nulled without clearing the active flag.
bool viewport_readable(const df::graphic_viewportst *vp)
{
	return vp!=nullptr&&vp->flag.bits.active&&animation_input(vp).valid();
}

std::vector<df::graphic_viewportst *> active_viewports()
{
	std::vector<df::graphic_viewportst *> viewports;
	if(gps==nullptr)return viewports;
	for(int32_t lower=7;lower>=0;--lower)
		{
		df::graphic_viewportst *vp=gps->lower_viewport[lower];
		if(viewport_readable(vp))viewports.push_back(vp);
		}
	if(viewport_readable(gps->main_viewport))
		viewports.push_back(gps->main_viewport);
	return viewports;
}

// Save what the renderer holds right now as a BMP next to the game: the map and this pass's
// sprites, before or after the UI is drawn over them. Reading the GPU back is slow; it runs
// on request only. Consecutive captures are numbered file-1, file-2, ...
void save_snapshot(df::renderer_2d_base *renderer)
{
	render_statest &r=state.render;
	if(r.snapshot_frames_left<=0)
		{
		r.snapshot_frames_left=state.snapshot.frames;
		r.snapshot_frame_index=0;
		}
	const bool last=--r.snapshot_frames_left<=0;
	++r.snapshot_frame_index;
	std::string path=state.snapshot.path;
	if(r.snapshot_frame_index>1||!last)
		{
		// Number the file name, not a directory: the last dot after the last separator.
		const size_t dot=path.rfind('.');
		const size_t sep=path.find_last_of("/\\");
		const size_t at=dot==std::string::npos||(sep!=std::string::npos&&dot<sep)?path.size():dot;
		path.insert(at,"-"+std::to_string(r.snapshot_frame_index));
		}
	// Disarmed only once the path has been read: the console may then write a new one.
	struct disarmst{bool last;~disarmst(){if(last)state.snapshot.armed=false;}} disarm{last};
	if(!state.sdl.can_snapshot())return;
	SDL_Renderer *sdl_renderer=static_cast<SDL_Renderer *>(renderer->sdl_renderer);
	int w=0,h=0;
	if(state.sdl.get_renderer_output_size(sdl_renderer,&w,&h)!=0||w<=0||h<=0)
		{
		Core::printerr("smooth-movement: snapshot: no renderer output size\n");
		return;
		}
	SDL_Surface *surface=state.sdl.create_rgb_surface_with_format(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);
	if(surface==nullptr)
		{
		Core::printerr("smooth-movement: snapshot: surface allocation failed\n");
		return;
		}
	bool ok=state.sdl.render_read_pixels(
		sdl_renderer,nullptr,surface->format->format,surface->pixels,surface->pitch)==0;
	if(ok)
		{
		SDL_RWops *file=state.sdl.rw_from_file(path.c_str(),"wb");
		ok=file!=nullptr&&state.sdl.save_bmp_rw(surface,file,1)==0;
		}
	state.sdl.free_surface(surface);
	if(ok)Core::print("smooth-movement: snapshot saved to {} ({}x{})\n",path,w,h);
	else Core::printerr("smooth-movement: snapshot failed\n");
}

void render_interpolated_world(df::renderer_2d_base *renderer)
{
	render_statest &r=state.render;
	state.mailbox.drain(r);
	frame_statisticst &stats=state.stats;
	df::graphic_viewportst *vp=gps?gps->main_viewport:nullptr;
	const std::vector<df::graphic_viewportst *> viewports=active_viewports();
	const uint64_t t0=stats.clock();
	stats.add(stats.frames);
	struct frame_timerst
	{
		frame_statisticst &stats;
		uint64_t t0;
		~frame_timerst()
			{
			if(!t0)return;
			const uint64_t t=stats.now_us()-t0;
			stats.add(stats.total_us,t);
			stats.note_max(t);
			}
	} timer{stats,t0};

	if(vp!=nullptr)
		{
		if(r.view_context.observe(vp,view_signature(renderer,vp)))r.camera.cancel_transients();
		}
	const uint32_t now_ms=Core::getInstance().p->getTickCount();
	const bool drawing_at_start=state.draws.drawing.load(std::memory_order_acquire);
	r.simulation_tick=r.take_drawn_tick(state.draws);
	if(r.simulation_tick>=0)stats.add(stats.drawn);
	r.animation_manager.begin_frame(now_ms);
	for(const df::graphic_viewportst *viewport:viewports)
		r.animation_manager.synchronize_viewport(animation_input(viewport));
	r.animation_manager.end_frame();
	if(t0)stats.add(stats.sync_us,stats.now_us()-t0);
	if(state.trace_budget>0)
		{
		// Opened only on a frame that has something to log.
		std::ofstream trace;
		for(const df::graphic_viewportst *viewport:viewports)
			{
			for(const visual_movementst &m:r.animation_manager.movements(viewport))
				{
				if(m.start_time_ms!=now_ms||state.trace_budget<=0)continue;
				--state.trace_budget;
				if(!trace.is_open())trace.open("smooth-movement-trace.txt",std::ios::app);
				trace<<"t="<<now_ms<<" paused="<<(pause_state!=nullptr&&*pause_state)
					<<" vp="<<(viewport==vp?"main":"other")<<" layer="<<int(m.layer)
					<<" texpos="<<m.texpos<<" from ("<<m.source_x<<","<<m.source_y
					<<") to ("<<m.target_x<<","<<m.target_y<<") facing="
					<<int(r.animation_manager.get_facing(viewport,m.target_x,m.target_y))<<"\n";
				}
			}
		}

	if(!viewport_readable(vp)||renderer->sdl_renderer==nullptr)
		return;
	const double cam_tile=tile_px(renderer);
	camera_framest camera_frame;
	camera_frame.window_x=window_x?*window_x:0;
	camera_frame.window_y=window_y?*window_y:0;
	camera_frame.middle_button=enabler!=nullptr&&enabler->mouse_mbut;
	camera_frame.mouse_x=gps->precise_mouse_x;
	camera_frame.mouse_y=gps->precise_mouse_y;
	camera_frame.tile=cam_tile;
	camera_frame.delta_ms=r.animation_manager.get_frame_delta_ms();
	r.camera.update(
		camera_frame,
		[vp](int32_t dx,int32_t dy){return background_match_ratio(vp,dx,dy);},
		scroll_window);
	const int32_t glide_x=r.camera.glide_x(cam_tile);
	const int32_t glide_y=r.camera.glide_y(cam_tile);
	const bool glide=glide_x!=0||glide_y!=0;
	// The engine redraws every map tile every frame before this hook runs (measured: one
	// update_viewport_tile call per tile per frame with nothing changed), so nothing painted
	// here outlives its frame; frame_render.h decides what a frame has to show.
	const frame_paintst paint=r.frame_renderer.frame_paint(glide,viewports,r.animation_manager);
	if(paint==frame_paintst::moving)stats.add(stats.moving);
	if(paint==frame_paintst::resting)stats.add(stats.resting);
	if(paint!=frame_paintst::nothing)
		{
		stats.add(stats.rendered);
		const uint64_t t1=stats.clock();
		{
		canvasst canvas(renderer,state.sdl,vp,stats,r.fill_scratch);
		r.frame_renderer.render(canvas,viewports,vp,r.animation_manager,glide_x,glide_y);
		}
		if(t1)stats.add(stats.render_us,stats.now_us()-t1);
		}
	if(state.snapshot.armed&&!state.snapshot.after_ui)save_snapshot(renderer);
	// The pass reads the viewport buffers and blanks parts of them around each engine repaint,
	// which is sound only while the simulation thread is not drawing into them. It draws
	// them in the map viewscreens' render; a draw in progress at either end of this hook, or
	// one that started and finished inside it, means the two overlap. Counted so `stats` can
	// show it never happens.
	if(drawing_at_start||state.draws.drawing.load(std::memory_order_acquire)||
		state.draws.drawn_serial()!=r.painted_draw_serial)
		stats.add(stats.overlapped_draws);
}

struct renderer_hook : df::renderer_2d_base
{
	typedef df::renderer_2d_base interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,update_all,());
};

IMPLEMENT_VMETHOD_INTERPOSE(renderer_hook,update_all);

void renderer_hook::interpose_fn_update_all()
{
	struct in_flightst
	{
		in_flightst(){state.hooks_in_flight.fetch_add(1,std::memory_order_acq_rel);}
		~in_flightst(){state.hooks_in_flight.fetch_sub(1,std::memory_order_acq_rel);}
	} in_flight;
	// update_all is the existing UI stage, so world correction must run first.
	render_interpolated_world(this);
	INTERPOSE_NEXT(update_all)();
	// After the UI stage, only once the same readability check the pre-UI capture passed.
	if(state.snapshot.armed&&state.snapshot.after_ui&&viewport_readable(gps?gps->main_viewport:nullptr))
		save_snapshot(this);
}

// Both map screens draw the viewports; the interpose records the tick they drew.
struct dwarfmode_hook : df::viewscreen_dwarfmodest
{
	typedef df::viewscreen_dwarfmodest interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,render,(uint32_t curtick))
		{
		state.draws.note_drawn();
		state.draws.drawing.store(true,std::memory_order_release);
		INTERPOSE_NEXT(render)(curtick);
		state.draws.drawing.store(false,std::memory_order_release);
		}
};

struct dungeonmode_hook : df::viewscreen_dungeonmodest
{
	typedef df::viewscreen_dungeonmodest interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,render,(uint32_t curtick))
		{
		state.draws.note_drawn();
		state.draws.drawing.store(true,std::memory_order_release);
		INTERPOSE_NEXT(render)(curtick);
		state.draws.drawing.store(false,std::memory_order_release);
		}
};

IMPLEMENT_VMETHOD_INTERPOSE(dwarfmode_hook,render);
IMPLEMENT_VMETHOD_INTERPOSE(dungeonmode_hook,render);

void clear_sdl_bindings()
{
	state.sdl=sdl_apist();
}

bool load_sdl(color_ostream &out)
{
	clear_sdl_bindings();
	DFLibrary *sdl_handle=DFSDL::obtain_library_handle();
	#define bind(name,target) \
		state.sdl.target=reinterpret_cast<decltype(state.sdl.target)>(LookupPlugin(sdl_handle,#name)); \
		if(state.sdl.target==nullptr) { \
			out.printerr("smooth-movement: SDL2 function unavailable: " #name "\n"); \
			clear_sdl_bindings(); \
			return false; \
		}
	#define bind_optional(name,target) \
		state.sdl.target=reinterpret_cast<decltype(state.sdl.target)>(LookupPlugin(sdl_handle,#name));
	bind(SDL_RenderCopyF,render_copy_f);
	bind(SDL_RenderCopyExF,render_copy_ex_f);
	bind(SDL_RenderFillRects,render_fill_rects);
	bind(SDL_RenderSetClipRect,render_set_clip_rect);
	bind(SDL_GetRenderDrawColor,get_render_draw_color);
	bind(SDL_SetRenderDrawColor,set_render_draw_color);
	bind_optional(SDL_GetRendererOutputSize,get_renderer_output_size);
	bind_optional(SDL_CreateRGBSurfaceWithFormat,create_rgb_surface_with_format);
	bind_optional(SDL_RenderReadPixels,render_read_pixels);
	bind_optional(SDL_RWFromFile,rw_from_file);
	bind_optional(SDL_SaveBMP_RW,save_bmp_rw);
	bind_optional(SDL_FreeSurface,free_surface);
	#undef bind_optional
	#undef bind
	return true;
}

// Called at enable, before the hooks go in. The last disable removed the hooks and waited
// for their last frame a command or more ago, so nothing else is in the render state.
// Commands posted meanwhile land in the fresh state.
void reset_state()
{
	state.render=render_statest();
	state.settings.apply_to(state.render);
	state.render.painted_draw_serial=state.draws.drawn_serial();
	state.snapshot.armed=false;
	state.trace_budget=0;
	state.mailbox.drain(state.render);
}

// Removing a hook does not wait for a hook body already running on the render thread. Waits
// for it, boundedly: the render thread may be blocked on the simulation thread this command
// suspended, in which case the wait gives up and says so; the render state is not touched
// until the next enable either way.
bool wait_for_last_frame()
{
	const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(200);
	while(state.hooks_in_flight.load(std::memory_order_acquire)!=0)
		{
		if(std::chrono::steady_clock::now()>=deadline)return false;
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	return true;
}

command_result status_command(
	color_ostream &out,
	std::vector<std::string> &parameters)
{
	// Sprite flipping and walk bob, both off by default: `flip on`, `bob on`.
	console_settingst &console=state.settings;
	render_settingst &settings=console.render;
	// The camera's offset is read from the render thread's state for display only.
	const free_camerast &camera=state.render.camera;
	const auto apply=[](command_mailboxst::commandt command)
		{
		state.mailbox.post(std::move(command));
		};
	// Pushes the console's settings to the render thread.
	const auto push_settings=[&]
		{
		apply([settings=console](render_statest &r){settings.apply_to(r);});
		};
	if(parameters.empty())
		{
		out.print(
			"smooth-movement {}: {}\n",
			plugin_version,
			is_enabled?"enabled":"disabled");
		out.print("free camera: {}, offset {:.3f} {:.3f} (tiles east/south of the grid)\n",
			console.camera?"on":"off",-camera.rest_offset_x(),-camera.rest_offset_y());
		out.print("sprite flipping: {}\n",
			settings.flip?"on":"off");
		out.print("time step: {} ms\n",console.time_step_ms);
		out.print("walk bob: {}\n",
			settings.bob.enabled?"on":"off");
		out.print("bob multipliers: horizontal {:.2f}, diagonal {:.2f}, vertical {:.2f}\n",
			settings.bob.horizontal_mult,settings.bob.diagonal_mult,settings.bob.vertical_mult);
		out.print("hops per step: {}\n",settings.bob.hops);
		return CR_OK;
		}
	if(parameters[0]=="camera")
		{
		if(parameters.size()==1)
			{
			out.print("free camera: {}, offset {:.3f} {:.3f}\n",
				console.camera?"on":"off",-camera.rest_offset_x(),-camera.rest_offset_y());
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="on")
			{
			console.camera=true;
			push_settings();
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="off")
			{
			console.camera=false;
			push_settings();
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="reset")
			{
			apply([](render_statest &r){r.camera.set_rest(0.0,0.0);});
			return CR_OK;
			}
		if(parameters.size()==3)
			{
			try
				{
				const double fx=std::stod(parameters[1]);
				const double fy=std::stod(parameters[2]);
				if(fx<-0.99||fx>0.99||fy<-0.99||fy>0.99)
					{
					out.printerr("offsets must be within -0.99..0.99 tiles\n");
					return CR_FAILURE;
					}
				// User-facing: positive = view sits east/south of the grid position.
				console.camera=true;
				push_settings();
				apply([fx,fy](render_statest &r)
					{
					r.camera.set_rest(-fx,-fy);
					r.camera.normalize_rest(scroll_window);
					});
				return CR_OK;
				}
			catch(...)
				{
				return CR_WRONG_USAGE;
				}
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="snapshot")
		{
		// snapshot [after] [count] [file]: save the next frame (or `count` consecutive frames,
		// numbered file-1, file-2, ...) before the UI goes on top, or after the engine's UI stage.
		std::vector<std::string> args(parameters.begin()+1,parameters.end());
		const bool after=!args.empty()&&args[0]=="after";
		if(after)args.erase(args.begin());
		int32_t count=1;
		const auto all_digits=[](const std::string &s)
			{
			return !s.empty()&&std::all_of(s.begin(),s.end(),
				[](char c){return std::isdigit(static_cast<unsigned char>(c))!=0;});
			};
		if(!args.empty()&&all_digits(args[0]))
			{
			try{count=std::stoi(args[0]);}catch(...){return CR_WRONG_USAGE;}
			if(count<1||count>1000)return CR_WRONG_USAGE;
			args.erase(args.begin());
			}
		if(args.size()>1)return CR_WRONG_USAGE;
		if(state.snapshot.armed)
			{
			out.printerr("smooth-movement: a capture is still armed; wait for it to finish\n");
			return CR_FAILURE;
			}
		if(!is_enabled)
			{
			out.printerr("smooth-movement: enable the plugin first\n");
			return CR_FAILURE;
			}
		if(!state.sdl.can_snapshot())
			{
			out.printerr("smooth-movement: frame capture needs SDL functions this SDL lacks\n");
			return CR_FAILURE;
			}
		state.snapshot.path=args.size()==1?args[0]:"smooth-movement-snapshot.bmp";
		state.snapshot.after_ui=after;
		state.snapshot.frames=count;
		state.snapshot.armed=true;
		out.print("smooth-movement: saving the next {} frame(s) to {}\n",count,state.snapshot.path);
		return CR_OK;
		}
	if(parameters[0]=="trace")
		{
		// trace [count]: log the next detected movements to smooth-movement-trace.txt.
		int32_t count=100;
		if(parameters.size()==2)
			{
			try{count=std::stoi(parameters[1]);}
			catch(const std::exception &){return CR_WRONG_USAGE;}
			}
		state.trace_budget=count;
		out.print("smooth-movement: tracing the next {} movements\n",count);
		return CR_OK;
		}
	if(parameters[0]=="stats")
		{
		// stats | stats on|off | stats reset | stats detail on|off
		if(parameters.size()==1){out.print("{}",state.stats.report());return CR_OK;}
		if(parameters[1]=="reset"){state.stats.reset();return CR_OK;}
		if(parameters[1]=="on"||parameters[1]=="off")
			{
			state.stats.enabled=parameters[1]=="on";
			state.stats.reset();
			out.print("smooth-movement: stats {}\n",state.stats.enabled?"on":"off");
			return CR_OK;
			}
		if(parameters[1]=="detail"&&parameters.size()==3&&
			(parameters[2]=="on"||parameters[2]=="off"))
			{
			state.stats.detail=parameters[2]=="on";
			state.stats.reset();
			out.print("smooth-movement: stats detail {}\n",state.stats.detail?"on":"off");
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="flip")
		{
		if(parameters.size()==1)
			{
			out.print("sprite flipping: {}\n",
				settings.flip?"on":"off");
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="on")
			{
			settings.flip=true;
			push_settings();
			out.print("smooth-movement: sprite flipping enabled\n");
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="off")
			{
			settings.flip=false;
			push_settings();
			out.print("smooth-movement: sprite flipping disabled\n");
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="timestep")
		{
		if(parameters.size()==1)
			{
			out.print("time step: {} ms\n",console.time_step_ms);
			return CR_OK;
			}
		if(parameters.size()==2)
			{
			try
				{
				const int32_t ms=std::stoi(parameters[1]);
				if(ms<20||ms>2000)return CR_WRONG_USAGE;
				console.time_step_ms=uint32_t(ms);
				push_settings();
				out.print("smooth-movement: time step {} ms\n",ms);
				return CR_OK;
				}
			catch(...){return CR_WRONG_USAGE;}
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="hops")
		{
		if(parameters.size()==1)
			{
			out.print("hops per step: {}\n",settings.bob.hops);
			return CR_OK;
			}
		if(parameters.size()==2&&(parameters[1]=="1"||parameters[1]=="2"))
			{
			settings.bob.hops=parameters[1]=="1"?1:2;
			push_settings();
			out.print("smooth-movement: hops per step {}\n",settings.bob.hops);
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="bobmult")
		{
		if(parameters.size()==1)
			{
			out.print("bob multipliers: horizontal {:.2f}, diagonal {:.2f}, vertical {:.2f}\n",
				settings.bob.horizontal_mult,settings.bob.diagonal_mult,settings.bob.vertical_mult);
			return CR_OK;
			}
		if(parameters.size()==4)
			{
			float horizontal=0.0f,diagonal=0.0f,vertical=0.0f;
			try
				{
				horizontal=std::stof(parameters[1]);
				diagonal=std::stof(parameters[2]);
				vertical=std::stof(parameters[3]);
				}
			catch(...){return CR_WRONG_USAGE;}
			// Written so that NaN fails too.
			if(!(horizontal>=0.0f&&horizontal<=5.0f)||!(diagonal>=0.0f&&diagonal<=5.0f)||
				!(vertical>=0.0f&&vertical<=5.0f))return CR_WRONG_USAGE;
			if(!walk_bob_lift_fits(settings.bob.amplitude,horizontal,diagonal,vertical))
				{
				out.printerr("smooth-movement: bob {:.2f} times that multiplier lifts more than {:.2f} tile; lower one of them\n",
					settings.bob.amplitude,max_walk_bob_lift);
				return CR_FAILURE;
				}
			settings.bob.horizontal_mult=horizontal;
			settings.bob.diagonal_mult=diagonal;
			settings.bob.vertical_mult=vertical;
			push_settings();
			out.print("smooth-movement: bob multipliers horizontal {:.2f}, diagonal {:.2f}, vertical {:.2f}\n",
				settings.bob.horizontal_mult,settings.bob.diagonal_mult,settings.bob.vertical_mult);
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="bob")
		{
		if(parameters.size()==1)
			{
			out.print("walk bob: {} (amount {:.2f})\n",settings.bob.enabled?"on":"off",settings.bob.amplitude);
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]!="on"&&parameters[1]!="off")
			{
			try
				{
				const float amount=std::stof(parameters[1]);
				// Turning the bob off is 'bob off'; an amount only sets the height.
				if(!(amount>0.0f)||amount>max_walk_bob_lift)return CR_WRONG_USAGE;
				if(!walk_bob_lift_fits(amount,settings.bob.horizontal_mult,settings.bob.diagonal_mult,
						settings.bob.vertical_mult))
					{
					out.printerr("smooth-movement: bob {:.2f} times the current multipliers lifts more than {:.2f} tile; lower the multipliers first\n",
						amount,max_walk_bob_lift);
					return CR_FAILURE;
					}
				settings.bob.amplitude=amount;
				push_settings();
				out.print("smooth-movement: bob amount {:.2f}\n",settings.bob.amplitude);
				return CR_OK;
				}
			catch(...){return CR_WRONG_USAGE;}
			}
		if(parameters.size()==2&&(parameters[1]=="on"||parameters[1]=="off"))
			{
			settings.bob.enabled=parameters[1]=="on";
			push_settings();
			out.print("smooth-movement: walk bob {}\n",settings.bob.enabled?"enabled":"disabled");
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	return CR_WRONG_USAGE;
}

} // namespace

DFhackCExport command_result
plugin_init(color_ostream &,std::vector<PluginCommand> &commands)
{
	commands.emplace_back(
		"smooth-movement",
		"Smooth movement status; time step: timestep <ms>; free camera: camera on|off|reset|<fx> <fy>; "
		"sprite flipping: flip on|off; "
		"walk bob: bob on|off|<amount>; bob multipliers: bobmult <horizontal> <diagonal> <vertical>; "
		"hops per step: hops 1|2; profiling: stats [on|off|reset|detail on|off]; "
		"frame capture: snapshot [after] [count] [file]; movement log: trace [count].",
		status_command);
	return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out,bool enable)
{
	if(is_enabled==enable)return CR_OK;
	if(enable)
		{
		reset_state();
		if(!load_sdl(out))return CR_FAILURE;
		if(!INTERPOSE_HOOK(dwarfmode_hook,render).apply()||
			!INTERPOSE_HOOK(dungeonmode_hook,render).apply()||
			!INTERPOSE_HOOK(renderer_hook,update_all).apply())
			{
			out.printerr("smooth-movement: could not hook the map screens and 2D renderer\n");
			INTERPOSE_HOOK(dwarfmode_hook,render).remove();
			INTERPOSE_HOOK(dungeonmode_hook,render).remove();
			INTERPOSE_HOOK(renderer_hook,update_all).remove();
			clear_sdl_bindings();
			return CR_FAILURE;
			}
		}
	else
		{
		INTERPOSE_HOOK(renderer_hook,update_all).remove();
		INTERPOSE_HOOK(dwarfmode_hook,render).remove();
		INTERPOSE_HOOK(dungeonmode_hook,render).remove();
		if(!wait_for_last_frame())
			out.printerr("smooth-movement: the render thread is still in the last frame\n");
		state.snapshot.armed=false;
		state.trace_budget=0;
		}
	is_enabled=enable;
	out.print("smooth-movement: {}\n",enable?"enabled":"disabled");
	return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out)
{
	return plugin_enable(out,false);
}
