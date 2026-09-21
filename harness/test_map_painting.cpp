// SPDX-License-Identifier: MIT
// Checks map_painting.h: which pixels of the frame the game has already finished the plugin
// paints over, and what it does to the renderer while it paints them. Two passes, one for
// each of the two ways a frame can need repainting. While the camera is mid-glide the whole
// map rectangle goes down again at an origin shifted by a fraction of a tile, clipped to
// that rectangle; otherwise only the tiles a sprite covers this frame and the ones it
// covered last frame are painted again, each blacked out first. Every claim here is about
// what was handed to SDL and to the game and in what order -- a rectangle, a clip, a
// repaint, a sprite -- so the stand-ins below append to one shared list and the cases read
// it back as a sequence.
//
// Twenty-nine mutations of map_painting.h were run against this suite and against the four
// recorded replays. The suite catches all twenty-nine and the replays catch twenty-two, and
// the seven they miss are why this suite is worth having. Reading the renderer's draw colour
// back after the black has gone down instead of before: the trace holds what the plugin
// asked SDL for, not what SDL was left set to. Sizing the map rectangle by the rounded tile
// size rather than by the pixel its far edge falls on: every recording was made at a zoom
// where a tile divides exactly, so the two agree. Leaving the origin shifted when the pass
// returns: the replay sets the renderer up again from the recording at each frame, so it
// never sees a frame start where the last one left off. Building no blank summary, and
// keeping the summary past the frame: no tile of the recorded camera glide is blank, so the
// summary never skips one and a stale one answers the same as a fresh one. And the two
// checks that keep the pass inside the map rectangle it was handed: every viewport of a
// recorded frame is the same size, so no tile of one lies outside another.

#include "df/graphic_viewportst.h"

#include "map_painting.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <utility>
#include <vector>

namespace {

int failures=0;

constexpr int32_t dim_x=2,dim_y=3;
// The origin the game left the renderer at, chosen away from zero so that a pixel worked out
// from the wrong origin cannot come out right by accident.
constexpr int32_t origin_x=10,origin_y=20;
// The colour the game was drawing in when the plugin took over.
constexpr Uint8 game_r=7,game_g=8,game_b=9,game_a=10;

// One thing the painting did, in the order it did it.
struct eventst
{
	enum kindst{colour,fill,clip,unclip,repaint,copy};
	kindst kind;
	// fill and clip: the rectangle. repaint: the tile. copy: the top left corner of the
	// rectangle the sprite went into. colour: the four channels.
	int32_t x,y,w,h;
	// repaint only: the renderer's origin as the ask was made, and how many viewports the
	// camera glide's blank summary held. Not compared by expect_run; the cases that read
	// them say so.
	int32_t asked_origin_x,asked_origin_y;
	size_t summaries;
};

std::vector<eventst> events;

// What the renderer is set to draw in. SDL hands back whatever was last set rather than
// whatever the game last set, so the stand-in does too: a colour read after the black has
// gone down reads black, and a pass that saved it then would save the wrong one.
Uint8 set_r=game_r,set_g=game_g,set_b=game_b,set_a=game_a;

int record_colour(SDL_Renderer *,Uint8 r,Uint8 g,Uint8 b,Uint8 a)
{
	events.push_back({eventst::colour,r,g,b,a,0,0,0});
	set_r=r;set_g=g;set_b=b;set_a=a;
	return 0;
}

int give_set_colour(SDL_Renderer *,Uint8 *r,Uint8 *g,Uint8 *b,Uint8 *a)
{
	*r=set_r;*g=set_g;*b=set_b;*a=set_a;
	return 0;
}

int record_fill(SDL_Renderer *,const SDL_Rect *rect)
{
	events.push_back({eventst::fill,rect->x,rect->y,rect->w,rect->h,0,0,0});
	return 0;
}

int record_clip(SDL_Renderer *,const SDL_Rect *rect)
{
	if(rect==nullptr)events.push_back({eventst::unclip,0,0,0,0,0,0,0});
	else events.push_back({eventst::clip,rect->x,rect->y,rect->w,rect->h,0,0,0});
	return 0;
}

int record_copy(SDL_Renderer *,SDL_Texture *,const SDL_Rect *,const SDL_FRect *destination)
{
	events.push_back(
		{eventst::copy,int32_t(destination->x),int32_t(destination->y),0,0,0,0,0});
	return 0;
}

int record_copy_ex(
	SDL_Renderer *,
	SDL_Texture *,
	const SDL_Rect *,
	const SDL_FRect *destination,
	double,
	const SDL_FPoint *,
	SDL_RendererFlip)
{
	events.push_back(
		{eventst::copy,int32_t(destination->x),int32_t(destination->y),0,0,0,0,0});
	return 0;
}

// The plugin reaches SDL through a table of function pointers it binds at load (sdl_apist in
// plugin_state.h); filling it with recorders is how this suite watches the painting.
void record_through(plugin_statest &state)
{
	state.sdl.render_copy_f=record_copy;
	state.sdl.render_copy_ex_f=record_copy_ex;
	state.sdl.render_fill_rect=record_fill;
	state.sdl.render_set_clip_rect=record_clip;
	state.sdl.get_render_draw_color=give_set_colour;
	state.sdl.set_render_draw_color=record_colour;
}

// Sprites, as addresses to tell apart rather than textures to draw with. Nothing here
// dereferences one.
SDL_Texture *sprite(int n){return reinterpret_cast<SDL_Texture *>(uintptr_t(0x1000+n));}

// map_painting.h is templated over the renderer, so a stand-in needs the members the pixel
// arithmetic reads, somewhere for SDL to go, and the one call a repaint makes. It notes the
// origin and the blank summary as they stood at each ask, which is how the cases below watch
// the glide's shift without stopping it half way.
struct recording_rendererst
{
	void *sdl_renderer=reinterpret_cast<void *>(uintptr_t(0x900));
	int32_t viewport_zoom_factor=128;
	int32_t origin_x=::origin_x;
	int32_t origin_y=::origin_y;
	plugin_statest *state=nullptr;

