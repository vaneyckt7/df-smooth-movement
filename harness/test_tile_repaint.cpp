// Checks the repaint passes in tile_repaint.h against the stub viewport: for each pass, which
// of the 25 per-tile arrays the repaint sees zeroed, that every entry is restored afterwards
// and that other tiles are untouched. The recordings in recordings/ have no top shadow and
// little of several other arrays, so the digests cannot tell a pass that zeroes one array too
// many or too few; this test can.
#include "df/graphic_viewportst.h"
#include "tile_repaint.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace {

// The 25 arrays in the game's draw order, the order visit_tile_arrays walks them.
enum arrayst
{
	background,floor_flag,background_two,liquid_flag,spatter_flag,spatter,ramp_flag,
	shadow_flag,building_one,item,vehicle,vermin,left,center,right,building_two,projectile,
	high_flow,top_shadow,signpost,upleft,up,upright,designation,interface,array_count
};

const char *const array_names[array_count]=
	{
	"background","floor_flag","background_two","liquid_flag","spatter_flag","spatter",
	"ramp_flag","shadow_flag","building_one","item","vehicle","vermin","left","center",
	"right","building_two","projectile","high_flow","top_shadow","signpost","upleft","up",
	"upright","designation","interface"
	};

constexpr int32_t dim_x=2,dim_y=3,tile_count=dim_x*dim_y;

struct viewportst
{
	df::graphic_viewportst vp{};
	std::vector<int32_t> words[array_count];
	std::vector<uint64_t> floor,ramp;
	std::vector<uint32_t> liquid,spatter_flags,shadow;
	std::vector<int32_t> previous[visual_layer_count];

