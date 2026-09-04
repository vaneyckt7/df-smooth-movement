// SPDX-License-Identifier: MIT

// The free camera against a scripted engine: window scrolls are attributed when the background
// buffers show them landed, our own normalization writes are visual no-ops, glides decay, big
// jumps snap, and a middle-mouse drag moves the view by the pixel and rests where released.

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "free_camera.h"

#include <array>
#include <cassert>
#include <cstdlib>
#include <cmath>
#include <cstdint>
#include <vector>

namespace {

struct enginest
{
	int32_t window_x=20;
	int32_t window_y=20;
	// The shift the background buffers currently agree with, or none.
	bool has_landed=true;
	int32_t landed_dx=0;
	int32_t landed_dy=0;
	bool empty_background=false;
	std::vector<std::array<int32_t,2>> scroll_writes;

	// The buffers hold the landed shift, so every prefix of it also matches (a shift by less
	// than what landed still lines up most of the terrain); anything else does not.
	double match_ratio(int32_t dx,int32_t dy) const
		{
		if(empty_background)return -1.0;
		if(!has_landed)return 0.0;
		const auto prefix=[](int32_t d,int32_t landed)
			{
			return d==0||((d>0)==(landed>0)&&std::abs(d)<=std::abs(landed));
			};
		if(!prefix(dx,landed_dx)||!prefix(dy,landed_dy))return 0.1;
		// Farther from the landed shift, less of the terrain lines up.
		return 1.0-0.05*double(std::abs(landed_dx-dx)+std::abs(landed_dy-dy));
		}

	std::array<bool,2> scroll_window(int32_t dx,int32_t dy)
		{
		scroll_writes.push_back({dx,dy});
		std::array<bool,2> applied{false,false};
		if(dx!=0&&window_x+dx>=0){window_x+=dx;applied[0]=true;}
		if(dy!=0&&window_y+dy>=0){window_y+=dy;applied[1]=true;}
		return applied;
		}

	camera_framest frame(uint32_t delta_ms=16,bool mbut=false,int32_t mx=0,int32_t my=0) const
		{
		camera_framest f;
		f.window_x=window_x;
		f.window_y=window_y;
		f.middle_button=mbut;
		f.mouse_x=mx;
		f.mouse_y=my;
		f.tile=32.0;
		f.delta_ms=delta_ms;
		return f;
		}
};

void step(free_camerast &camera,enginest &engine,const camera_framest &frame)
{
	camera.update(
		frame,
		[&](int32_t dx,int32_t dy){return engine.match_ratio(dx,dy);},
		[&](int32_t dx,int32_t dy){return engine.scroll_window(dx,dy);});
}

void test_disabled_does_nothing()
{
	free_camerast camera;
	enginest engine;
	step(camera,engine,engine.frame());
	engine.window_x+=1;
	engine.landed_dx=1;
	step(camera,engine,engine.frame());
	assert(camera.glide_x(32.0)==0&&camera.glide_y(32.0)==0);
}

void test_scroll_glides_once_landed_then_decays()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	// The scroll is announced but the buffers still show the old view: nothing moves yet.
	engine.window_x+=1;
	engine.landed_dx=0;
	step(camera,engine,engine.frame());
	assert(camera.glide_x(32.0)==0);
	// The buffers shift: the view is now one tile ahead of what was on screen, so the render
	// offset jumps a whole tile in the scroll direction and then eases back onto the grid.
	engine.landed_dx=1;
	step(camera,engine,engine.frame(0));
	assert(camera.glide_x(32.0)==32);
	assert(camera.glide_y(32.0)==0);
	int32_t previous=32;
	int frames=0;
	while(camera.glide_x(32.0)!=0&&frames<100)
		{
		step(camera,engine,engine.frame(16));
		assert(camera.glide_x(32.0)<=previous);
		previous=camera.glide_x(32.0);
		++frames;
		}
	assert(frames>2&&frames<20);   // tau 35 ms: gone within a few hundred ms
	assert(engine.scroll_writes.empty());
}

void test_far_jump_snaps()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	engine.window_x+=5;
	engine.landed_dx=5;
	step(camera,engine,engine.frame());
	step(camera,engine,engine.frame());
	assert(camera.glide_x(32.0)==0);
}

void test_unrecognized_scroll_gives_up_quietly()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	engine.window_y+=2;
	engine.has_landed=false;   // heavy change: no shift matches, not even "unchanged"
	for(int i=0;i<8;++i)step(camera,engine,engine.frame());
	assert(camera.glide_y(32.0)==0);
	// Landing much later is not attributed any more: the debt was dropped.
	engine.has_landed=true;
	engine.landed_dy=2;
	step(camera,engine,engine.frame());
	assert(camera.glide_y(32.0)==0);
}