	void update_viewport_tile(df::graphic_viewportst *,int32_t x,int32_t y)
		{
		events.push_back(
			{eventst::repaint,x,y,0,0,origin_x,origin_y,
			state==nullptr?0:state->render.blank_summaries.summaries.size()});
		}
};

// The 25 per-tile arrays the game draws a tile from, plus the 9 previous-frame arrays the
// layer table needs.
struct viewportst
{
	df::graphic_viewportst vp{};
	std::vector<int32_t> words[25];
	std::vector<uint64_t> floor,ramp;
	std::vector<uint32_t> liquid,spatter_flags,shadow;
	std::vector<int32_t> previous[visual_layer_count];

	explicit viewportst(int32_t across=dim_x,int32_t down=dim_y)
		{
		const int32_t tiles=across*down;
		vp.dim_x=across;vp.dim_y=down;
		vp.clipx[0]=0;vp.clipx[1]=across-1;
		vp.clipy[0]=0;vp.clipy[1]=down-1;
		for(auto &w:words)w.assign(tiles,0);
		floor.assign(tiles,0);ramp.assign(tiles,0);
		liquid.assign(tiles,0);spatter_flags.assign(tiles,0);
		shadow.assign(tiles,0);
		for(auto &p:previous)p.assign(tiles,0);
		vp.screentexpos_background=words[0].data();
		vp.screentexpos_floor_flag=floor.data();
		vp.screentexpos_background_two=words[1].data();
		vp.screentexpos_liquid_flag=liquid.data();
		vp.screentexpos_spatter_flag=spatter_flags.data();
		vp.screentexpos_spatter=words[2].data();
		vp.screentexpos_ramp_flag=ramp.data();
		vp.screentexpos_shadow_flag=shadow.data();
		vp.screentexpos_building_one=words[3].data();
		vp.screentexpos_item=words[4].data();
		vp.screentexpos_vehicle=words[5].data();
		vp.screentexpos_vermin=words[6].data();
		vp.screentexpos_left_creature=words[7].data();
		vp.screentexpos=words[8].data();
		vp.screentexpos_right_creature=words[9].data();
		vp.screentexpos_building_two=words[10].data();
		vp.screentexpos_projectile=words[11].data();
		vp.screentexpos_high_flow=words[12].data();
		vp.screentexpos_top_shadow=words[13].data();
		vp.screentexpos_signpost=words[14].data();
		vp.screentexpos_upleft_creature=words[15].data();
		vp.screentexpos_up_creature=words[16].data();
		vp.screentexpos_upright_creature=words[17].data();
		vp.screentexpos_designation=words[18].data();
		vp.screentexpos_interface=words[19].data();
		vp.screentexpos_right_creature_old=previous[0].data();
		vp.screentexpos_old=previous[1].data();
		vp.screentexpos_left_creature_old=previous[2].data();
		vp.screentexpos_upright_creature_old=previous[3].data();
		vp.screentexpos_up_creature_old=previous[4].data();
		vp.screentexpos_upleft_creature_old=previous[5].data();
		vp.screentexpos_vehicle_old=previous[6].data();
		vp.screentexpos_item_old=previous[7].data();
		vp.screentexpos_designation_old=previous[8].data();
		// A repaint is skipped when the tile, with the pass's layers hidden, would paint
		// nothing at all, so a tile needs something in it that the pass does not hide, or no
		// ask reaches the renderer and a case passes by painting nothing. Three layers
		// between them survive every pass run below: designations are the last group and
		// nothing repaints above them, the shading is what the interface pass alone paints,
		// and the terrain survives a staged repaint, which hides the moving layers only.
		for(int32_t i=0;i<tiles;++i)
			{
			words[0][i]=100+i;
			words[18][i]=300+i;
			words[19][i]=200+i;
			}
		}
};

// A sprite of one layer covering one tile. The coverage is what the painting walks: a
// proxy's target tile is where it lands, its coverage is every tile it has to repaint.
render_proxyst proxy_on(
	viewport_visual_layer layer,
	int32_t x,
	int32_t y,
	SDL_Texture *texture)
{
	render_proxyst proxy{};
	proxy.layer=layer;
	proxy.source_x_tiles=float(x);
	proxy.source_y_tiles=float(y);
	proxy.target_x=x;
	proxy.target_y=y;
	proxy.texture=texture;
	proxy.coverage.insert({x,y});
	return proxy;
}

viewport_renderst render_of(df::graphic_viewportst *vp,std::vector<render_proxyst> proxies)
{
	viewport_renderst render={vp,std::move(proxies),{}};
	render.coverage=collect_coverage(render.proxies,vp->dim_y);
	return render;
}

// What a run was expected to do, written the way the cases read best.
eventst colour_of(Uint8 r,Uint8 g,Uint8 b,Uint8 a)
{
	return {eventst::colour,r,g,b,a,0,0,0};
}

eventst fill_of(int32_t x,int32_t y,int32_t w,int32_t h)
{
	return {eventst::fill,x,y,w,h,0,0,0};
}

eventst repaint_of(int32_t x,int32_t y){return {eventst::repaint,x,y,0,0,0,0,0};}

eventst copy_at(int32_t x,int32_t y){return {eventst::copy,x,y,0,0,0,0,0};}

void describe(const eventst &event)
{
	switch(event.kind)
		{
		case eventst::colour:
			printf("draw colour %d,%d,%d,%d",event.x,event.y,event.w,event.h);break;
		case eventst::fill:
			printf("fill of %d,%d %dx%d",event.x,event.y,event.w,event.h);break;
		case eventst::clip:
			printf("clip to %d,%d %dx%d",event.x,event.y,event.w,event.h);break;
		case eventst::unclip:printf("clip cleared");break;
		case eventst::repaint:printf("repaint of (%d,%d)",event.x,event.y);break;
		case eventst::copy:printf("sprite at %d,%d",event.x,event.y);break;
		}
}

// Compares a run against what it was expected to do, in order. The origin and the summary
// size a repaint was asked at are not compared here: the two cases that read them do it
// themselves.
void expect_run(const char *name,const std::vector<eventst> &expected)
{
	bool same=events.size()==expected.size();
	for(size_t i=0;same&&i<expected.size();++i)
		same=events[i].kind==expected[i].kind&&events[i].x==expected[i].x&&
			events[i].y==expected[i].y&&events[i].w==expected[i].w&&
			events[i].h==expected[i].h;
	if(same)return;
	++failures;
	printf("%s: the run differs from what was expected\n",name);
	for(size_t i=0;i<events.size()||i<expected.size();++i)
		{
		printf("  %2zu ",i);
		if(i<events.size())describe(events[i]);else printf("(nothing)");
		printf("   expected ");
		if(i<expected.size())describe(expected[i]);else printf("(nothing)");
		printf("\n");
		}
}

void expect(const char *name,bool held)
{
	if(held)return;
	++failures;
	printf("%s: no\n",name);
}

// The tiles the game was asked to repaint, however many times each.
std::set<std::pair<int32_t,int32_t>> repainted_tiles()
{
	std::set<std::pair<int32_t,int32_t>> tiles;
	for(const eventst &event:events)
		if(event.kind==eventst::repaint)tiles.insert({event.x,event.y});
	return tiles;
}

// The five rows below set up what every case needs: a viewport, a state whose SDL table
// records, and a renderer at the game's origin that can see the state.
struct scenest
{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;

