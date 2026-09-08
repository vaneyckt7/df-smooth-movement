// SPDX-License-Identifier: MIT

#ifndef VIEW_CONTEXT_H
#define VIEW_CONTEXT_H

#include <cstdint>

// What a viewport's per-tile arrays are laid out against, apart from the map scroll: z-level,
// viewport dimensions and clip, zoom, pixel origin and the screen grid. A change in any of
// these means the arrays cannot be compared with the previous frame's, so the animation
// context is reset (the revision bumps). window_x/window_y are deliberately excluded: a
// horizontal/vertical scroll is followed, not reset. window_z (z-level) stays, since a z
// change is not followable.
struct view_signaturest
{
	int32_t window_z=0;
	int32_t dim_x=0;
	int32_t dim_y=0;
	int32_t clip_x0=0;
	int32_t clip_x1=0;
	int32_t clip_y0=0;
	int32_t clip_y1=0;
	int32_t zoom=0;
	int32_t origin_x=0;
	int32_t origin_y=0;
	int32_t screen_dim_x=0;
	int32_t screen_dim_y=0;

	constexpr bool operator==(const view_signaturest &other) const
		{
		return window_z==other.window_z&&dim_x==other.dim_x&&dim_y==other.dim_y&&
			clip_x0==other.clip_x0&&clip_x1==other.clip_x1&&
			clip_y0==other.clip_y0&&clip_y1==other.clip_y1&&
			zoom==other.zoom&&origin_x==other.origin_x&&origin_y==other.origin_y&&
			screen_dim_x==other.screen_dim_x&&screen_dim_y==other.screen_dim_y;
		}
	constexpr bool operator!=(const view_signaturest &other) const
		{
		return !(*this==other);
		}
};

struct view_context_changest
{
	bool reset=false;    // the signature or the main viewport object changed
	bool panned=false;   // the map scroll changed; also true on a reset and on the first frame
};

// Tracks the main viewport's signature and map scroll from frame to frame and hands out the
// context revision the animation manager keys its per-viewport state on.
class view_context_trackerst
{
	uint64_t revision_=0;
	const void *previous_viewport=nullptr;
	view_signaturest previous_signature;
	bool has_signature=false;
	// Map scroll (window_x/window_y) is tracked separately from the reset signature: a pure
	// pan is followed (movements are translated) instead of triggering a full reset, so it
	// must NOT bump the context revision. It only invalidates the viewport-space blackout
	// coverage from the prior frame.
	int32_t previous_pan_x=0;
	int32_t previous_pan_y=0;
	bool has_pan=false;

	public:
		uint64_t revision() const
			{
			return revision_;
			}

		// A reset the plugin asks for itself, such as a change of the game's follow target.
		void bump()
			{
			++revision_;
			}

		view_context_changest observe(
			const void *viewport,
			const view_signaturest &signature,
			int32_t pan_x,
			int32_t pan_y)
			{
			view_context_changest change;
			change.reset=!has_signature||previous_viewport!=viewport||
				previous_signature!=signature;
			if(change.reset)++revision_;
			previous_viewport=viewport;
			previous_signature=signature;
			has_signature=true;

			// On a pure pan the reset signature is unchanged, but last frame's blackout
			// coverage is in the old viewport frame, so it is discarded (the game repaints
			// the whole scrolled viewport anyway).
			change.panned=change.reset||!has_pan||previous_pan_x!=pan_x||
				previous_pan_y!=pan_y;
			previous_pan_x=pan_x;
			previous_pan_y=pan_y;
			has_pan=true;
			return change;
			}
};

#endif
