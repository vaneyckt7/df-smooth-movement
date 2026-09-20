// SPDX-License-Identifier: MIT
//
// Handing one of the plugin's sprites to SDL. Where a gliding sprite belongs on screen is
// sprite_placement.h's answer; this is what is done with it: the rectangle a creature's
// sprite is copied into, the smaller rectangle a hauled item's icon sits in on top of it,
// and which of SDL's two copy calls a sprite that faces the mirrored way needs. Generic over
// the renderer type, the game's renderer_2d_base or a test double with the same members, and
// over the proxy type, so the only thing it takes from the plugin around it is the table of
// SDL functions bound at load.

#pragma once

#include "plugin_state.h"
#include "sprite_placement.h"
#include "visual_animation.h"

#include <SDL_render.h>

// The render_copy_ex_f null check is defensive only, not a graceful-degradation path.
// `bind` aborts load_sdl on any missing symbol and plugin_enable then refuses the render hook.
inline void render_copy_maybe_mirrored(
	const sdl_apist &sdl,
	SDL_Renderer *renderer,
	SDL_Texture *texture,
	const SDL_FRect &destination,
	bool mirrored)
{
	if(mirrored&&sdl.render_copy_ex_f!=nullptr)
		{
		sdl.render_copy_ex_f(
			renderer,texture,nullptr,&destination,
			0.0,nullptr,SDL_FLIP_HORIZONTAL);
		return;
		}
	sdl.render_copy_f(renderer,texture,nullptr,&destination);
}

template<typename Renderer,typename Proxy>
void draw_proxy(const sdl_apist &sdl,Renderer *renderer,const Proxy &proxy)
{
	const sprite_placementst placement=place_sprite(renderer,proxy);
	const SDL_FRect destination=
		{
		placement.x_px+float(proxy.mirror_shift)*placement.tile_size_px,
		placement.y_px,
		placement.tile_size_px,
		placement.tile_size_px
		};
	render_copy_maybe_mirrored(
		sdl,
		static_cast<SDL_Renderer *>(renderer->sdl_renderer),
		proxy.texture,
		destination,
		proxy.mirrored);
}

template<typename Renderer,typename Proxy>
void draw_carried_item_proxy(const sdl_apist &sdl,Renderer *renderer,const Proxy &proxy)
{
	// The icon rides the creature's movement so it stays on the sprite that carries it.
	const sprite_placementst placement=place_sprite(renderer,proxy);
	const auto icon=carried_item_icon_rect(placement.x_px,placement.y_px,placement.tile_size_px);
	const SDL_FRect destination={icon.x_px,icon.y_px,icon.width_px,icon.height_px};
	sdl.render_copy_f(
		static_cast<SDL_Renderer *>(renderer->sdl_renderer),
		proxy.texture,nullptr,&destination);
}
