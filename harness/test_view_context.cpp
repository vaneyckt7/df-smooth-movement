// SPDX-License-Identifier: MIT
// Checks view_context_trackerst: which changes of the main viewport's signature reset the
// animation context (the revision bumps), that a map scroll only reports a pan, and that the
// first frame after a reset of the tracker is a reset. The recordings never change zoom, z or
// the screen grid, so those fields are pinned here.

#include "view_context.h"

#include <cstdio>

namespace {

int failures=0;
int marker_a=0,marker_b=0;
const void *const vp_a=&marker_a;
const void *const vp_b=&marker_b;

void expect(const char *name,view_context_changest change,bool reset,bool panned)
{
	if(change.reset!=reset||change.panned!=panned)
		printf("%s: reset %d panned %d, expected reset %d panned %d\n",name,
			int(change.reset),int(change.panned),int(reset),int(panned)),++failures;
}

void expect_revision(const char *name,const view_context_trackerst &tracker,uint64_t rev)
{
	if(tracker.revision()!=rev)
		printf("%s: revision %llu, expected %llu\n",name,
			(unsigned long long)tracker.revision(),(unsigned long long)rev),++failures;
}

view_signaturest base_signature()
{
	return {158,25,17,0,24,0,16,192,0,4,150,66};
}

} // namespace

int main()
{
	{
	// The first frame resets; a frame like it does nothing; a scroll only pans.
	view_context_trackerst tracker;
	expect_revision("fresh",tracker,0);
	expect("first frame",tracker.observe(vp_a,base_signature(),76,83),true,true);
	expect_revision("first frame",tracker,1);
	expect("same frame",tracker.observe(vp_a,base_signature(),76,83),false,false);
	expect_revision("same frame",tracker,1);
	expect("scroll east",tracker.observe(vp_a,base_signature(),77,83),false,true);
	expect("scroll north",tracker.observe(vp_a,base_signature(),77,81),false,true);
	expect("still",tracker.observe(vp_a,base_signature(),77,81),false,false);
	expect_revision("after scrolls",tracker,1);
	// Another main viewport object resets even with the same signature.
	expect("other viewport",tracker.observe(vp_b,base_signature(),77,81),true,true);
	expect_revision("other viewport",tracker,2);
	// A reset asked for by the plugin bumps the revision and nothing else.
	tracker.bump();
	expect_revision("bump",tracker,3);
	expect("after bump",tracker.observe(vp_b,base_signature(),77,81),false,false);
	}
	{
	// Every field of the signature resets on its own, and a reset frame also pans.
	const char *names[12]={"window_z","dim_x","dim_y","clip_x0","clip_x1","clip_y0",
		"clip_y1","zoom","origin_x","origin_y","screen_dim_x","screen_dim_y"};
	for(int i=0;i<12;++i)
		{
		view_context_trackerst tracker;
		tracker.observe(vp_a,base_signature(),76,83);
		view_signaturest changed=base_signature();
		int32_t *fields[12]={&changed.window_z,&changed.dim_x,&changed.dim_y,
			&changed.clip_x0,&changed.clip_x1,&changed.clip_y0,&changed.clip_y1,
			&changed.zoom,&changed.origin_x,&changed.origin_y,&changed.screen_dim_x,
			&changed.screen_dim_y};
		*fields[i]+=1;
		expect(names[i],tracker.observe(vp_a,changed,76,83),true,true);
		expect_revision(names[i],tracker,2);
		expect("after the change",tracker.observe(vp_a,changed,76,83),false,false);
		// Changing back is a change again.
		expect("changed back",tracker.observe(vp_a,base_signature(),76,83),true,true);
		expect_revision("changed back",tracker,3);
		}
	}
	{
	// A scroll on the same frame as a signature change is one reset, and a scroll noticed
	// while the signature changes is not reported again on the next frame.
	view_context_trackerst tracker;
	tracker.observe(vp_a,base_signature(),76,83);
	view_signaturest zoomed=base_signature();
	zoomed.zoom=128;
	expect("zoom and scroll",tracker.observe(vp_a,zoomed,77,83),true,true);
	expect_revision("zoom and scroll",tracker,2);
	expect("after zoom and scroll",tracker.observe(vp_a,zoomed,77,83),false,false);
	}
	{
	// A fresh tracker replaces one that has observed frames: the next frame resets again,
	// from revision 0.
	view_context_trackerst tracker;
	tracker.observe(vp_a,base_signature(),76,83);
	tracker.observe(vp_a,base_signature(),76,83);
	tracker=view_context_trackerst();
	expect_revision("replaced",tracker,0);
	expect("first frame after replacing",tracker.observe(vp_a,base_signature(),76,83),
		true,true);
	expect_revision("first frame after replacing",tracker,1);
	}
	if(failures!=0){printf("view context tests: %d failures\n",failures);return 1;}
	printf("view context tests: OK\n");
	return 0;
}
