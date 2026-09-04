// SPDX-License-Identifier: MIT

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

#include "frame_render.h"
#include "frame_stats.h"
#include "visual_animation.h"

#include <SDL_render.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>
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

namespace {

constexpr const char *plugin_version="0.3.0";

// Runtime harness for the engine-owned visual state; gameplay data is never read.
decltype(&SDL_RenderCopyF) render_copy_f=nullptr;
decltype(&SDL_RenderCopyExF) render_copy_ex_f=nullptr;
decltype(&SDL_RenderFillRects) render_fill_rects=nullptr;
decltype(&SDL_RenderSetClipRect) render_set_clip_rect=nullptr;
decltype(&SDL_GetRenderDrawColor) get_render_draw_color=nullptr;
decltype(&SDL_SetRenderDrawColor) set_render_draw_color=nullptr;

visual_animation_managerst animation_manager;
std::vector<SDL_Rect> fill_scratch;

frame_statisticst frame_stats;
frame_rendererst<df::graphic_viewportst> frame_renderer;
// Sprite flipping and walk bob, both off by default: `flip on`, `bob on`.
render_settingst &render_settings=frame_renderer.get_settings();
uint64_t visual_context_revision=0;
const void *previous_viewport=nullptr;
std::array<int32_t,12> previous_view_signature{};
bool has_view_signature=false;
// Map scroll (window_x/window_y) is tracked separately from the reset signature: a pure pan is
// followed (movements are translated) instead of triggering a full reset, so it must NOT bump the
// context revision. It only invalidates the viewport-space blackout coverage from the prior frame.
int32_t previous_pan_x=0;
int32_t previous_pan_y=0;
bool has_pan_context=false;

// --- free camera -------------------------------------------------------------------------------
// The camera is visually unbound from the tile grid. Two layered offsets:
//   rest      -- a PERSISTENT sub-tile offset in tiles: the free camera. Set by pixel-perfect
//                middle-mouse drag panning (the view rests wherever released, mid-tile or not)
//                and by the `smooth-movement camera <fx> <fy>` console command. Survives zoom
//                and z-level changes. Kept in [-0.5,0.5] by normalization: whole-tile parts are
//                folded into window_x/window_y (a plain UI scroll write -- NEVER the viewport
//                dims, which crash DF; the sub-tile strip this leaves at one screen edge has no
//                buffer data and stays black).
//   transient -- the decaying scroll glide from before, in pixels, layered on top.
// Render offset = transient + rest*tile. window_x/window_y remain the game's own tile camera.
bool camera_enabled=false;                    // OFF by default: plain `enable smooth-movement`
                                              // keeps upstream behavior (creature interpolation
                                              // only); `smooth-movement camera on` opts in.
constexpr int32_t camera_max_glide_tiles=3;   // per-jump: farther than this snaps instantly
constexpr double camera_tau_ms=35.0;          // transient catch-up (~95% done after 100ms)
double transient_x=0.0;                       // decaying glide offset, pixels
double transient_y=0.0;
double rest_x=0.0;                            // persistent free-camera offset, tiles
double rest_y=0.0;                            // (positive = view sits WEST/NORTH of window)
int32_t camera_pending_dx=0;                  // scroll delta announced but not yet in the buffers
int32_t camera_pending_dy=0;
int32_t camera_pending_frames=0;
int32_t self_scroll_x=0;                      // window deltas WE wrote: visual no-ops when landing
int32_t self_scroll_y=0;
bool drag_active=false;
double drag_anchor_vx=0.0;                    // visual camera at drag start, tiles
double drag_anchor_vy=0.0;
int32_t drag_anchor_mx=0;                     // precise mouse at drag start, pixels
int32_t drag_anchor_my=0;
bool camera_was_offset=false;                 // edge-detects offset->0 for one cleanup redraw
int16_t full_display_count_seen=0;            // gps->force_full_display_count last frame
bool has_full_display_count=false;
int32_t full_redraw_frames=0;                 // frames still to paint after a full engine redraw
int32_t camera_prev_wx=0;                     // window-scroll observation baseline
int32_t camera_prev_wy=0;
bool camera_has_prev=false;

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

// Cancel everything except the persistent rest offset (the camera keeps its sub-tile position
// across zoom/z/resize; only the in-flight animation state is unfollowable).
void clear_camera_pending()
{
	camera_pending_dx=0;
	camera_pending_dy=0;
	camera_pending_frames=0;
	self_scroll_x=0;
	self_scroll_y=0;
}

void cancel_camera_transients()
{
	transient_x=0.0;
	transient_y=0.0;
	clear_camera_pending();
	drag_active=false;
}

void set_camera_enabled(bool enable)
{
	if(camera_enabled==enable)return;
	camera_enabled=enable;
	cancel_camera_transients();
	rest_x=0.0;
	rest_y=0.0;
	camera_has_prev=false;   // fresh observation baseline; no phantom scroll on re-enable
	// camera_was_offset stays: the render path issues one cleanup redraw if we were mid-offset.
}

// Fold whole tiles of rest into window_x/window_y so |rest| <= 0.5 (minimal edge strip). The
// visual position is unchanged: the window write is attributed via self_scroll when it lands.
void normalize_rest()
{
	const int32_t kx=int32_t(-std::llround(rest_x));
	const int32_t ky=int32_t(-std::llround(rest_y));
	if(kx!=0&&window_x!=nullptr&&*window_x+kx>=0)
		{
		*window_x+=kx;
		self_scroll_x+=kx;
		}
	if(ky!=0&&window_y!=nullptr&&*window_y+ky>=0)
		{
		*window_y+=ky;
		self_scroll_y+=ky;
		}
}

// A scroll of (ax,ay) tiles has landed in the buffers: our own normalization writes are visual
// no-ops (they move into rest); the remainder is a real scroll and glides -- unless a drag is
// driving the position directly, in which case it folds into rest wholesale.
void attribute_landed(int32_t ax,int32_t ay,double tile)
{
	int32_t sx=0;
	if(self_scroll_x!=0&&(self_scroll_x>0)==(ax>0)&&ax!=0)
		sx=(std::abs(self_scroll_x)<=std::abs(ax))?self_scroll_x:ax;
	int32_t sy=0;
	if(self_scroll_y!=0&&(self_scroll_y>0)==(ay>0)&&ay!=0)
		sy=(std::abs(self_scroll_y)<=std::abs(ay))?self_scroll_y:ay;
	self_scroll_x-=sx;
	self_scroll_y-=sy;
	rest_x+=sx;
	rest_y+=sy;
	const int32_t gx=ax-sx;
	const int32_t gy=ay-sy;
	if(drag_active)
		{
		rest_x+=gx;
		rest_y+=gy;
		}
	else
		{
		transient_x+=gx*tile;
		transient_y+=gy*tile;
		const double cap=tile*(camera_max_glide_tiles+0.5);
		transient_x=std::clamp(transient_x,-cap,cap);
		transient_y=std::clamp(transient_y,-cap,cap);
		}
}

// Per-frame camera bookkeeping: observe window scrolls, attribute them when the buffers apply
// them (glide vs our own normalization writes), drive the drag, decay the transient.
void update_camera(
	df::renderer_2d_base *renderer,
	const df::graphic_viewportst *vp,
	uint32_t delta_ms)
{
	if(!camera_enabled)return;
	const double tile=tile_px(renderer);
	const int32_t wx=window_x?*window_x:0;
	const int32_t wy=window_y?*window_y:0;
	if(camera_has_prev&&(wx!=camera_prev_wx||wy!=camera_prev_wy))
		{
		const int32_t dx=wx-camera_prev_wx;
		const int32_t dy=wy-camera_prev_wy;
		if((std::abs(dx)>camera_max_glide_tiles||std::abs(dy)>camera_max_glide_tiles)&&
			!drag_active)
			cancel_camera_transients();   // teleport-like jump (recenter/minimap): snap
		else
			{
			camera_pending_dx+=dx;
			camera_pending_dy+=dy;
			camera_pending_frames=0;
			}
		}
	camera_prev_wx=wx;
	camera_prev_wy=wy;
	camera_has_prev=true;

	if(camera_pending_dx!=0||camera_pending_dy!=0)
		{
		if(std::abs(camera_pending_dx)>6||std::abs(camera_pending_dy)>6)
			{
			// Scrolling far outran detection: snap (keep rest, drop the animation debt).
			transient_x=0.0;
			transient_y=0.0;
			clear_camera_pending();
			}
		else
			{
			// Fast scrolling applies the pending delta PIECEMEAL: the buffers may hold +1 of a
			// pending +3 this frame. Testing only the total made landings miss, time out, and
			// snap -- the fast-scroll jitter. Instead, find the LARGEST applied prefix of the
			// pending scroll and attribute just that; the rest keeps pending. Ties between
			// qualifying shifts only happen on uniform terrain, where mistiming is invisible.
			const int32_t stepx=(camera_pending_dx>0)-(camera_pending_dx<0);
			const int32_t stepy=(camera_pending_dy>0)-(camera_pending_dy<0);
			int32_t best_ax=0,best_ay=0,best_mag=-1;
			double best_score=-1.0;
			bool no_data=false;
			for(int32_t ix=0;ix<=std::abs(camera_pending_dx);++ix)
				{
				for(int32_t iy=0;iy<=std::abs(camera_pending_dy);++iy)
					{
					const double score=background_match_ratio(vp,ix*stepx,iy*stepy);
					if(score<0.0){no_data=true;break;}
					const int32_t mag=ix+iy;
					if(score>=0.6&&(mag>best_mag||(mag==best_mag&&score>best_score)))
						{
						best_mag=mag;
						best_score=score;
						best_ax=ix*stepx;
						best_ay=iy*stepy;
						}
					}
				if(no_data)break;
				}
			if(no_data)
				{
				// Nothing to compare against (empty background): give up on attribution.
				clear_camera_pending();
				}
			else if(best_mag>0)
				{
				attribute_landed(best_ax,best_ay,tile);
				camera_pending_dx-=best_ax;
				camera_pending_dy-=best_ay;
				camera_pending_frames=0;
				}
			else if(best_mag==0)
				{
				// Content demonstrably hasn't moved yet: keep waiting, no timeout pressure.
				camera_pending_frames=0;
				}
			else if(++camera_pending_frames>4)
				{
				// Neither static nor any prefix recognizable (heavy simultaneous change):
				// drop the debt without touching the in-flight glide.
				clear_camera_pending();
				}
			}
		}

	// --- pixel-perfect middle-mouse drag: the view follows the mouse 1:1 and rests where
	// released. DF's own drag still moves window in tile steps; rest carries the remainder.
	// Positions are tracked against the CONTENT window (window minus unlanded jumps) so the
	// buffer lag never causes a visible stutter.
	const bool mbut=enabler!=nullptr&&enabler->mouse_mbut;
	const double content_wx=double(wx-camera_pending_dx);
	const double content_wy=double(wy-camera_pending_dy);
	if(mbut&&!drag_active&&gps!=nullptr)
		{
		drag_active=true;
		drag_anchor_vx=content_wx-rest_x-transient_x/tile;
		drag_anchor_vy=content_wy-rest_y-transient_y/tile;
		drag_anchor_mx=gps->precise_mouse_x;
		drag_anchor_my=gps->precise_mouse_y;
		transient_x=0.0;
		transient_y=0.0;
		}
	if(drag_active)
		{
		if(!mbut)
			{
			drag_active=false;
			normalize_rest();
			}
		else if(gps!=nullptr)
			{
			double vx=drag_anchor_vx-double(gps->precise_mouse_x-drag_anchor_mx)/tile;
			double vy=drag_anchor_vy-double(gps->precise_mouse_y-drag_anchor_my)/tile;
			rest_x=content_wx-vx;
			rest_y=content_wy-vy;
			// If DF's own drag disagrees by more than a tile and a half, rebase on its view.
			const double lim=1.5;
			if(rest_x<-lim||rest_x>lim||rest_y<-lim||rest_y>lim)
				{
				rest_x=std::clamp(rest_x,-lim,lim);
				rest_y=std::clamp(rest_y,-lim,lim);
				drag_anchor_vx=content_wx-rest_x+
					double(gps->precise_mouse_x-drag_anchor_mx)/tile;
				drag_anchor_vy=content_wy-rest_y+
					double(gps->precise_mouse_y-drag_anchor_my)/tile;
				}
			}
		}

	if(transient_x!=0.0||transient_y!=0.0)
		{
		const double k=std::exp(-double(delta_ms)/camera_tau_ms);
		transient_x*=k;
		transient_y*=k;
		if(std::abs(transient_x)<0.5&&std::abs(transient_y)<0.5)
			{
			transient_x=0.0;
			transient_y=0.0;
			}
		}
}

void update_visual_context(
	const df::renderer_2d_base *renderer,
	const df::graphic_viewportst *vp)
{
	// window_x/window_y are deliberately excluded: a horizontal/vertical scroll is followed, not
	// reset. window_z (z-level) stays, since a z change is not followable.
	const std::array<int32_t,12> signature=
		{
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
	const bool changed=!has_view_signature||previous_viewport!=vp||
		previous_view_signature!=signature;
	if(changed)
		{
		++visual_context_revision;
		frame_renderer.forget_coverage();
		cancel_camera_transients();
		}
	previous_viewport=vp;
	previous_view_signature=signature;
	has_view_signature=true;

	// On a pure pan the reset signature is unchanged, but last frame's blackout coverage is in the
	// old viewport frame, so discard it (the engine repaints the whole scrolled viewport anyway).
	const int32_t pan_x=window_x?*window_x:0;
	const int32_t pan_y=window_y?*window_y:0;
	if(!has_pan_context||previous_pan_x!=pan_x||previous_pan_y!=pan_y)
		frame_renderer.forget_coverage();
	previous_pan_x=pan_x;
	previous_pan_y=pan_y;
	has_pan_context=true;
}

viewport_visual_animation_inputst animation_input(const df::graphic_viewportst *vp)
{
	using table=viewport_layer_tablest<df::graphic_viewportst>;
	return {
		vp,
		vp->dim_x,
		vp->dim_y,
		visual_context_revision,
		table::current(vp),
		table::previous(vp),
		window_x?*window_x:0,
		window_y?*window_y:0
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

SDL_Texture *cached_texture(
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

// The engine's renderer as the canvas frame_rendererst paints on.
class sdl_canvasst
{
	df::renderer_2d_base *renderer;
	SDL_Renderer *sdl;
	bool filling=false;
	Uint8 saved_r=0,saved_g=0,saved_b=0,saved_a=255;
	// The frame's black fills, issued as one SDL call before anything paints over them.
	std::vector<SDL_Rect> &fills=fill_scratch;

	void flush_fills()
		{
		if(fills.empty())return;
		const uint64_t t=frame_stats.detail_clock();
		render_fill_rects(sdl,fills.data(),int(fills.size()));
		if(t)frame_stats.add(frame_stats.fill_us,frame_stats.now_us()-t);
		fills.clear();
		}
	// Time from construction to the first canvas call is the sprite collection.
	uint64_t start_us=frame_stats.clock();
	bool first_call=true;
	void note_call()
		{
		if(!first_call)return;
		first_call=false;
		if(start_us)frame_stats.add(frame_stats.collect_us,frame_stats.now_us()-start_us);
		}

	public:
		explicit sdl_canvasst(df::renderer_2d_base *renderer):
			renderer(renderer),
			sdl(static_cast<SDL_Renderer *>(renderer->sdl_renderer))
			{
			}

		~sdl_canvasst()
			{
			flush_fills();
			if(filling)set_render_draw_color(sdl,saved_r,saved_g,saved_b,saved_a);
			}

		sdl_canvasst(const sdl_canvasst &)=delete;
		sdl_canvasst &operator=(const sdl_canvasst &)=delete;

		int32_t origin_x() const
			{
			return renderer->origin_x;
			}

		int32_t origin_y() const
			{
			return renderer->origin_y;
			}

		int32_t zoom() const
			{
			return renderer->viewport_zoom_factor;
			}

		void offset_origin(int32_t dx,int32_t dy)
			{
			renderer->origin_x+=dx;
			renderer->origin_y+=dy;
			}

		void repaint(df::graphic_viewportst *vp,int32_t x,int32_t y)
			{
			flush_fills();
			note_call();
			if(frame_stats.enabled)classify_repaint(vp,x,y);
			const uint64_t t=frame_stats.detail_clock();
			renderer->update_viewport_tile(vp,x,y);
			if(t)frame_stats.add(frame_stats.engine_us,frame_stats.now_us()-t);
			}

		// Where the repaints land: the ones the pass could in principle avoid are counted.
		static void classify_repaint(const df::graphic_viewportst *vp,int32_t x,int32_t y)
			{
			frame_stats.add(frame_stats.tile_repaints);
			const int32_t index=x*vp->dim_y+y;
			if(tile_paints_nothing(vp,index))frame_stats.add(frame_stats.blank_repaints);
			const df::graphic_viewportst *main=gps?gps->main_viewport:nullptr;
			if(vp==main)frame_stats.add(frame_stats.main_repaints);
			else if(main&&main->dim_x==vp->dim_x&&main->dim_y==vp->dim_y&&
				main->screentexpos_background&&main->screentexpos_background[index]!=0)
				frame_stats.add(frame_stats.occluded_repaints);
			}

		const void *texture(int32_t texpos) const
			{
			return cached_texture(renderer,texpos);
			}

		// The render_copy_ex_f null check is defensive only, not a graceful-degradation path:
		// `bind` aborts load_sdl on any missing symbol and plugin_enable then refuses the hook.
		void draw_sprite(const void *texture,float x,float y,float size,bool mirrored)
			{
			flush_fills();
			note_call();
			frame_stats.add(frame_stats.sprites);
			const uint64_t t=frame_stats.detail_clock();
			SDL_Texture *sdl_texture=static_cast<SDL_Texture *>(const_cast<void *>(texture));
			const SDL_FRect destination={x,y,size,size};
			if(mirrored&&render_copy_ex_f!=nullptr)
				render_copy_ex_f(
					sdl,sdl_texture,nullptr,&destination,0.0,nullptr,SDL_FLIP_HORIZONTAL);
			else render_copy_f(sdl,sdl_texture,nullptr,&destination);
			if(t)frame_stats.add(frame_stats.sprite_us,frame_stats.now_us()-t);
			}

		// The draw colour is switched to black on the first fill and put back when the
		// frame's canvas goes away, not around every tile. Fills are collected and issued
		// together: the frame pass fills before it paints anything over them.
		void fill_black(const pixel_rectst &rect)
			{
			note_call();
			frame_stats.add(frame_stats.fills);
			if(!filling)
				{
				get_render_draw_color(sdl,&saved_r,&saved_g,&saved_b,&saved_a);
				set_render_draw_color(sdl,0,0,0,255);
				filling=true;
				}
			fills.push_back({rect.x,rect.y,rect.w,rect.h});
			}

		void set_clip(const pixel_rectst &rect)
			{
			flush_fills();
			note_call();
			frame_stats.add(frame_stats.glides);
			const SDL_Rect sdl_rect={rect.x,rect.y,rect.w,rect.h};
			render_set_clip_rect(sdl,&sdl_rect);
			}

		void clear_clip()
			{
			flush_fills();
			render_set_clip_rect(sdl,nullptr);
			}
};

void render_interpolated_world(df::renderer_2d_base *renderer)
{
	df::graphic_viewportst *vp=gps?gps->main_viewport:nullptr;
	const std::vector<df::graphic_viewportst *> viewports=active_viewports();
	const uint64_t t0=frame_stats.clock();
	frame_stats.add(frame_stats.frames);
	struct frame_timerst
	{
		uint64_t t0;
		~frame_timerst()
			{
			if(!t0)return;
			const uint64_t t=frame_stats.now_us()-t0;
			frame_stats.add(frame_stats.total_us,t);
			frame_stats.note_max(t);
			}
	} timer{t0};

	if(vp!=nullptr)update_visual_context(renderer,vp);
	const uint32_t now_ms=Core::getInstance().p->getTickCount();
	animation_manager.begin_frame(now_ms);
	for(const df::graphic_viewportst *viewport:viewports)
		animation_manager.synchronize_viewport(animation_input(viewport));
	animation_manager.end_frame();
	if(t0)frame_stats.add(frame_stats.sync_us,frame_stats.now_us()-t0);

	if(!viewport_readable(vp)||renderer->sdl_renderer==nullptr)
		return;
	update_camera(renderer,vp,animation_manager.get_frame_delta_ms());
	const double cam_tile=tile_px(renderer);
	const int32_t glide_x=int32_t(std::lround(transient_x+rest_x*cam_tile));
	const int32_t glide_y=int32_t(std::lround(transient_y+rest_y*cam_tile));
	const bool glide=glide_x!=0||glide_y!=0;
	if(!glide&&camera_was_offset)
		{
		// The camera just re-joined the grid: one engine redraw replaces the last shifted frame.
		camera_was_offset=false;
		if(gps!=nullptr)++gps->force_full_display_count;
		}
	if(glide)camera_was_offset=true;
	// A full engine redraw wipes the resting mirrored sprites; whether the engine acts on the
	// counter before or after this hook, painting this frame and the next covers it.
	const int16_t full_display_count=gps!=nullptr?gps->force_full_display_count:0;
	if(!has_full_display_count||full_display_count!=full_display_count_seen)full_redraw_frames=2;
	full_display_count_seen=full_display_count;
	has_full_display_count=true;
	const bool after_full_redraw=full_redraw_frames>0;
	if(after_full_redraw)--full_redraw_frames;
	const bool moving=animation_manager.requires_full_redraw();
	const bool disturbed=!glide&&!after_full_redraw&&!moving&&
		frame_renderer.resting_sprites_disturbed(viewports,animation_manager);
	if(moving)frame_stats.add(frame_stats.moving);
	if(disturbed)frame_stats.add(frame_stats.disturbed);
	if(!glide&&!after_full_redraw&&!moving&&!disturbed)return;

	frame_stats.add(frame_stats.rendered);
	const uint64_t t1=frame_stats.clock();
	{
	sdl_canvasst canvas(renderer);
	frame_renderer.render(canvas,viewports,vp,animation_manager,glide_x,glide_y);
	}
	if(t1)frame_stats.add(frame_stats.render_us,frame_stats.now_us()-t1);
}

struct renderer_hook : df::renderer_2d_base
{
	typedef df::renderer_2d_base interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,update_all,());
};

IMPLEMENT_VMETHOD_INTERPOSE(renderer_hook,update_all);

void renderer_hook::interpose_fn_update_all()
{
	// update_all is the existing UI stage, so world correction must run first.
	render_interpolated_world(this);
	INTERPOSE_NEXT(update_all)();
}

void clear_sdl_bindings()
{
	render_copy_f=nullptr;
	render_copy_ex_f=nullptr;
	render_fill_rects=nullptr;
	render_set_clip_rect=nullptr;
	get_render_draw_color=nullptr;
	set_render_draw_color=nullptr;
}

bool load_sdl(color_ostream &out)
{
	clear_sdl_bindings();
	DFLibrary *sdl_handle=DFSDL::obtain_library_handle();
	#define bind(name,target) \
		target=reinterpret_cast<decltype(target)>(LookupPlugin(sdl_handle,#name)); \
		if(target==nullptr) { \
			out.printerr("smooth-movement: SDL2 function unavailable: " #name "\n"); \
			clear_sdl_bindings(); \
			return false; \
		}
	bind(SDL_RenderCopyF,render_copy_f);
	bind(SDL_RenderCopyExF,render_copy_ex_f);
	bind(SDL_RenderFillRects,render_fill_rects);
	bind(SDL_RenderSetClipRect,render_set_clip_rect);
	bind(SDL_GetRenderDrawColor,get_render_draw_color);
	bind(SDL_SetRenderDrawColor,set_render_draw_color);
	#undef bind
	return true;
}

void reset_state()
{
	animation_manager=visual_animation_managerst();
	frame_renderer=frame_rendererst<df::graphic_viewportst>();
	visual_context_revision=0;
	previous_viewport=nullptr;
	previous_view_signature={};
	has_view_signature=false;
	previous_pan_x=0;
	previous_pan_y=0;
	has_pan_context=false;
	cancel_camera_transients();
	rest_x=0.0;
	rest_y=0.0;
	camera_enabled=false;
	camera_has_prev=false;
	camera_was_offset=false;
	has_full_display_count=false;
	full_redraw_frames=0;
}

command_result status_command(
	color_ostream &out,
	std::vector<std::string> &parameters)
{
	if(parameters.empty())
		{
		out.print(
			"smooth-movement {}: {}\n",
			plugin_version,
			is_enabled?"enabled":"disabled");
		out.print("free camera: {}, offset {:.3f} {:.3f} (tiles east/south of the grid)\n",
			camera_enabled?"on":"off",-rest_x,-rest_y);
		out.print("sprite flipping: {}\n",
			render_settings.flip?"on":"off");
		out.print("time step: {} ms\n",animation_manager.base_duration_ms());
		out.print("walk bob: {}\n",
			render_settings.bob.enabled?"on":"off");
		out.print("bob multipliers: horizontal {:.2f}, diagonal {:.2f}, vertical {:.2f}\n",
			render_settings.bob.horizontal_mult,render_settings.bob.diagonal_mult,render_settings.bob.vertical_mult);
		out.print("hops per step: {}\n",render_settings.bob.hops);
		return CR_OK;
		}
	if(parameters[0]=="camera")
		{
		if(parameters.size()==1)
			{
			out.print("free camera: {}, offset {:.3f} {:.3f}\n",
				camera_enabled?"on":"off",-rest_x,-rest_y);
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="on")
			{
			set_camera_enabled(true);
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="off")
			{
			set_camera_enabled(false);
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="reset")
			{
			rest_x=0.0;
			rest_y=0.0;
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
				set_camera_enabled(true);
				rest_x=-fx;
				rest_y=-fy;
				normalize_rest();
				return CR_OK;
				}
			catch(...)
				{
				return CR_WRONG_USAGE;
				}
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="stats")
		{
		// stats | stats on|off | stats reset | stats detail on|off
		if(parameters.size()==1){out.print("{}",frame_stats.report());return CR_OK;}
		if(parameters[1]=="reset"){frame_stats.reset();return CR_OK;}
		if(parameters[1]=="on"||parameters[1]=="off")
			{
			frame_stats.enabled=parameters[1]=="on";
			frame_stats.reset();
			out.print("smooth-movement: stats {}\n",frame_stats.enabled?"on":"off");
			return CR_OK;
			}
		if(parameters[1]=="detail"&&parameters.size()==3&&
			(parameters[2]=="on"||parameters[2]=="off"))
			{
			frame_stats.detail=parameters[2]=="on";
			frame_stats.reset();
			out.print("smooth-movement: stats detail {}\n",frame_stats.detail?"on":"off");
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="flip")
		{
		if(parameters.size()==1)
			{
			out.print("sprite flipping: {}\n",
				render_settings.flip?"on":"off");
			return CR_OK;
			}
		// A toggle changes the screen without changing anything DF knows, so DF will not repaint.
		// OFF matters most: the render path stops touching tiles it painted every frame.
		// The last mirrored frame would persist.
		// Same flush plugin_enable(false) uses.
		if(parameters.size()==2&&parameters[1]=="on")
			{
			render_settings.flip=true;
			if(gps!=nullptr)++gps->force_full_display_count;
			out.print("smooth-movement: sprite flipping enabled\n");
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]=="off")
			{
			render_settings.flip=false;
			if(gps!=nullptr)++gps->force_full_display_count;
			out.print("smooth-movement: sprite flipping disabled\n");
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="timestep")
		{
		if(parameters.size()==1)
			{
			out.print("time step: {} ms\n",animation_manager.base_duration_ms());
			return CR_OK;
			}
		if(parameters.size()==2)
			{
			try
				{
				const int32_t ms=std::stoi(parameters[1]);
				if(ms<20||ms>2000)return CR_WRONG_USAGE;
				animation_manager.set_base_duration_ms(uint32_t(ms));
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
			out.print("hops per step: {}\n",render_settings.bob.hops);
			return CR_OK;
			}
		if(parameters.size()==2&&(parameters[1]=="1"||parameters[1]=="2"))
			{
			render_settings.bob.hops=parameters[1]=="1"?1:2;
			out.print("smooth-movement: hops per step {}\n",render_settings.bob.hops);
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="bobmult")
		{
		if(parameters.size()==1)
			{
			out.print("bob multipliers: horizontal {:.2f}, diagonal {:.2f}, vertical {:.2f}\n",
				render_settings.bob.horizontal_mult,render_settings.bob.diagonal_mult,render_settings.bob.vertical_mult);
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
			if(!walk_bob_lift_fits(render_settings.bob.amplitude,horizontal,diagonal,vertical))
				{
				out.printerr("smooth-movement: bob {:.2f} times that multiplier lifts more than {:.2f} tile; lower one of them\n",
					render_settings.bob.amplitude,max_walk_bob_lift);
				return CR_FAILURE;
				}
			render_settings.bob.horizontal_mult=horizontal;
			render_settings.bob.diagonal_mult=diagonal;
			render_settings.bob.vertical_mult=vertical;
			out.print("smooth-movement: bob multipliers horizontal {:.2f}, diagonal {:.2f}, vertical {:.2f}\n",
				render_settings.bob.horizontal_mult,render_settings.bob.diagonal_mult,render_settings.bob.vertical_mult);
			return CR_OK;
			}
		return CR_WRONG_USAGE;
		}
	if(parameters[0]=="bob")
		{
		if(parameters.size()==1)
			{
			out.print("walk bob: {} (amount {:.2f})\n",render_settings.bob.enabled?"on":"off",render_settings.bob.amplitude);
			return CR_OK;
			}
		if(parameters.size()==2&&parameters[1]!="on"&&parameters[1]!="off")
			{
			try
				{
				const float amount=std::stof(parameters[1]);
				// Turning the bob off is 'bob off'; an amount only sets the height.
				if(!(amount>0.0f)||amount>max_walk_bob_lift)return CR_WRONG_USAGE;
				if(!walk_bob_lift_fits(amount,render_settings.bob.horizontal_mult,render_settings.bob.diagonal_mult,
						render_settings.bob.vertical_mult))
					{
					out.printerr("smooth-movement: bob {:.2f} times the current multipliers lifts more than {:.2f} tile; lower the multipliers first\n",
						amount,max_walk_bob_lift);
					return CR_FAILURE;
					}
				render_settings.bob.amplitude=amount;
				if(gps!=nullptr)++gps->force_full_display_count;
				out.print("smooth-movement: bob amount {:.2f}\n",render_settings.bob.amplitude);
				return CR_OK;
				}
			catch(...){return CR_WRONG_USAGE;}
			}
		if(parameters.size()==2&&(parameters[1]=="on"||parameters[1]=="off"))
			{
			render_settings.bob.enabled=parameters[1]=="on";
			if(gps!=nullptr)++gps->force_full_display_count;
			out.print("smooth-movement: walk bob {}\n",render_settings.bob.enabled?"enabled":"disabled");
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
		"hops per step: hops 1|2; profiling: stats [on|off|reset|detail on|off].",
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
		if(!INTERPOSE_HOOK(renderer_hook,update_all).apply())
			{
			out.printerr("smooth-movement: could not hook the 2D renderer\n");
			clear_sdl_bindings();
			return CR_FAILURE;
			}
		}
	else
		{
		INTERPOSE_HOOK(renderer_hook,update_all).remove();
		reset_state();
		clear_sdl_bindings();
		if(gps!=nullptr)++gps->force_full_display_count;
		}
	is_enabled=enable;
	out.print("smooth-movement: {}\n",enable?"enabled":"disabled");
	return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out)
{
	return plugin_enable(out,false);
}
