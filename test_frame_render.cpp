// SPDX-License-Identifier: MIT

// Drives frame_rendererst against a viewport double and a recording canvas: what the pass
// asks the engine to repaint, what it blanks while doing so, and where it draws sprites.

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "frame_render.h"
#include "visual_animation.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <vector>

namespace {

// A stand-in for df::graphic_viewportst with the members the pass touches, same types.
struct test_viewportst
{
	int32_t dim_x=0;
	int32_t dim_y=0;
	std::array<int32_t,2> clipx{};
	std::array<int32_t,2> clipy{};
	int32_t *screentexpos_background=nullptr;
	uint64_t *screentexpos_floor_flag=nullptr;
	int32_t *screentexpos_background_two=nullptr;
	uint32_t *screentexpos_liquid_flag=nullptr;
	uint32_t *screentexpos_spatter_flag=nullptr;
	int32_t *screentexpos_spatter=nullptr;
	uint64_t *screentexpos_ramp_flag=nullptr;
	uint32_t *screentexpos_shadow_flag=nullptr;
	int32_t *screentexpos_building_one=nullptr;
	int32_t *screentexpos_item=nullptr;
	int32_t *screentexpos_vehicle=nullptr;
	int32_t *screentexpos_vermin=nullptr;
	int32_t *screentexpos_left_creature=nullptr;
	int32_t *screentexpos=nullptr;
	int32_t *screentexpos_right_creature=nullptr;
	int32_t *screentexpos_building_two=nullptr;
	int32_t *screentexpos_projectile=nullptr;
	int32_t *screentexpos_high_flow=nullptr;
	int32_t *screentexpos_top_shadow=nullptr;
	int32_t *screentexpos_signpost=nullptr;
	int32_t *screentexpos_upleft_creature=nullptr;
	int32_t *screentexpos_up_creature=nullptr;
	int32_t *screentexpos_upright_creature=nullptr;
	int32_t *screentexpos_designation=nullptr;
	int32_t *screentexpos_interface=nullptr;
	int32_t *screentexpos_item_old=nullptr;
	int32_t *screentexpos_vehicle_old=nullptr;
	int32_t *screentexpos_left_creature_old=nullptr;
	int32_t *screentexpos_old=nullptr;
	int32_t *screentexpos_right_creature_old=nullptr;
	int32_t *screentexpos_upleft_creature_old=nullptr;
	int32_t *screentexpos_up_creature_old=nullptr;
	int32_t *screentexpos_upright_creature_old=nullptr;
	int32_t *screentexpos_designation_old=nullptr;
	int32_t *screentexpos_background_old=nullptr;
	uint64_t *screentexpos_floor_flag_old=nullptr;
	int32_t *screentexpos_background_two_old=nullptr;
	uint32_t *screentexpos_liquid_flag_old=nullptr;
	uint32_t *screentexpos_spatter_flag_old=nullptr;
	int32_t *screentexpos_spatter_old=nullptr;
	uint64_t *screentexpos_ramp_flag_old=nullptr;
	uint32_t *screentexpos_shadow_flag_old=nullptr;
	int32_t *screentexpos_building_one_old=nullptr;
	int32_t *screentexpos_vermin_old=nullptr;
	int32_t *screentexpos_building_two_old=nullptr;
	int32_t *screentexpos_projectile_old=nullptr;
	int32_t *screentexpos_high_flow_old=nullptr;
	int32_t *screentexpos_top_shadow_old=nullptr;
	int32_t *screentexpos_signpost_old=nullptr;
	int32_t *screentexpos_interface_old=nullptr;

	std::vector<int32_t> i32[40];
	std::vector<uint32_t> u32[6];
	std::vector<uint64_t> u64[4];

