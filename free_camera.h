// SPDX-License-Identifier: MIT

#ifndef FREE_CAMERA_H
#define FREE_CAMERA_H

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>

// The camera visually unbound from the tile grid. Two layered offsets:
//   rest      -- a PERSISTENT sub-tile offset in tiles: the free camera. Set by pixel-perfect
//                middle-mouse drag panning (the view rests wherever released, mid-tile or not)
//                and by the `smooth-movement camera <fx> <fy>` console command. Survives zoom
//                and z-level changes. Kept in [-0.5,0.5] by normalization: whole-tile parts are
//                folded into window_x/window_y (a plain UI scroll write -- NEVER the viewport
//                dims, which crash DF; the sub-tile strip this leaves at one screen edge has no
//                buffer data and stays black).
//   transient -- the decaying scroll glide, in pixels, layered on top.
// Render offset = transient + rest*tile. window_x/window_y remain the game's own tile camera.
//
// Off by default: plain `enable smooth-movement` keeps upstream behaviour (creature
// interpolation only); `smooth-movement camera on` opts in.
//
// The class never touches the engine itself. Each frame it is handed what it observes in
// `camera_framest`, a match function telling how well the background buffers agree with a
// given shift, and a scroll function that writes the game's window position for it.

struct camera_framest
{
	int32_t window_x=0;         // the game's tile camera
	int32_t window_y=0;
	bool middle_button=false;   // middle mouse held
	int32_t mouse_x=0;          // precise mouse position, pixels
	int32_t mouse_y=0;
	double tile=32.0;           // tile size on screen, pixels
	uint32_t delta_ms=0;        // time since the previous frame
};

class free_camerast
{
	bool enabled=false;
	double transient_x=0.0;      // decaying glide offset, pixels
	double transient_y=0.0;
	double rest_x=0.0;           // persistent free-camera offset, tiles
	double rest_y=0.0;           // (positive = view sits WEST/NORTH of window)
	int32_t pending_dx=0;        // scroll delta announced but not yet in the buffers
	int32_t pending_dy=0;
	int32_t pending_frames=0;
	int32_t self_scroll_x=0;     // window deltas WE wrote: visual no-ops when landing
	int32_t self_scroll_y=0;
	bool drag_active=false;
	double drag_anchor_vx=0.0;   // visual camera at drag start, tiles
	double drag_anchor_vy=0.0;
	int32_t drag_anchor_mx=0;    // precise mouse at drag start, pixels
	int32_t drag_anchor_my=0;
	bool was_offset=false;       // edge-detects offset->0 for one cleanup redraw
	int32_t prev_wx=0;           // window-scroll observation baseline
	int32_t prev_wy=0;
	bool has_prev=false;

	void clear_pending()
		{
		pending_dx=0;
		pending_dy=0;
		pending_frames=0;
		self_scroll_x=0;
		self_scroll_y=0;
		}

