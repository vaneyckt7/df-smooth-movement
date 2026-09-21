// SPDX-License-Identifier: MIT
// Checks viewport_collection.h against stub viewports driven by the real animation
// manager: which of the game's nine viewports a frame may read and in what order, what
// the manager is told about each of them, which sprites each viewport contributes and
// which tiles those sprites cover, and which creature in view carries a hauled item icon
// and where that icon travels. The stub viewport below binds every per-tile array the
// header reads to storage of its own, so a case can walk a creature across one the way
// the game does -- a sprite leaves one tile and turns up on the next -- and leave the
// real manager to work the movement out.
//
// Thirty-nine mutations of viewport_collection.h were run against this suite and against
// the five recorded replays. The suite catches all thirty-nine; the replays catch
// twenty-two, and the seventeen they miss are why this suite is worth having. Every one of
// them is something the recorded scenes happen not to hold, measured on the recordings
// themselves.
//
// All nine viewports are readable on every frame of all five recordings, in the same
// order every frame, and only the main one -- the last of the nine -- ever holds a
// sprite. So reversing the eight lower viewports among themselves changes nothing that is
// drawn, while reversing all nine with the main one among them is caught; and neither
// ignoring a viewport's active flag, nor ignoring whether the game has freed its arrays,
// nor dropping the readable check on the lower slots altogether changes a recorded frame,
// because no recorded frame holds a viewport that any of those checks would turn away.
//
// Three of the things the manager is told never vary inside a recording. The map scroll
// is the same on every frame of each of the five, so swapping the pan hint's x and y
// cannot show: the hint speaks only when it changes. The view context's revision is 1 on
// every frame of all five, so a revision hard-coded to zero is the same constant from the
// manager's side. The simulation tick advances on every frame but two, both in
// fortress-carried-400, so reporting it as unknown, which says the world advanced, is the
// answer it would have given anyway. Handing the current background in where the previous
// one belongs is read in two places and shows in neither: the signature that tells a
// repeated viewport from a redrawn one still changes whenever the current array does, and
// the score that tells a map scroll from a screenful of creatures stepping the same way
// is asked only when the scroll changed.
//
// Seven more are the icon path. Every icon in fortress-carried-400 finds the creature
// carrying it among the sprites of the viewport the icon is drawn over, and that settles
// afterwards what the icon's own overshoot flag and travelled fraction only guess at, so
// forcing that flag on, forcing it off and reporting the step as fully travelled are all
// three invisible there. The clip is the whole viewport on every frame of all five, and
// the creatures a frame considers come from a box the size of that viewport, so dropping
// the bounds and clip check on a creature's own tile, and dropping it again on the tiles
// its icon covers, turn nothing away that the recordings offer; covering the target tile
// alone leaves the tile the creature came from repainted by the creature's own sprite
// anyway; and every hauled item's texture is in the game's cache already, so never
// staging a tile to make one costs nothing there.
//
// The last two are the question a frame asks before it paints at all: whether any
// viewport holds a creature facing the way the tileset does not draw. Answering that with
// a flat no, or asking only the first viewport, changes no recorded frame, because every
// frame that holds such a creature is a frame the plugin was already painting for another
// reason; answering with a flat yes, which paints frames that would have been skipped,
// shows up on sixty-one of them.

#include "viewport_collection.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

int failures=0;
using L=viewport_visual_layer;
using tilest=std::set<std::pair<int32_t,int32_t>>;

// The viewport is deliberately not square: a tile index worked out from the wrong dimension
// lands on a different tile, and a swapped pair of dimensions is a different viewport.
constexpr int32_t dim_x=6,dim_y=4,tile_count=dim_x*dim_y;
// The map scroll. Both are non-zero and differ from each other, so that a position the
// window was never subtracted from, or subtracted the wrong way round, falls off the
// viewport instead of landing on a tile that happens to work.
constexpr int32_t window_x_tiles=100,window_y_tiles=200;
constexpr int32_t center_texpos=11,other_texpos=12,still_texpos=13;
constexpr int32_t item_texpos_value=71,second_item_texpos=72;
// A creature steps one tile east along `row`; another rests at `still_x`,`still_y`. No tile
// either of them stands on has the same x as y, and the two rows differ, so a source tile
// that took its x from the y, or the other way round, is a tile the expectations reject.
constexpr int32_t from_x=2,to_x=3,row=1,still_x=1,still_y=3;
constexpr uint32_t half_step_ms=visual_animation_managerst::default_step_duration_ms/2;