	test_viewportst(int32_t dx,int32_t dy)
		{
		dim_x=dx;
		dim_y=dy;
		clipx={0,dx-1};
		clipy={0,dy-1};
		const size_t n=size_t(dx)*size_t(dy);
		for(auto &v:i32)v.assign(n,0);
		for(auto &v:u32)v.assign(n,0);
		for(auto &v:u64)v.assign(n,0);
		int32_t **i32_members[]={
			&screentexpos_background,&screentexpos_background_two,&screentexpos_spatter,
			&screentexpos_building_one,&screentexpos_item,&screentexpos_vehicle,
			&screentexpos_vermin,&screentexpos_left_creature,&screentexpos,
			&screentexpos_right_creature,&screentexpos_building_two,&screentexpos_projectile,
			&screentexpos_high_flow,&screentexpos_top_shadow,&screentexpos_signpost,
			&screentexpos_upleft_creature,&screentexpos_up_creature,
			&screentexpos_upright_creature,&screentexpos_designation,&screentexpos_interface,
			&screentexpos_item_old,&screentexpos_vehicle_old,&screentexpos_left_creature_old,
			&screentexpos_old,&screentexpos_right_creature_old,&screentexpos_upleft_creature_old,
			&screentexpos_up_creature_old,&screentexpos_upright_creature_old,
			&screentexpos_designation_old,&screentexpos_background_old,
			&screentexpos_background_two_old,&screentexpos_spatter_old,
			&screentexpos_building_one_old,&screentexpos_vermin_old,
			&screentexpos_building_two_old,&screentexpos_projectile_old,
			&screentexpos_high_flow_old,&screentexpos_top_shadow_old,
			&screentexpos_signpost_old,&screentexpos_interface_old};
		static_assert(sizeof(i32_members)/sizeof(i32_members[0])==40);
		for(size_t i=0;i<40;++i)*i32_members[i]=i32[i].data();
		uint32_t **u32_members[]={
			&screentexpos_liquid_flag,&screentexpos_spatter_flag,&screentexpos_shadow_flag,
			&screentexpos_liquid_flag_old,&screentexpos_spatter_flag_old,
			&screentexpos_shadow_flag_old};
		for(size_t i=0;i<6;++i)*u32_members[i]=u32[i].data();
		uint64_t **u64_members[]={&screentexpos_floor_flag,&screentexpos_ramp_flag,
			&screentexpos_floor_flag_old,&screentexpos_ramp_flag_old};
		for(size_t i=0;i<4;++i)*u64_members[i]=u64[i].data();
		}

	test_viewportst(const test_viewportst &)=delete;
	test_viewportst &operator=(const test_viewportst &)=delete;

	int32_t index(int32_t x,int32_t y) const
		{
		return x*dim_y+y;
		}

	// A new engine redraw: the current buffers become the previous ones and start empty.
	// The other buffers keep their content, which the engine has by now copied as well.
	void redraw()
		{
		using table=viewport_layer_tablest<test_viewportst>;
		const size_t n=size_t(dim_x)*size_t(dim_y);
		for(const auto &buffer:table::buffers)
			{
			int32_t *current=this->*buffer.current;
			int32_t *previous=this->*buffer.previous;
			std::copy(current,current+n,previous);
			std::fill(current,current+n,0);
			}
		const auto settle=[n](auto *current,auto *previous){std::copy(current,current+n,previous);};
		settle(screentexpos_background,screentexpos_background_old);
		settle(screentexpos_floor_flag,screentexpos_floor_flag_old);
		settle(screentexpos_background_two,screentexpos_background_two_old);
		settle(screentexpos_liquid_flag,screentexpos_liquid_flag_old);
		settle(screentexpos_spatter_flag,screentexpos_spatter_flag_old);
		settle(screentexpos_spatter,screentexpos_spatter_old);
		settle(screentexpos_ramp_flag,screentexpos_ramp_flag_old);
		settle(screentexpos_shadow_flag,screentexpos_shadow_flag_old);
		settle(screentexpos_building_one,screentexpos_building_one_old);
		settle(screentexpos_vermin,screentexpos_vermin_old);
		settle(screentexpos_building_two,screentexpos_building_two_old);
		settle(screentexpos_projectile,screentexpos_projectile_old);
		settle(screentexpos_high_flow,screentexpos_high_flow_old);
		settle(screentexpos_top_shadow,screentexpos_top_shadow_old);
		settle(screentexpos_signpost,screentexpos_signpost_old);
		settle(screentexpos_interface,screentexpos_interface_old);
		}
};

struct canvas_eventst
{
	enum kindst{repaint,sprite,fill,clip,unclip};
	kindst kind;
	const test_viewportst *viewport=nullptr;
	int32_t x=0;             // repaint: tile; fill/clip: pixel rect
	int32_t y=0;
	int32_t w=0;
	int32_t h=0;
	int32_t origin_x=0;      // the origin the engine would paint with
	int32_t origin_y=0;
	int32_t center=0;        // repaint: the center buffer as the engine would read it
	int32_t interface=0;
	int32_t background=0;
	float px=0.0f;           // sprite: pixel position
	float py=0.0f;
	bool mirrored=false;
};

struct recording_canvasst
{
	int32_t ox=0;
	int32_t oy=0;
	int32_t zoom_=128;
	std::vector<canvas_eventst> events;

