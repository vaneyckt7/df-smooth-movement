// SPDX-License-Identifier: MIT

#include "Core.h"
#include "MemAccess.h"
#include "PluginManager.h"
#include "VTableInterpose.h"

#include "modules/DFSDL.h"
#include "modules/Materials.h"
#include "modules/Units.h"

#include "df/enabler.h"
#include "df/graphic.h"
#include "df/graphic_viewportst.h"
#include "df/item.h"
#include "df/item_type.h"
#include "df/material.h"
#include "df/plotinfost.h"
#include "df/renderer_2d_base.h"
#include "df/texture_fullid.h"
#include "df/unit.h"
#include "df/unit_inventory_item.h"
#include "df/viewport_spatter_flag.h"
#include "df/viewscreen_dungeonmodest.h"
#include "df/viewscreen_dwarfmodest.h"
#include "df/world.h"

#include "frame_record.h"
#include "frame_recorder.h"
#include "frame_stats.h"
#include "free_camera.h"
#include "plugin_commands.h"
#include "plugin_state.h"
#include "sprite_proxies.h"
#include "tile_coverage.h"
#include "tile_repaint.h"
#include "view_context.h"
#include "visual_animation.h"

#include <SDL_render.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace DFHack;

DFHACK_PLUGIN("smooth-movement");
DFHACK_PLUGIN_IS_ENABLED(is_enabled);

REQUIRE_GLOBAL(enabler);
REQUIRE_GLOBAL(gps);
REQUIRE_GLOBAL(pause_state);
REQUIRE_GLOBAL(plotinfo);
REQUIRE_GLOBAL(window_x);
REQUIRE_GLOBAL(window_y);
REQUIRE_GLOBAL(window_z);
REQUIRE_GLOBAL(world);