std::string tiles_text(const tilest &tiles)
{
	std::string out;
	for(const auto &[x,y]:tiles)out+="("+std::to_string(x)+","+std::to_string(y)+")";
	return out.empty()?"(none)":out;
}

// A texture is compared and handed on, never read, so any distinct address is one.
SDL_Texture *fake_texture(int32_t texpos)
{
	static char textures[256];
	return reinterpret_cast<SDL_Texture *>(&textures[texpos&0xff]);
}

// The game's key for a texture, written out here rather than taken from the header so that
// a key the header builds differently is a miss (see test_texture_cache.cpp).
df::texture_fullid key(int32_t texpos)
{
	df::texture_fullid id;
	id.texpos=texpos;
	id.r=id.g=id.b=1.0f;
	id.br=id.bg=id.bb=0.0f;
	id.flag=uint32_t(df::texture_fullid_flag::mask_transparent_background);
	return id;
}

// The two things this header asks of a renderer: the texture cache the game fills as it
// draws, and the tile repaint that fills it. This one fills the cache the way the game does,
// with a texture for whatever the tile says at the moment of the repaint.
struct rendererst
{
	struct { std::unordered_map<df::texture_fullid,void *> tile_cache; } tile_cache;
	std::vector<std::pair<int32_t,int32_t>> calls;
	bool fills_the_cache=true;

	void cache(int32_t texpos){tile_cache.tile_cache[key(texpos)]=fake_texture(texpos);}

	void update_viewport_tile(df::graphic_viewportst *vp,int32_t x,int32_t y)
		{
		calls.push_back({x,y});
		if(!fills_the_cache)return;
		cache(vp->screentexpos_background_two[x*vp->dim_y+y]);
		}
};

// A viewport with every per-tile array the header reads bound to storage of its own: the
// nine layers current and previous, the background the manager compares, and the layer the
// icon path stages an item on.
struct viewportst
{
	df::graphic_viewportst vp{};
	std::vector<int32_t> current[visual_layer_count];
	std::vector<int32_t> previous[visual_layer_count];
	std::vector<int32_t> background,background_old,background_two;

	viewportst()
		{
		vp.flag.bits.active=1;
		vp.dim_x=dim_x;vp.dim_y=dim_y;
		vp.clipx={0,dim_x-1};vp.clipy={0,dim_y-1};
		for(auto &layer:current)layer.assign(tile_count,0);
		for(auto &layer:previous)layer.assign(tile_count,0);
		background.assign(tile_count,0);
		background_old.assign(tile_count,0);
		background_two.assign(tile_count,0);
		vp.screentexpos_background=background.data();
		vp.screentexpos_background_old=background_old.data();
		vp.screentexpos_background_two=background_two.data();
		for(const auto &entry:viewport_layer_tablest<df::graphic_viewportst>::entries)
			{
			vp.*entry.current=current[size_t(entry.layer)].data();
			vp.*entry.previous=previous[size_t(entry.layer)].data();
			}
		}

	int32_t &at(L layer,int32_t x,int32_t y){return current[size_t(layer)][x*dim_y+y];}

	// Ends the frame: what was current is now previous.
	void advance()
		{
		for(size_t layer=0;layer<visual_layer_count;++layer)previous[layer]=current[layer];
		}
};

// A creature the game lists in view, hauling one item. The plugin works a creature's tile
// out by subtracting the map scroll from its world position, so a creature asked for on
// tile (x,y) is put at the window plus that tile.
struct carrierst
{
	df::material material;
	df::item item;
	df::unit_inventory_item entry;
	df::unit unit;

	carrierst(
		int32_t x,
		int32_t y,
		int32_t texpos,
		df::inv_item_role_type role=df::inv_item_role_type::Hauled)
		{
		material.boulder_texpos1=texpos;
		item.type=df::item_type::BOULDER;
		item.harness_material=&material;
		entry={&item,role};
		unit.pos.x=int16_t(window_x_tiles+x);
		unit.pos.y=int16_t(window_y_tiles+y);
		unit.inventory={&entry};
		}