	scenest()
		{
		record_through(state);
		renderer.state=&state;
		events.clear();
		set_r=game_r;set_g=game_g;set_b=game_b;set_a=game_a;
		}
};

void test_black_goes_under_every_rectangle_and_the_colour_comes_back()
{
	scenest scene;
	fill_black(
		scene.state.sdl,static_cast<SDL_Renderer *>(scene.renderer.sdl_renderer),
		{{1,2,3,4},{5,6,7,8}});
	// The game is drawing in a colour of its own and goes on drawing after the hook returns,
	// so the black is set, used for every rectangle at once, and handed back.
	expect_run("filling black",
		{
		colour_of(0,0,0,255),
		fill_of(1,2,3,4),
		fill_of(5,6,7,8),
		colour_of(game_r,game_g,game_b,game_a)
		});
}

void test_a_covered_tile_is_blacked_out_where_it_sits_on_screen()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_covered_tiles(scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{});
	// Tile (1,0) at the default zoom is 32 pixels wide, one tile right of the origin and
	// none down.
	int32_t fills=0;
	for(const eventst &event:events)
		if(event.kind==eventst::fill)
			{
			++fills;
			expect("the black rectangle is the tile's own",
				event.x==origin_x+32&&event.y==origin_y&&event.w==32&&event.h==32);
			}
	expect("one tile, one rectangle",fills==1);
}