namespace {

constexpr const char *plugin_version="0.5.0";

// Runs a callback when the scope ends, whichever return path is taken.
template<typename Callback>
struct scope_guardst
{
	Callback callback;
	explicit scope_guardst(Callback c):callback(std::move(c)){}
	~scope_guardst(){callback();}
	scope_guardst(const scope_guardst &)=delete;
	scope_guardst &operator=(const scope_guardst &)=delete;
};

plugin_statest state;

double tile_px(const df::renderer_2d_base *renderer)
{
	const int32_t zoom=renderer->viewport_zoom_factor;
	return double(zoom==128?32:std::max(1,zoom*32/128));
}

// Scrolls the game's window position by whole tiles for the camera and says which axes it
// applied: an axis without a window, or that would go negative, is left alone.
std::array<bool,2> scroll_window(int32_t kx,int32_t ky)
{
	std::array<bool,2> applied={false,false};
	if(kx!=0&&window_x!=nullptr&&*window_x+kx>=0)
		{
		*window_x+=kx;
		applied[0]=true;
		}
	if(ky!=0&&window_y!=nullptr&&*window_y+ky>=0)
		{
		*window_y+=ky;
		applied[1]=true;
		}
	return applied;
}

// What the camera observes on this frame.
camera_framest camera_frame(
	const df::graphic_viewportst *vp,
	double tile,
	uint32_t delta_ms,
	bool native_follow_active)
{
	return {
		vp,
		window_x?*window_x:0,
		window_y?*window_y:0,
		enabler!=nullptr&&enabler->mouse_mbut,
		gps?gps->precise_mouse_x:0,
		gps?gps->precise_mouse_y:0,
		tile,
		delta_ms,
		native_follow_active
		};
}

view_signaturest view_signature(
	const df::renderer_2d_base *renderer,
	const df::graphic_viewportst *vp)
{
	return {
		window_z?*window_z:0,
		vp->dim_x,
		vp->dim_y,
		vp->clipx[0],
		vp->clipx[1],
		vp->clipy[0],
		vp->clipy[1],
		renderer->viewport_zoom_factor,
		renderer->origin_x,
		renderer->origin_y,
		gps->dimx,
		gps->dimy
		};
}

void update_visual_context(
	const df::renderer_2d_base *renderer,
	const df::graphic_viewportst *vp)
{
	const view_context_changest change=state.render.view_context.observe(
		vp,view_signature(renderer,vp),window_x?*window_x:0,window_y?*window_y:0);
	if(change.reset)state.render.camera.cancel_transients();
	if(change.panned)state.render.previous_coverage.clear();
}

viewport_visual_animation_inputst animation_input(df::graphic_viewportst *vp)
{
	const df::graphic_viewportst *const_viewport=vp;
	return {
		vp,
		vp->dim_x,
		vp->dim_y,
		state.render.view_context.revision(),
		visual_layers(const_viewport),
		visual_layers(const_viewport,true),
		vp->screentexpos_background,
		vp->screentexpos_background_old,
		window_x?*window_x:0,
		window_y?*window_y:0,
		state.render.drawn_buffers.frame_simulation_tick
		};
}

// The layer buffers are freed and nulled without clearing the active flag.
bool viewport_readable(df::graphic_viewportst *vp)
{
	return vp!=nullptr&&vp->flag.bits.active&&animation_input(vp).valid();
}

int32_t tile_pixel(int32_t tile,int32_t origin,int32_t zoom)
{
	return zoom==128?32*tile+origin:(zoom*32*tile)/128+origin;
}

struct viewport_renderst
{
	df::graphic_viewportst *viewport;
	std::vector<render_proxyst> proxies;
	render_coveragest coverage;
};

// Every tile repaint the plugin asks the game for goes through here so `stats` can count them.
void game_repaint(df::renderer_2d_base *renderer,df::graphic_viewportst *vp,int32_t x,int32_t y)
{
	state.stats.repaints.fetch_add(1,std::memory_order_relaxed);
	++state.render.hook_repaints;
	renderer->update_viewport_tile(vp,x,y);
}

// A staged repaint: skipped when the tile, with the stage's layers hidden, has nothing to
// paint.
void staged_repaint(
	df::renderer_2d_base *renderer,df::graphic_viewportst *vp,int32_t x,int32_t y)
{
	if(tile_paints_nothing(vp,x*vp->dim_y+y))return;
	game_repaint(renderer,vp,x,y);
}

void redraw_viewport_tile(
	df::renderer_2d_base *renderer,
	const viewport_renderst &viewport,
	int32_t x,
	int32_t y,
	bool defer_interface)
{
	df::graphic_viewportst *vp=viewport.viewport;
	const int32_t index=x*vp->dim_y+y;
	if(state.render.blank_summaries.known_blank(vp,index))return;
	repaint_staged(
		vp,x,y,selected_mask(viewport.coverage.selected,index),defer_interface,
		[renderer](df::graphic_viewportst *vp,int32_t x,int32_t y)
			{
			staged_repaint(renderer,vp,x,y);
			});
}

// Runs after the proxies so the shading covers them rather than sitting underneath.
void draw_interface_only(
	df::renderer_2d_base *renderer,
	df::graphic_viewportst *vp,
	int32_t x,
	int32_t y)
{
	if(!interface_pass_readable(vp))return;
	if(state.render.blank_summaries.known_blank(vp,x*vp->dim_y+y))return;
	repaint_interface_only(
		vp,x,y,[renderer](df::graphic_viewportst *vp,int32_t x,int32_t y)
			{
			staged_repaint(renderer,vp,x,y);
			});
}

void redraw_world_tile(
	df::renderer_2d_base *renderer,
	const std::vector<viewport_renderst> &viewports,
	const tile_coveragest &staged,
	int32_t x,
	int32_t y)
{
	// The stage pass repaints everything above the lowest across the staged tiles, after the proxies.
	const bool staged_tile=staged.count({x,y})!=0;
	for(const viewport_renderst &viewport:viewports)
		{
		if(inside_clip(viewport.viewport,x,y))
			redraw_viewport_tile(renderer,viewport,x,y,staged_tile);
		if(staged_tile)break;
		}
}

void redraw_above(
	df::renderer_2d_base *renderer,
	df::graphic_viewportst *vp,
	int32_t x,
	int32_t y,
	visual_render_groupst group,
	const std::unordered_map<int32_t,uint16_t> &selected)
{
	const int32_t index=x*vp->dim_y+y;
	if(state.render.blank_summaries.known_blank(vp,index))return;
	repaint_above(
		vp,x,y,group,selected_mask(selected,index),
		[renderer](df::graphic_viewportst *vp,int32_t x,int32_t y)
			{
			staged_repaint(renderer,vp,x,y);
			});
}

SDL_Texture *cached_texture(
	df::renderer_2d_base *renderer,
	int32_t texpos,
	bool transparent_background=true)
{
	if(texpos==0)return nullptr;
	df::texture_fullid texture_id;
	texture_id.texpos=texpos;
	texture_id.r=texture_id.g=texture_id.b=1.0f;
	texture_id.br=texture_id.bg=texture_id.bb=0.0f;
	texture_id.flag=transparent_background?
		df::texture_fullid_flag::mask_transparent_background:
		0;
	const auto texture=renderer->tile_cache.tile_cache.find(texture_id);
	return texture==renderer->tile_cache.tile_cache.end()?
		nullptr:
		static_cast<SDL_Texture *>(texture->second);
}

// The render_copy_ex_f null check is defensive only, not a graceful-degradation path.
// `bind` aborts load_sdl on any missing symbol and plugin_enable then refuses the render hook.
void render_copy_maybe_mirrored(
	SDL_Renderer *renderer,
	SDL_Texture *texture,
	const SDL_FRect &destination,
	bool mirrored)
{
	if(mirrored&&state.sdl.render_copy_ex_f!=nullptr)
		{
		state.sdl.render_copy_ex_f(
			renderer,texture,nullptr,&destination,
			0.0,nullptr,SDL_FLIP_HORIZONTAL);
		return;
		}
	state.sdl.render_copy_f(renderer,texture,nullptr,&destination);
}

void draw_proxy(df::renderer_2d_base *renderer,const render_proxyst &proxy)
{
	const int32_t zoom=renderer->viewport_zoom_factor;
	const int32_t target_x=tile_pixel(proxy.target_x,renderer->origin_x,zoom);
	const int32_t target_y=tile_pixel(proxy.target_y,renderer->origin_y,zoom);
	const float tile_size=float(zoom==128?32:std::max(1,zoom*32/128));
	const float source_x=target_x+(proxy.source_x-proxy.target_x)*tile_size;
	const float source_y=target_y+(proxy.source_y-proxy.target_y)*tile_size;
	const float mirror_offset=float(proxy.mirror_shift)*tile_size;
	// A bobbing sprite is lifted towards the row above its path; the lift is zero at both
	// ends of the step, so it lands on the grid.
	const float bob_offset=proxy.bob?
		-state.bob.lift(proxy.source_x,proxy.source_y,proxy.target_x,proxy.target_y,
			proxy.progress)*tile_size:
		0.0f;
	const SDL_FRect destination=
		{
		source_x+(target_x-source_x)*proxy.progress+mirror_offset,
		source_y+(target_y-source_y)*proxy.progress+bob_offset,
		tile_size,
		tile_size
		};
	render_copy_maybe_mirrored(
		static_cast<SDL_Renderer *>(renderer->sdl_renderer),
		proxy.texture,
		destination,
		proxy.mirrored);
}

void draw_carried_item_proxy(
	df::renderer_2d_base *renderer,
	const carried_item_proxyst &proxy)
{
	const int32_t zoom=renderer->viewport_zoom_factor;
	const float tile_size=float(zoom==128?32:std::max(1,zoom*32/128));
	const float target_x=tile_pixel(proxy.target_x,renderer->origin_x,zoom);
	const float target_y=tile_pixel(proxy.target_y,renderer->origin_y,zoom);
	const float source_x=target_x+(proxy.source_x-proxy.target_x)*tile_size;
	const float source_y=target_y+(proxy.source_y-proxy.target_y)*tile_size;
	// The icon rides the creature's walk bob so it stays on the sprite that carries it.
	const float bob_offset=proxy.bob?
		-state.bob.lift(proxy.source_x,proxy.source_y,proxy.target_x,proxy.target_y,
			proxy.progress)*tile_size:
		0.0f;
	const auto icon=carried_item_icon_rect(
		source_x+(target_x-source_x)*proxy.progress,
		source_y+(target_y-source_y)*proxy.progress+bob_offset,
		tile_size);
	const SDL_FRect destination={icon.x,icon.y,icon.width,icon.height};
	state.sdl.render_copy_f(
		static_cast<SDL_Renderer *>(renderer->sdl_renderer),
		proxy.texture,nullptr,&destination);
}

df::item *hauled_item(const df::unit *unit)
{
	if(unit==nullptr)return nullptr;
	for(const df::unit_inventory_item *inventory_item:unit->inventory)
		if(inventory_item!=nullptr&&inventory_item->item!=nullptr&&
			inventory_item->mode==df::inv_item_role_type::Hauled)
			return inventory_item->item;
	return nullptr;
}

SDL_Texture *cached_viewport_texture(
	df::renderer_2d_base *renderer,
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
	game_repaint(renderer,vp,index/vp->dim_y,index%vp->dim_y);
	return cached_texture(renderer,texpos);
}

int32_t item_texpos(df::item *item)
{
	if(item==nullptr)return 0;
	const MaterialInfo material(item);
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

// The active viewports with their recording slots, for the recorder.
frame_recorderst::viewport_listst recorded_viewports()
{
	frame_recorderst::viewport_listst viewports;
	if(gps==nullptr)return viewports;
	for(int slot=0;slot<frame_record::main_slot;++slot)
		{
		const df::graphic_viewportst *vp=gps->lower_viewport[slot];
		if(vp!=nullptr&&vp->flag.bits.active)viewports.emplace_back(slot,vp);
		}
	const df::graphic_viewportst *vp=gps->main_viewport;
	if(vp!=nullptr&&vp->flag.bits.active)viewports.emplace_back(frame_record::main_slot,vp);
	return viewports;
}

// Hands the recorder what the hook reads at the start of a frame.
void record_frame_start(df::renderer_2d_base *renderer,uint32_t now_ms)
{
	state.recorder.capture([&](bool first)
		{
		if(first)
			{
			state.reset_visual();
			if(gps!=nullptr)++gps->force_full_display_count;
			}
		frame_recorderst::frame_inputst input;
		frame_record::frame_headerst &header=input.header;
		header.flip=state.flip_enabled;
		header.hauled=state.hauled_enabled;
		header.camera=state.render.camera.is_enabled();
		header.linear=state.render.animation_manager.is_linear();
		header.step_ms=state.render.animation_manager.step_duration_ms();
		header.simulation_tick=state.render.drawn_buffers.frame_simulation_tick;
		header.bob=state.bob;
		header.tick_ms=now_ms;
		header.window_x=window_x?*window_x:0;
		header.window_y=window_y?*window_y:0;
		header.window_z=window_z?*window_z:0;
		header.paused=pause_state&&*pause_state;
		header.follow_unit=plotinfo?plotinfo->follow_unit:-1;
		header.mouse_x=gps?gps->precise_mouse_x:0;
		header.mouse_y=gps?gps->precise_mouse_y:0;
		header.mouse_mbut=enabler!=nullptr&&enabler->mouse_mbut;
		header.zoom=renderer->viewport_zoom_factor;
		header.origin_x=renderer->origin_x;
		header.origin_y=renderer->origin_y;
		header.dimx=gps?gps->dimx:0;
		header.dimy=gps?gps->dimy:0;
		header.rest_x=state.render.camera.rest_offset_x();
		header.rest_y=state.render.camera.rest_offset_y();
		input.viewports=recorded_viewports();
		return input;
		});
}

// Hands the recorder the units in view, where the hook looks them up.
void record_frame_units(df::renderer_2d_base *renderer)
{
	state.recorder.capture_units([&]
		{
		std::vector<frame_record::unit_recordst> records;
		const df::graphic_viewportst *vp=gps?gps->main_viewport:nullptr;
		if(vp!=nullptr&&window_x!=nullptr&&window_y!=nullptr&&window_z!=nullptr)
			{
			std::vector<df::unit *> units;
			Units::getUnitsInBox(
				units,
				*window_x,*window_y,*window_z,
				*window_x+vp->dim_x-1,*window_y+vp->dim_y-1,*window_z,
				[](df::unit *unit){return !Units::isHidden(unit);});
			for(const df::unit *unit:units)
				{
				const int32_t texpos=item_texpos(hauled_item(unit));
				records.push_back({unit->pos.x,unit->pos.y,unit->pos.z,texpos,
					texpos!=0&&cached_texture(renderer,texpos)!=nullptr});
				}
			}
		return records;
		});
}

std::vector<carried_item_proxyst> collect_carried_item_proxies(
	df::renderer_2d_base *renderer,
	df::graphic_viewportst *vp)
{
	std::vector<carried_item_proxyst> proxies;
	if(window_x==nullptr||window_y==nullptr||window_z==nullptr)return proxies;

	std::vector<df::unit *> units;
	Units::getUnitsInBox(
		units,
		*window_x,*window_y,*window_z,
		*window_x+vp->dim_x-1,*window_y+vp->dim_y-1,*window_z,
		[](df::unit *unit){return !Units::isHidden(unit);});
	for(const df::unit *unit:units)
		{
		const int32_t x=unit->pos.x-*window_x;
		const int32_t y=unit->pos.y-*window_y;
		if(!inside_clip(vp,x,y))continue;
		const int32_t index=x*vp->dim_y+y;
		if(vp->screentexpos[index]==0)continue;
		const int32_t texpos=item_texpos(hauled_item(unit));
		SDL_Texture *texture=cached_viewport_texture(renderer,vp,index,texpos);
		if(texture==nullptr)continue;
		const auto movement=state.render.animation_manager.get_movement(
			vp,viewport_visual_layer::center,x,y);
		const float source_x=movement.active?movement.source_x:float(x);
		const float source_y=movement.active?movement.source_y:float(y);
		carried_item_proxyst proxy={
			source_x,source_y,x,y,movement.active?movement.progress:1.0f,texture,false,{}};
		for(int32_t coverage_x=int32_t(std::floor(std::min(source_x,float(x))));
			coverage_x<=int32_t(std::ceil(std::max(source_x,float(x))));++coverage_x)
			for(int32_t coverage_y=int32_t(std::floor(std::min(source_y,float(y))));
				coverage_y<=int32_t(std::ceil(std::max(source_y,float(y))));++coverage_y)
				if(inside_clip(vp,coverage_x,coverage_y))
					proxy.coverage.emplace(coverage_x,coverage_y);
		proxies.push_back(std::move(proxy));
		}
	return proxies;
}

std::vector<df::graphic_viewportst *> active_viewports()
{
	std::vector<df::graphic_viewportst *> viewports;
	if(gps==nullptr)return viewports;
	for(int32_t lower=7;lower>=0;--lower)
		{
		df::graphic_viewportst *vp=gps->lower_viewport[lower];
		if(viewport_readable(vp))viewports.push_back(vp);
		}
	if(viewport_readable(gps->main_viewport))
		viewports.push_back(gps->main_viewport);
	return viewports;
}

std::vector<viewport_renderst> collect_viewport_renders(
	df::renderer_2d_base *renderer,
	const std::vector<df::graphic_viewportst *> &viewports)
{
	std::vector<viewport_renderst> renders;
	renders.reserve(viewports.size());
	for(df::graphic_viewportst *vp:viewports)
		{
		viewport_renderst render=
			{
			vp,
			collect_proxies(
				vp,state.render.animation_manager,state.flip_enabled,state.bob.enabled,
				[renderer](int32_t texpos){return cached_texture(renderer,texpos);}),
			{}
			};
		render.coverage=collect_coverage(render.proxies,vp->dim_y);
		renders.push_back(std::move(render));
		}
	return renders;
}

void draw_interpolation_stages(
	df::renderer_2d_base *renderer,
	df::graphic_viewportst *vp,
	const std::vector<render_proxyst> &proxies,
	const render_coveragest &coverage)
{
	for(size_t index=0;index<coverage.groups.size();++index)
		{
		const auto group=static_cast<visual_render_groupst>(index);
		for(const render_proxyst &proxy:proxies)
			if(visual_render_group(proxy.layer)==group)draw_proxy(renderer,proxy);
		if(group==visual_render_groupst::designation)continue;
		for(const auto &[x,y]:coverage.groups[index])
			redraw_above(renderer,vp,x,y,group,coverage.selected);
		}
}

void redraw_viewport_tiles(
	df::renderer_2d_base *renderer,
	const viewport_renderst &viewport,
	const tile_coveragest &coverage)
{
	df::graphic_viewportst *vp=viewport.viewport;
	for(const auto &[x,y]:coverage)
		{
		if(!inside_clip(vp,x,y))continue;
		redraw_viewport_tile(renderer,viewport,x,y,true);
		}
}

void draw_viewport_interpolation_stages(
	df::renderer_2d_base *renderer,
	const std::vector<viewport_renderst> &viewports,
	const tile_coveragest &coverage,
	const std::vector<carried_item_proxyst> &carried_items)
{
	for(size_t index=0;index<viewports.size();++index)
		{
		// A lower z-level's proxy must be covered by the next viewport's fog and terrain.
		// Reapply that viewport before its own proxies, matching DF's lower-to-main draw order.
		if(index>0)redraw_viewport_tiles(renderer,viewports[index],coverage);
		const viewport_renderst &viewport=viewports[index];
		draw_interpolation_stages(
			renderer,viewport.viewport,viewport.proxies,viewport.coverage);
		if(index+1==viewports.size())
			for(const carried_item_proxyst &proxy:carried_items)
				draw_carried_item_proxy(renderer,proxy);
		// A viewport shades everything drawn beneath it, so this covers every staged tile.
		// Restricting it to the tiles this viewport has sprites on would not deepen with distance.
		for(const auto &[x,y]:coverage)
			{
			if(inside_clip(viewport.viewport,x,y))
				draw_interface_only(renderer,viewport.viewport,x,y);
			}
		}
}

bool has_mirrored_viewport_facing(
	const std::vector<df::graphic_viewportst *> &viewports)
{
	for(const df::graphic_viewportst *vp:viewports)
		if(state.render.animation_manager.has_mirrored_facing(vp))return true;
	return false;
}

void render_interpolated_world(df::renderer_2d_base *renderer)
{
	state.stats.frames.fetch_add(1,std::memory_order_relaxed);
	// Read once: if the console flipped the flag on mid-frame, the guard would subtract a
	// start time of zero.
	const bool timing_enabled=state.stats.enabled.load(std::memory_order_relaxed);
	// The frame's clock, read once so that a recording carries the value the frame used.
	const uint32_t now_ms=Core::getInstance().p->getTickCount();
	state.render.drawn_buffers.frame_simulation_tick=state.render.drawn_buffers.take_tick();
	record_frame_start(renderer,now_ms);
	state.render.hook_repaints=0;
	state.render.hook_painted=false;
	const uint64_t frame_start_us=timing_enabled?frame_statsst::now_us():0;
	uint64_t sync_end_us=frame_start_us;
	// Runs on every exit, including the early return for frames with nothing to draw.
	const scope_guardst timing([&]
		{
		state.recorder.finish(
			state.render.hook_repaints,state.render.hook_painted,recorded_viewports);
		if(!timing_enabled)return;
		const uint64_t end_us=frame_statsst::now_us();
		state.stats.timed.fetch_add(1,std::memory_order_relaxed);
		state.stats.add_sync(sync_end_us-frame_start_us);
		state.stats.add_render(end_us-sync_end_us);
		});
	df::graphic_viewportst *vp=gps?gps->main_viewport:nullptr;
	const std::vector<df::graphic_viewportst *> viewports=active_viewports();

	if(vp!=nullptr)update_visual_context(renderer,vp);
	const int32_t follow_id=plotinfo?plotinfo->follow_unit:-1;
	if(native_follow_changed(state.render.native_follow_id,follow_id))
		{
		state.render.native_follow_id=follow_id;
		state.render.view_context.bump();
		state.render.previous_coverage.clear();
		state.render.camera.restart();
		}
	state.render.animation_manager.begin_frame(now_ms);
	for(df::graphic_viewportst *viewport:viewports)
		state.render.animation_manager.synchronize_viewport(animation_input(viewport));
	state.render.animation_manager.end_frame();
	if(timing_enabled)sync_end_us=frame_statsst::now_us();

	if(!viewport_readable(vp)||renderer->sdl_renderer==nullptr)
		return;
	const bool paused=pause_state&&*pause_state;
	if(paused)state.render.camera.restart();
	const bool native_follow_active=follow_id>=0;
	const double cam_tile=tile_px(renderer);
	if(!paused)
		state.render.camera.update(
			camera_frame(vp,cam_tile,state.render.animation_manager.get_frame_delta_ms(),
				native_follow_active),
			state.render.animation_manager,scroll_window);
	const int32_t glide_x=state.render.camera.glide_x(cam_tile);
	const int32_t glide_y=state.render.camera.glide_y(cam_tile);
	const bool glide=glide_x!=0||glide_y!=0;
	if(!glide&&state.render.camera_was_offset)
		{
		// The camera just re-joined the grid: one engine redraw replaces the last shifted frame.
		state.render.camera_was_offset=false;
		if(gps!=nullptr)++gps->force_full_display_count;
		}
	if(glide)state.render.camera_was_offset=true;
	record_frame_units(renderer);
	std::vector<carried_item_proxyst> carried_items=
		state.hauled_enabled?collect_carried_item_proxies(renderer,vp):
		std::vector<carried_item_proxyst>{};
	if(!glide&&!state.render.animation_manager.requires_full_redraw()&&
		(!state.flip_enabled||!has_mirrored_viewport_facing(viewports))&&
		carried_items.empty()&&state.render.previous_coverage.empty())
		return;
	state.stats.painted.fetch_add(1,std::memory_order_relaxed);
	state.render.hook_painted=true;

	std::vector<viewport_renderst> viewport_renders=
		collect_viewport_renders(renderer,viewports);
	tile_coveragest coverage=collect_viewport_coverage(viewport_renders);
	// A hauled icon bobs with the creature under it: the main viewport's centre proxy on the
	// same tile at the same point of the same step. That proxy's coverage already holds the
	// row the lift reaches into, so the icon adds nothing to it.
	if(state.bob.enabled&&!carried_items.empty()&&!viewport_renders.empty())
		for(carried_item_proxyst &item:carried_items)
			for(const render_proxyst &proxy:viewport_renders.back().proxies)
				if(proxy.bob&&proxy.layer==viewport_visual_layer::center&&
					proxy.target_x==item.target_x&&proxy.target_y==item.target_y&&
					proxy.source_x==item.source_x&&proxy.source_y==item.source_y&&
					proxy.progress==item.progress)
					{
					item.bob=true;
					break;
					}
	for(const carried_item_proxyst &proxy:carried_items)
		coverage.insert(proxy.coverage.begin(),proxy.coverage.end());

	SDL_Renderer *sdl_renderer=static_cast<SDL_Renderer *>(renderer->sdl_renderer);
	const int32_t zoom=renderer->viewport_zoom_factor;
	const int32_t tile_size=zoom==128?32:std::max(1,zoom*32/128);

	if(glide)
		{
		// Camera mid-glide: repaint the WHOLE map rect at the shifted origin so the world (and
		// the creature proxies, which read origin at draw time) renders between tiles. The engine
		// already drew this frame at the snapped position; everything here overdraws it, clipped
		// to the map rect so shifted tiles never spill over the UI. The uncovered strip on the
		// trailing edge stays black until the glide lands.
		const SDL_Rect map_rect=
			{
			tile_pixel(vp->clipx[0],renderer->origin_x,zoom),
			tile_pixel(vp->clipy[0],renderer->origin_y,zoom),
			tile_pixel(vp->clipx[1]+1,renderer->origin_x,zoom)-
				tile_pixel(vp->clipx[0],renderer->origin_x,zoom),
			tile_pixel(vp->clipy[1]+1,renderer->origin_y,zoom)-
				tile_pixel(vp->clipy[0],renderer->origin_y,zoom)
			};
		state.sdl.render_set_clip_rect(sdl_renderer,&map_rect);
		Uint8 old_r=0,old_g=0,old_b=0,old_a=255;
		state.sdl.get_render_draw_color(sdl_renderer,&old_r,&old_g,&old_b,&old_a);
		state.sdl.set_render_draw_color(sdl_renderer,0,0,0,255);
		state.sdl.render_fill_rect(sdl_renderer,&map_rect);
		state.sdl.set_render_draw_color(sdl_renderer,old_r,old_g,old_b,old_a);

		const int32_t saved_origin_x=renderer->origin_x;
		const int32_t saved_origin_y=renderer->origin_y;
		renderer->origin_x+=glide_x;
		renderer->origin_y+=glide_y;
		state.render.blank_summaries.summarize(
			viewport_renders,[](const viewport_renderst &v){return v.viewport;});
		for(int32_t x=vp->clipx[0];x<=vp->clipx[1];++x)
			{
			for(int32_t y=vp->clipy[0];y<=vp->clipy[1];++y)
				redraw_world_tile(renderer,viewport_renders,coverage,x,y);
			}
		draw_viewport_interpolation_stages(
			renderer,viewport_renders,coverage,carried_items);
		renderer->origin_x=saved_origin_x;
		renderer->origin_y=saved_origin_y;
		state.render.blank_summaries.clear();
		state.sdl.render_set_clip_rect(sdl_renderer,nullptr);

		// Everything was repainted; per-tile coverage bookkeeping restarts after the glide.
		state.render.previous_coverage.clear();
		return;
		}

	tile_coveragest redraw_coverage=coverage;
	redraw_coverage.insert(
		state.render.previous_coverage.begin(),state.render.previous_coverage.end());
	Uint8 old_r=0,old_g=0,old_b=0,old_a=255;
	state.sdl.get_render_draw_color(sdl_renderer,&old_r,&old_g,&old_b,&old_a);
	state.sdl.set_render_draw_color(sdl_renderer,0,0,0,255);
	for(const auto &[x,y]:redraw_coverage)
		{
		if(!inside_clip(vp,x,y))continue;
		const SDL_Rect tile_rect=
			{
			tile_pixel(x,renderer->origin_x,zoom),
			tile_pixel(y,renderer->origin_y,zoom),
			tile_size,
			tile_size
			};
		state.sdl.render_fill_rect(sdl_renderer,&tile_rect);
		}
	state.sdl.set_render_draw_color(sdl_renderer,old_r,old_g,old_b,old_a);

	for(const auto &[x,y]:redraw_coverage)
		{
		if(inside_clip(vp,x,y))
			redraw_world_tile(renderer,viewport_renders,coverage,x,y);
		}
	draw_viewport_interpolation_stages(
		renderer,viewport_renders,coverage,carried_items);

	state.render.previous_coverage=std::move(coverage);
}

struct renderer_hook : df::renderer_2d_base
{
	typedef df::renderer_2d_base interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,update_all,());
};

IMPLEMENT_VMETHOD_INTERPOSE(renderer_hook,update_all);

void renderer_hook::interpose_fn_update_all()
{
	// update_all is the existing UI stage, so world correction must run first.
	render_interpolated_world(this);
	INTERPOSE_NEXT(update_all)();
}

// Both map screens fill the viewports' per-tile arrays in their render, on the simulation
// thread; the interposes note the simulation tick they were filled at.
struct dwarfmode_hook : df::viewscreen_dwarfmodest
{
	typedef df::viewscreen_dwarfmodest interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,render,(uint32_t curtick))
		{
		if(world!=nullptr)state.render.drawn_buffers.note_drawn(world->frame_counter);
		INTERPOSE_NEXT(render)(curtick);
		}
};