	// The entry points at this object's own item, so a copy would haul the original's.
	carrierst(const carrierst &)=delete;
	carrierst &operator=(const carrierst &)=delete;
};

void select(plugin_statest &state,const char *name)
{
	movementst *movement=state.movements.find(name);
	if(movement==nullptr){printf("no movement named %s\n",name);++failures;return;}
	state.render.animation_manager.set_movement(*movement);
}

// One frame the way render_world_with_movement runs it: open the frame, tell the manager
// about every viewport, close it.
void run_frame(
	plugin_statest &state,
	const std::vector<viewportst *> &viewports,
	uint32_t now_ms)
{
	state.render.animation_manager.begin_frame(now_ms);
	for(viewportst *v:viewports)
		state.render.animation_manager.synchronize_viewport(
			animation_input(state,&v->vp,window_x_tiles,window_y_tiles));
	state.render.animation_manager.end_frame();
}

struct stepst
{
	int32_t from_x;
	int32_t to_x;
	int32_t y;
	int32_t texpos;
};

// Steps one creature per viewport across the pair of frames the manager needs to see a move
// at all, then advances the clock to the middle of the step, where a sprite on the straight
// path is half a tile from where it started. `steps` is index-matched to `viewports`.
void walk(
	plugin_statest &state,
	const std::vector<viewportst *> &viewports,
	const std::vector<stepst> &steps)
{
	if(steps.size()!=viewports.size()){printf("walk: one step per viewport\n");++failures;return;}
	for(size_t i=0;i<viewports.size();++i)
		viewports[i]->at(L::center,steps[i].from_x,steps[i].y)=steps[i].texpos;
	run_frame(state,viewports,1000);
	for(viewportst *v:viewports)v->advance();
	for(size_t i=0;i<viewports.size();++i)
		{
		viewports[i]->at(L::center,steps[i].from_x,steps[i].y)=0;
		viewports[i]->at(L::center,steps[i].to_x,steps[i].y)=steps[i].texpos;
		}
	run_frame(state,viewports,1016);
	run_frame(state,viewports,1016+half_step_ms);
}

void expect(const char *name,bool ok)
{
	if(!ok)printf("%s\n",name),++failures;
}

void expect_value(const char *name,long long got,long long expected)
{
	if(got!=expected)printf("%s: %lld, expected %lld\n",name,got,expected),++failures;
}

void expect_float(const char *name,float got,float expected)
{
	if(std::fabs(got-expected)>0.001f)
		printf("%s: %g, expected %g\n",name,double(got),double(expected)),++failures;
}

template<typename T>
void expect_pointer(const char *name,const T *got,const T *expected)
{
	if(got!=expected)
		printf("%s: %p, expected %p\n",name,(const void *)got,(const void *)expected),++failures;
}

void expect_tiles(const char *name,const tilest &got,const tilest &expected)
{
	if(got!=expected)
		printf("%s: covers %s\n  expected %s\n",name,tiles_text(got).c_str(),
			tiles_text(expected).c_str()),++failures;
}

void expect_viewports(
	const char *name,
	const std::vector<df::graphic_viewportst *> &got,
	const std::vector<df::graphic_viewportst *> &expected)
{
	if(got.size()!=expected.size())
		{
		printf("%s: %zu viewports, expected %zu\n",name,got.size(),expected.size());
		++failures;
		return;
		}
	for(size_t i=0;i<got.size();++i)
		if(got[i]!=expected[i])
			printf("%s: viewport %zu is %p, expected %p\n",name,i,
				(const void *)got[i],(const void *)expected[i]),++failures;
}

