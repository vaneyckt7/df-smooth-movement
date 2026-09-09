// SPDX-License-Identifier: MIT
// Checks free_camerast against a stand-in animation manager: what a landed scroll, a window
// jump, a followed movement, a normalization write and a middle-mouse drag do to the render
// offset. The recordings only ever hold the camera at rest, so the moving cases are pinned
// here.

#include "free_camera.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures=0;
constexpr double tile=32.0;
constexpr const void *viewport=&failures;

// The manager as the camera sees it: this frame's scroll and the followed movement's offset.
struct managerst
{
	visual_scroll_renderst scroll;
	visual_follow_renderst follow;
	visual_scroll_renderst get_scroll(const void *vp) const
		{
		if(vp!=viewport)printf("scroll asked for another viewport\n"),++failures;
		return scroll;
		}
	visual_follow_renderst get_follow(const void *vp,visual_movement_idst id) const
		{
		if(vp!=viewport||id!=follow.movement_id)
			printf("follow asked for another movement\n"),++failures;
		return follow;
		}
};

// The game's window position, with the writes the camera asked for. The camera takes the
// scroll function by const reference, as the plugin passes a plain function.
struct windowst
{
	mutable int32_t x=10,y=10;
	std::array<bool,2> allow={true,true};
	mutable std::vector<std::array<int32_t,2>> writes;
	std::array<bool,2> operator()(int32_t kx,int32_t ky) const
		{
		writes.push_back({kx,ky});
		std::array<bool,2> applied={kx!=0&&allow[0],ky!=0&&allow[1]};
		if(applied[0])x+=kx;
		if(applied[1])y+=ky;
		return applied;
		}
};

struct scenest
{
	free_camerast camera;
	managerst manager;
	windowst window;
	camera_framest frame;

	scenest(bool enabled=true)
		{
		camera.set_enabled(enabled);
		frame.viewport=viewport;
		frame.tile=tile;
		frame.delta_ms=16;
		}

	// Runs one frame with the window's position and the manager's scroll, then clears the
	// scroll so the next frame sees a quiet manager unless the test says otherwise.
	void step()
		{
		frame.window_x=window.x;
		frame.window_y=window.y;
		camera.update(frame,manager,window);
		manager.scroll=visual_scroll_renderst{};
		}

	void land(int32_t x,int32_t y)
		{
		manager.scroll.landed=true;
		manager.scroll.landed_x=x;
		manager.scroll.landed_y=y;
		}
};

void expect_glide(const char *name,const free_camerast &camera,int32_t x,int32_t y)
{
	if(camera.glide_x(tile)!=x||camera.glide_y(tile)!=y)
		printf("%s: glide (%d,%d), expected (%d,%d)\n",name,camera.glide_x(tile),
			camera.glide_y(tile),x,y),++failures;
}

void expect_rest(const char *name,const free_camerast &camera,double x,double y)
{
	if(std::abs(camera.rest_offset_x()-x)>1e-9||std::abs(camera.rest_offset_y()-y)>1e-9)
		printf("%s: rest (%g,%g), expected (%g,%g)\n",name,camera.rest_offset_x(),
			camera.rest_offset_y(),x,y),++failures;
}

void test_glide()
{
	{
	// A landed scroll of one tile east starts a glide of a tile that decays with tau_ms.
	scenest scene;
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_glide("landed scroll",scene.camera,32,0);
	scene.frame.delta_ms=35;
	scene.step();
	expect_glide("one tau later",scene.camera,int32_t(std::lround(32*std::exp(-1.0))),0);
	for(int i=0;i<5;++i)scene.step();
	expect_glide("settled",scene.camera,0,0);
	// Below half a pixel the glide is exactly zero (it would be 0.08 pixels), so a drag
	// starting now folds nothing into rest.
	scene.frame.middle_button=true;
	scene.step();
	expect_rest("settled exactly",scene.camera,0.0,0.0);
	}
	{
	// A landing farther than the glide allows is capped at max_glide_tiles and a half.
	scenest scene;
	scene.land(0,-5);
	scene.step();
	expect_glide("capped",scene.camera,0,-int32_t(tile*(free_camerast::max_glide_tiles+0.5)));
	}
	{
	// Nothing glides with the camera off, unless the game follows a unit.
	scenest scene(false);
	scene.land(1,0);
	scene.step();
	expect_glide("camera off",scene.camera,0,0);
	scene.frame.native_follow_active=true;
	scene.land(1,0);
	scene.step();
	expect_glide("camera off, native follow",scene.camera,32,0);
	}
	{
	// Successive landings add up; the glide is the sum, not the last.
	scenest scene;
	scene.land(1,0);
	scene.step();
	scene.frame.delta_ms=0;
	scene.land(1,0);
	scene.step();
	expect_glide("two landings",scene.camera,64,0);
	}
}

