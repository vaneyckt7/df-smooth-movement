#pragma once
#include "df/item.h"
namespace df {
enum class inv_item_role_type { Hauled=0, Worn=1 };
struct unit_inventory_item { item *item=nullptr; inv_item_role_type mode=inv_item_role_type::Worn; }; }
