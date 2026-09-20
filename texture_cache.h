// SPDX-License-Identifier: MIT
//
// Finding the texture the game has already made for a sprite. The game builds a texture the
// first time it draws something and keeps it in the renderer's tile cache, keyed by what was
// asked for; the plugin draws those same things again at their in-between positions, so it
// looks one up there rather than making its own. A hauled item is the awkward case, because
// the game never draws one: nothing has put it in the cache, and the only way to get it there
// is to stage the item on a tile and ask for a repaint. Which item a creature is hauling and
// which texture position its material uses are answered here too, since that is the question
// the lookup is asked. Generic over the renderer type, the game's renderer_2d_base or a test
// double with a tile cache and an update_viewport_tile of the same shape. It takes the
// plugin's state as an argument rather than reading the plugin's own, which is what
// plugin_commands.h does and for the same reason: a test can hand it a state of its own.

#pragma once

#include "modules/Materials.h"

#include "df/graphic_viewportst.h"
#include "df/item.h"
#include "df/item_type.h"
#include "df/material.h"
#include "df/texture_fullid.h"
#include "df/unit.h"
#include "df/unit_inventory_item.h"

#include "plugin_state.h"
#include "tile_redraw.h"
#include "tile_repaint.h"

#include <SDL_render.h>

#include <cstdint>

template<typename Renderer>
SDL_Texture *cached_texture(
	Renderer *renderer,
	int32_t texpos,
	bool transparent_background=true)
{
	if(texpos==0)return nullptr;
	df::texture_fullid texture_id;
	texture_id.texpos=texpos;
	texture_id.r=texture_id.g=texture_id.b=1.0f;
	texture_id.br=texture_id.bg=texture_id.bb=0.0f;
	// Both arms are the bitfield's own width. Writing the bare 0 instead makes one arm an
	// enumeration and the other an int, which is a warning and reads as if the two were
	// different kinds of thing; they are the same field, set and clear.
	texture_id.flag=transparent_background?
		uint32_t(df::texture_fullid_flag::mask_transparent_background):
		uint32_t(0);
	const auto texture=renderer->tile_cache.tile_cache.find(texture_id);
	return texture==renderer->tile_cache.tile_cache.end()?
		nullptr:
		static_cast<SDL_Texture *>(texture->second);
}

inline df::item *hauled_item(const df::unit *unit)
{
	if(unit==nullptr)return nullptr;
	for(const df::unit_inventory_item *inventory_item:unit->inventory)
		if(inventory_item!=nullptr&&inventory_item->item!=nullptr&&
			inventory_item->mode==df::inv_item_role_type::Hauled)
			return inventory_item->item;
	return nullptr;
}

template<typename Renderer>
SDL_Texture *cached_viewport_texture(
	plugin_statest &state,
	Renderer *renderer,
	df::graphic_viewportst *vp,
	int32_t index,
	int32_t texpos)
{
	if(texpos==0)return nullptr;
	SDL_Texture *texture=cached_texture(renderer,texpos);
	if(texture!=nullptr||vp->screentexpos_background_two==nullptr)return texture;
	// Hauled items are not normally drawn, so stage one tile to populate the renderer cache.
	scoped_value_restorest<int32_t> staged(vp->screentexpos_background_two[index]);
	vp->screentexpos_background_two[index]=texpos;
	game_repaint(state,renderer,vp,index/vp->dim_y,index%vp->dim_y);
	return cached_texture(renderer,texpos);
}

inline int32_t item_texpos(df::item *item)
{
	if(item==nullptr)return 0;
	const DFHack::MaterialInfo material(item);
	if(!material.isValid())return 0;
	switch(item->getType())
		{
		case df::item_type::BOULDER:
			return material.material->boulder_texpos1!=0?
				material.material->boulder_texpos1:
				material.material->boulder_texpos2;
		case df::item_type::BAR:
			return material.material->bar_texpos;
		case df::item_type::WOOD:
			return material.material->wood_texpos;
		default:
			return 0;
		}
}