void test_animation_input()
{
	plugin_statest state;
	viewportst v;
	// A revision the tracker has moved on from its start, and a simulation tick that is not
	// the "unknown" default: both are read off the state, and both have to arrive.
	state.render.view_context.bump();
	state.render.view_context.bump();
	state.render.drawn_arrays.frame_simulation_tick=4242;
	const viewport_visual_animation_inputst in=
		animation_input(state,&v.vp,window_x_tiles,window_y_tiles);
	expect_pointer("viewport",(const df::graphic_viewportst *)in.viewport,&v.vp);
	expect_value("dim_x",in.dim_x,dim_x);
	expect_value("dim_y",in.dim_y,dim_y);
	expect_value("context revision",(long long)in.context_revision,2);
	expect_value("simulation tick",(long long)in.simulation_tick,4242);
	// The map scroll goes in as the manager's pan hint, which is how a panned frame is told
	// from a frame every creature stepped on.
	expect_value("pan x",in.pan_x,window_x_tiles);
	expect_value("pan y",in.pan_y,window_y_tiles);
	for(size_t layer=0;layer<visual_layer_count;++layer)
		{
		expect_pointer("current layer",in.current[layer],
			(const int32_t *)v.current[layer].data());
		expect_pointer("previous layer",in.previous[layer],
			(const int32_t *)v.previous[layer].data());
		}
	// The background is the one array compared against its own previous copy rather than a
	// layer's; handing the same array in twice would say it never changed.
	expect_pointer("current background",in.current_background,
		(const int32_t *)v.background.data());
	expect_pointer("previous background",in.previous_background,
		(const int32_t *)v.background_old.data());
	expect("a fully bound viewport is valid",in.valid());
}

void test_viewport_readable()
{
	plugin_statest state;
	{
	viewportst v;
	expect("a bound active viewport",viewport_readable(state,&v.vp,0,0));
	}
	{
	// The game frees a viewport's arrays without clearing its active flag, so the flag on
	// its own is not an answer -- and neither is a viewport that is there but switched off.
	viewportst v;
	v.vp.flag.bits.active=0;
	expect("inactive",!viewport_readable(state,&v.vp,0,0));
	}
	{
	viewportst v;
	v.vp.screentexpos_up_creature=nullptr;
	expect("a freed current layer",!viewport_readable(state,&v.vp,0,0));
	}
	{
	viewportst v;
	v.vp.screentexpos_old=nullptr;
	expect("a freed previous layer",!viewport_readable(state,&v.vp,0,0));
	}
	{
	// Dimensions the game has not filled in yet: every tile index off them is out of range.
	viewportst v;
	v.vp.dim_y=0;
	expect("no dimensions",!viewport_readable(state,&v.vp,0,0));
	}
	expect("no viewport",!viewport_readable(state,nullptr,0,0));
}

void test_active_viewports()
{
	plugin_statest state;
	expect_viewports("no graphics",active_viewports(state,nullptr,0,0),{});
	{
	// The main viewport alone, which is every frame above the lowest z-level shown.
	df::graphic gps;
	viewportst main;
	gps.main_viewport=&main.vp;
	expect_viewports("the main viewport alone",
		active_viewports(state,&gps,0,0),{&main.vp});
	}
	{
	// Stacked viewports come back lowest z-level first and the main one last, which is the
	// order the game draws them in and the order a lower level's sprite must be painted in
	// to end up under the next level's fog.
	df::graphic gps;
	viewportst deep,middle,shallow,main;
	gps.lower_viewport[7]=&deep.vp;
	gps.lower_viewport[3]=&middle.vp;
	gps.lower_viewport[0]=&shallow.vp;
	gps.main_viewport=&main.vp;
	expect_viewports("lowest first",active_viewports(state,&gps,0,0),
		{&deep.vp,&middle.vp,&shallow.vp,&main.vp});
	}
	{
	// A slot the game left empty, one whose arrays it has freed, and one it switched off
	// are all passed over; the rest of the stack is still collected.
	df::graphic gps;
	viewportst deep,freed,off,main;
	freed.vp.screentexpos=nullptr;
	off.vp.flag.bits.active=0;
	gps.lower_viewport[7]=&deep.vp;
	gps.lower_viewport[5]=&freed.vp;
	gps.lower_viewport[2]=&off.vp;
	gps.main_viewport=&main.vp;
	expect_viewports("unreadable levels skipped",active_viewports(state,&gps,0,0),
		{&deep.vp,&main.vp});
	}
	{
	// An unreadable main viewport is skipped like any other, and the stack under it stands.
	df::graphic gps;
	viewportst deep,main;
	main.vp.screentexpos_designation_old=nullptr;
	gps.lower_viewport[4]=&deep.vp;
	gps.main_viewport=&main.vp;
	expect_viewports("an unreadable main viewport",active_viewports(state,&gps,0,0),
		{&deep.vp});
	}
	{
	df::graphic gps;
	expect_viewports("an empty stack",active_viewports(state,&gps,0,0),{});
	}
}

