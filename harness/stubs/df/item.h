#pragma once
#include "df/item_type.h"
#include "df/material.h"
namespace df { struct item { item_type type=item_type::NONE; material *harness_material=nullptr;
	bool harness_no_material=false; item_type getType() const {return type;} }; }
