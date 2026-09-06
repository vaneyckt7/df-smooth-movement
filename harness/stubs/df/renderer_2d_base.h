#pragma once
#include <cstdint>
#include <unordered_map>
#include "df/graphic_viewportst.h"
#include "df/texture_fullid.h"
namespace df {
struct tile_cachest { std::unordered_map<texture_fullid,void*> tile_cache; };
struct renderer_2d_base {
	void *sdl_renderer=nullptr;
	tile_cachest tile_cache;
	int32_t origin_x=0; int32_t origin_y=0; int32_t viewport_zoom_factor=128;
	virtual ~renderer_2d_base()=default;
	virtual void update_viewport_tile(graphic_viewportst *,int32_t,int32_t){}
	virtual void update_all(){}
};
}