void expect_carried(
	const char *name,
	const std::vector<carried_item_proxyst> &proxies,
	size_t index,
	float source_x_tiles,
	float source_y_tiles,
	int32_t target_x,
	int32_t target_y,
	float offset_x_tiles,
	float offset_y_tiles,
	float travelled_pct,
	SDL_Texture *texture,
	const tilest &coverage)
{
	if(proxies.size()<=index){printf("%s: no proxy %zu\n",name,index);++failures;return;}
	const carried_item_proxyst &proxy=proxies[index];
	expect_float(name,proxy.source_x_tiles,source_x_tiles);
	expect_float(name,proxy.source_y_tiles,source_y_tiles);
	expect_value(name,proxy.target_x,target_x);
	expect_value(name,proxy.target_y,target_y);
	expect_float(name,proxy.offset_x_tiles,offset_x_tiles);
	expect_float(name,proxy.offset_y_tiles,offset_y_tiles);
	expect_float(name,proxy.travelled_pct,travelled_pct);
	expect_pointer(name,proxy.texture,texture);
	expect_tiles(name,proxy.coverage,coverage);
}

void test_carried_items()
{
	{
	// A creature halfway through a step east, hauling a boulder the game has already drawn
	// somewhere this session: the icon rides the creature's movement, covering the tile it
	// came from and the one it is going to.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	carrierst carrier(to_x,row,item_texpos_value);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles);
	expect_value("one carrier, proxies",(long long)proxies.size(),1);
	expect_carried("moving carrier",proxies,0,float(from_x),float(row),to_x,row,
		0.5f,0.0f,0.5f,fake_texture(item_texpos_value),{{from_x,row},{to_x,row}});
	expect("moving carrier: rides the movement",!proxies[0].use_straight_path);
	expect_value("moving carrier: tiles staged",(long long)renderer.calls.size(),0);
	}
	{
	// With a movement that leaves the straight path, the icon is marked to take the path
	// instead -- until a creature proxy on the same tile proves the icon rides along.
	plugin_statest state;
	select(state,"hop");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	carrierst carrier(to_x,row,item_texpos_value);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles);
	expect_value("hopping carrier, proxies",(long long)proxies.size(),1);
	if(proxies.size()==1)
		expect("hopping carrier: takes the straight path",proxies[0].use_straight_path);
	}
	{
	// A creature standing still carries its icon on its own tile, with nothing to travel.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	v.at(L::center,still_x,still_y)=still_texpos;
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	carrierst carrier(still_x,still_y,item_texpos_value);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles);
	expect_value("still carrier, proxies",(long long)proxies.size(),1);
	expect_carried("still carrier",proxies,0,float(still_x),float(still_y),still_x,still_y,
		0.0f,0.0f,1.0f,fake_texture(item_texpos_value),{{still_x,still_y}});
	}
	{
	// Every creature in the list gets its own proxy, in the order the list gives them, and
	// each one's icon is its own item's.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	renderer.cache(second_item_texpos);
	v.at(L::center,still_x,still_y)=still_texpos;
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	carrierst moving(to_x,row,item_texpos_value);
	carrierst standing(still_x,still_y,second_item_texpos);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&standing.unit,&moving.unit},window_x_tiles,window_y_tiles);
	expect_value("two carriers, proxies",(long long)proxies.size(),2);
	expect_carried("two carriers, first listed",proxies,0,
		float(still_x),float(still_y),still_x,still_y,0.0f,0.0f,1.0f,
		fake_texture(second_item_texpos),{{still_x,still_y}});
	expect_carried("two carriers, second listed",proxies,1,
		float(from_x),float(row),to_x,row,0.5f,0.0f,0.5f,
		fake_texture(item_texpos_value),{{from_x,row},{to_x,row}});
	}
	{
	// The icon rides the movement of the creature the game drew on the tile, so the layer
	// the movement is asked for is the creature's own. Asked for on the item layer instead,
	// the manager finds no movement of that layer and falls back to what the creatures
	// around the tile are doing; here one of them is stepping another way, which the manager
	// calls ambiguous and answers with no movement at all, leaving the icon standing still
	// on a creature that is walking.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	const int32_t beside_x=to_x+1,beside_from_y=row-1;
	v.at(L::center,from_x,row)=center_texpos;
	v.at(L::center,beside_x,beside_from_y)=other_texpos;
	run_frame(state,{&v},1000);
	v.advance();
	v.at(L::center,from_x,row)=0;
	v.at(L::center,to_x,row)=center_texpos;
	v.at(L::center,beside_x,beside_from_y)=0;
	v.at(L::center,beside_x,row)=other_texpos;
	run_frame(state,{&v},1016);
	run_frame(state,{&v},1016+half_step_ms);
	carrierst carrier(to_x,row,item_texpos_value);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles);
	expect_value("a creature stepping another way alongside, proxies",
		(long long)proxies.size(),1);
	expect_carried("a creature stepping another way alongside",proxies,0,
		float(from_x),float(row),to_x,row,0.5f,0.0f,0.5f,
		fake_texture(item_texpos_value),{{from_x,row},{to_x,row}});
	}
}

