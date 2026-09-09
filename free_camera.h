// SPDX-License-Identifier: MIT

#ifndef FREE_CAMERA_H
#define FREE_CAMERA_H

#include "visual_animation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

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
//
// The camera never reads the game itself. Each frame the plugin hands it what it observes in a
// camera_framest, the animation manager it asks about scrolls and followed movements, and a
// scroll function that writes the game's window position for it.

struct camera_framest
{
	const void *viewport=nullptr;   // the main viewport, the manager's key for its scroll
	int32_t window_x=0;             // the game's tile camera
	int32_t window_y=0;
	bool middle_button=false;       // middle mouse held
	int32_t mouse_x=0;              // precise mouse position, pixels
	int32_t mouse_y=0;
	double tile=32.0;               // tile size on screen, pixels
	uint32_t delta_ms=0;            // time since the previous frame
	bool native_follow_active=false;   // the game is following a unit
};

class free_camerast
{
	bool enabled=false;                  // OFF by default: plain `enable smooth-movement`
	                                     // keeps upstream behavior (creature interpolation
	                                     // only); `smooth-movement camera on` opts in.
	double transient_x=0.0;              // decaying glide offset, pixels
	double transient_y=0.0;
	double rest_x=0.0;                   // persistent free-camera offset, tiles
	double rest_y=0.0;                   // (positive = view sits WEST/NORTH of window)
	int32_t self_scroll_x=0;             // window deltas WE wrote: visual no-ops when landing
	int32_t self_scroll_y=0;
	visual_movement_idst follow_id=no_visual_movement;
	const void *follow_viewport=nullptr;
	double follow_x=0.0;
	double follow_y=0.0;
	bool ignore_pending=false;
	bool drag_active=false;
	double drag_anchor_vx=0.0;           // visual camera at drag start, tiles
	double drag_anchor_vy=0.0;
	int32_t drag_anchor_mx=0;            // precise mouse at drag start, pixels
	int32_t drag_anchor_my=0;
	int32_t prev_wx=0;                   // window-scroll observation baseline
	int32_t prev_wy=0;
	bool has_prev=false;

	// Drop the scroll and follow tracking. A window write of the camera's own that has
	// not landed yet is folded into rest first: whoever clears the tracking (a restart,
	// a view reset, an abandoned scroll) also ends the manager's reporting of that
	// landing, and without the fold the view would sit a tile off for good.
	void clear_tracking()
		{
		rest_x+=self_scroll_x;
		rest_y+=self_scroll_y;
		self_scroll_x=0;
		self_scroll_y=0;
		follow_id=no_visual_movement;
		follow_viewport=nullptr;
		follow_x=0.0;
		follow_y=0.0;
		ignore_pending=false;
		}

	public:
		static constexpr int32_t max_glide_tiles=3;   // per-jump: farther than this snaps
		static constexpr double tau_ms=35.0;          // transient catch-up (~95% after 100ms)

		bool is_enabled() const
			{
			return enabled;
			}

		void set_enabled(bool enable)
			{
			if(enabled==enable)return;
			enabled=enable;
			cancel_transients();
			rest_x=0.0;
			rest_y=0.0;
			has_prev=false;   // fresh observation baseline; no phantom scroll on re-enable
			}

		// The persistent offset in tiles, positive = view sits west/north of the window.
		double rest_offset_x() const
			{
			return rest_x;
			}

		double rest_offset_y() const
			{
			return rest_y;
			}

		void set_rest(double x,double y)
			{
			rest_x=x;
			rest_y=y;
			}

		// Cancel everything except the persistent rest offset (the camera keeps its sub-tile
		// position across zoom/z/resize; only the in-flight animation state is unfollowable).
		void cancel_transients()
			{
			transient_x=0.0;
			transient_y=0.0;
			clear_tracking();
			drag_active=false;
			}

		// Cancel the transients and forget the window baseline, so the next frame's window
		// position is a fresh start rather than a scroll.
		void restart()
			{
			cancel_transients();
			has_prev=false;
			}

		// Fold whole tiles of rest into window_x/window_y so |rest| <= 0.5 (minimal edge
		// strip). The visual position is unchanged: the window write is attributed via
		// self_scroll when it lands. scroll_window(kx,ky) writes the window position and
		// returns which axes it applied.
		template<typename ScrollWindow>
		void normalize_rest(const ScrollWindow &scroll_window)
			{
			const int32_t kx=int32_t(-std::llround(rest_x));
			const int32_t ky=int32_t(-std::llround(rest_y));
			const std::array<bool,2> applied=scroll_window(kx,ky);
			if(applied[0])self_scroll_x+=kx;
			if(applied[1])self_scroll_y+=ky;
			}

