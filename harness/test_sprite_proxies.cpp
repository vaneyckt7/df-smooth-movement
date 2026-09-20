// SPDX-License-Identifier: MIT
// Checks collect_proxies and the coverage helpers against the stub viewport with the real
// animation manager: which sprites get a proxy, what tiles each covers, and what blocks one.
// The recordings exercise the moving cases but have no burning tile, no sprite at the clip
// edge and no missing texture, so those are pinned here.

#include "df/graphic_viewportst.h"
#include "sprite_proxies.h"

#include <cmath>
#include <cstdio>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int32_t dim_x=6,dim_y=4,tile_count=dim_x*dim_y;
constexpr int32_t center_texpos=11,right_texpos=12,up_texpos=13,vehicle_texpos=14;
constexpr int32_t neighbour_texpos=15;
// A creature with a right and an up fragment steps one tile east: the render groups main
// and upper, a fragment whose mirror shifts it two tiles, and one that flips in place.
constexpr int32_t from_x=2,to_x=3,row=2;

int failures=0;
using L=viewport_visual_layer;
using tilest=std::set<std::pair<int32_t,int32_t>>;

std::string tiles_text(const tilest &tiles)
{
	std::string out;
	for(const auto &[x,y]:tiles)out+="("+std::to_string(x)+","+std::to_string(y)+")";
	return out.empty()?"(none)":out;
}

struct viewportst
{
	df::graphic_viewportst vp{};
	std::vector<int32_t> current[visual_layer_count];
	std::vector<int32_t> previous[visual_layer_count];
	std::vector<int32_t> background;
	std::vector<uint32_t> spatter_flags;

	viewportst()
		{
		vp.dim_x=dim_x;vp.dim_y=dim_y;
		vp.clipx={0,dim_x-1};vp.clipy={0,dim_y-1};
		for(auto &w:current)w.assign(tile_count,0);
		for(auto &w:previous)w.assign(tile_count,0);
		background.assign(tile_count,0);
		spatter_flags.assign(tile_count,0);
		vp.screentexpos_background=background.data();
		vp.screentexpos_spatter_flag=spatter_flags.data();
		int k=0;
		for(const auto &descriptor:visual_layer_descriptors)
			{
			const auto &entry=viewport_layer_tablest<df::graphic_viewportst>::entries[k];
			if(entry.layer!=descriptor.layer)
				printf("layer table order differs from the descriptors\n"),++failures;
			vp.*entry.current=current[k].data();
			vp.*entry.previous=previous[k].data();
			++k;
			}
		}

	int32_t &at(L layer,int32_t x,int32_t y){return current[size_t(layer)][x*dim_y+y];}

	// The manager's view of this viewport: the current arrays against the previous ones.
	viewport_visual_animation_inputst input() const
		{
		viewport_visual_animation_inputst in;
		in.viewport=&vp;in.dim_x=dim_x;in.dim_y=dim_y;in.context_revision=1;
		for(size_t k=0;k<visual_layer_count;++k)
			{
			in.current[k]=current[k].data();
			in.previous[k]=previous[k].data();
			}
		in.current_background=background.data();
		in.previous_background=background.data();
		return in;
		}

	// Ends the frame: what was current is now previous.
	void advance()
		{
		for(size_t k=0;k<visual_layer_count;++k)previous[k]=current[k];
		}
};

// Runs the creature's step through the manager: one frame at rest, one frame moved. The
// creature walks along `row`; on row 0 it has no up fragment, since that would be off the
// viewport.
struct scenest
{
	viewportst v;
	visual_animation_managerst manager;
	// The movements the scene can select on its manager: `hop` overshoots above the path.
	movement_sett movements=make_movements();

	// With `vehicle`, a vehicle on the creature's own tile steps east with it.
	// With `neighbour`, a one-tile creature a column to the left on the row above, that is,
	// beside the up fragment, steps east in lockstep too.
	explicit scenest(int32_t row=::row,bool vehicle=false,bool neighbour=false)
		{
		v.at(L::center,from_x,row)=center_texpos;
		v.at(L::right,from_x+1,row)=right_texpos;
		if(row>0)v.at(L::up,from_x,row-1)=up_texpos;
		if(vehicle)v.at(L::vehicle,from_x,row)=vehicle_texpos;
		if(neighbour)v.at(L::center,from_x-1,row-1)=neighbour_texpos;
		manager.begin_frame(1000);
		manager.synchronize_viewport(v.input());
		manager.end_frame();
		v.advance();
		v.at(L::center,from_x,row)=0;v.at(L::right,from_x+1,row)=0;
		if(row>0)v.at(L::up,from_x,row-1)=0;
		v.at(L::center,to_x,row)=center_texpos;
		v.at(L::right,to_x+1,row)=right_texpos;
		if(row>0)v.at(L::up,to_x,row-1)=up_texpos;
		if(vehicle)
			{
			v.at(L::vehicle,from_x,row)=0;
			v.at(L::vehicle,to_x,row)=vehicle_texpos;
			}
		if(neighbour)
			{
			v.at(L::center,from_x-1,row-1)=0;
			v.at(L::center,to_x-1,row-1)=neighbour_texpos;
			}
		manager.begin_frame(1016);
		manager.synchronize_viewport(v.input());
		manager.end_frame();
		}

	// Selects the hop movement, which overshoots into the row above the path.
	void select_hop()
		{
		manager.set_movement(*movements.find("hop"));
		}

	std::vector<render_proxyst> collect(bool flip,int32_t missing_texpos=0)
		{
		return collect_proxies(&v.vp,manager,flip,[missing_texpos](int32_t texpos)
			{
			static int token;
			return texpos==missing_texpos?nullptr:reinterpret_cast<SDL_Texture *>(&token);
			});
		}
};

const render_proxyst *find(const std::vector<render_proxyst> &proxies,L layer)
{
	for(const render_proxyst &proxy:proxies)if(proxy.layer==layer)return &proxy;
	return nullptr;
}

