// SPDX-License-Identifier: MIT
// Checks movement_drawing.h: the order a frame's movement goes down in. A sprite between
// two tiles has to be painted with the layers below it already drawn and the layers above it
// not yet, so the plugin paints one render group of sprites, asks the game to repaint the
// tiles they cover with that group and everything under it hidden, and repeats for the next
// group; stacked viewports repeat the whole thing lowest first and the hauled item icons go
// on last. Every one of those is a claim about order, so the stand-ins below append to one
// shared list -- every sprite handed to SDL and every tile handed to the game, in the order
// they were handed over -- and each case reads that list as a sequence.
//
// The four recorded replays cannot stand in for this, which was measured rather than
// assumed. An instrumented replay of each recording reports that of the nine viewports a
// frame carries, only the topmost one ever does anything: viewports 0 to 7 hold no proxy and
// ask for no repaint at all, through any of the three paths, in any of the four recordings.
// The lowest viewport is offered the same staged tiles as the topmost -- 2322, 978, 3646 and
// 3417 of them -- and paints on none, either because the camera glide's blank summary
// already said the tile was empty or because the per-tile check did. No unit in any of the
// four hauls anything, so no icon is ever drawn. The designation stage, the one stage that
// skips its repaint, is reached on 76 tiles across two of the recordings and the game would
// paint nothing on any of them. Nine of the 25 mutations of this header that were tried
// against the recordings therefore leave all four digests and all four repaint counts
// identical: reapplying the lowest viewport as well, running the viewports from the top
// down, shading a tile a viewport cannot reach, shading only the tiles a viewport has
// sprites on, repainting above the designation stage too, and all four mistakes about the
// hauled icons. This suite is what watches those instead.

#include "df/graphic_viewportst.h"

#include "movement_drawing.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures=0;

constexpr int32_t dim_x=2,dim_y=3,tile_count=dim_x*dim_y;

// One thing the drawing code did, in the order it did it. The whole point of drawing in
// stages is the order, so the cases below compare a run against a list of these rather than
// counting calls: a sprite drawn one stage too early is the failure this catches.
struct eventst
{
	enum kindst{copy,repaint};
	kindst kind;
	// repaint: the viewport the game was asked to repaint on, and the tile. copy: null.
	const void *viewport;
	int32_t x;
	int32_t y;
	// copy: the sprite handed to SDL. repaint: what the tile's center layer read at the
	// moment of the ask, which says whether the repaint had that layer hidden.
	SDL_Texture *texture;
	int32_t center_entry;
};

std::vector<eventst> events;

// Sprites, as addresses to tell apart rather than textures to draw with. Nothing here
// dereferences one.
SDL_Texture *sprite(int n){return reinterpret_cast<SDL_Texture *>(uintptr_t(0x1000+n));}

int record_copy(
	SDL_Renderer *,
	SDL_Texture *texture,
	const SDL_Rect *,
	const SDL_FRect *)
{
	events.push_back({eventst::copy,nullptr,0,0,texture,0});
	return 0;
}

int record_copy_ex(
	SDL_Renderer *,
	SDL_Texture *texture,
	const SDL_Rect *,
	const SDL_FRect *,
	double,
	const SDL_FPoint *,
	SDL_RendererFlip)
{
	events.push_back({eventst::copy,nullptr,0,0,texture,0});
	return 0;
}

// The plugin reaches SDL through a table of function pointers it binds at load (sdl_apist in
// plugin_state.h); filling it with recorders is how the sprite drawing suite watches the
// copies, and this suite wants them interleaved with the repaints.
void record_through(plugin_statest &state)
{
	state.sdl.render_copy_f=record_copy;
	state.sdl.render_copy_ex_f=record_copy_ex;
}

// movement_drawing.h is templated over the renderer, so a stand-in needs the members the
// sprite placement reads, somewhere for SDL to go, and the one call the repaints make.
struct recording_rendererst
{
	void *sdl_renderer=reinterpret_cast<void *>(uintptr_t(0x900));
	int32_t viewport_zoom_factor=128;
	int32_t origin_x=0;
	int32_t origin_y=0;