void test_the_tiles_of_the_frame_before_are_painted_again_too()
{
	scenest scene;
	// A sprite that has moved on leaves the tile it was over last frame showing what the
	// plugin painted there, so that tile is repainted even with nothing on it now. It goes
	// down as a plain tile rather than a staged one: no sprite is going over it this frame,
	// so nothing above it is held back and nothing is asked for again once the sprites are
	// down.
	scene.state.render.previous_coverage.insert({0,2});
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_covered_tiles(scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{});
	expect_run("last frame's tile and this frame's",
		{
		colour_of(0,0,0,255),
		fill_of(origin_x,origin_y+64,32,32),
		fill_of(origin_x+32,origin_y,32,32),
		colour_of(game_r,game_g,game_b,game_a),
		repaint_of(0,2),
		repaint_of(1,0),
		copy_at(origin_x+32,origin_y),
		repaint_of(1,0),
		repaint_of(1,0)
		});
}

void test_a_tile_off_the_viewport_is_left_alone()
{
	scenest scene;
	// The tiles of the frame before are the union over every viewport the frame had, so one
	// of them can lie outside the viewport being painted while a taller viewport of the same
	// frame still holds it. The pass is bounded by the map rectangle it was handed, so that
	// tile is left exactly as the game drew it.
	viewportst tall(dim_x,dim_y+4);
	scene.state.render.previous_coverage.insert({1,5});
	const std::vector<viewport_renderst> viewports=
		{render_of(&tall.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_covered_tiles(scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{});
	int32_t fills=0;
	for(const eventst &event:events)if(event.kind==eventst::fill)++fills;
	expect("the tile off the viewport is not blacked out",fills==1);
	const std::set<std::pair<int32_t,int32_t>> expected={{1,0}};
	expect("the tile off the viewport is not repainted",repainted_tiles()==expected);
}

void test_the_world_goes_down_before_the_sprites()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_covered_tiles(scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{});
	// The whole order in one run: the tile is blacked out, the game paints its world layers
	// over the black, the sprite goes on top of those, and only then is the game asked for
	// what sits above the sprite and for the interface shading over everything. A sprite
	// drawn before the world layers would be painted over by them.
	expect_run("the covered pass in order",
		{
		colour_of(0,0,0,255),
		fill_of(origin_x+32,origin_y,32,32),
		colour_of(game_r,game_g,game_b,game_a),
		repaint_of(1,0),
		copy_at(origin_x+32,origin_y),
		repaint_of(1,0),
		repaint_of(1,0)
		});
}

void test_the_covered_pass_leaves_the_clip_and_the_summary_alone()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_covered_tiles(scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{});
	// Only the glide clips, because only the glide paints tiles the game did not ask for.
	// The blank summary is the glide's too: it pays for itself over a whole map and not
	// over the handful of tiles here.
	bool clipped=false;
	bool summarized=false;
	for(const eventst &event:events)
		{
		if(event.kind==eventst::clip||event.kind==eventst::unclip)clipped=true;
		if(event.kind==eventst::repaint&&event.summaries!=0)summarized=true;
		}
	expect("the covered pass does not touch the clip",!clipped);
	expect("the covered pass builds no blank summary",!summarized);
}

