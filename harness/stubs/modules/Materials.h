#pragma once
#include "df/item.h"
#include "df/material.h"
namespace DFHack {
// The harness: every item is made of one material with a boulder sprite.
inline df::material harness_material{6000,0,0,0};
struct MaterialInfo { df::material *material=nullptr; explicit MaterialInfo(const df::item *item){if(item)material=&harness_material;} bool isValid() const {return material!=nullptr;} }; }