	int32_t origin_x() const {return ox;}
	int32_t origin_y() const {return oy;}
	int32_t zoom() const {return zoom_;}

	void offset_origin(int32_t dx,int32_t dy)
		{
		ox+=dx;
		oy+=dy;
		}

	const void *texture(int32_t texpos) const
		{
		return texpos==0?nullptr:reinterpret_cast<const void *>(intptr_t(texpos));
		}

	void repaint(test_viewportst *vp,int32_t x,int32_t y)
		{
		canvas_eventst e{canvas_eventst::repaint};
		e.viewport=vp;
		e.x=x;
		e.y=y;
		e.origin_x=ox;
		e.origin_y=oy;
		e.center=vp->screentexpos[vp->index(x,y)];
		e.interface=vp->screentexpos_interface[vp->index(x,y)];
		e.background=vp->screentexpos_background[vp->index(x,y)];
		events.push_back(e);
		}

	void draw_sprite(const void *,float x,float y,float,bool mirrored)
		{
		canvas_eventst e{canvas_eventst::sprite};
		e.px=x;
		e.py=y;
		e.mirrored=mirrored;
		events.push_back(e);
		}

	void fill_black(const pixel_rectst &r)
		{
		events.push_back({canvas_eventst::fill,nullptr,r.x,r.y,r.w,r.h});
		}

	void set_clip(const pixel_rectst &r)
		{
		events.push_back({canvas_eventst::clip,nullptr,r.x,r.y,r.w,r.h});
		}

	void clear_clip()
		{
		events.push_back({canvas_eventst::unclip});
		}

	size_t count(canvas_eventst::kindst kind) const
		{
		return size_t(std::count_if(events.begin(),events.end(),
			[&](const canvas_eventst &e){return e.kind==kind;}));
		}

	std::vector<canvas_eventst> repaints_of(const test_viewportst *vp,int32_t x,int32_t y) const
		{
		std::vector<canvas_eventst> found;
		for(const canvas_eventst &e:events)
			if(e.kind==canvas_eventst::repaint&&e.viewport==vp&&e.x==x&&e.y==y)
				found.push_back(e);
		return found;
		}

	bool filled(int32_t px,int32_t py) const
		{
		for(const canvas_eventst &e:events)
			if(e.kind==canvas_eventst::fill&&e.x==px&&e.y==py&&e.w==32&&e.h==32)return true;
		return false;
		}
};

struct scenest
{
	test_viewportst vp;
	visual_animation_managerst manager;
	frame_rendererst<test_viewportst> renderer;
	recording_canvasst canvas;
	std::vector<test_viewportst *> viewports;

	scenest(int32_t dx,int32_t dy):vp(dx,dy),viewports{&vp}
		{
		manager.set_base_duration_ms(100);
		std::fill(vp.screentexpos_background,vp.screentexpos_background+dx*dy,7);
		std::fill(vp.screentexpos_interface,vp.screentexpos_interface+dx*dy,3);
		}

	void sync(uint32_t now_ms)
		{
		using table=viewport_layer_tablest<test_viewportst>;
		const test_viewportst *cvp=&vp;
		manager.begin_frame(now_ms);
		manager.synchronize_viewport(
			{cvp,vp.dim_x,vp.dim_y,1,table::current(cvp),table::previous(cvp),0,0});
		manager.end_frame();
		}

	void render(int32_t glide_x=0,int32_t glide_y=0)
		{
		canvas.events.clear();
		renderer.render(canvas,viewports,&vp,manager,glide_x,glide_y);
		}

