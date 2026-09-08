// SPDX-License-Identifier: MIT
// Checks collect_proxies and the coverage helpers against the stub viewport with the real
// animation manager: which sprites get a proxy, what tiles each covers, and what blocks one.
// The recordings exercise the moving cases but have no burning tile, no sprite at the clip
// edge and no missing texture, so those are pinned here.

#include "df/graphic_viewportst.h"
#include "sprite_proxies.h"

#include <cstdio>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace {

constexpr int32_t dim_x=6,dim_y=4,tile_count=dim_x*dim_y;
constexpr int32_t center_texpos=11,right_texpos=12,up_texpos=13;
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

	explicit scenest(int32_t row=::row)
		{
		v.at(L::center,from_x,row)=center_texpos;
		v.at(L::right,from_x+1,row)=right_texpos;
		if(row>0)v.at(L::up,from_x,row-1)=up_texpos;
		manager.begin_frame(1000);
		manager.synchronize_viewport(v.input());
		manager.end_frame();
		v.advance();
		v.at(L::center,from_x,row)=0;v.at(L::right,from_x+1,row)=0;
		if(row>0)v.at(L::up,from_x,row-1)=0;
		v.at(L::center,to_x,row)=center_texpos;
		v.at(L::right,to_x+1,row)=right_texpos;
		if(row>0)v.at(L::up,to_x,row-1)=up_texpos;
		manager.begin_frame(1016);
		manager.synchronize_viewport(v.input());
		manager.end_frame();
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
		if(proxy.source_x!=float(proxy.target_x-1)||proxy.source_y!=float(proxy.target_y))
			printf("flip off: source (%g,%g)\n",proxy.source_x,proxy.source_y),++failures;
		if(proxy.progress<0.0f||proxy.progress>=1.0f||proxy.progress!=proxies[0].progress)
			printf("flip off: progress %g\n",proxy.progress),++failures;
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
	if(right!=nullptr&&right->progress!=1.0f)
		printf("clip at the source, flip on: right fragment still moving\n"),++failures;
	}
	{
	// With flipping on, fire on a tile only the right fragment's mirrored path crosses
	// blocks its moving proxy; the resting sweep then paints it mirrored in place, which
	// does not reach that tile.
	scenest scene;
	scene.v.spatter_flags[(from_x-1)*dim_y+row]=0x30000000U;
	const auto proxies=scene.collect(true);
	expect_count("fire on the mirrored path",proxies,3);
	expect_proxy("fire on the mirrored path, right",proxies,L::right,to_x+1,row,true,-2,
		{{to_x-1,row},{to_x,row},{to_x+1,row}},right_texpos);
	const render_proxyst *right=find(proxies,L::right);
	if(right!=nullptr&&right->progress!=1.0f)
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
		if(proxy.progress!=1.0f||proxy.source_x!=float(proxy.target_x)||
			proxy.source_y!=float(proxy.target_y))
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
	v.vp.screentexpos_spatter_flag=nullptr;
	if(has_fire(&v.vp,2,1))printf("has_fire without the array\n"),++failures;
}

} // namespace

int main()
{
	test_moving();
	test_blocked();
	test_resting();
	test_coverage();
	test_tile_checks();
	if(failures!=0){printf("sprite proxy tests: %d failures\n",failures);return 1;}
	printf("sprite proxy tests: OK\n");
	return 0;
}