void expect_count(const char *name,const std::vector<render_proxyst> &proxies,size_t count)
{
	if(proxies.size()!=count)
		printf("%s: %zu proxies, expected %zu\n",name,proxies.size(),count),++failures;
}

void expect_proxy(const char *name,const std::vector<render_proxyst> &proxies,L layer,
	int32_t target_x,int32_t target_y,bool mirrored,int32_t mirror_shift,
	const tilest &coverage,int32_t texpos)
{
	const render_proxyst *proxy=find(proxies,layer);
	if(proxy==nullptr){printf("%s: no proxy for the layer\n",name);++failures;return;}
	if(proxy->target_x!=target_x||proxy->target_y!=target_y)
		printf("%s: target (%d,%d)\n",name,proxy->target_x,proxy->target_y),++failures;
	if(proxy->mirrored!=mirrored||proxy->mirror_shift!=mirror_shift)
		printf("%s: mirrored %d shift %d\n",name,proxy->mirrored,proxy->mirror_shift),++failures;
	if(proxy->coverage!=coverage)
		printf("%s: covers %s\n  expected %s\n",name,tiles_text(proxy->coverage).c_str(),
			tiles_text(coverage).c_str()),++failures;
	if(proxy->texpos!=texpos||proxy->texture==nullptr)
		printf("%s: texpos %d\n",name,proxy->texpos),++failures;
}

