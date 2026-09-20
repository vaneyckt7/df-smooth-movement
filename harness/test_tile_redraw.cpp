// SPDX-License-Identifier: MIT
// Checks tile_redraw.h: which tiles the plugin actually asks the game to repaint, and the
// two counters it bumps when it does. tile_repaint.h's own suite covers what each pass
// hides; this one covers the gates around the ask -- the per-tile check that a repaint would
// paint something, the camera glide's blank summary, the layers a tile's own sprites already
// cover, and the rule that a staged tile is repainted by the lowest viewport alone. The
// replays exercise all of this, but what they hold is a digest of what was drawn and a count
// of the repaints asked for, and some of these gates reach neither. Dropping the blank
// summary's check in any of the three passes leaves all four recordings' digests and repaint
// counts exactly as they were, because that check only saves an ask that the paints-nothing
// check would refuse a moment later; reading the summary off the wrong tile is caught by one
// recording in the staged pass and by none in the other two. A counter that stops counting
// is invisible to a digest as well. So for those the suite below is the only guard there is.

#include "df/graphic_viewportst.h"

#include "tile_redraw.h"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

int failures=0;

constexpr int32_t dim_x=2,dim_y=3,tile_count=dim_x*dim_y;

// The 25 per-tile arrays the game draws a tile from, plus the 9 previous-frame arrays the
// layer table needs. Every entry starts at zero, which is a viewport that paints nothing,
// and the clip covers every tile.
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
		}

	// The viewport's own layout, which every pass computes for itself. The cases below sit
	// on tile (1,0) rather than (1,1) because a 2x3 viewport gives (1,1) the same index
	// whichever way round the two terms are written, so it could not tell a pass that read
	// the tile it was asked about from one that read a different tile.
	static int32_t index_of(int32_t x,int32_t y){return x*dim_y+y;}
};

// tile_redraw.h is templated over the renderer, so a stand-in needs the one member the header
// calls and can record instead of painting.
struct repaint_callst
{
	const df::graphic_viewportst *viewport;
	int32_t x;
	int32_t y;
};

struct recording_rendererst
{
	std::vector<repaint_callst> calls;

	void update_viewport_tile(df::graphic_viewportst *vp,int32_t x,int32_t y)
		{
		calls.push_back({vp,x,y});
		}
};

void expect_calls(const char *name,const recording_rendererst &renderer,size_t expected)
{
	if(renderer.calls.size()!=expected)
		printf("%s: %zu repaints, expected %zu\n",name,renderer.calls.size(),expected),
			++failures;
}

void expect_tile(const char *name,const repaint_callst &call,int32_t x,int32_t y)
{
	if(call.x!=x||call.y!=y)
		printf("%s: repainted (%d,%d), expected (%d,%d)\n",name,call.x,call.y,x,y),++failures;
}

void expect_counter(const char *name,uint64_t got,uint64_t expected)
{
	if(got!=expected)
		printf("%s: %llu, expected %llu\n",name,
			(unsigned long long)got,(unsigned long long)expected),++failures;
}

viewport_renderst make_render(df::graphic_viewportst *vp)
{
	viewport_renderst render={vp,{},{}};
	return render;
}

// The summary the camera glide takes before it starts repainting: one word per tile of the
// viewport, holding the bitwise OR of that tile's 25 entries, so a word of zero is a tile
// that paints nothing. The game takes it once for the frame and the arrays do not change
// under it.
void take_summary(plugin_statest &state,df::graphic_viewportst *vp)
{
	const std::vector<df::graphic_viewportst *> viewports={vp};
	state.render.blank_summaries.summarize(
		viewports,[](df::graphic_viewportst *v){return v;});
}

void test_counts_every_ask()
{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	game_repaint(state,&renderer,&viewport.vp,1,2);
	game_repaint(state,&renderer,&viewport.vp,0,1);
	expect_calls("game_repaint",renderer,2);
	if(renderer.calls.size()==2)
		{
		expect_tile("game_repaint, first",renderer.calls[0],1,2);
		expect_tile("game_repaint, second",renderer.calls[1],0,1);
		}
	expect_counter("stats repaints",state.stats.repaints.load(),2);
	expect_counter("hook repaints",state.render.hook_repaints,2);
	// The two counters are not the same counter. The hook clears its own at the start of
	// every frame, for the recorder; the stats one runs for the life of the plugin and is
	// read from the console, so it is atomic and a frame boundary must not touch it.
	state.render.hook_repaints=0;
	game_repaint(state,&renderer,&viewport.vp,0,0);
	expect_counter("stats repaints after a frame boundary",state.stats.repaints.load(),3);
	expect_counter("hook repaints after a frame boundary",state.render.hook_repaints,1);
}