void test_carried_items_passed_over()
{
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	v.at(L::center,still_x,still_y)=still_texpos;
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	const auto collect=[&](const std::vector<df::unit *> &units)
		{
		return collect_carried_item_proxies(
			state,&renderer,&v.vp,units,window_x_tiles,window_y_tiles);
		};
	{
	// A world position the map scroll was never taken off: this creature is standing on the
	// tile the plugin would draw its icon on if it read world positions as viewport tiles.
	df::unit unscrolled;
	unscrolled.pos.x=int16_t(to_x);unscrolled.pos.y=int16_t(row);
	carrierst carrier(to_x,row,item_texpos_value);
	unscrolled.inventory=carrier.unit.inventory;
	expect_value("a position the window was not taken off",
		(long long)collect({&unscrolled}).size(),0);
	}
	{
	// Off the end of the viewport, and behind its start: reading either tile runs off the
	// per-tile arrays.
	carrierst past_the_end(dim_x,row,item_texpos_value);
	carrierst before_the_start(-1,row,item_texpos_value);
	carrierst below(to_x,dim_y,item_texpos_value);
	expect_value("off the viewport",
		(long long)collect({&past_the_end.unit,&before_the_start.unit,&below.unit}).size(),0);
	}
	{
	// A tile with no creature drawn on it. The unit list is the game's, and it holds
	// creatures the viewport is not showing -- ones hidden behind a wall, or on another
	// z-level -- so the tile's own sprite is what says a creature is there to hang an icon
	// on. Nothing is staged for one either: staging is a whole tile's repaint.
	carrierst nowhere(0,0,item_texpos_value);
	expect_value("no creature on the tile",(long long)collect({&nowhere.unit}).size(),0);
	expect_value("no creature on the tile, tiles staged",(long long)renderer.calls.size(),0);
	}
	{
	// Carrying nothing, and wearing rather than hauling: neither names a sprite to draw.
	df::unit empty_handed;
	empty_handed.pos.x=int16_t(window_x_tiles+to_x);
	empty_handed.pos.y=int16_t(window_y_tiles+row);
	carrierst wearing(to_x,row,item_texpos_value,df::inv_item_role_type::Worn);
	expect_value("nothing hauled",
		(long long)collect({&empty_handed,&wearing.unit}).size(),0);
	expect_value("nothing hauled, tiles staged",(long long)renderer.calls.size(),0);
	}
	{
	// An item whose material names no sprite at all.
	carrierst nameless(to_x,row,0);
	expect_value("an item with no sprite",(long long)collect({&nameless.unit}).size(),0);
	expect_value("an item with no sprite, tiles staged",(long long)renderer.calls.size(),0);
	}
	{
	// A creature on a tile the game is not drawing this pass: inside the viewport's arrays,
	// so reading it is safe, but outside the clip the game set from what is on screen, so
	// an icon painted on it would be painted outside the map.
	const auto clip=v.vp.clipx;
	v.vp.clipx={0,from_x};
	carrierst outside_the_clip(to_x,row,item_texpos_value);
	expect_value("outside the clip",(long long)collect({&outside_the_clip.unit}).size(),0);
	expect_value("outside the clip, tiles staged",(long long)renderer.calls.size(),0);
	v.vp.clipx=clip;
	}
}