	viewportst()
		{
		vp.dim_x=dim_x;vp.dim_y=dim_y;
		for(auto &w:words)w.assign(tile_count,0);
		floor.assign(tile_count,0);ramp.assign(tile_count,0);
		liquid.assign(tile_count,0);spatter_flags.assign(tile_count,0);
		shadow.assign(tile_count,0);
		for(auto &p:previous)p.assign(tile_count,0);
		vp.screentexpos_background=words[background].data();
		vp.screentexpos_floor_flag=floor.data();
		vp.screentexpos_background_two=words[background_two].data();
		vp.screentexpos_liquid_flag=liquid.data();
		vp.screentexpos_spatter_flag=spatter_flags.data();
		vp.screentexpos_spatter=words[spatter].data();
		vp.screentexpos_ramp_flag=ramp.data();
		vp.screentexpos_shadow_flag=shadow.data();
		vp.screentexpos_building_one=words[building_one].data();
		vp.screentexpos_item=words[item].data();
		vp.screentexpos_vehicle=words[vehicle].data();
		vp.screentexpos_vermin=words[vermin].data();
		vp.screentexpos_left_creature=words[left].data();
		vp.screentexpos=words[center].data();
		vp.screentexpos_right_creature=words[right].data();
		vp.screentexpos_building_two=words[building_two].data();
		vp.screentexpos_projectile=words[projectile].data();
		vp.screentexpos_high_flow=words[high_flow].data();
		vp.screentexpos_top_shadow=words[top_shadow].data();
		vp.screentexpos_signpost=words[signpost].data();
		vp.screentexpos_upleft_creature=words[upleft].data();
		vp.screentexpos_up_creature=words[up].data();
		vp.screentexpos_upright_creature=words[upright].data();
		vp.screentexpos_designation=words[designation].data();
		vp.screentexpos_interface=words[interface].data();
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

	// Every entry of every array gets a value that names its array and tile.
	void fill()
		{
		int k=0;
		visit_tile_arrays(&vp,[&](const auto *array)
			{
			using T=std::remove_cv_t<std::remove_pointer_t<decltype(array)>>;
			T *writable=const_cast<T *>(array);
			for(int32_t i=0;i<tile_count;++i)writable[i]=T((k+1)*100+i+1);
			++k;
			return true;
			});
		}

	// Which arrays hold zero at the tile right now, as a bit per array.
	uint32_t zero_mask(int32_t index) const
		{
		uint32_t mask=0;int k=0;
		visit_tile_arrays(&vp,[&](const auto *array)
			{
			if(array!=nullptr&&array[index]==0)mask|=1U<<k;
			++k;
			return true;
			});
		return mask;
		}

	// Whether every entry of every array still holds what fill() wrote. An array a case
	// set to null is skipped by both checks.
	bool filled() const
		{
		bool ok=true;int k=0;
		visit_tile_arrays(&vp,[&](const auto *array)
			{
			for(int32_t i=0;array!=nullptr&&i<tile_count;++i)
				if(int64_t(array[i])!=(k+1)*100+i+1)ok=false;
			++k;
			return true;
			});
		return ok;
		}
};

std::string mask_names(uint32_t mask)
{
	std::string out;
	for(int k=0;k<array_count;++k)
		if(mask&(1U<<k))out+=std::string(out.empty()?"":" ")+array_names[k];
	return out.empty()?"(none)":out;
}

constexpr uint32_t bits(std::initializer_list<arrayst> arrays)
{
	uint32_t mask=0;
	for(arrayst a:arrays)mask|=1U<<a;
	return mask;
}

int failures=0;

// Runs `pass` on tile (1,2) of a filled viewport and checks the repaint saw exactly `expected`
// zeroed, was called once, and left everything as it was.
template<typename Pass>
void check(const char *name,uint32_t expected,const Pass &pass,
	void (*setup)(df::graphic_viewportst &)=nullptr)
{
	viewportst v;v.fill();
	if(setup)setup(v.vp);
	const int32_t x=1,y=2,index=x*dim_y+y;
	int calls=0;uint32_t seen=0;
	pass(&v.vp,x,y,[&](df::graphic_viewportst *vp,int32_t px,int32_t py)
		{
		++calls;
		if(vp!=&v.vp||px!=x||py!=y)printf("%s: repaint of the wrong tile\n",name),++failures;
		seen=v.zero_mask(index);
		for(int32_t i=0;i<tile_count;++i)
			if(i!=index&&v.zero_mask(i)!=0)
				printf("%s: tile %d touched: %s\n",name,i,mask_names(v.zero_mask(i)).c_str()),
					++failures;
		});
	if(calls!=1)printf("%s: repaint called %d times\n",name,calls),++failures;
	if(seen!=expected)
		{
		printf("%s: zeroed %s\n  expected %s\n",name,mask_names(seen).c_str(),
			mask_names(expected).c_str());
		++failures;
		}
	if(!v.filled())printf("%s: entries not restored\n",name),++failures;
}

constexpr uint32_t base_arrays=bits({background,floor_flag,background_two,liquid_flag,
	spatter_flag,spatter,ramp_flag,shadow_flag,building_one});
constexpr uint32_t upper_arrays=bits({building_two,projectile,high_flow,top_shadow,signpost});

void test_passes()
{
	using L=viewport_visual_layer;
	using G=visual_render_groupst;
	check("staged, nothing hidden",0,[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_staged(vp,x,y,0,false,repaint);});
	check("staged, center and item hidden, interface deferred",
		bits({center,item,interface}),[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{
		repaint_staged(
			vp,x,y,uint16_t(visual_layer_bit(L::center)|visual_layer_bit(L::item)),true,repaint);
		});
	check("above item group, designation hidden",
		base_arrays|bits({item,designation,interface}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_above(vp,x,y,G::item,visual_layer_bit(L::designation),repaint);});
	check("above vehicle group",base_arrays|bits({item,vehicle,interface}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_above(vp,x,y,G::vehicle,0,repaint);});
	check("above main group",base_arrays|bits({vermin,item,vehicle,left,center,right,interface}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_above(vp,x,y,G::main,0,repaint);});
	check("above upper group",
		base_arrays|upper_arrays|
			bits({vermin,item,vehicle,left,center,right,upleft,up,upright,interface}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_above(vp,x,y,G::upper,0,repaint);});
	// Each layer bit hides one array, so a table entry pointing at the wrong array shows up
	// even though the groups above hide neighbouring layers together.
	const std::pair<L,arrayst> layer_arrays[]={{L::right,right},{L::center,center},
		{L::left,left},{L::upright,upright},{L::up,up},{L::upleft,upleft},
		{L::vehicle,vehicle},{L::item,item},{L::designation,designation}};
	for(const auto &[layer,array]:layer_arrays)
		{
		char name[64];snprintf(name,sizeof name,"staged, %s hidden alone",array_names[array]);
		const uint16_t mask=visual_layer_bit(layer);
		check(name,bits({array}),[mask](auto *vp,int32_t x,int32_t y,const auto &repaint)
			{repaint_staged(vp,x,y,mask,false,repaint);});
		}
	// A viewport without an interface array is repainted without touching it.
	const auto no_interface=[](df::graphic_viewportst &vp){vp.screentexpos_interface=nullptr;};
	check("staged, interface deferred, no interface array",bits({item}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_staged(vp,x,y,visual_layer_bit(L::item),true,repaint);},no_interface);
	check("above main group, no interface array",
		base_arrays|bits({vermin,item,vehicle,left,center,right}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_above(vp,x,y,G::main,0,repaint);},no_interface);
	// The interface-only pass leaves the interface and the top shadow in place.
	check("interface only",
		base_arrays|bits({vermin,building_two,projectile,high_flow,signpost,item,vehicle,left,
			center,right,upleft,up,upright,designation}),
		[](auto *vp,int32_t x,int32_t y,const auto &repaint)
		{repaint_interface_only(vp,x,y,repaint);});
}

void test_blank_checks()
{
	viewportst v;
	if(!tile_paints_nothing(&v.vp,0))printf("all-zero tile paints something\n"),++failures;
	// One non-zero entry in any of the 25 arrays makes the tile paint.
	int k=0;
	visit_tile_arrays(&v.vp,[&](const auto *array)
		{
		using T=std::remove_cv_t<std::remove_pointer_t<decltype(array)>>;
		T *writable=const_cast<T *>(array);
		writable[4]=1;
		if(tile_paints_nothing(&v.vp,4))printf("%s alone not seen\n",array_names[k]),++failures;
		if(!tile_paints_nothing(&v.vp,3))printf("%s leaks to tile 3\n",array_names[k]),++failures;
		writable[4]=0;
		++k;
		return true;
		});
	// The glide summary answers the same for its viewport and nothing for another.
	v.words[top_shadow][2]=5;
	blank_summariest<df::graphic_viewportst> summaries;
	const df::graphic_viewportst *list[1]={&v.vp};
	summaries.summarize(list,[](const df::graphic_viewportst *vp){return vp;});
	for(int32_t i=0;i<tile_count;++i)
		if(summaries.known_blank(&v.vp,i)!=(i!=2))
			printf("summary wrong on tile %d\n",i),++failures;
	viewportst other;
	if(summaries.known_blank(&other.vp,0))
		printf("summary answers for another viewport\n"),++failures;
	summaries.clear();
	if(summaries.known_blank(&v.vp,0))printf("summary answers after clear\n"),++failures;
}

void test_interface_pass_readable()
{
	viewportst v;
	if(!interface_pass_readable(&v.vp))printf("readable viewport not readable\n"),++failures;
	if(interface_pass_readable(static_cast<df::graphic_viewportst *>(nullptr)))
		printf("null viewport readable\n"),++failures;
	// Each array the pass zeroes must be checked; the top shadow is not zeroed by it.
	int32_t *df::graphic_viewportst::*const zeroed[]=
		{
		&df::graphic_viewportst::screentexpos_interface,
		&df::graphic_viewportst::screentexpos_background,
		&df::graphic_viewportst::screentexpos_background_two,
		&df::graphic_viewportst::screentexpos_spatter,
		&df::graphic_viewportst::screentexpos_building_one,
		&df::graphic_viewportst::screentexpos_vermin,
		&df::graphic_viewportst::screentexpos_building_two,
		&df::graphic_viewportst::screentexpos_projectile,
		&df::graphic_viewportst::screentexpos_high_flow,
		&df::graphic_viewportst::screentexpos_signpost
		};
	for(auto member:zeroed)
		{
		int32_t *saved=v.vp.*member;v.vp.*member=nullptr;
		if(interface_pass_readable(&v.vp))printf("null array not noticed\n"),++failures;
		v.vp.*member=saved;
		}
	uint64_t *df::graphic_viewportst::*const zeroed64[]=
		{
		&df::graphic_viewportst::screentexpos_floor_flag,
		&df::graphic_viewportst::screentexpos_ramp_flag
		};
	for(auto member:zeroed64)
		{
		uint64_t *saved=v.vp.*member;v.vp.*member=nullptr;
		if(interface_pass_readable(&v.vp))printf("null flag array not noticed\n"),++failures;
		v.vp.*member=saved;
		}
	uint32_t *df::graphic_viewportst::*const zeroed32[]=
		{
		&df::graphic_viewportst::screentexpos_liquid_flag,
		&df::graphic_viewportst::screentexpos_spatter_flag,
		&df::graphic_viewportst::screentexpos_shadow_flag
		};
	for(auto member:zeroed32)
		{
		uint32_t *saved=v.vp.*member;v.vp.*member=nullptr;
		if(interface_pass_readable(&v.vp))printf("null flag array not noticed\n"),++failures;
		v.vp.*member=saved;
		}
}

}

int main()
{
	test_passes();
	test_blank_checks();
	test_interface_pass_readable();
	if(failures!=0){printf("tile repaint tests: %d failures\n",failures);return 1;}
	printf("tile repaint tests: OK\n");
	return 0;
}