void test_skips_a_tile_that_paints_nothing()
{
	viewportst viewport;
	plugin_statest state;
	{
	// Nothing in any of the 25 arrays: a repaint would put no pixel on screen, so it is
	// not asked for and neither counter moves.
	recording_rendererst renderer;
	staged_repainter(state,&renderer)(&viewport.vp,1,0);
	expect_calls("blank tile",renderer,0);
	expect_counter("blank tile, stats",state.stats.repaints.load(),0);
	}
	{
	// One entry in one array is enough to make the tile worth painting.
	recording_rendererst renderer;
	viewport.words[0][viewportst::index_of(1,0)]=7;
	staged_repainter(state,&renderer)(&viewport.vp,1,0);
	expect_calls("tile with terrain",renderer,1);
	expect_counter("tile with terrain, stats",state.stats.repaints.load(),1);
	}
}

void test_blank_summary_gates_every_pass()
{
	// The top shadow is what makes the tile worth painting in all three cases. Each pass
	// hides a different part of the tile before it looks, and the top shadow is the one thing
	// none of these three hides: the interface pass leaves it in place on purpose, and the
	// above pass is asked for the vehicle group, which hides only the terrain beneath the
	// sprites. So a repaint that does not happen here is the summary's doing and nothing
	// else's.
	//
	// Each pass is put through the same three steps, and the repaints are counted as they
	// accumulate on the one renderer:
	//
	//  1. a summary taken while the tile is blank, which the caller then makes paint. The
	//     game never does that -- it takes the summary once and the arrays hold still under
	//     it -- but it is the only way to see this gate on its own: left alone, a blank tile
	//     is stopped by tile_paints_nothing a moment later, so dropping the gate would change
	//     nothing visible. The pass must skip the tile.
	//  2. the same tile with no summary at all, which must be painted. That is what says the
	//     summary is what stopped it in step 1 and not something else.
	//  3. a summary taken with the tile already painting, which is the order the game uses.
	//     The tile must be painted, which is what says the gate reads the word for the tile
	//     it was asked about: the other five tiles of this viewport are blank, so a gate
	//     reading any of them would skip a tile that has something to paint.
	const int32_t index=viewportst::index_of(1,0);
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	take_summary(state,&viewport.vp);
	viewport.words[13][index]=7;
	redraw_viewport_tile(state,&renderer,make_render(&viewport.vp),1,0,false);
	expect_calls("staged pass, summary says blank",renderer,0);
	state.render.blank_summaries.clear();
	redraw_viewport_tile(state,&renderer,make_render(&viewport.vp),1,0,false);
	expect_calls("staged pass, no summary",renderer,1);
	take_summary(state,&viewport.vp);
	redraw_viewport_tile(state,&renderer,make_render(&viewport.vp),1,0,false);
	expect_calls("staged pass, summary says the tile paints",renderer,2);
	}
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	take_summary(state,&viewport.vp);
	viewport.words[13][index]=7;
	draw_interface_only(state,&renderer,&viewport.vp,1,0);
	expect_calls("interface pass, summary says blank",renderer,0);
	state.render.blank_summaries.clear();
	draw_interface_only(state,&renderer,&viewport.vp,1,0);
	expect_calls("interface pass, no summary",renderer,1);
	take_summary(state,&viewport.vp);
	draw_interface_only(state,&renderer,&viewport.vp,1,0);
	expect_calls("interface pass, summary says the tile paints",renderer,2);
	}
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	const std::unordered_map<int32_t,uint16_t> selected;
	take_summary(state,&viewport.vp);
	viewport.words[13][index]=7;
	redraw_above(
		state,&renderer,&viewport.vp,1,0,visual_render_groupst::vehicle,selected);
	expect_calls("above pass, summary says blank",renderer,0);
	state.render.blank_summaries.clear();
	redraw_above(
		state,&renderer,&viewport.vp,1,0,visual_render_groupst::vehicle,selected);
	expect_calls("above pass, no summary",renderer,1);
	take_summary(state,&viewport.vp);
	redraw_above(
		state,&renderer,&viewport.vp,1,0,visual_render_groupst::vehicle,selected);
	expect_calls("above pass, summary says the tile paints",renderer,2);
	}
}

