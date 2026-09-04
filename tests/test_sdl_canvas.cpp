// SPDX-License-Identifier: MIT

// Drives sdl_canvasst against a fake SDL: fills are batched into one call issued before anything
// paints over them, the draw colour is saved once and restored once, mirrored sprites go
// through the flip entry point, and the counters land in frame_statisticst.

#ifdef NDEBUG
#undef NDEBUG
#endif

#include "sdl_canvas.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace {

struct fake_renderer;
struct fake_texture{int id;};

struct fake_viewportst
{
	int32_t dim_x=0;
	int32_t dim_y=0;
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
};

struct fake_rect{int x,y,w,h;};
struct fake_frect{float x,y,w,h;};
struct fake_sdl_renderer{};

// Every call the canvas makes, in order, as text.
std::vector<std::string> log;

struct fake_renderer
{
	int32_t origin_x=100;
	int32_t origin_y=200;
	int32_t viewport_zoom_factor=128;
	fake_sdl_renderer sdl_object;
	void *sdl_renderer=&sdl_object;

	void update_viewport_tile(fake_viewportst *,int32_t x,int32_t y)
		{
		log.push_back("repaint "+std::to_string(x)+","+std::to_string(y));
		}
};

struct fake_api
{
	using renderer_type=fake_sdl_renderer;
	using texture_type=fake_texture;
	using rect_type=fake_rect;
	using frect_type=fake_frect;
	using color_type=uint8_t;
	static constexpr int flip_horizontal=77;

	mutable uint8_t r=10,g=20,b=30,a=40;   // the "current" draw colour

	std::function<void(fake_sdl_renderer *,fake_texture *,const void *,const fake_frect *)>
		render_copy_f=[](fake_sdl_renderer *,fake_texture *t,const void *,const fake_frect *d)
		{
		log.push_back("copy "+std::to_string(t->id)+" @"+std::to_string(int(d->x))+","+
			std::to_string(int(d->y))+" size "+std::to_string(int(d->w)));
		};
	std::function<void(fake_sdl_renderer *,fake_texture *,const void *,const fake_frect *,
		double,const void *,int)>
		render_copy_ex_f=[](fake_sdl_renderer *,fake_texture *t,const void *,const fake_frect *d,
			double,const void *,int flip)
		{
		log.push_back("copyex "+std::to_string(t->id)+" @"+std::to_string(int(d->x))+","+
			std::to_string(int(d->y))+" flip "+std::to_string(flip));
		};
	std::function<void(fake_sdl_renderer *,const fake_rect *,int)>
		render_fill_rects=[](fake_sdl_renderer *,const fake_rect *rects,int count)
		{
		std::string s="fill";
		for(int i=0;i<count;++i)
			s+=" ["+std::to_string(rects[i].x)+","+std::to_string(rects[i].y)+" "+
				std::to_string(rects[i].w)+"x"+std::to_string(rects[i].h)+"]";
		log.push_back(s);
		};
	std::function<void(fake_sdl_renderer *,const fake_rect *)>
		render_set_clip_rect=[](fake_sdl_renderer *,const fake_rect *rect)
		{
		log.push_back(rect?"clip "+std::to_string(rect->x)+","+std::to_string(rect->y)+" "+
			std::to_string(rect->w)+"x"+std::to_string(rect->h):std::string("unclip"));
		};
	std::function<void(fake_sdl_renderer *,uint8_t *,uint8_t *,uint8_t *,uint8_t *)>
		get_render_draw_color=[this](fake_sdl_renderer *,uint8_t *pr,uint8_t *pg,uint8_t *pb,
			uint8_t *pa)
		{
		*pr=r;*pg=g;*pb=b;*pa=a;
		log.push_back("getcolor");
		};
	std::function<void(fake_sdl_renderer *,uint8_t,uint8_t,uint8_t,uint8_t)>
		set_render_draw_color=[this](fake_sdl_renderer *,uint8_t nr,uint8_t ng,uint8_t nb,
			uint8_t na)
		{
		r=nr;g=ng;b=nb;a=na;
		log.push_back("setcolor "+std::to_string(nr)+","+std::to_string(ng)+","+
			std::to_string(nb)+","+std::to_string(na));
		};

	fake_texture textures[4]={{0},{1},{2},{3}};
	const void *texture_of(fake_renderer *,int32_t texpos) const
		{
		return texpos==0?nullptr:&textures[texpos];
		}
};

using canvasst=sdl_canvasst<fake_renderer,fake_viewportst,fake_api>;

void expect(const std::vector<std::string> &expected)
{
	assert(log==expected);
	log.clear();
}

