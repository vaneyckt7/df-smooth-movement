#pragma once
#include <cstdint>
#include <vector>
#include "df/unit_inventory_item.h"
namespace df { struct unit { struct { int16_t x=0,y=0,z=0; } pos; std::vector<unit_inventory_item*> inventory; }; }