void test_carried_item_coverage()
{
	// The icon is drawn between the tile its carrier came from and the tile it is going to,
	// so both are repainted under it; a tile outside the clip is not the plugin's to touch.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(item_texpos_value);
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	v.vp.clipx={to_x,dim_x-1};
	carrierst carrier(to_x,row,item_texpos_value);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles);
	expect_value("a source outside the clip, proxies",(long long)proxies.size(),1);
	expect_carried("a source outside the clip",proxies,0,float(from_x),float(row),to_x,row,
		0.5f,0.0f,0.5f,fake_texture(item_texpos_value),{{to_x,row}});
}

void test_carried_item_staging()
{
	{
	// A hauled item the game has never drawn: there is no texture for it until a tile is
	// staged with the item on it and repainted, which is the creature's own tile.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	carrierst carrier(to_x,row,item_texpos_value);
	const auto proxies=collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles);
	expect_value("an uncached item, proxies",(long long)proxies.size(),1);
	expect_value("an uncached item, tiles staged",(long long)renderer.calls.size(),1);
	if(renderer.calls.size()==1)
		{
		expect_value("an uncached item, staged x",renderer.calls[0].first,to_x);
		expect_value("an uncached item, staged y",renderer.calls[0].second,row);
		}
	if(!proxies.empty())
		expect_pointer("an uncached item, texture",
			proxies[0].texture,fake_texture(item_texpos_value));
	// And the tile is put back: the frame it was borrowed from is still to be drawn.
	expect_value("an uncached item, the tile afterwards",
		v.background_two[to_x*dim_y+row],0);
	expect_value("an uncached item, repaints counted",
		(long long)state.stats.repaints.load(),1);
	}
	{
	// A repaint the game makes no texture out of leaves nothing to draw, so no proxy.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.fills_the_cache=false;
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	carrierst carrier(to_x,row,item_texpos_value);
	expect_value("staging made no texture",(long long)collect_carried_item_proxies(
		state,&renderer,&v.vp,{&carrier.unit},window_x_tiles,window_y_tiles).size(),0);
	expect_value("staging made no texture, tiles staged",
		(long long)renderer.calls.size(),1);
	}
}

const render_proxyst *find(const std::vector<render_proxyst> &proxies,L layer)
{
	for(const render_proxyst &proxy:proxies)if(proxy.layer==layer)return &proxy;
	return nullptr;
}