	// A creature standing on (x0,y0) at the first redraw and on (x1,y1) at the second.
	void step(int32_t x0,int32_t y0,int32_t x1,int32_t y1,int32_t texpos=50)
		{
		vp.screentexpos[vp.index(x0,y0)]=texpos;
		sync(1000);
		vp.redraw();
		vp.screentexpos[vp.index(x1,y1)]=texpos;
		sync(1100);
		}
};

void test_step_repaints_the_path_with_the_creature_hidden()
{
	scenest scene(8,6);
	scene.step(2,3,3,3);
	scene.sync(1150);   // halfway
	scene.render();
	const auto &canvas=scene.canvas;
	assert(canvas.count(canvas_eventst::sprite)==1);
	const auto sprite=std::find_if(canvas.events.begin(),canvas.events.end(),
		[](const canvas_eventst &e){return e.kind==canvas_eventst::sprite;});
	assert(sprite->px==2*32+16.0f);
	assert(sprite->py==3*32);
	assert(!sprite->mirrored);   // flipping is off by default
	// Both tiles of the path are blanked, then repainted with the creature's own sprite
	// hidden and the level shading deferred to a final interface-only pass.
	assert(canvas.filled(2*32,3*32));
	assert(canvas.filled(3*32,3*32));
	for(const int32_t x:{2,3})
		{
		const auto repaints=canvas.repaints_of(&scene.vp,x,3);
		assert(repaints.size()>=2);
		for(const canvas_eventst &e:repaints)
			{
			assert(e.center==0);
			assert(e.origin_x==0&&e.origin_y==0);
			}
		assert(repaints.front().interface==0);
		assert(repaints.front().background==7);
		assert(repaints.back().interface==3);
		assert(repaints.back().background==0);
		}
	// Nothing else is touched.
	for(const canvas_eventst &e:canvas.events)
		if(e.kind==canvas_eventst::repaint)assert(e.y==3&&(e.x==2||e.x==3));
	assert(canvas.count(canvas_eventst::clip)==0);
	// The buffers are restored after the pass.
	assert(scene.vp.screentexpos[scene.vp.index(3,3)]==50);
	assert(scene.vp.screentexpos_interface[scene.vp.index(3,3)]==3);
	assert(scene.vp.screentexpos_background[scene.vp.index(2,3)]==7);
}

void test_last_frames_tiles_are_repainted_once_more()
{
	scenest scene(8,6);
	scene.step(2,3,3,3);
	scene.sync(1150);
	scene.render();
	// A new redraw: the creature is gone. Its previous frame's tiles still show the
	// interpolated sprite, so this frame blanks and repaints them, and nothing else.
	scene.vp.redraw();
	scene.sync(1250);
	scene.render();
	assert(scene.canvas.count(canvas_eventst::sprite)==0);
	assert(scene.canvas.filled(2*32,3*32));
	assert(scene.canvas.filled(3*32,3*32));
	assert(scene.canvas.count(canvas_eventst::fill)==2);
	assert(scene.canvas.count(canvas_eventst::repaint)==2);
	// And once that is done, the next frame has nothing left to do.
	scene.sync(1300);
	scene.render();
	assert(scene.canvas.events.empty());
	// Unless the frame was forgotten (a pan or a context change): then nothing is owed.
	scene.step(4,1,5,1);
	scene.sync(1150);
	scene.render();
	scene.renderer.forget_coverage();
	scene.vp.redraw();
	scene.sync(1250);
	scene.render();
	assert(scene.canvas.events.empty());
}

void test_fire_on_the_path_drops_the_sprite()
{
	scenest scene(8,6);
	scene.vp.screentexpos_spatter_flag[scene.vp.index(2,3)]=0x10000000U;
	scene.step(2,3,3,3);
	scene.sync(1150);
	scene.render();
	assert(scene.canvas.events.empty());
}

void test_clip_edges_drop_the_sprite()
{
	scenest scene(8,6);
	scene.vp.clipx={3,7};
	scene.step(2,3,3,3);
	scene.sync(1150);
	scene.render();
	assert(scene.canvas.events.empty());
}

void test_flip_mirrors_an_eastbound_creature_and_keeps_it_mirrored()
{
	scenest scene(8,6);
	scene.renderer.get_settings().flip=true;
	scene.step(2,3,3,3);
	scene.sync(1150);
	scene.render();
	assert(scene.canvas.count(canvas_eventst::sprite)==1);
	assert(scene.canvas.events.back().kind!=canvas_eventst::sprite);
	for(const canvas_eventst &e:scene.canvas.events)
		if(e.kind==canvas_eventst::sprite)assert(e.mirrored);
	// The creature stands still on the next redraw: still facing east, so its native
	// sprite is hidden and the mirrored one is drawn in place.
	scene.vp.redraw();
	scene.vp.screentexpos[scene.vp.index(3,3)]=50;
	scene.sync(1300);
	assert(scene.manager.has_mirrored_facing(&scene.vp));
	scene.render();
	assert(scene.canvas.count(canvas_eventst::sprite)==1);
	for(const canvas_eventst &e:scene.canvas.events)
		{
		if(e.kind==canvas_eventst::sprite)
			{
			assert(e.mirrored);
			assert(e.px==3*32&&e.py==3*32);
			}
		// The tile it left last frame is repainted once more; its own tile has it hidden.
		if(e.kind==canvas_eventst::repaint)
			{
			assert(e.y==3&&(e.x==2||e.x==3));
			if(e.x==3)assert(e.center==0);
			}
		}
	// Walking back west restores the native facing: nothing to draw once it lands.
	scene.vp.redraw();
	scene.vp.screentexpos[scene.vp.index(2,3)]=50;
	scene.sync(1400);
	scene.sync(1600);
	assert(!scene.manager.has_mirrored_facing(&scene.vp));
}

void test_bob_covers_the_row_above()
{
	scenest scene(8,6);
	scene.renderer.get_settings().bob.enabled=true;
	scene.step(2,3,3,3);
	scene.sync(1125);   // a quarter through: the first hop is at its peak
	scene.render();
	assert(scene.canvas.count(canvas_eventst::sprite)==1);
	for(const canvas_eventst &e:scene.canvas.events)
		if(e.kind==canvas_eventst::sprite)assert(e.py<3*32);
	assert(!scene.canvas.repaints_of(&scene.vp,2,2).empty());
	assert(!scene.canvas.repaints_of(&scene.vp,3,2).empty());
	assert(scene.canvas.repaints_of(&scene.vp,2,4).empty());
	// On the top row there is no row above, so the creature glides without the bob.
	scenest top(8,6);
	top.renderer.get_settings().bob.enabled=true;
	top.step(2,0,3,0);
	top.sync(1125);
	top.render();
	assert(top.canvas.count(canvas_eventst::sprite)==1);
	for(const canvas_eventst &e:top.canvas.events)
		if(e.kind==canvas_eventst::sprite)assert(e.py==0);
}

void test_glide_repaints_the_whole_clip_at_the_shifted_origin()
{
	scenest scene(8,6);
	scene.vp.clipx={1,6};
	scene.vp.clipy={0,4};
	scene.canvas.ox=10;
	scene.canvas.oy=20;
	scene.step(2,3,3,3);
	scene.sync(1150);
	scene.render(5,-3);
	const auto &events=scene.canvas.events;
	assert(events.front().kind==canvas_eventst::clip);
	assert(events.front().x==10+32&&events.front().y==20);
	assert(events.front().w==6*32&&events.front().h==5*32);
	assert(events[1].kind==canvas_eventst::fill);
	assert(events[1].x==events.front().x&&events[1].w==events.front().w);
	assert(events.back().kind==canvas_eventst::unclip);
	assert(scene.canvas.count(canvas_eventst::repaint)>=6*5);
	for(int32_t x=1;x<=6;++x)
		for(int32_t y=0;y<=4;++y)
			assert(!scene.canvas.repaints_of(&scene.vp,x,y).empty());
	for(const canvas_eventst &e:events)
		{
		if(e.kind==canvas_eventst::repaint)
			{
			assert(e.origin_x==15&&e.origin_y==17);
			assert(e.x>=1&&e.x<=6&&e.y>=0&&e.y<=4);
			}
		if(e.kind==canvas_eventst::sprite)assert(e.px==15+2*32+16.0f&&e.py==17+3*32);
		}
	assert(scene.canvas.ox==10&&scene.canvas.oy==20);
	// The glide repainted everything, so the next frame owes nothing for it.
	scene.vp.redraw();
	scene.sync(1250);
	scene.render();
	assert(scene.canvas.events.empty());
}

void test_lower_level_sprite_is_shaded_by_the_main_level()
{
	test_viewportst lower(8,6);
	scenest scene(8,6);
	std::fill(lower.screentexpos_background,lower.screentexpos_background+48,9);
	scene.viewports={&lower,&scene.vp};
	using table=viewport_layer_tablest<test_viewportst>;
	const auto sync_both=[&](uint32_t now_ms)
		{
		const test_viewportst *cl=&lower;
		const test_viewportst *cm=&scene.vp;
		scene.manager.begin_frame(now_ms);
		scene.manager.synchronize_viewport(
			{cl,8,6,1,table::current(cl),table::previous(cl),0,0});
		scene.manager.synchronize_viewport(
			{cm,8,6,1,table::current(cm),table::previous(cm),0,0});
		scene.manager.end_frame();
		};
	lower.screentexpos[lower.index(2,3)]=50;
	sync_both(1000);
	lower.redraw();
	scene.vp.redraw();
	lower.screentexpos[lower.index(3,3)]=50;
	sync_both(1100);
	sync_both(1150);
	scene.render();
	assert(scene.canvas.count(canvas_eventst::sprite)==1);
	// The lower level's tile is repainted with the creature hidden, then the sprite is
	// drawn, then the main level is repainted whole over it, shading included.
	const auto lower_repaints=scene.canvas.repaints_of(&lower,2,3);
	const auto main_repaints=scene.canvas.repaints_of(&scene.vp,2,3);
	assert(!lower_repaints.empty());
	assert(!main_repaints.empty());
	for(const canvas_eventst &e:lower_repaints)assert(e.center==0);
	assert(main_repaints.size()==1);
	assert(main_repaints.back().interface==3);
	assert(main_repaints.back().background==7);
	size_t sprite_at=0;
	size_t last_main=0;
	for(size_t i=0;i<scene.canvas.events.size();++i)
		{
		const canvas_eventst &e=scene.canvas.events[i];
		if(e.kind==canvas_eventst::sprite)sprite_at=i;
		if(e.kind==canvas_eventst::repaint&&e.viewport==&scene.vp)last_main=i;
		}
	assert(last_main>sprite_at);
}

} // namespace