void test_fills_batch_before_the_next_paint()
{
	fake_renderer renderer;
	fake_api api;
	frame_statisticst stats;
	std::vector<fake_rect> scratch;
	fake_viewportst vp;
	vp.dim_x=2;
	vp.dim_y=2;
	int32_t background[4]={1,1,1,1};
	vp.screentexpos_background=background;
	{
	canvasst canvas(&renderer,api,&vp,stats,scratch);
	canvas.fill_black({0,0,32,32});
	canvas.fill_black({32,0,32,32});
	expect({"getcolor","setcolor 0,0,0,255"});
	canvas.repaint(&vp,1,0);
	expect({"fill [0,0 32x32] [32,0 32x32]","repaint 1,0"});
	canvas.fill_black({0,32,32,32});
	canvas.draw_sprite(canvas.texture(2),5.0f,6.0f,32.0f,false);
	expect({"fill [0,32 32x32]","copy 2 @5,6 size 32"});
	canvas.fill_black({64,0,32,32});
	canvas.set_clip({0,0,64,64});
	expect({"fill [64,0 32x32]","clip 0,0 64x64"});
	canvas.fill_black({96,0,32,32});
	canvas.clear_clip();
	expect({"fill [96,0 32x32]","unclip"});
	canvas.fill_black({0,64,32,32});
	}
	// Destruction flushes the last batch and restores the colour it found.
	expect({"fill [0,64 32x32]","setcolor 10,20,30,40"});
	assert(api.r==10&&api.a==40);
	assert(scratch.empty());
}

void test_no_fill_means_no_colour_traffic()
{
	fake_renderer renderer;
	fake_api api;
	frame_statisticst stats;
	std::vector<fake_rect> scratch;
	fake_viewportst vp;
	{
	canvasst canvas(&renderer,api,&vp,stats,scratch);
	canvas.draw_sprite(canvas.texture(1),0.0f,0.0f,16.0f,true);
	canvas.clear_clip();
	}
	expect({"copyex 1 @0,0 flip 77","unclip"});
}

void test_origin_and_texture_pass_through()
{
	fake_renderer renderer;
	fake_api api;
	frame_statisticst stats;
	std::vector<fake_rect> scratch;
	fake_viewportst vp;
	canvasst canvas(&renderer,api,&vp,stats,scratch);
	assert(canvas.origin_x()==100&&canvas.origin_y()==200&&canvas.zoom()==128);
	canvas.offset_origin(3,-4);
	assert(renderer.origin_x==103&&renderer.origin_y==196);
	assert(canvas.texture(0)==nullptr);
	assert(canvas.texture(3)==&api.textures[3]);
}

void test_stats_counters()
{
	fake_renderer renderer;
	fake_api api;
	frame_statisticst stats;
	stats.enabled=true;
	std::vector<fake_rect> scratch;
	// Main viewport 2x1, lower viewport of the same size. Main tile 0 is opaque, tile 1 blank.
	int32_t main_background[2]={1,0};
	fake_viewportst main;
	main.dim_x=2;
	main.dim_y=1;
	main.screentexpos_background=main_background;
	int32_t lower_background[2]={0,0};
	int32_t lower_center[2]={9,0};
	fake_viewportst lower;
	lower.dim_x=2;
	lower.dim_y=1;
	lower.screentexpos_background=lower_background;
	lower.screentexpos=lower_center;
	{
	canvasst canvas(&renderer,api,&main,stats,scratch);
	canvas.repaint(&main,0,0);   // main, paints something
	canvas.repaint(&main,1,0);   // main, blank
	canvas.repaint(&lower,0,0);  // lower, under an opaque main tile
	canvas.repaint(&lower,1,0);  // lower, blank, main above it blank too
	canvas.draw_sprite(canvas.texture(1),0,0,32,false);
	canvas.draw_sprite(canvas.texture(1),0,0,32,true);
	canvas.fill_black({0,0,1,1});
	canvas.fill_black({0,0,1,1});
	canvas.fill_black({0,0,1,1});
	canvas.set_clip({0,0,1,1});
	}
	assert(stats.tile_repaints==4);
	assert(stats.blank_repaints==2);
	assert(stats.main_repaints==2);
	assert(stats.occluded_repaints==1);
	assert(stats.sprites==2);
	assert(stats.fills==3);
	assert(stats.glides==1);
	log.clear();
}

void test_disabled_stats_count_nothing()
{
	fake_renderer renderer;
	fake_api api;
	frame_statisticst stats;
	std::vector<fake_rect> scratch;
	fake_viewportst vp;
	vp.dim_x=1;
	vp.dim_y=1;
	{
	canvasst canvas(&renderer,api,&vp,stats,scratch);
	canvas.repaint(&vp,0,0);
	canvas.draw_sprite(canvas.texture(1),0,0,32,false);
	canvas.fill_black({0,0,1,1});
	}
	assert(stats.tile_repaints==0&&stats.sprites==0&&stats.fills==0&&stats.collect_us==0);
	log.clear();
}

} // namespace

int main()
{
	test_fills_batch_before_the_next_paint();
	test_no_fill_means_no_colour_traffic();
	test_origin_and_texture_pass_through();
	test_stats_counters();
	test_disabled_stats_count_nothing();
	return 0;
}