void test_jump()
{
	{
	// A window jump farther than the glide allows snaps: its landing is ignored and any
	// glide in flight is dropped.
	scenest scene;
	scene.land(1,0);
	scene.step();
	scene.window.x+=4;
	scene.land(4,0);
	scene.step();
	expect_glide("jump",scene.camera,0,0);
	scene.land(1,0);
	scene.step();
	expect_glide("scroll after the jump",scene.camera,32,0);
	}
	{
	// While the jump's landing is still pending, the ignore holds.
	scenest scene;
	scene.step();
	scene.window.x+=4;
	scene.manager.scroll.pending=true;
	scene.land(1,0);
	scene.step();
	expect_glide("jump partly landed",scene.camera,0,0);
	scene.land(3,0);
	scene.step();
	expect_glide("jump fully landed",scene.camera,0,0);
	scene.land(1,0);
	scene.step();
	expect_glide("scroll after the pending jump",scene.camera,32,0);
	}
	{
	// After restart the next window position is a fresh baseline, not a jump.
	scenest scene;
	scene.land(1,0);
	scene.step();
	scene.camera.restart();
	expect_glide("restart",scene.camera,0,0);
	scene.window.x+=4;
	scene.land(1,0);
	scene.step();
	expect_glide("scroll after restart",scene.camera,32,0);
	}
	{
	// A jump of one tile is a scroll, and one of max_glide_tiles still glides.
	scenest scene;
	scene.step();
	scene.window.x+=free_camerast::max_glide_tiles;
	scene.land(free_camerast::max_glide_tiles,0);
	scene.step();
	expect_glide("jump at the limit",scene.camera,32*free_camerast::max_glide_tiles,0);
	}
}

