#pragma once
#include "df/item_type.h"
namespace df { struct item { item_type type=item_type::NONE; item_type getType() const {return type;} }; }