void test_resting_mirrored_sprite_needs_a_frame_only_when_the_engine_repaints_under_it()
{
	scenest scene(8,6);
	scene.renderer.get_settings().flip=true;
	scene.step(2,3,3,3);
	scene.sync(1150);
	scene.render();
	// At rest, facing east, with every buffer settled: nothing on screen changed.
	scene.vp.redraw();
	scene.vp.screentexpos[scene.vp.index(3,3)]=50;
	scene.sync(1300);
	assert(scene.manager.has_mirrored_facing(&scene.vp));
	assert(!scene.renderer.resting_sprites_disturbed(scene.viewports,scene.manager));
	// A repaint far from the creature leaves the sprite on screen.
	scene.vp.screentexpos_background[scene.vp.index(7,0)]=8;
	assert(!scene.renderer.resting_sprites_disturbed(scene.viewports,scene.manager));
	// Any buffer changing within reach of the sprite calls for a frame.
	scene.vp.screentexpos_interface[scene.vp.index(4,3)]=4;
	assert(scene.renderer.resting_sprites_disturbed(scene.viewports,scene.manager));
	scene.vp.screentexpos_interface[scene.vp.index(4,3)]=3;
	scene.vp.screentexpos_liquid_flag[scene.vp.index(3,2)]=1;
	assert(scene.renderer.resting_sprites_disturbed(scene.viewports,scene.manager));
	// Without flipping there is no resting sprite to protect.
	scene.renderer.get_settings().flip=false;
	assert(!scene.renderer.resting_sprites_disturbed(scene.viewports,scene.manager));
}

int main()
{
	test_resting_mirrored_sprite_needs_a_frame_only_when_the_engine_repaints_under_it();
	test_step_repaints_the_path_with_the_creature_hidden();
	test_last_frames_tiles_are_repainted_once_more();
	test_fire_on_the_path_drops_the_sprite();
	test_clip_edges_drop_the_sprite();
	test_flip_mirrors_an_eastbound_creature_and_keeps_it_mirrored();
	test_bob_covers_the_row_above();
	test_glide_repaints_the_whole_clip_at_the_shifted_origin();
	test_lower_level_sprite_is_shaded_by_the_main_level();
	return 0;
}