		// Per-frame camera bookkeeping: observe window scrolls, attribute them when the
		// buffers apply them (glide vs our own normalization writes), drive the drag, decay
		// the transient.
		template<typename Manager,typename ScrollWindow>
		void update(
			const camera_framest &frame,
			const Manager &animation_manager,
			const ScrollWindow &scroll_window)
			{
			if(!camera_glide_enabled(enabled,frame.native_follow_active))return;
			const double tile=frame.tile;
			const double k=std::exp(-double(frame.delta_ms)/tau_ms);
			transient_x*=k;
			transient_y*=k;
			const int32_t wx=frame.window_x;
			const int32_t wy=frame.window_y;
			if(has_prev&&(wx!=prev_wx||wy!=prev_wy))
				{
				const int32_t dx=wx-prev_wx;
				const int32_t dy=wy-prev_wy;
				if((std::abs(dx)>max_glide_tiles||std::abs(dy)>max_glide_tiles)&&
					!drag_active)
					{
					transient_x=0.0;
					transient_y=0.0;
					follow_id=no_visual_movement;
					follow_x=0.0;
					follow_y=0.0;
					ignore_pending=true;
					}
				}
			prev_wx=wx;
			prev_wy=wy;
			has_prev=true;

			const visual_scroll_renderst scroll=animation_manager.get_scroll(frame.viewport);
			if(scroll.abandoned)
				{
				clear_tracking();
				}
			if(scroll.landed)
				{
				int32_t sx=0;
				if(self_scroll_x!=0&&scroll.landed_x!=0&&
					(self_scroll_x>0)==(scroll.landed_x>0))
					sx=std::abs(self_scroll_x)<=std::abs(scroll.landed_x)?
						self_scroll_x:scroll.landed_x;
				int32_t sy=0;
				if(self_scroll_y!=0&&scroll.landed_y!=0&&
					(self_scroll_y>0)==(scroll.landed_y>0))
					sy=std::abs(self_scroll_y)<=std::abs(scroll.landed_y)?
						self_scroll_y:scroll.landed_y;
				self_scroll_x-=sx;
				self_scroll_y-=sy;
				rest_x+=sx;
				rest_y+=sy;
				const int32_t gx=scroll.landed_x-sx;
				const int32_t gy=scroll.landed_y-sy;
				const bool ignore=ignore_pending;
				if(!scroll.pending)ignore_pending=false;
				if(drag_active)
					{
					rest_x+=gx;
					rest_y+=gy;
					}
				else if((gx!=0||gy!=0)&&!ignore&&frame.native_follow_active&&
					sx==0&&sy==0&&scroll.follow_candidate!=no_visual_movement)
					{
					follow_id=scroll.follow_candidate;
					follow_viewport=frame.viewport;
					transient_x=0.0;
					transient_y=0.0;
					}
				else if((gx!=0||gy!=0)&&!ignore)
					{
					follow_id=no_visual_movement;
					follow_viewport=nullptr;
					follow_x=0.0;
					follow_y=0.0;
					const double cap=tile*(max_glide_tiles+0.5);
					transient_x=std::clamp(transient_x+gx*tile,-cap,cap);
					transient_y=std::clamp(transient_y+gy*tile,-cap,cap);
					}
				}
			if(follow_id!=no_visual_movement)
				{
				const auto follow=animation_manager.get_follow(follow_viewport,follow_id);
				if(follow.active)
					{
					follow_x=follow.offset_x*tile;
					follow_y=follow.offset_y*tile;
					}
				else
					{
					follow_id=no_visual_movement;
					follow_viewport=nullptr;
					follow_x=0.0;
					follow_y=0.0;
					}
				}

			// --- pixel-perfect middle-mouse drag: the view follows the mouse 1:1 and rests
			// where released. DF's own drag still moves window in tile steps; rest carries the
			// remainder. Positions are tracked against the CONTENT window (window minus
			// unlanded jumps) so the buffer lag never causes a visible stutter.
			const bool mbut=enabled&&frame.middle_button;
			const double content_wx=double(wx-scroll.pending_x);
			const double content_wy=double(wy-scroll.pending_y);
			if(mbut&&!drag_active)
				{
				drag_active=true;
				drag_anchor_vx=content_wx-rest_x-(transient_x+follow_x)/tile;
				drag_anchor_vy=content_wy-rest_y-(transient_y+follow_y)/tile;
				drag_anchor_mx=frame.mouse_x;
				drag_anchor_my=frame.mouse_y;
				transient_x=0.0;
				transient_y=0.0;
				follow_id=no_visual_movement;
				follow_viewport=nullptr;
				follow_x=0.0;
				follow_y=0.0;
				}
			if(drag_active)
				{
				if(!mbut)
					{
					drag_active=false;
					normalize_rest(scroll_window);
					}
				else
					{
					double vx=drag_anchor_vx-double(frame.mouse_x-drag_anchor_mx)/tile;
					double vy=drag_anchor_vy-double(frame.mouse_y-drag_anchor_my)/tile;
					rest_x=content_wx-vx;
					rest_y=content_wy-vy;
					// If DF's own drag disagrees by more than a tile and a half, rebase on
					// its view.
					const double lim=1.5;
					if(rest_x<-lim||rest_x>lim||rest_y<-lim||rest_y>lim)
						{
						rest_x=std::clamp(rest_x,-lim,lim);
						rest_y=std::clamp(rest_y,-lim,lim);
						drag_anchor_vx=content_wx-rest_x+
							double(frame.mouse_x-drag_anchor_mx)/tile;
						drag_anchor_vy=content_wy-rest_y+
							double(frame.mouse_y-drag_anchor_my)/tile;
						}
					}
				}

			if(std::abs(transient_x)<0.5&&std::abs(transient_y)<0.5)
				{
				transient_x=0.0;
				transient_y=0.0;
				}
			}

		// This frame's render offset in whole pixels: the glide, the followed movement and
		// the rest offset, at the given tile size.
		int32_t glide_x(double tile) const
			{
			return int32_t(std::lround(transient_x+follow_x+rest_x*tile));
			}

		int32_t glide_y(double tile) const
			{
			return int32_t(std::lround(transient_y+follow_y+rest_y*tile));
			}
};

#endif