	// A scroll of (ax,ay) tiles has landed in the buffers: our own normalization writes are
	// visual no-ops (they move into rest); the remainder is a real scroll and glides -- unless a
	// drag is driving the position directly, in which case it folds into rest wholesale.
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
			const double cap=tile*(max_glide_tiles+0.5);
			transient_x=std::clamp(transient_x,-cap,cap);
			transient_y=std::clamp(transient_y,-cap,cap);
			}
		}

	// Fast scrolling applies the pending delta PIECEMEAL: the buffers may hold +1 of a pending
	// +3 this frame. Testing only the total made landings miss, time out, and snap -- the
	// fast-scroll jitter. Instead, find the LARGEST applied prefix of the pending scroll and
	// attribute just that; the rest keeps pending. Ties between qualifying shifts only happen on
	// uniform terrain, where mistiming is invisible.
	template<typename MatchRatio>
	void settle_pending(const MatchRatio &match_ratio,double tile)
		{
		if(pending_dx==0&&pending_dy==0)return;
		if(std::abs(pending_dx)>6||std::abs(pending_dy)>6)
			{
			// Scrolling far outran detection: snap (keep rest, drop the animation debt).
			transient_x=0.0;
			transient_y=0.0;
			clear_pending();
			return;
			}
		const int32_t stepx=(pending_dx>0)-(pending_dx<0);
		const int32_t stepy=(pending_dy>0)-(pending_dy<0);
		int32_t best_ax=0,best_ay=0,best_mag=-1;
		double best_score=-1.0;
		bool no_data=false;
		for(int32_t ix=0;ix<=std::abs(pending_dx)&&!no_data;++ix)
			{
			for(int32_t iy=0;iy<=std::abs(pending_dy);++iy)
				{
				const double score=match_ratio(ix*stepx,iy*stepy);
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
			}
		if(no_data)
			{
			// Nothing to compare against (empty background): give up on attribution.
			clear_pending();
			}
		else if(best_mag>0)
			{
			attribute_landed(best_ax,best_ay,tile);
			pending_dx-=best_ax;
			pending_dy-=best_ay;
			pending_frames=0;
			}
		else if(best_mag==0)
			{
			// Content demonstrably hasn't moved yet: keep waiting, no timeout pressure.
			pending_frames=0;
			}
		else if(++pending_frames>4)
			{
			// Neither static nor any prefix recognizable (heavy simultaneous change): drop the
			// debt without touching the in-flight glide.
			clear_pending();
			}
		}

	// Pixel-perfect middle-mouse drag: the view follows the mouse 1:1 and rests where released.
	// DF's own drag still moves window in tile steps; rest carries the remainder. Positions are
	// tracked against the CONTENT window (window minus unlanded jumps) so the buffer lag never
	// causes a visible stutter.
	template<typename ScrollWindow>
	void drive_drag(const camera_framest &frame,const ScrollWindow &scroll_window)
		{
		const double tile=frame.tile;
		const double content_wx=double(frame.window_x-pending_dx);
		const double content_wy=double(frame.window_y-pending_dy);
		if(frame.middle_button&&!drag_active)
			{
			drag_active=true;
			drag_anchor_vx=content_wx-rest_x-transient_x/tile;
			drag_anchor_vy=content_wy-rest_y-transient_y/tile;
			drag_anchor_mx=frame.mouse_x;
			drag_anchor_my=frame.mouse_y;
			transient_x=0.0;
			transient_y=0.0;
			}
		if(!drag_active)return;
		if(!frame.middle_button)
			{
			drag_active=false;
			normalize_rest(scroll_window);
			return;
			}
		const double vx=drag_anchor_vx-double(frame.mouse_x-drag_anchor_mx)/tile;
		const double vy=drag_anchor_vy-double(frame.mouse_y-drag_anchor_my)/tile;
		rest_x=content_wx-vx;
		rest_y=content_wy-vy;
		// If DF's own drag disagrees by more than a tile and a half, rebase on its view.
		const double lim=1.5;
		if(rest_x<-lim||rest_x>lim||rest_y<-lim||rest_y>lim)
			{
			rest_x=std::clamp(rest_x,-lim,lim);
			rest_y=std::clamp(rest_y,-lim,lim);
			drag_anchor_vx=content_wx-rest_x+double(frame.mouse_x-drag_anchor_mx)/tile;
			drag_anchor_vy=content_wy-rest_y+double(frame.mouse_y-drag_anchor_my)/tile;
			}
		}

	void decay_transient(uint32_t delta_ms)
		{
		if(transient_x==0.0&&transient_y==0.0)return;
		const double k=std::exp(-double(delta_ms)/tau_ms);
		transient_x*=k;
		transient_y*=k;
		if(std::abs(transient_x)<0.5&&std::abs(transient_y)<0.5)
			{
			transient_x=0.0;
			transient_y=0.0;
			}
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
			// was_offset stays: the render path issues one cleanup redraw if we were mid-offset.
			}

		// Persistent offset in tiles, positive = view sits west/north of the window position.
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
			clear_pending();
			drag_active=false;
			}

		// Fold whole tiles of rest into the window position so |rest| <= 0.5 (minimal edge
		// strip). The visual position is unchanged: the window write is attributed via
		// self_scroll when it lands. scroll_window(dx,dy) returns which axes it applied.
		template<typename ScrollWindow>
		void normalize_rest(const ScrollWindow &scroll_window)
			{
			const int32_t kx=int32_t(-std::llround(rest_x));
			const int32_t ky=int32_t(-std::llround(rest_y));
			if(kx==0&&ky==0)return;
			const std::array<bool,2> applied=scroll_window(kx,ky);
			if(applied[0])self_scroll_x+=kx;
			if(applied[1])self_scroll_y+=ky;
			}

		// Per-frame bookkeeping: observe window scrolls, attribute them when the buffers apply
		// them (glide vs our own normalization writes), drive the drag, decay the transient.
		// match_ratio(dx,dy) is the fraction of the background agreeing with a shift, or a
		// negative value when there is nothing to compare.
		template<typename MatchRatio,typename ScrollWindow>
		void update(
			const camera_framest &frame,
			const MatchRatio &match_ratio,
			const ScrollWindow &scroll_window)
			{
			if(!enabled)return;
			if(has_prev&&(frame.window_x!=prev_wx||frame.window_y!=prev_wy))
				{
				const int32_t dx=frame.window_x-prev_wx;
				const int32_t dy=frame.window_y-prev_wy;
				if((std::abs(dx)>max_glide_tiles||std::abs(dy)>max_glide_tiles)&&!drag_active)
					cancel_transients();   // teleport-like jump (recenter/minimap): snap
				else
					{
					pending_dx+=dx;
					pending_dy+=dy;
					pending_frames=0;
					}
				}
			prev_wx=frame.window_x;
			prev_wy=frame.window_y;
			has_prev=true;
			settle_pending(match_ratio,frame.tile);
			drive_drag(frame,scroll_window);
			decay_transient(frame.delta_ms);
			}

		// This frame's render offset in whole pixels.
		int32_t glide_x(double tile) const
			{
			return int32_t(std::lround(transient_x+rest_x*tile));
			}

		int32_t glide_y(double tile) const
			{
			return int32_t(std::lround(transient_y+rest_y*tile));
			}

		// True once, on the frame the offset returns to zero: the last shifted frame is still
		// on screen and one engine redraw must replace it.
		bool rejoined_grid(bool offset_now)
			{
			const bool rejoined=!offset_now&&was_offset;
			was_offset=offset_now;
			return rejoined;
			}
};

#endif