void test_interface_pass_needs_every_array()
{
	viewportst viewport;
	plugin_statest state;
	// The pass zeroes everything under the shading, so the top shadow, which it leaves alone
	// on purpose, is what is left for it to paint.
	viewport.words[13][viewportst::index_of(1,0)]=7;
	{
	recording_rendererst renderer;
	draw_interface_only(state,&renderer,&viewport.vp,1,0);
	expect_calls("interface pass, arrays all there",renderer,1);
	}
	{
	// The pass zeroes fifteen named arrays and restores them, so one of them missing would
	// be a null dereference rather than a smaller repaint. It refuses instead.
	recording_rendererst renderer;
	viewport.vp.screentexpos_signpost=nullptr;
	draw_interface_only(state,&renderer,&viewport.vp,1,0);
	expect_calls("interface pass, an array missing",renderer,0);
	viewport.vp.screentexpos_signpost=viewport.words[14].data();
	}
}

void test_selected_layers_are_hidden()
{
	// The only thing on the tile is a creature in the centre layer, and that creature is one
	// the plugin is painting itself this frame: the repaint has to leave the layer blank, and
	// then there is nothing left to paint, so the repaint is not asked for at all. This is
	// the coverage of the viewport's own proxies reaching the pass through selected_mask.
	const int32_t index=viewportst::index_of(1,0);
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[8][index]=7;
	redraw_viewport_tile(state,&renderer,make_render(&viewport.vp),1,0,false);
	expect_calls("centre creature, not covered by a proxy",renderer,1);
	}
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[8][index]=7;
	viewport_renderst render=make_render(&viewport.vp);
	render.coverage.selected[index]=visual_layer_bit(viewport_visual_layer::center);
	redraw_viewport_tile(state,&renderer,render,1,0,false);
	expect_calls("centre creature, covered by a proxy",renderer,0);
	}
}

void test_above_pass_takes_the_group()
{
	// redraw_above hides every layer drawn up to and including the group whose sprites have
	// just been painted. With a creature in the centre layer, which belongs to the main
	// group, asking for everything above `vehicle` still paints it and asking for everything
	// above `main` does not. What each group hides is tile_repaint.h's business and its own
	// test's; this is only that the group reaches it.
	const int32_t index=viewportst::index_of(1,0);
	const std::unordered_map<int32_t,uint16_t> selected;
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[8][index]=7;
	redraw_above(
		state,&renderer,&viewport.vp,1,0,visual_render_groupst::vehicle,selected);
	expect_calls("above the vehicle group",renderer,1);
	}
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[8][index]=7;
	redraw_above(
		state,&renderer,&viewport.vp,1,0,visual_render_groupst::main,selected);
	expect_calls("above the main group",renderer,0);
	}
	{
	// The pass hides the layers its group covers and the layers the proxies cover on top of
	// those, so the same creature is left out when a proxy of this frame is standing on it
	// even though the vehicle group does not reach its layer.
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[8][index]=7;
	const std::unordered_map<int32_t,uint16_t> covered={
		{index,visual_layer_bit(viewport_visual_layer::center)}};
	redraw_above(
		state,&renderer,&viewport.vp,1,0,visual_render_groupst::vehicle,covered);
	expect_calls("above the vehicle group, centre covered by a proxy",renderer,0);
	}
}

void test_interface_shading_is_deferred_on_a_staged_tile()
{
	// A tile whose only content is the interface shading, which is what a tile of a level
	// below the camera looks like. A staged tile has sprites drawn over it afterwards and its
	// shading is put back by the interface pass, once, so the staged repaint leaves it out and
	// there is then nothing left to paint. An unstaged tile keeps it and is painted.
	const int32_t index=viewportst::index_of(1,0);
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[19][index]=7;
	redraw_viewport_tile(state,&renderer,make_render(&viewport.vp),1,0,false);
	expect_calls("shading alone, not staged",renderer,1);
	}
	{
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[19][index]=7;
	redraw_viewport_tile(state,&renderer,make_render(&viewport.vp),1,0,true);
	expect_calls("shading alone, staged",renderer,0);
	}
	{
	// The world-tile pass hands the deferral on: the same tile, staged, is repainted by the
	// lowest viewport with its shading left out, so again there is nothing left to paint.
	viewportst lower,upper;
	plugin_statest state;
	recording_rendererst renderer;
	lower.words[19][index]=7;upper.words[19][index]=7;
	const std::vector<viewport_renderst> viewports={
		make_render(&lower.vp),make_render(&upper.vp)};
	const tile_coveragest staged={{1,0}};
	redraw_world_tile(state,&renderer,viewports,staged,1,0);
	expect_calls("shading alone, staged world tile",renderer,0);
	}
	{
	// Reapplying the viewports above a staged tile is the same deferral: those tiles are all
	// staged, so redraw_viewport_tiles asks for them with the shading left out.
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[19][index]=7;
	const tile_coveragest coverage={{1,0}};
	redraw_viewport_tiles(state,&renderer,make_render(&viewport.vp),coverage);
	expect_calls("shading alone, reapplied over a staged tile",renderer,0);
	}
}

