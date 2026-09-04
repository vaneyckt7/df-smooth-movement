// SPDX-License-Identifier: MIT

// The view-context tracker: what resets the animation context and what is only a pan.

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
	const view_context_changest first=tracker.observe(&viewport,base_signature(),5,7);
	assert(first.reset&&first.panned);
	assert(tracker.revision()==1);
	const view_context_changest same=tracker.observe(&viewport,base_signature(),5,7);
	assert(!same.reset&&!same.panned);
	assert(tracker.revision()==1);
}

void test_pan_is_reported_without_reset()
{
	view_context_trackerst tracker;
	int viewport=0;
	tracker.observe(&viewport,base_signature(),5,7);
	const view_context_changest panned_x=tracker.observe(&viewport,base_signature(),6,7);
	assert(!panned_x.reset&&panned_x.panned);
	const view_context_changest panned_y=tracker.observe(&viewport,base_signature(),6,9);
	assert(!panned_y.reset&&panned_y.panned);
	const view_context_changest still=tracker.observe(&viewport,base_signature(),6,9);
	assert(!still.reset&&!still.panned);
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
		tracker.observe(&viewport,base,0,0);
		view_signaturest changed=base;
		changed.*field+=1;
		const view_context_changest change=tracker.observe(&viewport,changed,0,0);
		assert(change.reset&&change.panned);
		assert(tracker.revision()==2);
		// Changing back is a change too: the buffers in between were laid out differently.
		assert(tracker.observe(&viewport,base,0,0).reset);
		assert(tracker.revision()==3);
		}
}

void test_new_viewport_object_resets()
{
	view_context_trackerst tracker;
	int a=0,b=0;
	tracker.observe(&a,base_signature(),0,0);
	assert(tracker.observe(&b,base_signature(),0,0).reset);
	assert(tracker.revision()==2);
}

} // namespace

int main()
{
	test_first_frame_resets_once();
	test_pan_is_reported_without_reset();
	test_every_signature_field_resets();
	test_new_viewport_object_resets();
	return 0;
}