void test_the_glide_clips_to_the_map_and_clears_the_clip_after()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports={render_of(&scene.viewport.vp,{})};
	const tile_coveragest coverage;
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},5,-3);
	// Everything the glide paints is a tile or a fraction of a tile out of place, so it is
	// fenced into the map rectangle first and the fence comes down before the game draws the
	// interface around it.
	int32_t clips=0,unclips=0;
	for(const eventst &event:events)
		{
		if(event.kind==eventst::clip)++clips;
		if(event.kind==eventst::unclip)++unclips;
		}
	expect("the clip goes on first",
		!events.empty()&&events.front().kind==eventst::clip&&
		events.front().x==origin_x&&events.front().y==origin_y&&
		events.front().w==64&&events.front().h==96);
	expect("the clip comes off last",
		!events.empty()&&events.back().kind==eventst::unclip);
	expect("the clip is set once and cleared once",clips==1&&unclips==1);
}

void test_the_glide_blacks_out_the_whole_map_once()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports={render_of(&scene.viewport.vp,{})};
	const tile_coveragest coverage;
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},5,-3);
	// The whole map is about to be painted again a fraction of a tile over, so the strip the
	// shifted tiles no longer cover has to be blacked out; one rectangle does both.
	std::vector<eventst> fills;
	for(const eventst &event:events)if(event.kind==eventst::fill)fills.push_back(event);
	expect("one rectangle for the whole map",fills.size()==1);
	if(fills.size()==1)
		expect("it is the map rectangle",
			fills[0].x==origin_x&&fills[0].y==origin_y&&fills[0].w==64&&fills[0].h==96);
}

void test_the_glide_measures_the_map_before_it_shifts()
{
	scenest scene;
	// A zoom whose tile size does not divide exactly, so that the rectangle's width is not
	// the tile count times the rounded tile size: at zoom 50 a tile rounds to 12 pixels, and
	// the map is 2 tiles by 3, but the edges fall at 25 and 37 pixels rather than 24 and 36.
	scene.renderer.viewport_zoom_factor=50;
	const std::vector<viewport_renderst> viewports={render_of(&scene.viewport.vp,{})};
	const tile_coveragest coverage;
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},7,3);
	// The rectangle is where the game left the map, not where the shifted tiles land: it is
	// the fence the shift is painted inside, so it is measured from the unshifted origin.
	expect("the map rectangle is the game's",
		!events.empty()&&events.front().kind==eventst::clip&&
		events.front().x==origin_x&&events.front().y==origin_y&&
		events.front().w==25&&events.front().h==37);
}

void test_the_glide_paints_every_tile_of_the_map_not_only_the_covered_ones()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},5,-3);
	// Every tile is out of place by the same fraction of a tile, whether a sprite is moving
	// over it or not, so the covered tiles are no longer the ones that need repainting.
	const std::set<std::pair<int32_t,int32_t>> whole_map=
		{{0,0},{0,1},{0,2},{1,0},{1,1},{1,2}};
	expect("every tile of the map is repainted",repainted_tiles()==whole_map);
}

