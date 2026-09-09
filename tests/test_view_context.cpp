// SPDX-License-Identifier: MIT

// The view-context tracker: what resets the animation context.

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "view_context.h"

#include <cassert>

namespace {

view_signaturest base_signature()
{
	view_signaturest s;
	s.window_z=10;
	s.dim_x=75;
	s.dim_y=50;
	s.clip_x0=0;
	s.clip_x1=74;
	s.clip_y0=0;
	s.clip_y1=49;
	s.zoom=128;
	s.origin_x=0;
	s.origin_y=0;
	s.screen_dim_x=240;
	s.screen_dim_y=100;
	return s;
}

void test_first_frame_resets_once()
{
	view_context_trackerst tracker;
	int viewport=0;
	assert(tracker.revision()==0);
	assert(tracker.observe(&viewport,base_signature()));
	assert(tracker.revision()==1);
	assert(!tracker.observe(&viewport,base_signature()));
	assert(tracker.revision()==1);
}

void test_every_signature_field_resets()
{
	int viewport=0;
	const view_signaturest base=base_signature();
	int32_t view_signaturest::*fields[]=
		{
		&view_signaturest::window_z,&view_signaturest::dim_x,&view_signaturest::dim_y,
		&view_signaturest::clip_x0,&view_signaturest::clip_x1,
		&view_signaturest::clip_y0,&view_signaturest::clip_y1,
		&view_signaturest::zoom,&view_signaturest::origin_x,&view_signaturest::origin_y,
		&view_signaturest::screen_dim_x,&view_signaturest::screen_dim_y
		};
	for(auto field:fields)
		{
		view_context_trackerst tracker;
		tracker.observe(&viewport,base);
		view_signaturest changed=base;
		changed.*field+=1;
		assert(tracker.observe(&viewport,changed));
		assert(tracker.revision()==2);
		// Changing back is a change too: the buffers in between were laid out differently.
		assert(tracker.observe(&viewport,base));
		assert(tracker.revision()==3);
		}
}

void test_new_viewport_object_resets()
{
	view_context_trackerst tracker;
	int a=0,b=0;
	tracker.observe(&a,base_signature());
	assert(tracker.observe(&b,base_signature()));
	assert(tracker.revision()==2);
}

} // namespace

int main()
{
	test_first_frame_resets_once();
	test_every_signature_field_resets();
	test_new_viewport_object_resets();
	return 0;
}