void test_normalize()
{
	{
	// A rest past half a tile is folded into the window; the camera's own write lands as a
	// scroll that moves rest instead of gliding.
	scenest scene;
	scene.camera.set_rest(0.7,-0.6);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.writes!=std::vector<std::array<int32_t,2>>{{-1,1}}||
		scene.window.x!=9||scene.window.y!=11)
		printf("normalize: window not scrolled\n"),++failures;
	expect_rest("normalize before landing",scene.camera,0.7,-0.6);
	scene.step();
	scene.land(-1,1);
	scene.step();
	expect_rest("normalize landed",scene.camera,-0.3,0.4);
	expect_glide("normalize landed",scene.camera,-10,13);
	}
	{
	// A restart before the camera's own write lands (the game paused, a recording started,
	// the followed unit changed) forgets the pending landing, so the write is folded into
	// rest at once: the window moved by a tile and rest must move the other way to keep the
	// view where it was. Left out, the view sits a tile off for good.
	scenest scene;
	scene.camera.set_rest(-0.75,0.6);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.x!=11||scene.window.y!=9)
		printf("restart before landing: window not scrolled\n"),++failures;
	scene.camera.restart();
	expect_rest("restart before landing",scene.camera,0.25,-0.4);
	scene.step();
	scene.land(1,-1);
	scene.step();
	// The landing is a fresh baseline's first scroll, not the write: rest keeps its value.
	expect_rest("landing after restart",scene.camera,0.25,-0.4);
	}
	{
	// A rest within half a tile writes nothing; exactly half rounds away from zero.
	scenest scene;
	scene.camera.set_rest(0.4,-0.4);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.writes!=std::vector<std::array<int32_t,2>>{{0,0}}||
		scene.window.x!=10||scene.window.y!=10)
		printf("normalize within half a tile: window scrolled\n"),++failures;
	scene.camera.set_rest(0.5,-0.5);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.x!=9||scene.window.y!=11)
		printf("normalize at half a tile: window not scrolled\n"),++failures;
	}
	{
	// An axis the game would not scroll is not attributed: its landing, if any, glides.
	scenest scene;
	scene.window.allow={false,true};
	scene.camera.set_rest(0.7,0.7);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.land(-1,-1);
	scene.step();
	expect_rest("normalize refused",scene.camera,0.7,-0.3);
	expect_glide("normalize refused",scene.camera,22-32,-10);
	}
	{
	// A landing larger than the write is the write plus a real scroll.
	scenest scene;
	scene.camera.set_rest(0.7,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.land(-2,0);
	scene.step();
	expect_rest("normalize with a scroll",scene.camera,-0.3,0.0);
	expect_glide("normalize with a scroll",scene.camera,-10-32,0);
	}
	{
	// The write is attributed once: a second landing of the same size is a plain scroll.
	scenest scene;
	scene.camera.set_rest(0.7,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.land(-1,0);
	scene.step();
	scene.frame.delta_ms=0;
	scene.land(-1,0);
	scene.step();
	expect_rest("write attributed once",scene.camera,-0.3,0.0);
	expect_glide("write attributed once",scene.camera,-10-32,0);
	}
	{
	// An abandoned scroll ends the reporting of the write's landing, so the write is folded
	// into rest at once; a landing that arrives after all is a plain scroll and glides.
	scenest scene;
	scene.camera.set_rest(0.7,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.manager.scroll.abandoned=true;
	scene.step();
	expect_rest("abandoned",scene.camera,-0.3,0.0);
	scene.manager.scroll.abandoned=false;
	scene.land(-1,0);
	scene.step();
	expect_rest("landing after abandon",scene.camera,-0.3,0.0);
	expect_glide("landing after abandon",scene.camera,-10-32,0);
	}
	{
	// A second camera command before the first one's write lands asks for its offset against
	// the window as written, while rest is against the content the arrays still show: the
	// write is taken off rest and comes back when it lands. The view is right before the
	// landing and after it, with no glide.
	scenest scene;
	scene.camera.request_rest(-0.75,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.camera.request_rest(0.0,0.0);
	expect_rest("reset before landing",scene.camera,-1.0,0.0);
	expect_glide("reset before landing",scene.camera,-32,0);
	scene.manager.scroll.pending=true;
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_rest("reset landed",scene.camera,0.0,0.0);
	expect_glide("reset landed",scene.camera,0,0);
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_glide("scroll after the reset",scene.camera,32,0);
	}
	{
	// The same with a second offset that writes again: both writes land together and rest
	// ends within half a tile.
	scenest scene;
	scene.camera.request_rest(-0.75,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.camera.request_rest(-0.75,0.0);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.x!=12)printf("second write: window %d, expected 12\n",scene.window.x),++failures;
	expect_rest("second write pending",scene.camera,-1.75,0.0);
	scene.manager.scroll.pending=true;
	scene.step();
	scene.land(2,0);
	scene.step();
	expect_rest("second write landed",scene.camera,0.25,0.0);
	expect_glide("second write landed",scene.camera,8,0);
	}
	{
	// An opposite offset before the first write lands puts the window back where it was, so
	// nothing ever lands: rest is right on its own, and a user scroll afterwards glides.
	scenest scene;
	scene.camera.request_rest(-0.75,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.camera.request_rest(0.75,0.0);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.x!=10)printf("opposite write: window %d, expected 10\n",scene.window.x),++failures;
	scene.step();
	scene.step();
	expect_rest("opposite write",scene.camera,-0.25,0.0);
	expect_glide("opposite write",scene.camera,-8,0);
	scene.window.x=11;
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_rest("scroll after the opposite write",scene.camera,-0.25,0.0);
	expect_glide("scroll after the opposite write",scene.camera,32-8,0);
	}
	{
	// A view reset (z-level, zoom, resize) cancels the transients while a write is pending;
	// the manager drops the pending landing with the view, so the write is folded into rest.
	scenest scene;
	scene.camera.set_rest(0.7,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.camera.cancel_transients();
	expect_rest("cancel before landing",scene.camera,-0.3,0.0);
	scene.step();
	scene.step();
	expect_rest("cancel before landing settled",scene.camera,-0.3,0.0);
	}
}

void test_follow()
{
	// With the game following a unit, a landed scroll that the manager can attribute to a
	// movement is followed at that movement's offset instead of gliding, until it ends.
	scenest scene;
	scene.frame.native_follow_active=true;
	scene.manager.follow={true,7,0.25f,-0.5f};
	scene.land(1,0);
	scene.manager.scroll.follow_candidate=7;
	scene.step();
	expect_glide("follow",scene.camera,8,-16);
	scene.manager.follow.offset_x=0.125f;
	scene.step();
	expect_glide("follow progressed",scene.camera,4,-16);
	scene.manager.follow.active=false;
	scene.step();
	expect_glide("follow ended",scene.camera,0,0);
	// Without the game following, the candidate is ignored and the scroll glides.
	scenest plain;
	plain.manager.follow={true,7,0.25f,0.0f};
	plain.land(1,0);
	plain.manager.scroll.follow_candidate=7;
	plain.step();
	expect_glide("candidate without native follow",plain.camera,32,0);
	// A landing that includes the camera's own write is never followed.
	scenest written;
	written.frame.native_follow_active=true;
	written.manager.follow={true,7,0.25f,0.0f};
	written.camera.set_rest(0.7,0.0);
	written.camera.normalize_rest(written.window);
	written.step();
	written.land(-2,0);
	written.manager.scroll.follow_candidate=7;
	written.step();
	expect_glide("candidate with a write",written.camera,-10-32,0);
}

void test_drag()
{
	{
	// The view follows the mouse one to one while the middle button is held, and rests
	// where it is released, normalized into the window.
	scenest scene;
	scene.step();
	scene.frame.middle_button=true;
	scene.frame.mouse_x=100;scene.frame.mouse_y=100;
	scene.step();
	expect_glide("drag start",scene.camera,0,0);
	scene.frame.mouse_x=116;scene.frame.mouse_y=92;
	scene.step();
	expect_rest("drag",scene.camera,0.5,-0.25);
	expect_glide("drag",scene.camera,16,-8);
	scene.frame.mouse_x=124;
	scene.step();
	expect_rest("drag on",scene.camera,0.75,-0.25);
	scene.frame.middle_button=false;
	scene.step();
	if(scene.window.writes!=std::vector<std::array<int32_t,2>>{{-1,0}})
		printf("drag release: window not normalized\n"),++failures;
	scene.land(-1,0);
	scene.step();
	expect_rest("drag released",scene.camera,-0.25,-0.25);
	expect_glide("drag released",scene.camera,-8,-8);
	}
	{
	// A glide in flight is folded into rest when the drag starts, so the view does not
	// jump, and the game's own drag moving the window a tile moves rest with it, since the
	// view stays where the mouse holds it.
	scenest scene;
	scene.land(1,0);
	scene.step();
	scene.frame.middle_button=true;
	scene.step();
	const double decayed=std::exp(-16.0/free_camerast::tau_ms);
	expect_rest("drag keeps the glide",scene.camera,decayed,0.0);
	expect_glide("drag keeps the glide",scene.camera,int32_t(std::lround(decayed*tile)),0);
	scene.window.y+=1;
	scene.land(0,1);
	scene.step();
	expect_rest("drag folds a landing",scene.camera,decayed,1.0);
	}
	{
	// The game's own drag moving the window is tracked against the content window, so a
	// pending jump does not move the view.
	scenest scene;
	scene.step();
	scene.frame.middle_button=true;
	scene.step();
	scene.window.x+=1;
	scene.manager.scroll.pending=true;
	scene.manager.scroll.pending_x=1;
	scene.step();
	expect_rest("drag with a pending scroll",scene.camera,0.0,0.0);
	}
	{
	// A drag that disagrees with the game's window by more than a tile and a half is
	// rebased on the game's view.
	scenest scene;
	scene.step();
	scene.frame.middle_button=true;
	scene.step();
	scene.frame.mouse_x=64;
	scene.step();
	expect_rest("drag rebased",scene.camera,1.5,0.0);
	scene.frame.mouse_x=96;
	scene.step();
	expect_rest("drag after rebase",scene.camera,1.5,0.0);
	scene.frame.mouse_x=80;
	scene.step();
	expect_rest("drag back after rebase",scene.camera,1.0,0.0);
	}
	{
	// The middle button does nothing while the camera is off, even with a native follow.
	scenest scene(false);
	scene.frame.native_follow_active=true;
	scene.step();
	scene.frame.middle_button=true;
	scene.step();
	scene.frame.mouse_x=32;
	scene.step();
	expect_rest("drag with the camera off",scene.camera,0.0,0.0);
	}
	{
	// A window jump during a drag is not a snap: the pending scroll is tracked as content
	// lag, and when it lands after the release it glides rather than being ignored.
	scenest scene;
	scene.step();
	scene.frame.middle_button=true;
	scene.step();
	scene.window.x+=4;
	scene.manager.scroll.pending=true;
	scene.manager.scroll.pending_x=4;
	scene.step();
	expect_rest("jump during a drag",scene.camera,0.0,0.0);
	scene.frame.middle_button=false;
	scene.step();
	scene.land(4,0);
	scene.step();
	expect_glide("jump landed after the drag",scene.camera,
		int32_t(tile*(free_camerast::max_glide_tiles+0.5)),0);
	}
}

void test_enable()
{
	scenest scene;
	scene.camera.set_rest(0.25,0.0);
	scene.camera.set_enabled(true);
	expect_rest("enable again",scene.camera,0.25,0.0);
	scene.land(1,0);
	scene.step();
	scene.camera.set_enabled(false);
	if(scene.camera.is_enabled())printf("still enabled\n"),++failures;
	expect_rest("disabled",scene.camera,0.0,0.0);
	expect_glide("disabled",scene.camera,0,0);
	// cancel_transients keeps the rest offset and drops the glide.
	scene.camera.set_enabled(true);
	scene.camera.set_rest(0.25,0.0);
	scene.land(1,0);
	scene.step();
	scene.camera.cancel_transients();
	expect_rest("cancelled",scene.camera,0.25,0.0);
	expect_glide("cancelled",scene.camera,8,0);
}

// The cases the other tests leave to one axis or to one feature at a time: the y axis of a
// jump, a drag anchor and a rebase, and a jump, a drag, an abandoned scroll or a cancel
// while a follow, a glide or a drag is in progress.
void test_combined()
{
	{
	// A jump on y alone snaps, and a landing on y that is larger than the camera's own
	// write glides only the remainder, on y as on x.
	scenest scene;
	scene.step();
	scene.window.y+=4;
	scene.step();
	scene.land(0,4);
	scene.step();
	expect_glide("jump on y",scene.camera,0,0);
	scenest written;
	written.camera.set_rest(0.0,0.7);
	written.camera.normalize_rest(written.window);
	written.step();
	written.land(0,-2);
	written.step();
	expect_rest("write on y landed",written.camera,0.0,-0.3);
	expect_glide("write on y landed",written.camera,0,-10-32);
	}
	{
	// A landing the other way from the camera's own write is not the write landing: the
	// write stays pending and the whole landing glides.
	scenest scene;
	scene.camera.set_rest(0.7,0.0);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_rest("opposite landing",scene.camera,0.7,0.0);
	expect_glide("opposite landing",scene.camera,22+32,0);
	scene.land(-1,0);
	scene.step();
	expect_rest("write landed after",scene.camera,-0.3,0.0);
	scenest on_y;
	on_y.camera.set_rest(0.0,0.7);
	on_y.camera.normalize_rest(on_y.window);
	on_y.step();
	on_y.land(0,1);
	on_y.step();
	expect_rest("opposite landing on y",on_y.camera,0.0,0.7);
	expect_glide("opposite landing on y",on_y.camera,0,22+32);
	}
	{
	// A landing smaller than the camera's own write consumes that much of it; the rest of
	// the write is still pending when the next landing comes.
	scenest scene;
	scene.window.x=2;
	scene.camera.set_rest(1.6,0.0);
	scene.camera.normalize_rest(scene.window);
	if(scene.window.x!=0)printf("two-tile write: window %d\n",scene.window.x),++failures;
	scene.step();
	scene.land(-1,0);
	scene.step();
	expect_rest("half the write landed",scene.camera,0.6,0.0);
	expect_glide("half the write landed",scene.camera,19,0);
	scene.land(-1,0);
	scene.step();
	expect_rest("write fully landed",scene.camera,-0.4,0.0);
	expect_glide("write fully landed",scene.camera,-13,0);
	}
	{
	// A landing that includes the camera's own write on y is not followed either.
	scenest scene;
	scene.frame.native_follow_active=true;
	scene.manager.follow={true,7,0.0f,0.25f};
	scene.camera.set_rest(0.0,0.7);
	scene.camera.normalize_rest(scene.window);
	scene.step();
	scene.land(0,-2);
	scene.manager.scroll.follow_candidate=7;
	scene.step();
	expect_glide("candidate with a write on y",scene.camera,0,-10-32);
	}
	{
	// A follow that starts while a glide is in flight drops the glide.
	scenest scene;
	scene.frame.native_follow_active=true;
	scene.manager.follow={true,7,0.25f,0.0f};
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_glide("glide before follow",scene.camera,32,0);
	scene.land(1,0);
	scene.manager.scroll.follow_candidate=7;
	scene.step();
	expect_glide("follow drops the glide",scene.camera,8,0);
	// A jump ends the follow.
	scene.window.x+=5;
	scene.step();
	expect_glide("jump ends the follow",scene.camera,0,0);
	}
	{
	// A follow that starts while a glide on y is in flight drops that glide too, and a drag
	// that starts during a follow anchors on the view as shown: the follow's offset is
	// kept as rest.
	scenest scene;
	scene.frame.native_follow_active=true;
	scene.manager.follow={true,7,0.25f,0.0f};
	scene.step();
	scene.land(0,1);
	scene.step();
	expect_glide("glide on y",scene.camera,0,32);
	scene.land(1,0);
	scene.manager.scroll.follow_candidate=7;
	scene.step();
	expect_glide("follow drops the glide on y",scene.camera,8,0);
	scene.frame.middle_button=true;
	scene.step();
	expect_rest("drag during follow",scene.camera,0.25,0.0);
	// A drag that starts with a glide on y in flight keeps that glide as rest.
	scenest glided;
	glided.step();
	glided.land(0,1);
	glided.step();
	glided.frame.middle_button=true;
	glided.step();
	expect_rest("drag with a glide on y",glided.camera,0.0,std::exp(-16.0/35.0));
	}
	{
	// A drag rebases on y as on x when the game's window disagrees by more than a tile and
	// a half.
	scenest scene;
	scene.step();
	scene.frame.middle_button=true;
	scene.frame.mouse_y=100;
	scene.step();
	scene.frame.mouse_y=36;
	scene.window.y-=4;
	scene.step();
	expect_rest("rebase on y",scene.camera,0.0,-1.5);
	scene.frame.mouse_y=52;
	scene.step();
	expect_rest("drag after the rebase on y",scene.camera,0.0,-1.0);
	}
	{
	// Cancelling the transients ends a drag: the next frame with the button held starts a
	// fresh one from the mouse's new position.
	scenest scene;
	scene.step();
	scene.frame.middle_button=true;
	scene.step();
	scene.frame.mouse_x=16;
	scene.step();
	expect_rest("drag before cancel",scene.camera,0.5,0.0);
	scene.camera.cancel_transients();
	scene.frame.mouse_x=32;
	scene.step();
	expect_rest("drag restarted after cancel",scene.camera,0.5,0.0);
	scene.frame.mouse_x=48;
	scene.step();
	expect_rest("drag continued after cancel",scene.camera,1.0,0.0);
	}
	{
	// A jump's landing is ignored, but an abandoned scroll ends that too: the next landing
	// glides.
	scenest scene;
	scene.step();
	scene.window.x+=5;
	scene.step();
	scene.manager.scroll.abandoned=true;
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_glide("landing after a jump and an abandon",scene.camera,32,0);
	}
	{
	// Re-enabling forgets the window baseline: a window that moved while the camera was
	// off is not a scroll to glide.
	scenest scene;
	scene.step();
	scene.camera.set_enabled(false);
	scene.window.x+=1;
	scene.camera.set_enabled(true);
	scene.step();
	scene.land(1,0);
	scene.step();
	expect_glide("landing after re-enable",scene.camera,32,0);
	scenest jumped;
	jumped.step();
	jumped.camera.set_enabled(false);
	jumped.window.x+=5;
	jumped.camera.set_enabled(true);
	jumped.step();
	jumped.land(5,0);
	jumped.step();
	expect_glide("jump while disabled glides capped",jumped.camera,112,0);
	}
	{
	// The glide settles at half a pixel, not before: 0.586 pixels still shows as a pixel.
	scenest scene;
	scene.step();
	scene.land(1,0);
	scene.step();
	scene.frame.delta_ms=35*4;
	scene.step();
	expect_glide("four tau later",scene.camera,1,0);
	}
}

} // namespace

int main()
{
	test_glide();
	test_jump();
	test_normalize();
	test_follow();
	test_drag();
	test_enable();
	test_combined();
	if(failures!=0){printf("free camera tests: %d failures\n",failures);return 1;}
	printf("free camera tests: OK\n");
	return 0;
}