void test_the_glide_lays_the_whole_map_down_before_the_sprites()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},5,-3);
	// All of the map goes down before any of the sprites. A sprite drawn first would be
	// painted over by the tiles that land on top of it afterwards, and the tiles that land
	// on top of it are not only the ones it covers: every tile has moved.
	std::set<std::pair<int32_t,int32_t>> before_the_sprites;
	for(const eventst &event:events)
		{
		if(event.kind==eventst::copy)break;
		if(event.kind==eventst::repaint)before_the_sprites.insert({event.x,event.y});
		}
	const std::set<std::pair<int32_t,int32_t>> whole_map=
		{{0,0},{0,1},{0,2},{1,0},{1,1},{1,2}};
	expect("the whole map is down before the first sprite",before_the_sprites==whole_map);
}

void test_the_glide_paints_at_the_shifted_origin_and_puts_it_back()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports=
		{render_of(&scene.viewport.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(1))})};
	const tile_coveragest coverage={{1,0}};
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},5,-3);
	// The shift is the point of the pass: the game reads the origin as it repaints a tile
	// and the sprite placement reads it as it draws, so both land a fraction of a tile over.
	bool shifted=true;
	bool drawn=false;
	for(const eventst &event:events)
		{
		if(event.kind==eventst::repaint)
			shifted=shifted&&event.asked_origin_x==origin_x+5&&
				event.asked_origin_y==origin_y-3;
		if(event.kind==eventst::copy)
			{
			drawn=true;
			expect("the sprite is drawn at the shifted origin",
				event.x==origin_x+5+32&&event.y==origin_y-3);
			}
		}
	expect("the game repaints at the shifted origin",shifted);
	expect("the sprite was drawn at all",drawn);
	// The game goes on drawing the interface after the hook returns and expects the origin
	// it left.
	expect("the origin is put back",
		scene.renderer.origin_x==origin_x&&scene.renderer.origin_y==origin_y);
}

void test_the_glide_summarizes_the_blank_tiles_and_drops_the_summary_after()
{
	scenest scene;
	const std::vector<viewport_renderst> viewports={render_of(&scene.viewport.vp,{})};
	const tile_coveragest coverage;
	paint_shifted_map(
		scene.state,&scene.renderer,&scene.viewport.vp,viewports,coverage,{},5,-3);
	// Repainting a whole map means asking about tiles that hold nothing, so the pass reads
	// each viewport's arrays once into a word per tile and skips the empty ones from that.
	bool summarized=true;
	bool asked=false;
	for(const eventst &event:events)
		if(event.kind==eventst::repaint)
			{
			asked=true;
			summarized=summarized&&event.summaries==1;
			}
	expect("the summary is in place while the map is painted",asked&&summarized);
	// The game writes the arrays again between frames, so a summary kept past this frame
	// would be answering for tiles it has not read.
	expect("the summary is dropped afterwards",
		scene.state.render.blank_summaries.summaries.empty());
}

}

int main()
{
	test_black_goes_under_every_rectangle_and_the_colour_comes_back();
	test_a_covered_tile_is_blacked_out_where_it_sits_on_screen();
	test_the_tiles_of_the_frame_before_are_painted_again_too();
	test_a_tile_off_the_viewport_is_left_alone();
	test_the_world_goes_down_before_the_sprites();
	test_the_covered_pass_leaves_the_clip_and_the_summary_alone();
	test_the_glide_clips_to_the_map_and_clears_the_clip_after();
	test_the_glide_blacks_out_the_whole_map_once();
	test_the_glide_measures_the_map_before_it_shifts();
	test_the_glide_paints_every_tile_of_the_map_not_only_the_covered_ones();
	test_the_glide_lays_the_whole_map_down_before_the_sprites();
	test_the_glide_paints_at_the_shifted_origin_and_puts_it_back();
	test_the_glide_summarizes_the_blank_tiles_and_drops_the_summary_after();
	if(failures!=0)
		{
		printf("map painting tests: %d FAILED\n",failures);
		return 1;
		}
	printf("map painting tests: OK\n");
	return 0;
}