void test_viewport_renders()
{
	{
	// Two stacked viewports, each with a creature of its own stepping east: one render per
	// viewport, in the order they were given, each holding its own viewport's sprites.
	plugin_statest state;
	select(state,"linear");
	viewportst deep,main;
	rendererst renderer;
	renderer.cache(center_texpos);
	renderer.cache(other_texpos);
	walk(state,{&deep,&main},
		{{from_x,to_x,row,other_texpos},{from_x,to_x,row,center_texpos}});
	const auto renders=collect_viewport_renders(state,&renderer,{&deep.vp,&main.vp});
	expect_value("two viewports, renders",(long long)renders.size(),2);
	if(renders.size()!=2)return;
	expect_pointer("the lower render's viewport",renders[0].viewport,&deep.vp);
	expect_pointer("the main render's viewport",renders[1].viewport,&main.vp);
	const render_proxyst *lower=find(renders[0].proxies,L::center);
	const render_proxyst *upper=find(renders[1].proxies,L::center);
	if(lower==nullptr||upper==nullptr)
		{printf("two viewports: a viewport with no centre proxy\n");++failures;return;}
	// Each render's sprites came off its own viewport, not the first one twice.
	expect_value("the lower render's sprite",lower->texpos,other_texpos);
	expect_value("the main render's sprite",upper->texpos,center_texpos);
	expect_pointer("the lower render's texture",lower->texture,fake_texture(other_texpos));
	// The coverage is gathered per viewport: the tiles its own sprites are drawn across,
	// and the layer bit at the tile the sprite lands on, indexed the way the game indexes a
	// per-tile array.
	expect_tiles("the main render's coverage",
		renders[1].coverage.all,{{from_x,row},{to_x,row}});
	expect_value("the main render's selected layers",
		selected_mask(renders[1].coverage.selected,to_x*dim_y+row),
		visual_layer_bit(L::center));
	expect_value("the main render's selected layers, elsewhere",
		selected_mask(renders[1].coverage.selected,from_x*dim_y+row),0);
	}
	{
	// Flipping is the plugin's own setting and has to reach the sweep: a creature facing
	// the mirrored way is drawn mirrored only while it is switched on.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	renderer.cache(center_texpos);
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	const auto plain=collect_viewport_renders(state,&renderer,{&v.vp});
	state.flip_enabled=true;
	const auto flipped=collect_viewport_renders(state,&renderer,{&v.vp});
	const render_proxyst *before=plain.empty()?nullptr:find(plain[0].proxies,L::center);
	const render_proxyst *after=flipped.empty()?nullptr:find(flipped[0].proxies,L::center);
	if(before==nullptr||after==nullptr)
		{printf("flipping: no centre proxy\n");++failures;return;}
	expect("flipping off",!before->mirrored);
	expect("flipping on",after->mirrored);
	}
	{
	// A sprite the game has no texture for is left to the game to draw, which is what the
	// renderer's cache answers; the render is still there, with nothing in it.
	plugin_statest state;
	select(state,"linear");
	viewportst v;
	rendererst renderer;
	walk(state,{&v},{{from_x,to_x,row,center_texpos}});
	const auto renders=collect_viewport_renders(state,&renderer,{&v.vp});
	expect_value("no texture, renders",(long long)renders.size(),1);
	if(renders.size()==1)
		{
		expect_value("no texture, proxies",(long long)renders[0].proxies.size(),0);
		expect_value("no texture, coverage",(long long)renders[0].coverage.all.size(),0);
		}
	}
	{
	// Nothing readable this frame: no renders at all, and the renderer is never asked.
	plugin_statest state;
	rendererst renderer;
	expect_value("no viewports",
		(long long)collect_viewport_renders(state,&renderer,{}).size(),0);
	expect_value("no viewports, tiles staged",(long long)renderer.calls.size(),0);
	}
}

void test_mirrored_facing()
{
	plugin_statest state;
	select(state,"linear");
	viewportst east,west;
	// The sprites the game ships face west, so a creature walking east is the one drawn
	// mirrored; one walking west is drawn as it comes.
	walk(state,{&east,&west},
		{{from_x,to_x,row,center_texpos},{to_x,from_x,row,center_texpos}});
	expect("no viewports",!has_mirrored_viewport_facing(state,{}));
	expect("walking east",has_mirrored_viewport_facing(state,{&east.vp}));
	expect("walking west",!has_mirrored_viewport_facing(state,{&west.vp}));
	// Any one viewport with a mirrored sprite on it is enough, wherever it is in the stack.
	expect("one of two, mirrored last",
		has_mirrored_viewport_facing(state,{&west.vp,&east.vp}));
	expect("one of two, mirrored first",
		has_mirrored_viewport_facing(state,{&east.vp,&west.vp}));
	// A viewport the manager was never told about has no facing on record.
	viewportst unseen;
	expect("a viewport the manager has not seen",
		!has_mirrored_viewport_facing(state,{&unseen.vp}));
}

} // namespace

int main()
{
	test_animation_input();
	test_viewport_readable();
	test_active_viewports();
	test_carried_items();
	test_carried_items_passed_over();
	test_carried_item_coverage();
	test_carried_item_staging();
	test_viewport_renders();
	test_mirrored_facing();
	if(failures!=0){printf("viewport collection tests: %d failures\n",failures);return 1;}
	printf("viewport collection tests: OK\n");
	return 0;
}
