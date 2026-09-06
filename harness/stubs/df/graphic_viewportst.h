#pragma once
#include <array>
#include <cstdint>
namespace df {
union graphic_viewport_flag { uint32_t whole; struct { uint32_t active:1; } bits; };
struct graphic_viewportst {
	graphic_viewport_flag flag{};
	int32_t dim_x=0; int32_t dim_y=0;
	std::array<int32_t,2> clipx{}; std::array<int32_t,2> clipy{};
	int32_t screen_x=0; int32_t screen_y=0;
	int32_t *screentexpos_background=nullptr; uint64_t *screentexpos_floor_flag=nullptr;
	int32_t *screentexpos_background_two=nullptr; uint32_t *screentexpos_liquid_flag=nullptr;
	uint32_t *screentexpos_spatter_flag=nullptr; int32_t *screentexpos_spatter=nullptr;
	uint64_t *screentexpos_ramp_flag=nullptr; uint32_t *screentexpos_shadow_flag=nullptr;
	int32_t *screentexpos_building_one=nullptr; int32_t *screentexpos_item=nullptr;
	int32_t *screentexpos_vehicle=nullptr; int32_t *screentexpos_vermin=nullptr;
	int32_t *screentexpos_left_creature=nullptr; int32_t *screentexpos=nullptr;
	int32_t *screentexpos_right_creature=nullptr; int32_t *screentexpos_building_two=nullptr;
	int32_t *screentexpos_projectile=nullptr; int32_t *screentexpos_high_flow=nullptr;
	int32_t *screentexpos_top_shadow=nullptr; int32_t *screentexpos_signpost=nullptr;
	int32_t *screentexpos_upleft_creature=nullptr; int32_t *screentexpos_up_creature=nullptr;
	int32_t *screentexpos_upright_creature=nullptr; int32_t *screentexpos_designation=nullptr;
	int32_t *screentexpos_interface=nullptr;
	int32_t *screentexpos_background_old=nullptr; uint64_t *screentexpos_floor_flag_old=nullptr;
	int32_t *screentexpos_background_two_old=nullptr; uint32_t *screentexpos_liquid_flag_old=nullptr;
	uint32_t *screentexpos_spatter_flag_old=nullptr; int32_t *screentexpos_spatter_old=nullptr;
	uint64_t *screentexpos_ramp_flag_old=nullptr; uint32_t *screentexpos_shadow_flag_old=nullptr;
	int32_t *screentexpos_building_one_old=nullptr; int32_t *screentexpos_item_old=nullptr;
	int32_t *screentexpos_vehicle_old=nullptr; int32_t *screentexpos_vermin_old=nullptr;
	int32_t *screentexpos_left_creature_old=nullptr; int32_t *screentexpos_old=nullptr;
	int32_t *screentexpos_right_creature_old=nullptr; int32_t *screentexpos_building_two_old=nullptr;
	int32_t *screentexpos_projectile_old=nullptr; int32_t *screentexpos_high_flow_old=nullptr;
	int32_t *screentexpos_top_shadow_old=nullptr; int32_t *screentexpos_signpost_old=nullptr;
	int32_t *screentexpos_upleft_creature_old=nullptr; int32_t *screentexpos_up_creature_old=nullptr;
	int32_t *screentexpos_upright_creature_old=nullptr; int32_t *screentexpos_designation_old=nullptr;
	int32_t *screentexpos_interface_old=nullptr;
};
}