void test_moving()
{
	{
	scenest scene;
	const auto proxies=scene.collect(false);
	expect_count("flip off",proxies,3);
	expect_proxy("flip off, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("flip off, right",proxies,L::right,to_x+1,row,false,0,
		{{from_x+1,row},{to_x+1,row}},right_texpos);
	expect_proxy("flip off, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-1},{to_x,row-1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		{
		if(proxy.source_x_tiles!=float(proxy.target_x-1)||proxy.source_y_tiles!=float(proxy.target_y))
			printf("flip off: source (%g,%g)\n",proxy.source_x_tiles,proxy.source_y_tiles),++failures;
		if(proxy.offset_x_tiles<0.0f||proxy.offset_x_tiles>=1.0f||proxy.offset_x_tiles!=proxies[0].offset_x_tiles||
			proxy.offset_y_tiles!=0.0f||proxy.use_straight_path)
			printf("flip off: offset %g,%g\n",proxy.offset_x_tiles,proxy.offset_y_tiles),++failures;
		}
	}
	{
	// Facing east is the mirrored way; the right fragment lands two tiles to the left.
	scenest scene;
	const auto proxies=scene.collect(true);
	expect_count("flip on",proxies,3);
	expect_proxy("flip on, center",proxies,L::center,to_x,row,true,0,
		{{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("flip on, right",proxies,L::right,to_x+1,row,true,-2,
		{{from_x-1,row},{from_x,row},{from_x+1,row},{to_x+1,row}},right_texpos);
	expect_proxy("flip on, up",proxies,L::up,to_x,row-1,true,0,
		{{from_x,row-1},{to_x,row-1}},up_texpos);
	}
}

void test_blocked()
{
	{
	// Fire on the up fragment's path: the upper group is drawn over fire, so nothing changes.
	scenest scene;
	scene.v.spatter_flags[from_x*dim_y+row-1]=0x10000000U;
	expect_count("fire under the up fragment",scene.collect(false),3);
	}
	{
	// Fire on the right fragment's path blocks that fragment alone.
	scenest scene;
	scene.v.spatter_flags[(to_x+1)*dim_y+row]=0x40000000U;
	const auto proxies=scene.collect(false);
	expect_count("fire under the right fragment",proxies,2);
	if(find(proxies,L::right)!=nullptr)printf("right fragment drawn over fire\n"),++failures;
	}
	{
	// Fire on the creature's target blocks the centre, and the fragments lose their anchor.
	scenest scene;
	scene.v.spatter_flags[to_x*dim_y+row]=0x70000000U;
	expect_count("fire under the creature",scene.collect(false),0);
	}
	{
	// Spatter without the fire bits is not fire.
	scenest scene;
	scene.v.spatter_flags[to_x*dim_y+row]=0x0fffffffU;
	expect_count("spatter without fire",scene.collect(false),3);
	}
	{
	// The clip starts at the creature's source column: every path fits.
	scenest scene;
	scene.v.vp.clipx={from_x,dim_x-1};
	expect_count("clip at the source",scene.collect(false),3);
	}
	{
	// The clip starts past the source column: the centre is out, and with it the fragments.
	scenest scene;
	scene.v.vp.clipx={from_x+1,dim_x-1};
	expect_count("clip past the source",scene.collect(false),0);
	}
	{
	// With flipping on, the right fragment's mirrored path falls outside that same clip, so
	// its moving proxy is dropped; the resting sweep then paints it mirrored in place, since
	// the tiles between its own column and its mirrored one are all inside.
	scenest scene;
	scene.v.vp.clipx={from_x,dim_x-1};
	const auto proxies=scene.collect(true);
	expect_count("clip at the source, flip on",proxies,3);
	expect_proxy("clip at the source, flip on, right",proxies,L::right,to_x+1,row,true,-2,
		{{to_x-1,row},{to_x,row},{to_x+1,row}},right_texpos);
	const render_proxyst *right=find(proxies,L::right);
	if(right!=nullptr&&(right->offset_x_tiles!=0.0f||right->source_x_tiles!=float(right->target_x)))
		printf("clip at the source, flip on: right fragment still moving\n"),++failures;
	}
	{
	// With flipping on, fire on a tile only the right fragment's mirrored path crosses
	// blocks its moving proxy; the resting sweep then paints it mirrored in place, which
	// does not overshoot to that tile.
	scenest scene;
	scene.v.spatter_flags[(from_x-1)*dim_y+row]=0x30000000U;
	const auto proxies=scene.collect(true);
	expect_count("fire on the mirrored path",proxies,3);
	expect_proxy("fire on the mirrored path, right",proxies,L::right,to_x+1,row,true,-2,
		{{to_x-1,row},{to_x,row},{to_x+1,row}},right_texpos);
	const render_proxyst *right=find(proxies,L::right);
	if(right!=nullptr&&(right->offset_x_tiles!=0.0f||right->source_x_tiles!=float(right->target_x)))
		printf("fire on the mirrored path: right fragment still moving\n"),++failures;
	}
	{
	// The clip ends before the right fragment's target column.
	scenest scene;
	scene.v.vp.clipx={0,to_x};
	const auto proxies=scene.collect(false);
	expect_count("clip before the right fragment",proxies,2);
	if(find(proxies,L::right)!=nullptr)printf("right fragment drawn outside\n"),++failures;
	}
	{
	// A sprite the renderer has no texture for is left to the game.
	scenest scene;
	const auto proxies=scene.collect(false,up_texpos);
	expect_count("missing texture",proxies,2);
	if(find(proxies,L::up)!=nullptr)printf("up fragment drawn without texture\n"),++failures;
	}
}

void test_resting()
{
	// Once the step is over the creature still faces east, so with flipping on every
	// fragment is painted mirrored in place each frame; with flipping off nothing is.
	scenest scene;
	scene.manager.cancel_transitions();
	expect_count("resting, flip off",scene.collect(false),0);
	const auto proxies=scene.collect(true);
	expect_count("resting, flip on",proxies,3);
	expect_proxy("resting, center",proxies,L::center,to_x,row,true,0,{{to_x,row}},
		center_texpos);
	expect_proxy("resting, right",proxies,L::right,to_x+1,row,true,-2,
		{{to_x-1,row},{to_x,row},{to_x+1,row}},right_texpos);
	expect_proxy("resting, up",proxies,L::up,to_x,row-1,true,0,{{to_x,row-1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		if(proxy.offset_x_tiles!=0.0f||proxy.offset_y_tiles!=0.0f||proxy.source_x_tiles!=float(proxy.target_x)||
			proxy.source_y_tiles!=float(proxy.target_y))
			printf("resting: proxy not in place\n"),++failures;
	// Fire under a resting main-group sprite blocks it as it does a moving one.
	scene.v.spatter_flags[to_x*dim_y+row]=0x20000000U;
	const auto burning=scene.collect(true);
	expect_count("resting over fire",burning,1);
	if(find(burning,L::up)==nullptr)printf("resting up fragment lost to fire\n"),++failures;
	scene.v.spatter_flags[to_x*dim_y+row]=0;
	// The clip starting at the creature's column cuts the right fragment's mirrored tiles.
	scene.v.vp.clipx={to_x,dim_x-1};
	const auto clipped=scene.collect(true);
	expect_count("resting at the clip edge",clipped,2);
	if(find(clipped,L::right)!=nullptr)
		printf("resting right fragment drawn outside the clip\n"),++failures;
	scene.v.vp.clipx={0,dim_x-1};
	// A resting sprite without a texture is left to the game.
	const auto untextured=scene.collect(true,right_texpos);
	expect_count("resting without a texture",untextured,2);
	if(find(untextured,L::right)!=nullptr)
		printf("resting right fragment drawn without texture\n"),++failures;
	// An item on the creature's tile is not a creature sprite: the sweep leaves it alone.
	scene.v.at(L::item,to_x,row)=21;
	const auto with_item=scene.collect(true);
	expect_count("resting on an item",with_item,3);
	if(find(with_item,L::item)!=nullptr)printf("item painted mirrored\n"),++failures;
	// A creature resting on the top row has its up fragment off the viewport. The sweep
	// must not read the entry before the column, which here holds an up sprite; the clip
	// is widened past the viewport so that only the bounds check stands in the way.
	scenest top(0);
	top.manager.cancel_transitions();
	top.v.at(L::up,to_x-1,dim_y-1)=up_texpos;
	top.v.vp.clipy={-1,dim_y-1};
	const auto edge=top.collect(true);
	expect_count("resting on the top row",edge,2);
	if(find(edge,L::up)!=nullptr)printf("up fragment painted off the viewport\n"),++failures;
}

void test_coverage()
{
	scenest scene;
	const auto proxies=scene.collect(true);
	const render_coveragest coverage=collect_coverage(proxies,dim_y);
	const tilest all={{from_x-1,row},{from_x,row},{from_x+1,row},{to_x+1,row},
		{from_x,row-1},{to_x,row-1}};
	if(coverage.all!=all)
		printf("coverage: %s\n",tiles_text(coverage.all).c_str()),++failures;
	using G=visual_render_groupst;
	const tilest main_tiles={{from_x-1,row},{from_x,row},{from_x+1,row},{to_x+1,row}};
	const tilest upper_tiles={{from_x,row-1},{to_x,row-1}};
	if(coverage.groups[size_t(G::main)]!=main_tiles||
		coverage.groups[size_t(G::upper)]!=upper_tiles||
		!coverage.groups[size_t(G::item)].empty()||
		!coverage.groups[size_t(G::vehicle)].empty()||
		!coverage.groups[size_t(G::designation)].empty())
		printf("coverage groups wrong\n"),++failures;
	if(coverage.selected.size()!=3||
		selected_mask(coverage.selected,to_x*dim_y+row)!=visual_layer_bit(L::center)||
		selected_mask(coverage.selected,(to_x+1)*dim_y+row)!=visual_layer_bit(L::right)||
		selected_mask(coverage.selected,to_x*dim_y+row-1)!=visual_layer_bit(L::up)||
		selected_mask(coverage.selected,from_x*dim_y+row)!=0)
		printf("selected layers wrong\n"),++failures;

	struct renderst{render_coveragest coverage;};
	std::vector<renderst> renders(2);
	renders[0].coverage.all={{0,0},{1,1}};
	renders[1].coverage.all={{1,1},{2,2}};
	if(collect_viewport_coverage(renders)!=tilest{{0,0},{1,1},{2,2}})
		printf("viewport coverage union wrong\n"),++failures;
}

// With a movement that overshoots beyond the path (the hop, into the row above), each proxy
// also covers the tiles overshot, and a creature any of whose overshoot tiles is blocked falls
// back to the default movement, as a whole.
void test_overshoot()
{
	// With the hop selected, the creature follows it and both fragments ride on its centre
	// proxy, so each proxy also covers the row above its path. The centre is the first proxy
	// collected.
	{
	scenest scene;
	scene.select_hop();
	const auto proxies=scene.collect(false);
	expect_count("hop on",proxies,3);
	expect_proxy("hop on, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row-1},{to_x,row-1},{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("hop on, right",proxies,L::right,to_x+1,row,false,0,
		{{from_x+1,row-1},{to_x+1,row-1},{from_x+1,row},{to_x+1,row}},right_texpos);
	expect_proxy("hop on, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-2},{to_x,row-2},{from_x,row-1},{to_x,row-1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		{
		const bool centre=proxy.layer==L::center;
		if(proxy.use_straight_path)printf("hop on: %s took the straight path\n",centre?"centre":"fragment"),++failures;
		if(proxy.anchor!=(centre?-1:0))
			printf("hop on: anchor %d\n",proxy.anchor),++failures;
		}
	if(!proxies.empty()&&proxies[0].layer!=L::center)
		printf("hop on: centre not first\n"),++failures;
	}
	// The offset comes out with the movement, in tiles: with the hop, a frame in the middle
	// of a one-hop step lifts the centre to the hop's peak, the hop amount for a horizontal
	// step, and the fragments, whose movement is inherited from the centre, are lifted just
	// as far. The fallback offset beside it is the straight path at the fraction travelled.
	{
	scenest scene;
	std::unique_ptr<movementst> hop=scene.movements.find("hop")->clone();
	if(apply_movement_settings(*hop,{{"hops-per-step",1.0f},{"hop-height",0.2f}})!="")
		printf("hop lift: settings refused\n"),++failures;
	scene.manager.set_movement(*hop);
	scene.manager.begin_frame(1016+visual_animation_managerst::default_step_duration_ms/2);
	scene.manager.synchronize_viewport(scene.v.input());
	scene.manager.end_frame();
	const auto proxies=scene.collect(false);
	expect_count("hop lift",proxies,3);
	for(const render_proxyst &proxy:proxies)
		{
		const bool centre=proxy.layer==L::center;
		if(std::fabs(proxy.offset_x_tiles-0.5f)>0.01f)
			printf("hop lift: %s offset x %g\n",centre?"centre":"fragment",proxy.offset_x_tiles),++failures;
		if(std::fabs(proxy.offset_y_tiles+0.2f)>0.001f)
			printf("hop lift: %s offset y %g\n",centre?"centre":"fragment",proxy.offset_y_tiles),++failures;
		}
	}
	{
	// With the default movement, the proxies, their coverage and the anchors are what they
	// were, and nothing falls back.
	scenest scene;
	const auto proxies=scene.collect(false);
	for(const render_proxyst &proxy:proxies)
		if(proxy.use_straight_path)printf("hop off: took the straight path\n"),++failures;
	expect_proxy("hop off, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-1},{to_x,row-1}},up_texpos);
	}
	{
	// With flipping on, the mirrored image's row above is covered too: the right fragment
	// lands two tiles to the left, so its overshoot row spans both places.
	scenest scene;
	scene.select_hop();
	const auto proxies=scene.collect(true);
	expect_count("hop and flip",proxies,3);
	expect_proxy("hop and flip, right",proxies,L::right,to_x+1,row,true,-2,
		{{from_x-1,row-1},{from_x,row-1},{from_x+1,row-1},{to_x+1,row-1},
		{from_x-1,row},{from_x,row},{from_x+1,row},{to_x+1,row}},right_texpos);
	}
	{
	// Fire on the row above the up fragment's path: that fragment cannot overshoot to it, so the
	// whole creature falls back to the default movement, and every proxy covers only its
	// path. (Fire under the up fragment itself does not block it, since the upper group is
	// drawn over fire.)
	scenest scene;
	scene.select_hop();
	scene.v.spatter_flags[to_x*dim_y+row-2]=0x10000000U;
	const auto proxies=scene.collect(false);
	expect_count("fire on the overshoot row",proxies,3);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)printf("fire on the overshoot row: did not take the straight path\n"),++failures;
	expect_proxy("fire on the overshoot row, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("fire on the overshoot row, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-1},{to_x,row-1}},up_texpos);
	}
	{
	// The row above the mirrored image is burning: the same, with flipping on.
	scenest scene;
	scene.select_hop();
	scene.v.spatter_flags[(from_x-1)*dim_y+row-1]=0x20000000U;
	const auto proxies=scene.collect(true);
	expect_count("fire on the mirrored overshoot row",proxies,3);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)printf("fire on the mirrored overshoot row: did not take the straight path\n"),++failures;
	expect_proxy("fire on the mirrored overshoot row, right",proxies,L::right,to_x+1,row,true,-2,
		{{from_x-1,row},{from_x,row},{from_x+1,row},{to_x+1,row}},right_texpos);
	}
	{
	// A creature walking along the top row has no row above inside the clip: it falls back
	// to the default movement rather than being dropped.
	scenest top(0);
	top.select_hop();
	const auto proxies=top.collect(false);
	expect_count("hop on the top row",proxies,2);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)printf("hop on the top row: did not take the straight path\n"),++failures;
	expect_proxy("hop on the top row, center",proxies,L::center,to_x,0,false,0,
		{{from_x,0},{to_x,0}},center_texpos);
	}
	{
	// A vehicle stepping on the creature's own tile with it (a pushed minecart) keeps to the
	// path, falling back on its own, and never rides on the creature's proxy, while the
	// creature follows the hop as before.
	scenest scene(row,true);
	scene.select_hop();
	const auto proxies=scene.collect(false);
	expect_count("vehicle",proxies,4);
	const render_proxyst *vehicle=find(proxies,L::vehicle);
	if(vehicle==nullptr)printf("vehicle: no proxy\n"),++failures;
	else if(!vehicle->use_straight_path||vehicle->anchor!=-1)
		printf("vehicle: straight path %d anchor %d\n",vehicle->use_straight_path,vehicle->anchor),++failures;
	else if(vehicle->coverage!=tilest{{from_x,row},{to_x,row}})
		printf("vehicle: covers %s\n",tiles_text(vehicle->coverage).c_str()),++failures;
	const render_proxyst *centre=find(proxies,L::center);
	if(centre==nullptr||centre->use_straight_path)printf("vehicle: creature took the straight path\n"),++failures;
	}
	{
	// A one-tile creature stepping in lockstep beside the up fragment, with fire on its own
	// overshoot row so it falls back: the fragments still ride on their own centre and follow
	// the hop with it. The neighbour's centre comes first in collection order and moves the
	// same way, so a match on any centre within a tile would tie the up fragment to the
	// neighbour and leave it on the path while its creature hops.
	scenest scene(row,false,true);
	scene.select_hop();
	scene.v.spatter_flags[(from_x-1)*dim_y+row-2]=0x10000000U;
	const auto proxies=scene.collect(false);
	expect_count("neighbour",proxies,4);
	int32_t own=-1,other=-1;
	for(size_t i=0;i<proxies.size();++i)
		if(proxies[i].layer==L::center)
			(proxies[i].texpos==center_texpos?own:other)=int32_t(i);
	if(own<0||other<0)printf("neighbour: centres missing\n"),++failures;
	else
		{
		if(other>own)printf("neighbour: not collected first\n"),++failures;
		if(!proxies[other].use_straight_path)printf("neighbour: neighbour did not take the straight path\n"),++failures;
		if(proxies[own].use_straight_path)printf("neighbour: creature took the straight path\n"),++failures;
		for(const render_proxyst &proxy:proxies)
			if(proxy.layer!=L::center&&(proxy.anchor!=own||proxy.use_straight_path))
				printf("neighbour: fragment anchor %d straight path %d\n",proxy.anchor,
					proxy.use_straight_path),++failures;
		}
	expect_proxy("neighbour, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-2},{to_x,row-2},{from_x,row-1},{to_x,row-1}},up_texpos);
	}
	{
	// A resting mirrored sprite has no movement to follow: zero offsets, no overshoot tiles.
	scenest scene;
	scene.select_hop();
	scene.manager.cancel_transitions();
	const auto proxies=scene.collect(true);
	expect_count("resting with hop on",proxies,3);
	for(const render_proxyst &proxy:proxies)
		if(proxy.offset_x_tiles!=0.0f||proxy.offset_y_tiles!=0.0f||proxy.anchor!=-1)
			printf("resting with hop on: moving\n"),++failures;
	expect_proxy("resting with hop on, center",proxies,L::center,to_x,row,true,0,
		{{to_x,row}},center_texpos);
	}
}

// A carried item follows the movement only with a centre proxy on its own tile that did
// not fall back and moves the same way: the same source and offset. Each field a step off
// on its own, a fragment on the tile, or a centre proxy that fell back leaves it on the
// fallback.
void test_carried_item_movement()
{
	scenest scene;
	scene.select_hop();
	const auto proxies=scene.collect(false);
	const render_proxyst *centre=find(proxies,L::center);
	if(centre==nullptr){printf("carried item: no centre proxy\n");++failures;return;}
	auto item=[&](int32_t target_x,int32_t target_y,float source_x_tiles,float source_y_tiles,
		float offset_x_tiles,float offset_y_tiles)
		{
		return carried_item_proxyst{source_x_tiles,source_y_tiles,target_x,target_y,offset_x_tiles,offset_y_tiles,
			centre->travelled_pct,centre->texture,true,{}};
		};
	std::vector<carried_item_proxyst> items={
		item(centre->target_x,centre->target_y,centre->source_x_tiles,centre->source_y_tiles,
			centre->offset_x_tiles,centre->offset_y_tiles),
		item(centre->target_x,centre->target_y,centre->source_x_tiles,centre->source_y_tiles,
			centre->offset_x_tiles+0.25f,centre->offset_y_tiles),
		item(centre->target_x,centre->target_y,centre->source_x_tiles,centre->source_y_tiles,
			centre->offset_x_tiles,centre->offset_y_tiles-0.1f),
		item(centre->target_x,centre->target_y,centre->source_x_tiles-1.0f,centre->source_y_tiles,
			centre->offset_x_tiles,centre->offset_y_tiles),
		item(centre->target_x,centre->target_y,centre->source_x_tiles,centre->source_y_tiles-1.0f,
			centre->offset_x_tiles,centre->offset_y_tiles),
		item(centre->target_x+1,centre->target_y,centre->source_x_tiles,centre->source_y_tiles,
			centre->offset_x_tiles,centre->offset_y_tiles),
		item(centre->target_x,centre->target_y+1,centre->source_x_tiles,centre->source_y_tiles,
			centre->offset_x_tiles,centre->offset_y_tiles),
		item(to_x+1,row,float(from_x+1),float(row),centre->offset_x_tiles,centre->offset_y_tiles)};
	mark_carried_item_movements(items,proxies);
	if(items[0].use_straight_path)printf("carried item on its carrier: took the straight path\n"),++failures;
	if(!items[1].use_straight_path)printf("carried item a different offset x: follows\n"),++failures;
	if(!items[2].use_straight_path)printf("carried item a different offset y: follows\n"),++failures;
	if(!items[3].use_straight_path)printf("carried item from a different source: follows\n"),++failures;
	if(!items[4].use_straight_path)printf("carried item from a different source row: follows\n"),++failures;
	if(!items[5].use_straight_path)printf("carried item a column off the carrier: follows\n"),++failures;
	if(!items[6].use_straight_path)printf("carried item a row off the carrier: follows\n"),++failures;
	if(!items[7].use_straight_path)printf("carried item on a fragment's tile: follows\n"),++failures;
	// The same carrier fallen back, or no proxies at all, clears nothing; an item that did
	// not come in fallen back (a movement on the path) is left alone.
	std::vector<render_proxyst> fallen=proxies;
	for(render_proxyst &proxy:fallen)proxy.use_straight_path=true;
	items[0].use_straight_path=true;
	mark_carried_item_movements(items,fallen);
	if(!items[0].use_straight_path)printf("carried item on a straight-path carrier: follows\n"),++failures;
	mark_carried_item_movements(items,{});
	if(!items[0].use_straight_path)printf("carried item with no proxies: follows\n"),++failures;
	items[1].use_straight_path=false;
	mark_carried_item_movements(items,proxies);
	if(items[1].use_straight_path)printf("carried item on the path: took the straight path\n"),++failures;
}

void test_tile_checks()
{
	viewportst v;
	v.vp.clipx={1,4};v.vp.clipy={0,2};
	if(!inside_clip(&v.vp,1,0)||!inside_clip(&v.vp,4,2)||inside_clip(&v.vp,0,0)||
		inside_clip(&v.vp,5,0)||inside_clip(&v.vp,1,3))
		printf("inside_clip wrong\n"),++failures;
	if(fire_frame(uint32_t(0x0fffffffU))||!fire_frame(uint32_t(0x10000000U)))
		printf("fire_frame on a word wrong\n"),++failures;
	struct flagst{struct{uint32_t fire_frame_type:3;}bits;};
	flagst flag{};
	if(fire_frame(flag))printf("fire_frame on a clear flag wrong\n"),++failures;
	flag.bits.fire_frame_type=2;
	if(!fire_frame(flag))printf("fire_frame on a set flag wrong\n"),++failures;
	v.spatter_flags[2*dim_y+1]=0x10000000U;
	if(!has_fire(&v.vp,2,1)||has_fire(&v.vp,2,0)||has_fire(&v.vp,1,1))
		printf("has_fire wrong\n"),++failures;
	// A tile inside the clip can still be outside the per-tile arrays, which are dim_x by
	// dim_y whatever the clip says. Both checks answer on the arrays, not the clip alone:
	// the index a tile past the last column lands one whole row into the next array.
	v.vp.clipx={-1,dim_x};v.vp.clipy={-1,dim_y};
	if(has_fire(&v.vp,dim_x,1)||has_fire(&v.vp,-1,1)||has_fire(&v.vp,2,dim_y)||
		has_fire(&v.vp,2,-1))
		printf("has_fire outside the arrays\n"),++failures;
	if(!paintable_tile(&v.vp,0,0)||!paintable_tile(&v.vp,dim_x-1,dim_y-1)||
		paintable_tile(&v.vp,-1,0)||paintable_tile(&v.vp,dim_x,0)||
		paintable_tile(&v.vp,0,-1)||paintable_tile(&v.vp,0,dim_y))
		printf("paintable_tile outside the arrays\n"),++failures;
	// Inside the arrays it is the clip test, so a tile the clip excludes is not paintable.
	v.vp.clipx={1,4};v.vp.clipy={0,2};
	if(paintable_tile(&v.vp,0,0)||paintable_tile(&v.vp,1,3)||!paintable_tile(&v.vp,1,0))
		printf("paintable_tile against the clip\n"),++failures;
	v.vp.screentexpos_spatter_flag=nullptr;
	if(has_fire(&v.vp,2,1))printf("has_fire without the array\n"),++failures;
}

// No proxy may cover a tile outside the per-tile arrays: the repaint indexes fifteen arrays
// at x*dim_y+y for every covered tile and writes through the entry, so a covered tile past
// the end is a write into whatever follows, not merely a stray read.
void expect_inside_arrays(const char *name,const std::vector<render_proxyst> &proxies)
{
	for(const render_proxyst &proxy:proxies)
		for(const auto &[x,y]:proxy.coverage)
			if(x<0||x>=dim_x||y<0||y>=dim_y)
				printf("%s: covers (%d,%d), outside the %dx%d arrays\n",name,x,y,dim_x,dim_y),
				++failures;
}

// The clip and the arrays are two different rectangles: nothing derives clipx and clipy from
// dim_x and dim_y, and nothing keeps the two in step. No recorded scene has a clip reaching
// past the arrays, so these cases are not a scene the game is known to produce; they are the
// smallest viewports that reach the sweep's per-tile reads and coverage inserts with a tile
// outside the arrays, which is what the bounds check has to answer. Each widens the clip past
// one edge so that the bounds check is the only thing left, and pins that the sweep neither
// reads nor covers such a tile. Without the check each of these reads out of bounds.
void test_outside_the_arrays()
{
	// A hopping creature on the top row overshoots into the row above, which is off the
	// arrays. The overshoot tile must block the hop, the way any other blocked overshoot
	// tile does, and leave the creature on the straight path.
	{
	scenest top(0);
	top.select_hop();
	top.v.vp.clipy={-1,dim_y-1};
	const auto proxies=top.collect(false);
	expect_inside_arrays("hop off the top",proxies);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)
			printf("hop off the top: hopped over a tile outside the arrays\n"),++failures;
	}
	// A creature resting at the last column after walking east is mirrored, and its left
	// fragment's mirrored coverage sweep reaches one tile past the last column. The
	// fragment's own tile is inside the arrays, so only the coverage sweep is out.
	{
	viewportst v;
	visual_animation_managerst manager;
	constexpr int32_t start_x=dim_x-2,end_x=dim_x-1,edge_row=2;
	// The left fragment's array tile sits one column left of the creature it belongs to.
	v.at(L::center,start_x,edge_row)=center_texpos;
	v.at(L::left,start_x-1,edge_row)=neighbour_texpos;
	manager.begin_frame(1000);
	manager.synchronize_viewport(v.input());
	manager.end_frame();
	v.advance();
	v.at(L::center,start_x,edge_row)=0;v.at(L::left,start_x-1,edge_row)=0;
	v.at(L::center,end_x,edge_row)=center_texpos;
	v.at(L::left,end_x-1,edge_row)=neighbour_texpos;
	manager.begin_frame(1016);
	manager.synchronize_viewport(v.input());
	manager.end_frame();
	// Resting, so the mirrored resting sweep runs rather than the moving one.
	manager.cancel_transitions();
	v.vp.clipx={0,dim_x};
	static int token;
	const auto proxies=collect_proxies(&v.vp,manager,true,[](int32_t texpos)
		{return texpos==0?nullptr:reinterpret_cast<SDL_Texture *>(&token);});
	expect_inside_arrays("resting at the last column",proxies);
	if(find(proxies,L::left)!=nullptr)
		printf("resting left fragment painted past the last column\n"),++failures;
	}
}

// A movement on the straight path that overshoots by whatever a test asks for. The shipped
// movements overshoot above and nowhere else -- the hop, by hop-height times its largest
// multiplier -- so this is how the other three directions, and any amount, get exercised.
class overshoot_movementst:public movementst
{
	public:
		movement_overshootst amount;
		const char *name() const override{return "overshoot";}
		float travelled_pct(float elapsed_pct) const override{return elapsed_pct;}
		sprite_offsetst path(float travelled_pct,tile_stepst step) const override
			{return straight_path(travelled_pct,step);}
		movement_overshootst overshoot() const override{return amount;}
		std::unique_ptr<movementst> clone() const override
			{return std::make_unique<overshoot_movementst>(*this);}
};

void test_column_overshoot()
{
	{
	// With a movement that overshoots to the right, the column to the right of the path
	// joins coverage.
	scenest scene;
	overshoot_movementst rightward;
	rightward.amount.right_tiles=0.3f;
	scene.manager.set_movement(rightward);
	const auto proxies=scene.collect(false);
	expect_count("rightward overshoot",proxies,3);
	expect_proxy("rightward overshoot, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row},{to_x,row},{to_x+1,row}},center_texpos);
	for(const render_proxyst &proxy:proxies)
		if(proxy.use_straight_path)printf("rightward overshoot: took the straight path\n"),++failures;
	}
	{
	// Fire on the column to the right of the right fragment's path: that fragment cannot
	// overshoot to it, so the whole creature falls back to the default movement and every
	// proxy covers only its path. (Fire on the fragment's own landing tile would drop the
	// fragment instead, since a creature's path must be off burning tiles to move at all.)
	scenest scene;
	overshoot_movementst rightward;
	rightward.amount.right_tiles=0.3f;
	scene.manager.set_movement(rightward);
	scene.v.spatter_flags[(to_x+2)*dim_y+row]=0x10000000U;
	const auto proxies=scene.collect(false);
	expect_count("fire on the overshoot column",proxies,3);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)printf("fire on the overshoot column: did not take the straight path\n"),++failures;
	expect_proxy("fire on the overshoot column, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("fire on the overshoot column, right",proxies,L::right,to_x+1,row,false,0,
		{{from_x+1,row},{to_x+1,row}},right_texpos);
	}
}

// Every case above overshoots by a fraction of a tile, which rounds up to one. The amount is
// not capped at a tile: the hop's is hop-height times its largest multiplier, which its own
// ranges allow up to five tiles, and a movement of the test's own can name any of the four
// directions. `collect_proxies` rounds each direction up to whole tiles once, at the top, and
// then sweeps that many rows or columns out from every proxy's path. These pin what that
// sweep covers when the rounding lands past one: how far it reaches on each axis, that the
// four directions reach independently of each other, and that a creature whose deeper
// overshoot no longer fits inside the viewport falls back the way a blocked one does.
void test_multi_tile_overshoot()
{
	// The hop of a scene, set to overshoot two rows: 0.35 tiles of hop-height times a
	// vertical multiplier of 5 is 1.75 rows, and each direction is rounded up. Both are
	// inside the settings' own ranges, which allow up to five tiles of overshoot. The
	// movement has to outlive the manager's use of it, so the caller keeps it.
	const auto two_row_hop=[](scenest &scene)
		{
		std::unique_ptr<movementst> hop=scene.movements.find("hop")->clone();
		if(apply_movement_settings(*hop,{{"hop-height",0.35f},{"vertical-mult",5.0f}})!="")
			printf("the two-row hop: settings refused\n"),++failures;
		return hop;
		};
	{
	// Two rows above, with the room for them: the creature walks along the bottom row, so
	// even the up fragment, a row higher than the centre, has two rows above it inside the
	// viewport. Every proxy covers both of them as well as its own path, and nothing falls
	// back.
	constexpr int32_t bottom_row=dim_y-1;
	scenest scene(bottom_row);
	const std::unique_ptr<movementst> hop=two_row_hop(scene);
	scene.manager.set_movement(*hop);
	const auto proxies=scene.collect(false);
	expect_count("two rows above",proxies,3);
	expect_proxy("two rows above, center",proxies,L::center,to_x,bottom_row,false,0,
		{{from_x,bottom_row-2},{from_x,bottom_row-1},{from_x,bottom_row},
		{to_x,bottom_row-2},{to_x,bottom_row-1},{to_x,bottom_row}},center_texpos);
	expect_proxy("two rows above, right",proxies,L::right,to_x+1,bottom_row,false,0,
		{{from_x+1,bottom_row-2},{from_x+1,bottom_row-1},{from_x+1,bottom_row},
		{to_x+1,bottom_row-2},{to_x+1,bottom_row-1},{to_x+1,bottom_row}},right_texpos);
	expect_proxy("two rows above, up",proxies,L::up,to_x,bottom_row-1,false,0,
		{{from_x,bottom_row-3},{from_x,bottom_row-2},{from_x,bottom_row-1},
		{to_x,bottom_row-3},{to_x,bottom_row-2},{to_x,bottom_row-1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		if(proxy.use_straight_path)printf("two rows above: took the straight path\n"),++failures;
	}
	{
	// The same movement one row higher up the viewport, which is where the other cases put
	// the creature: the up fragment's second row above is off the top, so the whole creature
	// falls back to the straight path and every proxy covers its path alone. Its first row
	// above is inside the viewport, and a single-row overshoot there does not fall back
	// (`hop on` above), so what refuses this one is how far the overshoot reaches and not
	// which direction it reaches in.
	scenest scene;
	const std::unique_ptr<movementst> hop=two_row_hop(scene);
	scene.manager.set_movement(*hop);
	const auto proxies=scene.collect(false);
	expect_count("two rows above the top",proxies,3);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)
			printf("two rows above the top: did not take the straight path\n"),++failures;
	expect_proxy("two rows above the top, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("two rows above the top, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-1},{to_x,row-1}},up_texpos);
	}
	{
	// Two columns to the left, which 1.4 columns rounds to. No shipped movement overshoots
	// sideways at all, so the column reach is the test's own movement throughout; the
	// creature's leftmost path column is the third, which leaves exactly the room.
	scenest scene;
	overshoot_movementst leftward;
	leftward.amount.left_tiles=1.4f;
	scene.manager.set_movement(leftward);
	const auto proxies=scene.collect(false);
	expect_count("two columns left",proxies,3);
	expect_proxy("two columns left, center",proxies,L::center,to_x,row,false,0,
		{{from_x-2,row},{from_x-1,row},{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("two columns left, right",proxies,L::right,to_x+1,row,false,0,
		{{from_x-1,row},{from_x,row},{from_x+1,row},{to_x+1,row}},right_texpos);
	expect_proxy("two columns left, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x-2,row-1},{from_x-1,row-1},{from_x,row-1},{to_x,row-1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		if(proxy.use_straight_path)printf("two columns left: took the straight path\n"),++failures;
	}
	{
	// Two rows below, which 1.2 rows rounds to, on a creature one row higher so that they
	// fit. That an amount of zero sweeps nothing is already clear from the rightward case
	// above; what is new here is that a non-zero amount in one direction does not reach in
	// another. The row above the centre's path is inside the viewport and stays out of its
	// coverage, which is what keeps a movement that only sinks from also erasing the row
	// above every sprite that follows it.
	constexpr int32_t second_row=1;
	scenest scene(second_row);
	overshoot_movementst downward;
	downward.amount.below_tiles=1.2f;
	scene.manager.set_movement(downward);
	const auto proxies=scene.collect(false);
	expect_count("two rows below",proxies,3);
	expect_proxy("two rows below, center",proxies,L::center,to_x,second_row,false,0,
		{{from_x,second_row},{from_x,second_row+1},{from_x,second_row+2},
		{to_x,second_row},{to_x,second_row+1},{to_x,second_row+2}},center_texpos);
	expect_proxy("two rows below, up",proxies,L::up,to_x,second_row-1,false,0,
		{{from_x,second_row-1},{from_x,second_row},{from_x,second_row+1},
		{to_x,second_row-1},{to_x,second_row},{to_x,second_row+1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		if(proxy.use_straight_path)printf("two rows below: took the straight path\n"),++failures;
	}
	{
	// Fire on the second column to the left of the centre's path: the check that gives up an
	// overshoot reaches as deep as the sweep that covers it, on the column axis as on the row
	// axis, so the whole creature falls back and every proxy covers its path alone. The first
	// column to the left is clear, so a check that only ever looked one column out would find
	// nothing to fall back from.
	scenest scene;
	overshoot_movementst leftward;
	leftward.amount.left_tiles=1.4f;
	scene.manager.set_movement(leftward);
	scene.v.spatter_flags[(from_x-2)*dim_y+row]=0x10000000U;
	const auto proxies=scene.collect(false);
	expect_count("fire two columns left",proxies,3);
	for(const render_proxyst &proxy:proxies)
		if(!proxy.use_straight_path)
			printf("fire two columns left: did not take the straight path\n"),++failures;
	expect_proxy("fire two columns left, center",proxies,L::center,to_x,row,false,0,
		{{from_x,row},{to_x,row}},center_texpos);
	expect_proxy("fire two columns left, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x,row-1},{to_x,row-1}},up_texpos);
	}
	{
	// A row above and two columns to the left at once: the sweep is one rectangle around the
	// path, reaching each of its four sides by that side's own amount, rather than one strip
	// per direction. The tiles that tell those two apart are the corners, where the row above
	// meets the columns beside: a pair of strips would cover the row above the path and the
	// columns beside it and leave the corners out, so the corners are what these expect.
	scenest scene;
	overshoot_movementst corner;
	corner.amount.above_tiles=0.2f;
	corner.amount.left_tiles=1.4f;
	scene.manager.set_movement(corner);
	const auto proxies=scene.collect(false);
	expect_count("a row above and two columns left",proxies,3);
	expect_proxy("a row above and two columns left, center",proxies,L::center,to_x,row,false,0,
		{{from_x-2,row-1},{from_x-2,row},{from_x-1,row-1},{from_x-1,row},
		{from_x,row-1},{from_x,row},{to_x,row-1},{to_x,row}},center_texpos);
	expect_proxy("a row above and two columns left, up",proxies,L::up,to_x,row-1,false,0,
		{{from_x-2,row-2},{from_x-2,row-1},{from_x-1,row-2},{from_x-1,row-1},
		{from_x,row-2},{from_x,row-1},{to_x,row-2},{to_x,row-1}},up_texpos);
	for(const render_proxyst &proxy:proxies)
		if(proxy.use_straight_path)
			printf("a row above and two columns left: took the straight path\n"),++failures;
	}
}

} // namespace

int main()
{
	test_moving();
	test_blocked();
	test_resting();
	test_coverage();
	test_overshoot();
	test_column_overshoot();
	test_multi_tile_overshoot();
	test_carried_item_movement();
	test_tile_checks();
	test_outside_the_arrays();
	if(failures!=0){printf("sprite proxy tests: %d failures\n",failures);return 1;}
	printf("sprite proxy tests: OK\n");
	return 0;
}
