#pragma once
#include <array>
#include <cstdint>
#include "df/graphic_viewportst.h"
namespace df { struct graphic {
	graphic_viewportst *main_viewport=nullptr; std::array<graphic_viewportst*,8> lower_viewport{};
	int32_t precise_mouse_x=0; int32_t precise_mouse_y=0; int16_t force_full_display_count=0;
	int32_t dimx=0; int32_t dimy=0; }; }