void test_world_tile_across_viewports()
{
	// A world tile is drawn by every viewport on screen, lowest z-level first. An unstaged
	// tile is repainted once per viewport that covers it; a staged one belongs to the lowest
	// viewport alone, because the sprites are drawn over it afterwards and the viewports
	// above it are reapplied by draw_viewport_movement_stages instead.
	const int32_t index=viewportst::index_of(1,0);
	const auto renders=[&](viewportst &lower,viewportst &upper)
		{
		return std::vector<viewport_renderst>{
			make_render(&lower.vp),make_render(&upper.vp)};
		};
	{
	viewportst lower,upper;
	plugin_statest state;
	recording_rendererst renderer;
	lower.words[0][index]=7;upper.words[0][index]=7;
	redraw_world_tile(state,&renderer,renders(lower,upper),tile_coveragest{},1,0);
	expect_calls("unstaged tile, two viewports",renderer,2);
	}
	{
	viewportst lower,upper;
	plugin_statest state;
	recording_rendererst renderer;
	lower.words[0][index]=7;upper.words[0][index]=7;
	const tile_coveragest staged={{1,0}};
	redraw_world_tile(state,&renderer,renders(lower,upper),staged,1,0);
	expect_calls("staged tile, two viewports",renderer,1);
	if(renderer.calls.size()==1&&renderer.calls[0].viewport!=&lower.vp)
		printf("staged tile: repainted by the upper viewport, expected the lower\n"),
			++failures;
	}
	{
	// A viewport whose clip does not reach the tile is skipped. Unstaged, the one below it
	// still paints.
	viewportst lower,upper;
	plugin_statest state;
	recording_rendererst renderer;
	lower.words[0][index]=7;upper.words[0][index]=7;
	lower.vp.clipx[1]=0;
	redraw_world_tile(state,&renderer,renders(lower,upper),tile_coveragest{},1,0);
	expect_calls("unstaged tile, lower viewport clipped out",renderer,1);
	if(renderer.calls.size()==1&&renderer.calls[0].viewport!=&upper.vp)
		printf("clipped lower viewport: the wrong viewport painted\n"),++failures;
	}
	{
	// Staged, it is not: the loop stops at the lowest viewport whether or not that viewport
	// covered the tile, so a staged tile the lowest viewport has clipped away is left to the
	// stage pass rather than handed to the viewport above.
	viewportst lower,upper;
	plugin_statest state;
	recording_rendererst renderer;
	lower.words[0][index]=7;upper.words[0][index]=7;
	lower.vp.clipx[1]=0;
	const tile_coveragest staged={{1,0}};
	redraw_world_tile(state,&renderer,renders(lower,upper),staged,1,0);
	expect_calls("staged tile, lower viewport clipped out",renderer,0);
	}
}

void test_viewport_tiles_skips_what_it_cannot_paint()
{
	// The coverage is in world tiles and is shared by every viewport, so a viewport that
	// does not reach one of them must not be asked to repaint it: the repaint would write a
	// zero off the end of as many as twenty-five arrays.
	viewportst viewport;
	plugin_statest state;
	recording_rendererst renderer;
	viewport.words[0][viewportst::index_of(0,1)]=7;
	viewport.vp.clipx[1]=0;
	const tile_coveragest coverage={{0,1},{1,1},{5,1}};
	redraw_viewport_tiles(state,&renderer,make_render(&viewport.vp),coverage);
	expect_calls("viewport tiles, two of three out of reach",renderer,1);
	if(renderer.calls.size()==1)expect_tile("viewport tiles",renderer.calls[0],0,1);
}

} // namespace

int main()
{
	test_counts_every_ask();
	test_skips_a_tile_that_paints_nothing();
	test_blank_summary_gates_every_pass();
	test_interface_pass_needs_every_array();
	test_selected_layers_are_hidden();
	test_above_pass_takes_the_group();
	test_interface_shading_is_deferred_on_a_staged_tile();
	test_world_tile_across_viewports();
	test_viewport_tiles_skips_what_it_cannot_paint();
	if(failures!=0){printf("tile redraw tests: %d failures\n",failures);return 1;}
	printf("tile redraw tests: OK\n");
	return 0;
}