void test_empty_background_drops_pending()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	engine.window_x+=1;
	engine.empty_background=true;
	step(camera,engine,engine.frame());
	engine.empty_background=false;
	engine.landed_dx=1;
	step(camera,engine,engine.frame());
	assert(camera.glide_x(32.0)==0);
}

void test_normalize_folds_whole_tiles_into_the_window()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	// A rest of 0.7 tiles west is one tile of window scroll plus 0.3 tiles east.
	camera.set_rest(0.7,0.0);
	camera.normalize_rest([&](int32_t dx,int32_t dy){return engine.scroll_window(dx,dy);});
	assert(engine.scroll_writes.size()==1&&engine.scroll_writes[0][0]==-1);
	assert(engine.window_x==19);
	// Until the write lands the view has not moved: rest still 0.7.
	assert(std::fabs(camera.rest_offset_x()-0.7)<1e-9);
	step(camera,engine,engine.frame());   // the window change is observed, pending
	engine.landed_dx=-1;
	step(camera,engine,engine.frame());   // lands: our own write, no glide
	assert(std::fabs(camera.rest_offset_x()-(-0.3))<1e-9);
	assert(camera.glide_x(32.0)==int32_t(std::lround(-0.3*32)));
	step(camera,engine,engine.frame(16));
	assert(camera.glide_x(32.0)==int32_t(std::lround(-0.3*32)));   // rest does not decay
}

// Enabling clears the window baseline; a normalization asked before the next update waits
// for it, or the write would land unobserved and leave a whole tile of rest behind.
void test_normalize_before_a_baseline_waits_for_it()
{
	free_camerast camera;
	enginest engine;
	camera.set_enabled(true);
	camera.set_rest(-0.5,0.0);   // half a tile east of window 20
	camera.normalize_rest([&](int32_t dx,int32_t dy){return engine.scroll_window(dx,dy);});
	assert(engine.scroll_writes.empty()&&engine.window_x==20);
	step(camera,engine,engine.frame());   // baseline observed, then the write goes out
	assert(engine.scroll_writes.size()==1&&engine.window_x==21);
	step(camera,engine,engine.frame());   // the window change is observed, pending
	engine.landed_dx=1;
	step(camera,engine,engine.frame());   // lands as our own write: view unchanged
	assert(std::fabs(camera.rest_offset_x()-0.5)<1e-9);
	assert(camera.glide_x(32.0)==16);
	// A real scroll afterwards glides instead of being swallowed by stale self_scroll.
	engine.landed_dx=0;
	engine.window_x+=1;
	step(camera,engine,engine.frame());
	engine.landed_dx=1;
	step(camera,engine,engine.frame());
	assert(std::fabs(camera.rest_offset_x()-0.5)<1e-9);
	assert(camera.glide_x(32.0)!=16);   // the transient carries the glide
}

void test_normalize_respects_a_refused_write()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	engine.window_x=0;
	step(camera,engine,engine.frame());
	camera.set_rest(0.7,0.0);
	camera.normalize_rest([&](int32_t dx,int32_t dy){return engine.scroll_window(dx,dy);});
	assert(engine.window_x==0);
	step(camera,engine,engine.frame());
	step(camera,engine,engine.frame());
	assert(std::fabs(camera.rest_offset_x()-0.7)<1e-9);
}

void test_drag_follows_the_mouse_and_rests()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	step(camera,engine,engine.frame(16,true,100,100));   // press
	assert(camera.glide_x(32.0)==0);
	step(camera,engine,engine.frame(16,true,116,100));   // 16 px east: half a tile
	assert(std::fabs(camera.rest_offset_x()-0.5)<1e-9);
	assert(camera.glide_x(32.0)==16);
	step(camera,engine,engine.frame(16,true,116,92));    // 8 px north
	assert(std::fabs(camera.rest_offset_y()-(-0.25))<1e-9);
	assert(camera.glide_y(32.0)==-8);
	// Release: the half tile east is folded into a window write, the view does not move.
	step(camera,engine,engine.frame(16,false,116,92));
	assert(engine.scroll_writes.size()==1);
	assert(engine.scroll_writes[0][0]==-1&&engine.scroll_writes[0][1]==0);
	step(camera,engine,engine.frame());
	engine.landed_dx=-1;
	step(camera,engine,engine.frame());
	assert(std::fabs(camera.rest_offset_x()-(-0.5))<1e-9);
	assert(camera.glide_x(32.0)==-16);
	assert(camera.glide_y(32.0)==-8);
}

void test_disable_clears_the_offset()
{
	free_camerast camera;
	camera.set_enabled(true);
	camera.set_rest(0.4,-0.2);
	assert(camera.glide_x(32.0)!=0);
	camera.set_enabled(false);
	assert(camera.glide_x(32.0)==0&&camera.glide_y(32.0)==0);
	assert(!camera.is_enabled());
}

