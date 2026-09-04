// SPDX-License-Identifier: MIT

#ifndef VIEW_CONTEXT_H
#define VIEW_CONTEXT_H

#include <array>
#include <cstdint>

// What the buffers of a viewport are laid out against, apart from the map scroll: z-level,
// viewport dimensions and clip, zoom, pixel origin and the screen grid. A change in any of
// these means the buffers cannot be compared with the previous frame's, so the animation
// context is reset (the revision bumps). Map scroll (window_x/window_y) is deliberately not
// part of the signature: a pan is followed by translating the movements, so it must NOT bump
// the revision; it only invalidates the blackout coverage kept from the previous frame, which
// is in the old viewport frame.
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
	bool reset=false;      // the signature (or the main viewport object) changed
	bool panned=false;     // the map scroll changed; also true on a reset and on the first frame
};

class view_context_trackerst
{
	uint64_t revision_=0;
	const void *viewport=nullptr;
	view_signaturest signature;
	bool has_signature=false;
	int32_t pan_x=0;
	int32_t pan_y=0;
	bool has_pan=false;

	public:
		// Revision 0 is never handed out for an observed frame: the first observation resets.
		uint64_t revision() const
			{
			return revision_;
			}

		view_context_changest observe(
			const void *main_viewport,
			const view_signaturest &current,
			int32_t current_pan_x,
			int32_t current_pan_y)
			{
			view_context_changest change;
			change.reset=!has_signature||viewport!=main_viewport||signature!=current;
			if(change.reset)++revision_;
			viewport=main_viewport;
			signature=current;
			has_signature=true;
			change.panned=change.reset||!has_pan||pan_x!=current_pan_x||pan_y!=current_pan_y;
			pan_x=current_pan_x;
			pan_y=current_pan_y;
			has_pan=true;
			return change;
			}
};

#endif
