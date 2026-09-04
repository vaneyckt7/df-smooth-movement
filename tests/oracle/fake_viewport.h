#pragma once
#include <array>
#include <cstdint>
#include <vector>
// A stand-in for df::graphic_viewportst with the members the plugin touches, same types.
struct fake_viewportst
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

	// Storage behind the pointers, so a viewport is a value the harness can copy and compare.
	std::vector<int32_t> i32[29];
	std::vector<uint32_t> u32[3];
	std::vector<uint64_t> u64[2];

	void allocate(int32_t dx,int32_t dy)
		{
		dim_x=dx;
		dim_y=dy;
		const size_t n=size_t(dx)*size_t(dy);
		for(auto &v:i32)v.assign(n,0);
		for(auto &v:u32)v.assign(n,0);
		for(auto &v:u64)v.assign(n,0);
		bind();
		}

	void bind()
		{
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
			&screentexpos_designation_old};
		static_assert(sizeof(i32_members)/sizeof(i32_members[0])==29);
		for(size_t i=0;i<29;++i)*i32_members[i]=i32[i].data();
		uint32_t **u32_members[]={&screentexpos_liquid_flag,&screentexpos_spatter_flag,&screentexpos_shadow_flag};
		for(size_t i=0;i<3;++i)*u32_members[i]=u32[i].data();
		uint64_t **u64_members[]={&screentexpos_floor_flag,&screentexpos_ramp_flag};
		for(size_t i=0;i<2;++i)*u64_members[i]=u64[i].data();
		}
};