void test_fast_scroll_lands_piecemeal()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	// Three tiles announced at once; the buffers first show one of them, later the other two.
	engine.window_x+=3;
	engine.landed_dx=0;
	step(camera,engine,engine.frame(0));
	assert(camera.glide_x(32.0)==0);
	engine.landed_dx=1;
	step(camera,engine,engine.frame(0));
	assert(camera.glide_x(32.0)==32);   // only the landed tile glides
	// The remaining two land relative to the buffers as they are now.
	engine.landed_dx=2;
	step(camera,engine,engine.frame(0));
	assert(camera.glide_x(32.0)==96);
	// Nothing is left pending: a later shift that matches by accident is not attributed.
	engine.landed_dx=1;
	step(camera,engine,engine.frame(0));
	assert(camera.glide_x(32.0)==96);
}

void test_glide_is_capped()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	for(int i=0;i<2;++i)
		{
		engine.window_x+=3;
		engine.landed_dx=3;
		step(camera,engine,engine.frame(0));
		}
	assert(camera.glide_x(32.0)==int32_t(std::lround(32.0*(free_camerast::max_glide_tiles+0.5))));
}

void test_pending_beyond_six_tiles_snaps()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	engine.landed_dx=0;   // the buffers never catch up while the scrolling goes on
	for(int i=0;i<3;++i)
		{
		engine.window_x+=3;
		step(camera,engine,engine.frame(0));
		}
	assert(camera.glide_x(32.0)==0);
	engine.landed_dx=3;
	step(camera,engine,engine.frame(0));
	assert(camera.glide_x(32.0)==0);   // the debt was dropped, not paid late
}

void test_scroll_during_drag_folds_into_rest()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	step(camera,engine,engine.frame(16,true,100,100));
	// DF's own drag moves the window a tile while the mouse has not moved: the view must not
	// jump, so the tile goes into rest instead of gliding.
	engine.window_x+=1;
	engine.landed_dx=0;
	step(camera,engine,engine.frame(16,true,100,100));
	assert(camera.glide_x(32.0)==0);
	engine.landed_dx=1;
	step(camera,engine,engine.frame(16,true,100,100));
	assert(std::fabs(camera.rest_offset_x()-1.0)<1e-9);
	assert(camera.glide_x(32.0)==32);   // the whole tile is rest, none of it glides
	step(camera,engine,engine.frame(16,true,100,100));
	assert(std::fabs(camera.rest_offset_x()-1.0)<1e-9);   // no decay: it is rest, not transient
	assert(camera.glide_x(32.0)==32);
}

void test_equal_prefixes_take_the_better_match()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	engine.window_x+=1;
	engine.window_y+=1;
	// Both single-tile prefixes clear the bar, the horizontal one lines up better.
	const auto match=[](int32_t dx,int32_t dy)
		{
		if(dx==1&&dy==0)return 1.0;
		if(dx==0&&dy==1)return 0.7;
		if(dx==0&&dy==0)return 0.65;
		return 0.1;
		};
	camera.update(engine.frame(0),match,
		[&](int32_t dx,int32_t dy){return engine.scroll_window(dx,dy);});
	assert(camera.glide_x(32.0)==32);
	assert(camera.glide_y(32.0)==0);
}

void test_drag_rebases_when_far_from_the_window()
{
	free_camerast camera;
	camera.set_enabled(true);
	enginest engine;
	step(camera,engine,engine.frame());
	step(camera,engine,engine.frame(16,true,100,100));
	step(camera,engine,engine.frame(16,true,164,100));   // two tiles east in one frame
	assert(std::fabs(camera.rest_offset_x()-1.5)<1e-9);
	// From the rebased anchor, further motion is tracked again.
	step(camera,engine,engine.frame(16,true,148,100));
	assert(std::fabs(camera.rest_offset_x()-1.0)<1e-9);
}

} // namespace

int main()
{
	test_disabled_does_nothing();
	test_scroll_glides_once_landed_then_decays();
	test_far_jump_snaps();
	test_unrecognized_scroll_gives_up_quietly();
	test_empty_background_drops_pending();
	test_normalize_folds_whole_tiles_into_the_window();
	test_normalize_respects_a_refused_write();
	test_normalize_before_a_baseline_waits_for_it();
	test_drag_follows_the_mouse_and_rests();
	test_disable_clears_the_offset();
	test_fast_scroll_lands_piecemeal();
	test_glide_is_capped();
	test_pending_beyond_six_tiles_snaps();
	test_scroll_during_drag_folds_into_rest();
	test_drag_rebases_when_far_from_the_window();
	test_equal_prefixes_take_the_better_match();
	return 0;
}