	void update_viewport_tile(df::graphic_viewportst *vp,int32_t x,int32_t y)
		{
		events.push_back(
			{eventst::repaint,vp,x,y,nullptr,vp->screentexpos[x*vp->dim_y+y]});
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

	viewportst()
		{
		vp.dim_x=dim_x;vp.dim_y=dim_y;
		vp.clipx[0]=0;vp.clipx[1]=dim_x-1;
		vp.clipy[0]=0;vp.clipy[1]=dim_y-1;
		for(auto &w:words)w.assign(tile_count,0);
		floor.assign(tile_count,0);ramp.assign(tile_count,0);
		liquid.assign(tile_count,0);spatter_flags.assign(tile_count,0);
		shadow.assign(tile_count,0);
		for(auto &p:previous)p.assign(tile_count,0);
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
		// A repaint is skipped when the tile, with that stage's layers hidden, would paint
		// nothing at all, so a tile needs something in it that the stage does not hide, or no
		// ask reaches the renderer and every case below passes by drawing nothing. Three
		// layers between them survive every pass the cases run. Designations are the last
		// group and nothing repaints above them, so no stage hides them; the shading is what
		// the interface pass alone paints; and the terrain survives a staged repaint, which
		// hides the moving layers and nothing else.
		for(int32_t i=0;i<tile_count;++i)
			{
			words[0][i]=100+i;
			words[18][i]=300+i;
			words[19][i]=200+i;
			}
		}

	// The viewport's own layout, which every pass computes for itself. The cases below use
	// tile (1,0) rather than (1,1) because a 2x3 viewport gives (1,1) the same index
	// whichever way round the two terms are written.
	static int32_t index_of(int32_t x,int32_t y){return x*dim_y+y;}
};

// A sprite of one layer covering one tile. The coverage is what the stages walk: a proxy's
// target tile is where it lands, its coverage is every tile it has to repaint around it.
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

carried_item_proxyst item_on(int32_t x,int32_t y,SDL_Texture *texture)
{
	carried_item_proxyst proxy{};
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

// What a run was expected to do, written the way the cases read best: a copy names its
// sprite, a repaint names its viewport and tile.
eventst copy_of(SDL_Texture *texture)
{
	return {eventst::copy,nullptr,0,0,texture,0};
}

eventst repaint_of(const viewportst &viewport,int32_t x,int32_t y)
{
	return {eventst::repaint,&viewport.vp,x,y,nullptr,0};
}

void describe(const eventst &event)
{
	if(event.kind==eventst::copy)
		printf("copy of sprite %p",static_cast<void *>(event.texture));
	else
		printf("repaint of (%d,%d) on viewport %p",
			event.x,event.y,const_cast<void *>(event.viewport));
}

// Compares a run against what it was expected to do, in order. center_entry is not compared:
// only the one case that reads it looks at it.
void expect_run(const char *name,const std::vector<eventst> &expected)
{
	bool same=events.size()==expected.size();
	for(size_t i=0;same&&i<expected.size();++i)
		same=events[i].kind==expected[i].kind&&
			events[i].viewport==expected[i].viewport&&
			events[i].x==expected[i].x&&
			events[i].y==expected[i].y&&
			events[i].texture==expected[i].texture;
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

void test_a_stage_draws_its_sprites_then_repaints_over_them()
{
	viewportst viewport;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	// One sprite in each of three of the five groups, on three different tiles. The groups
	// run item, vehicle, main, upper, designation; nothing is in the vehicle group, so that
	// stage draws nothing and repaints nothing.
	const std::vector<render_proxyst> proxies=
		{
		proxy_on(viewport_visual_layer::up,1,0,sprite(1)),
		proxy_on(viewport_visual_layer::item,0,0,sprite(2)),
		proxy_on(viewport_visual_layer::center,0,1,sprite(3))
		};
	const render_coveragest coverage=collect_coverage(proxies,dim_y);
	events.clear();
	draw_movement_stages(state,&renderer,&viewport.vp,proxies,coverage);
	// The order is the whole behaviour: each group's sprites go down, and only then is the
	// game asked to repaint what sits above that group on the tiles those sprites cover.
	// A sprite listed first but belonging to a later group waits for its own stage.
	expect_run("the stages in order",
		{
		copy_of(sprite(2)),repaint_of(viewport,0,0),
		copy_of(sprite(3)),repaint_of(viewport,0,1),
		copy_of(sprite(1)),repaint_of(viewport,1,0)
		});
}

void test_two_sprites_of_one_group_share_a_stage()
{
	viewportst viewport;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	// left and center are both in the main group; up is in the upper group above it.
	const std::vector<render_proxyst> proxies=
		{
		proxy_on(viewport_visual_layer::center,0,0,sprite(1)),
		proxy_on(viewport_visual_layer::up,1,0,sprite(2)),
		proxy_on(viewport_visual_layer::left,0,1,sprite(3))
		};
	const render_coveragest coverage=collect_coverage(proxies,dim_y);
	events.clear();
	draw_movement_stages(state,&renderer,&viewport.vp,proxies,coverage);
	// Both main sprites are down before either of the main group's tiles is repainted, and
	// the coverage of a group is walked in tile order, not in the order the sprites were
	// listed.
	expect_run("two sprites in one stage",
		{
		copy_of(sprite(1)),copy_of(sprite(3)),
		repaint_of(viewport,0,0),repaint_of(viewport,0,1),
		copy_of(sprite(2)),repaint_of(viewport,1,0)
		});
}

void test_the_designation_stage_asks_for_no_repaint()
{
	viewportst viewport;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	std::vector<render_proxyst> proxies=
		{proxy_on(viewport_visual_layer::designation,1,0,sprite(1))};
	// The sprite lands on (1,0) and covers (1,1) as well, a tile no sprite targets. A tile a
	// sprite targets has that sprite's layer hidden for the repaint, and with the designation
	// layer hidden this viewport's tiles paint nothing at all, so the repaint would be
	// skipped for a reason that has nothing to do with the stage. On (1,1) it is not hidden,
	// so a repaint asked for here would reach the renderer and be seen.
	proxies[0].coverage.insert({1,1});
	const render_coveragest coverage=collect_coverage(proxies,dim_y);
	events.clear();
	draw_movement_stages(state,&renderer,&viewport.vp,proxies,coverage);
	// Designations are the last group and nothing is drawn above them, so the stage draws
	// its sprite and stops. Everything else about the tile is the same as the cases above,
	// which do get a repaint.
	expect_run("the designation stage",{copy_of(sprite(1))});
}

void test_a_repaint_hides_the_layers_a_sprite_of_a_later_group_covers()
{
	viewportst viewport;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	// The item group is the first stage; the center layer belongs to the main group, two
	// stages later, so the item stage's repaint would paint the game's own center sprite
	// back over a tile the plugin is about to draw itself. The tile's covered layers are
	// what stops that, and they reach the repaint through the coverage's selected map.
	const int32_t tile=viewportst::index_of(1,0);
	viewport.words[8][tile]=77;
	std::vector<render_proxyst> proxies=
		{
		proxy_on(viewport_visual_layer::item,1,0,sprite(1)),
		proxy_on(viewport_visual_layer::center,1,0,sprite(2))
		};
	const render_coveragest coverage=collect_coverage(proxies,dim_y);
	events.clear();
	draw_movement_stages(state,&renderer,&viewport.vp,proxies,coverage);
	if(events.size()!=4)
		{
		printf("a covered layer: %zu events, expected 4\n",events.size());++failures;return;
		}
	if(events[1].kind!=eventst::repaint||events[1].center_entry!=0)
		{
		printf("a covered layer: the item stage's repaint read the center layer as %d, "
			"expected 0\n",events[1].center_entry);
		++failures;
		}
	// The entry is put back afterwards: hiding it is for the duration of the ask alone.
	if(viewport.words[8][tile]!=77)
		{
		printf("a covered layer: left at %d after the stages, expected 77\n",
			viewport.words[8][tile]);
		++failures;
		}
}

void test_a_repaint_hides_the_stage_it_is_painting_above()
{
	viewportst viewport;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	// The repaint of a tile is told which stage it is painting above, and that is what
	// decides how much of the tile it hides: everything through that stage, so that what the
	// plugin drew for those layers is not painted over. The case above covers the other half
	// of the same mask, the layers a sprite of a later stage covers on a tile of its own.
	// Here the tile carries no sprite at all, so that mask is empty and the stage is all
	// there is: the center layer belongs to the main group, so a repaint above the main
	// group has to leave it blank.
	const int32_t tile=viewportst::index_of(0,1);
	viewport.words[8][tile]=55;
	std::vector<render_proxyst> proxies=
		{proxy_on(viewport_visual_layer::center,0,0,sprite(1))};
	proxies[0].coverage.insert({0,1});
	const render_coveragest coverage=collect_coverage(proxies,dim_y);
	events.clear();
	draw_movement_stages(state,&renderer,&viewport.vp,proxies,coverage);
	expect_run("a repaint above the main stage",
		{
		copy_of(sprite(1)),repaint_of(viewport,0,0),repaint_of(viewport,0,1)
		});
	if(events.size()==3&&events[2].center_entry!=0)
		{
		printf("a repaint above the main stage: read the center layer as %d, expected 0\n",
			events[2].center_entry);
		++failures;
		}
	if(viewport.words[8][tile]!=55)
		{
		printf("a repaint above the main stage: left at %d after the stages, expected 55\n",
			viewport.words[8][tile]);
		++failures;
		}
}

void test_a_lower_viewport_is_reapplied_before_its_own_sprites()
{
	viewportst lower,top;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	const std::vector<viewport_renderst> viewports=
		{
		render_of(&lower.vp,{proxy_on(viewport_visual_layer::center,0,0,sprite(1))}),
		render_of(&top.vp,{proxy_on(viewport_visual_layer::center,1,0,sprite(2))})
		};
	const tile_coveragest coverage={{0,0},{1,0}};
	events.clear();
	draw_viewport_movement_stages(state,&renderer,viewports,coverage,{});
	// The first viewport is drawn on as it stands. Every later one is repainted over the
	// staged tiles first, so that a sprite of the viewport below ends up under this one's
	// fog and terrain rather than on top of them, and only then draws its own sprites.
	expect_run("two stacked viewports",
		{
		copy_of(sprite(1)),repaint_of(lower,0,0),
		repaint_of(lower,0,0),repaint_of(lower,1,0),
		repaint_of(top,0,0),repaint_of(top,1,0),
		copy_of(sprite(2)),repaint_of(top,1,0),
		repaint_of(top,0,0),repaint_of(top,1,0)
		});
}

void test_hauled_icons_go_on_after_the_last_viewport()
{
	viewportst lower,top;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	// The topmost viewport carries a sprite of its own, so that "after the sprites" is
	// something this case can tell from "before them".
	const std::vector<viewport_renderst> viewports=
		{
		render_of(&lower.vp,{}),
		render_of(&top.vp,{proxy_on(viewport_visual_layer::center,0,0,sprite(2))})
		};
	const tile_coveragest coverage={{0,0}};
	const std::vector<carried_item_proxyst> carried={item_on(0,0,sprite(5))};
	events.clear();
	draw_viewport_movement_stages(state,&renderer,viewports,coverage,carried);
	// The icons belong to the creatures of the topmost viewport and are drawn over
	// everything, so they go down once, after the last viewport's sprites and before its
	// shading.
	expect_run("a hauled icon over two viewports",
		{
		repaint_of(lower,0,0),
		repaint_of(top,0,0),
		copy_of(sprite(2)),repaint_of(top,0,0),
		copy_of(sprite(5)),
		repaint_of(top,0,0)
		});
}

void test_the_shading_skips_a_tile_the_viewport_does_not_reach()
{
	viewportst viewport;
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	// The staged tiles are the union over every viewport, so a tile can be staged by another
	// viewport and lie outside this one. The clip below stops at x=0, leaving (1,0) staged
	// but unreachable here.
	viewport.vp.clipx[1]=0;
	const std::vector<viewport_renderst> viewports={render_of(&viewport.vp,{})};
	const tile_coveragest coverage={{0,0},{1,0}};
	events.clear();
	draw_viewport_movement_stages(state,&renderer,viewports,coverage,{});
	expect_run("shading past the clip",{repaint_of(viewport,0,0)});
}

void test_nothing_is_drawn_without_a_viewport()
{
	plugin_statest state;
	record_through(state);
	recording_rendererst renderer;
	const tile_coveragest coverage={{0,0}};
	const std::vector<carried_item_proxyst> carried={item_on(0,0,sprite(5))};
	events.clear();
	draw_viewport_movement_stages(state,&renderer,{},coverage,carried);
	// The icons are drawn inside the last viewport's turn, so with no viewport at all there
	// is no turn to draw them in and nothing happens. The plugin never gets here with icons
	// and no viewport, since both come from the same frame's viewport list, but the drawing
	// code is not what enforces that.
	expect_run("no viewports",{});
}

}

int main()
{
	test_a_stage_draws_its_sprites_then_repaints_over_them();
	test_two_sprites_of_one_group_share_a_stage();
	test_the_designation_stage_asks_for_no_repaint();
	test_a_repaint_hides_the_layers_a_sprite_of_a_later_group_covers();
	test_a_repaint_hides_the_stage_it_is_painting_above();
	test_a_lower_viewport_is_reapplied_before_its_own_sprites();
	test_hauled_icons_go_on_after_the_last_viewport();
	test_the_shading_skips_a_tile_the_viewport_does_not_reach();
	test_nothing_is_drawn_without_a_viewport();
	if(failures!=0)
		{
		printf("movement drawing tests: %d FAILED\n",failures);
		return 1;
		}
	printf("movement drawing tests: OK\n");
	return 0;
}
