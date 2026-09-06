#pragma once
#include "df/item.h"
#include "df/material.h"
namespace DFHack {
// The harness: an item is made of its own material when it carries one, else of a shared
// material with a boulder sprite.
inline df::material harness_material{6000,0,0,0};
struct MaterialInfo { df::material *material=nullptr;
	explicit MaterialInfo(const df::item *item){if(item)material=item->harness_material?item->harness_material:&harness_material;}
	bool isValid() const {return material!=nullptr;} }; }