struct dungeonmode_hook : df::viewscreen_dungeonmodest
{
	typedef df::viewscreen_dungeonmodest interpose_base;
	DEFINE_VMETHOD_INTERPOSE(void,render,(uint32_t curtick))
		{
		if(world!=nullptr)state.render.drawn_buffers.note_drawn(world->frame_counter);
		INTERPOSE_NEXT(render)(curtick);
		}
};

IMPLEMENT_VMETHOD_INTERPOSE(dwarfmode_hook,render);
IMPLEMENT_VMETHOD_INTERPOSE(dungeonmode_hook,render);

bool load_sdl(color_ostream &out)
{
	state.sdl.clear();
	DFLibrary *sdl_handle=DFSDL::obtain_library_handle();
	#define bind(name,target) \
		target=reinterpret_cast<decltype(target)>(LookupPlugin(sdl_handle,#name)); \
		if(target==nullptr) { \
			out.printerr("smooth-movement: SDL2 function unavailable: " #name "\n"); \
			state.sdl.clear(); \
			return false; \
		}
	bind(SDL_RenderCopyF,state.sdl.render_copy_f);
	bind(SDL_RenderCopyExF,state.sdl.render_copy_ex_f);
	bind(SDL_RenderFillRect,state.sdl.render_fill_rect);
	bind(SDL_RenderSetClipRect,state.sdl.render_set_clip_rect);
	bind(SDL_GetRenderDrawColor,state.sdl.get_render_draw_color);
	bind(SDL_SetRenderDrawColor,state.sdl.set_render_draw_color);
	#undef bind
	return true;
}

// What the console commands need from the game.
void full_redraw()
{
	if(gps!=nullptr)++gps->force_full_display_count;
}

command_result status_command(
	color_ostream &out,
	std::vector<std::string> &parameters)
{
	const command_hostst host={plugin_version,is_enabled,full_redraw,scroll_window};
	switch(run_command(out,parameters,state,host))
		{
		case command_outcomest::ok:return CR_OK;
		case command_outcomest::failed:return CR_FAILURE;
		case command_outcomest::wrong_usage:break;
		}
	return CR_WRONG_USAGE;
}

} // namespace

DFhackCExport command_result
plugin_init(color_ostream &,std::vector<PluginCommand> &commands)
{
	commands.emplace_back(
		"smooth-movement",
		"Smooth movement status; free camera: camera on|off|reset|<fx> <fy>; "
		"flip, linear and hauled together: all on|off; "
		"sprite flipping: flip on|off; linear movement: linear on|off; "
		"one-tile step time: timestep <ms> (20-2000); "
		"hauled item icons: hauled on|off; "
		"walk bob: bob on|off|<amount>; bob multipliers: bobmult <horizontal> <diagonal> "
		"<vertical>; hops per step: hops 1|2; "
		"frame timing: stats [on|off|reset]; "
		"frame recording: record <file> [frames] | record stop | record status.",
		status_command);
	return CR_OK;
}

DFhackCExport command_result plugin_enable(color_ostream &out,bool enable)
{
	if(is_enabled==enable)return CR_OK;
	if(enable)
		{
		state.reset();
		if(!load_sdl(out))return CR_FAILURE;
		if(!INTERPOSE_HOOK(dwarfmode_hook,render).apply()||
			!INTERPOSE_HOOK(dungeonmode_hook,render).apply()||
			!INTERPOSE_HOOK(renderer_hook,update_all).apply())
			{
			out.printerr("smooth-movement: could not hook the map screens and 2D renderer\n");
			INTERPOSE_HOOK(dwarfmode_hook,render).remove();
			INTERPOSE_HOOK(dungeonmode_hook,render).remove();
			INTERPOSE_HOOK(renderer_hook,update_all).remove();
			state.sdl.clear();
			return CR_FAILURE;
			}
		}
	else
		{
		INTERPOSE_HOOK(renderer_hook,update_all).remove();
		INTERPOSE_HOOK(dwarfmode_hook,render).remove();
		INTERPOSE_HOOK(dungeonmode_hook,render).remove();
		state.reset();
		state.sdl.clear();
		if(gps!=nullptr)++gps->force_full_display_count;
		}
	is_enabled=enable;
	out.print("smooth-movement: {}\n",enable?"enabled":"disabled");
	return CR_OK;
}

DFhackCExport command_result plugin_shutdown(color_ostream &out)
{
	return plugin_enable(out,false);
}
