// Oracle: the pre-redesign repaint and frame code, ported mechanically onto a canvas.
#pragma once
#include "old_render.h"
namespace oracle {
inline float bob_amplitude=0.10f,bob_horizontal_mult=1.0f,bob_diagonal_mult=2.4f,bob_vertical_mult=2.7f;
inline int bob_hops=2;
constexpr uint32_t fire_bits=0x70000000U;
template<typename T> using old_scoped_value_restorest=scoped_value_restorest<T>;
template<typename... Args> void old_with_zeroed_values(Args &&...args){with_zeroed_values(std::forward<Args>(args)...);}
using viewport_layer_memberst=int32_t *oracle_viewport::*;

struct visual_layer_bufferst
{
	viewport_visual_layer layer;
	viewport_layer_memberst current;
	viewport_layer_memberst previous;
};

constexpr std::array visual_layer_buffers=
	{
	visual_layer_bufferst{viewport_visual_layer::right,
		&oracle_viewport::screentexpos_right_creature,
		&oracle_viewport::screentexpos_right_creature_old},
	visual_layer_bufferst{viewport_visual_layer::center,
		&oracle_viewport::screentexpos,
		&oracle_viewport::screentexpos_old},
	visual_layer_bufferst{viewport_visual_layer::left,
		&oracle_viewport::screentexpos_left_creature,
		&oracle_viewport::screentexpos_left_creature_old},
	visual_layer_bufferst{viewport_visual_layer::upright,
		&oracle_viewport::screentexpos_upright_creature,
		&oracle_viewport::screentexpos_upright_creature_old},
	visual_layer_bufferst{viewport_visual_layer::up,
		&oracle_viewport::screentexpos_up_creature,
		&oracle_viewport::screentexpos_up_creature_old},
	visual_layer_bufferst{viewport_visual_layer::upleft,
		&oracle_viewport::screentexpos_upleft_creature,
		&oracle_viewport::screentexpos_upleft_creature_old},
	visual_layer_bufferst{viewport_visual_layer::vehicle,
		&oracle_viewport::screentexpos_vehicle,
		&oracle_viewport::screentexpos_vehicle_old},
	visual_layer_bufferst{viewport_visual_layer::item,
		&oracle_viewport::screentexpos_item,
		&oracle_viewport::screentexpos_item_old},
	visual_layer_bufferst{viewport_visual_layer::designation,
		&oracle_viewport::screentexpos_designation,
		&oracle_viewport::screentexpos_designation_old}
	};

constexpr bool valid_visual_layer_buffers()
{
	uint16_t layers=0;
	for(const auto &buffer:visual_layer_buffers)
		{
		const uint16_t layer=uint16_t(1U<<static_cast<uint8_t>(buffer.layer));
		if(layers&layer)return false;
		layers|=layer;
		}
	return layers==uint16_t((1U<<visual_layer_count)-1);
}

static_assert(valid_visual_layer_buffers());

template<typename Viewport>
auto visual_layers(Viewport *vp,bool previous=false)
{
	using layer_pointer=std::conditional_t<
		std::is_const_v<Viewport>,const int32_t *,int32_t *>;
	std::array<layer_pointer,visual_layer_count> layers{};
	for(const auto &buffer:visual_layer_buffers)
		layers[static_cast<size_t>(buffer.layer)]=vp->*(previous?
			buffer.previous:buffer.current);
	return layers;
}

int32_t old_tile_pixel(int32_t tile,int32_t origin,int32_t zoom)
{
	return zoom==128?32*tile+origin:(zoom*32*tile)/128+origin;
}

bool inside_clip(const oracle_viewport *vp,int32_t x,int32_t y)
{
	return x>=vp->clipx[0]&&x<=vp->clipx[1]&&
		y>=vp->clipy[0]&&y<=vp->clipy[1];
}

bool has_fire(const oracle_viewport *vp,int32_t x,int32_t y)
{
	return vp->screentexpos_spatter_flag!=nullptr&&
		(vp->screentexpos_spatter_flag[x*vp->dim_y+y]&fire_bits)!=0;
}

struct viewport_renderst
{
	oracle_viewport *viewport;
	std::vector<render_proxyst> proxies;
	render_coveragest coverage;
};

uint16_t selected_mask(
	const std::unordered_map<int32_t,uint16_t> &selected,
	int32_t index)
{
	const auto found=selected.find(index);
	return found==selected.end()?0:found->second;
}

template<size_t Layer=0,typename Callback>
void with_suppressed_visual_layers(
	const std::array<int32_t *,visual_layer_count> &layers,
	int32_t index,
	uint16_t mask,
	const Callback &callback)
{
	if constexpr(Layer==visual_layer_count)
		callback();
	else if(mask&(1U<<Layer))
		{
		old_scoped_value_restorest<int32_t> zero(layers[Layer][index]);
		with_suppressed_visual_layers<Layer+1>(layers,index,mask,callback);
		}
	else
		with_suppressed_visual_layers<Layer+1>(layers,index,mask,callback);
}

template<typename Callback>
void with_base_suppressed(
	oracle_viewport *vp,
	int32_t index,
	const Callback &callback)
{
	old_with_zeroed_values(
		callback,
		vp->screentexpos_background[index],
		vp->screentexpos_floor_flag[index],
		vp->screentexpos_background_two[index],
		vp->screentexpos_liquid_flag[index],
		vp->screentexpos_spatter_flag[index],
		vp->screentexpos_spatter[index],
		vp->screentexpos_ramp_flag[index],
		vp->screentexpos_shadow_flag[index],
		vp->screentexpos_building_one[index]);
}

template<typename Callback>
void with_main_suppressed(
	oracle_viewport *vp,
	int32_t index,
	const Callback &callback)
{
	with_base_suppressed(vp,index,[&]
		{
		old_with_zeroed_values(callback,vp->screentexpos_vermin[index]);
		});
}

template<typename Callback>
void with_upper_suppressed(
	oracle_viewport *vp,
	int32_t index,
	const Callback &callback)
{
	with_main_suppressed(vp,index,[&]
		{
		old_with_zeroed_values(
			callback,
			vp->screentexpos_building_two[index],
			vp->screentexpos_projectile[index],
			vp->screentexpos_high_flow[index],
			vp->screentexpos_top_shadow[index],
			vp->screentexpos_signpost[index]);
		});
}

template<typename Canvas>
void redraw_viewport_tile(
	Canvas &canvas,
	const viewport_renderst &viewport,
	int32_t x,
	int32_t y,
	bool defer_interface)
{
	oracle_viewport *vp=viewport.viewport;
	const int32_t index=x*vp->dim_y+y;
	const auto redraw=[&]{canvas.repaint(vp,x,y);};
	const auto stage=[&]
		{
		with_suppressed_visual_layers(
			visual_layers(vp),index,
			selected_mask(viewport.coverage.selected,index),redraw);
		};
	// The interface layer is the shading for levels below the camera.
	// A staged tile has a sprite drawn over it afterwards, so draw_interface_only places it instead.
	if(!defer_interface||vp->screentexpos_interface==nullptr)stage();
	else old_with_zeroed_values(stage,vp->screentexpos_interface[index]);
}

// Every buffer the interface-only pass zeroes has to exist before it can be zeroed.
bool interface_pass_readable(const oracle_viewport *vp)
{
	return vp!=nullptr&&
		vp->screentexpos_interface!=nullptr&&
		vp->screentexpos_background!=nullptr&&
		vp->screentexpos_floor_flag!=nullptr&&
		vp->screentexpos_background_two!=nullptr&&
		vp->screentexpos_liquid_flag!=nullptr&&
		vp->screentexpos_spatter_flag!=nullptr&&
		vp->screentexpos_spatter!=nullptr&&
		vp->screentexpos_ramp_flag!=nullptr&&
		vp->screentexpos_shadow_flag!=nullptr&&
		vp->screentexpos_building_one!=nullptr&&
		vp->screentexpos_vermin!=nullptr&&
		vp->screentexpos_building_two!=nullptr&&
		vp->screentexpos_projectile!=nullptr&&
		vp->screentexpos_high_flow!=nullptr&&
		vp->screentexpos_signpost!=nullptr;
}

// Runs after the proxies so the shading covers them rather than sitting underneath.
template<typename Canvas>
void draw_interface_only(
	Canvas &canvas,
	oracle_viewport *vp,
	int32_t x,
	int32_t y)
{
	if(!interface_pass_readable(vp))return;
	const int32_t index=x*vp->dim_y+y;
	const auto redraw=[&]{canvas.repaint(vp,x,y);};
	const auto without_visuals=[&]
		{
		with_suppressed_visual_layers(
			visual_layers(vp),
			index,
			uint16_t((1U<<visual_layer_count)-1),
			redraw);
		};
	old_with_zeroed_values(
		without_visuals,
		vp->screentexpos_background[index],
		vp->screentexpos_floor_flag[index],
		vp->screentexpos_background_two[index],
		vp->screentexpos_liquid_flag[index],
		vp->screentexpos_spatter_flag[index],
		vp->screentexpos_spatter[index],
		vp->screentexpos_ramp_flag[index],
		vp->screentexpos_shadow_flag[index],
		vp->screentexpos_building_one[index],
		vp->screentexpos_vermin[index],
		vp->screentexpos_building_two[index],
		vp->screentexpos_projectile[index],
		vp->screentexpos_high_flow[index],
		// The shipped protocol left the top shadow here, painting it a second time over the
		// sprites; the redesign paints it once, in engine order, so the oracle models that.
		vp->screentexpos_top_shadow[index],
		vp->screentexpos_signpost[index]);
}

template<typename Canvas>
void redraw_world_tile(
	Canvas &canvas,
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
			redraw_viewport_tile(canvas,viewport,x,y,staged_tile);
		if(staged_tile)break;
		}
}


template<typename Canvas>
void redraw_above(
	Canvas &canvas,
	oracle_viewport *vp,
	int32_t x,
	int32_t y,
	visual_render_groupst group,
	const std::unordered_map<int32_t,uint16_t> &selected)
{
	const int32_t index=x*vp->dim_y+y;
	const auto redraw=[&]{canvas.repaint(vp,x,y);};
	const auto suppress_visuals=[&]
		{
		const auto stage=[&]
			{
			with_suppressed_visual_layers(
				visual_layers(vp),
				index,
				selected_mask(selected,index)|visual_layers_through_group(group),
				redraw);
			};
		// The interface layer sits above every group, so each group's redraw would paint it again.
		// draw_interface_only places it once, after the sprites.
		if(vp->screentexpos_interface==nullptr)stage();
		else old_with_zeroed_values(stage,vp->screentexpos_interface[index]);
		};
	if(group==visual_render_groupst::item||group==visual_render_groupst::vehicle)
		with_base_suppressed(vp,index,suppress_visuals);
	else if(group==visual_render_groupst::main)
		with_main_suppressed(vp,index,suppress_visuals);
	else
		with_upper_suppressed(vp,index,suppress_visuals);
}

template<typename Canvas>
void draw_proxy(Canvas &canvas,const render_proxyst &proxy)
{
	const int32_t zoom=canvas.zoom();
	const int32_t target_x=old_tile_pixel(proxy.target_x,canvas.origin_x(),zoom);
	const int32_t target_y=old_tile_pixel(proxy.target_y,canvas.origin_y(),zoom);
	const float tile_size=float(zoom==128?32:std::max(1,zoom*32/128));
	const float source_x=target_x+(proxy.source_x-proxy.target_x)*tile_size;
	const float source_y=target_y+(proxy.source_y-proxy.target_y)*tile_size;
	const float mirror_offset=float(proxy.mirror_shift)*tile_size;
	const walk_bob_directionst bob_direction=walk_bob_direction(
		proxy.source_x,proxy.source_y,proxy.target_x,proxy.target_y);
	const float bob_mult=
		bob_direction==walk_bob_directionst::horizontal?bob_horizontal_mult:
		bob_direction==walk_bob_directionst::vertical?bob_vertical_mult:
		bob_diagonal_mult;
	const float bob_offset=proxy.bob?
		-walk_bob_lift(proxy.progress,bob_hops,bob_amplitude,bob_mult)*tile_size:0.0f;
	const float base_x=source_x+(target_x-source_x)*proxy.progress+mirror_offset;
	const float base_y=source_y+(target_y-source_y)*proxy.progress+bob_offset;
	canvas.draw_sprite(proxy.texture,base_x,base_y,tile_size,proxy.mirrored);
}

template<typename Canvas,typename Manager,typename Texture>
std::vector<viewport_renderst> collect_viewport_renders(
	Canvas &canvas,
	const std::vector<oracle_viewport *> &viewports,
	const Manager &animation_manager,bool flip_enabled,bool bob_enabled,const Texture &texture_of)
{
	std::vector<viewport_renderst> renders;
	renders.reserve(viewports.size());
	for(oracle_viewport *vp:viewports)
		{
		const viewport_viewst view=view_of(static_cast<const oracle_viewport *>(vp));
		viewport_renderst render={vp,old_collect_proxies(&view,animation_manager,flip_enabled,bob_enabled,texture_of),{}};
		render.coverage=old_collect_coverage(render.proxies,vp->dim_y);
		renders.push_back(std::move(render));
		}
	return renders;
}

tile_coveragest collect_viewport_coverage(
	const std::vector<viewport_renderst> &viewports)
{
	tile_coveragest coverage;
	for(const viewport_renderst &viewport:viewports)
		coverage.insert(
			viewport.coverage.all.begin(),viewport.coverage.all.end());
	return coverage;
}

template<typename Canvas>
void draw_interpolation_stages(
	Canvas &canvas,
	oracle_viewport *vp,
	const std::vector<render_proxyst> &proxies,
	const render_coveragest &coverage)
{
	for(size_t index=0;index<coverage.groups.size();++index)
		{
		const auto group=static_cast<visual_render_groupst>(index);
		for(const render_proxyst &proxy:proxies)
			if(visual_render_group(proxy.layer)==group)draw_proxy(canvas,proxy);
		if(group==visual_render_groupst::designation)continue;
		for(const auto &[x,y]:coverage.groups[index])
			redraw_above(canvas,vp,x,y,group,coverage.selected);
		}
}

template<typename Canvas>
void redraw_viewport_tiles(
	Canvas &canvas,
	const viewport_renderst &viewport,
	const tile_coveragest &coverage)
{
	oracle_viewport *vp=viewport.viewport;
	for(const auto &[x,y]:coverage)
		{
		if(!inside_clip(vp,x,y))continue;
		redraw_viewport_tile(canvas,viewport,x,y,true);
		}
}

template<typename Canvas>
void draw_viewport_interpolation_stages(
	Canvas &canvas,
	const std::vector<viewport_renderst> &viewports,
	const tile_coveragest &coverage)
{
	for(size_t index=0;index<viewports.size();++index)
		{
		// A lower z-level's proxy must be covered by the next viewport's fog and terrain.
		// Reapply that viewport before its own proxies, matching DF's lower-to-main draw order.
		if(index>0)redraw_viewport_tiles(canvas,viewports[index],coverage);
		const viewport_renderst &viewport=viewports[index];
		draw_interpolation_stages(
			canvas,viewport.viewport,viewport.proxies,viewport.coverage);
		// A viewport shades everything drawn beneath it, so this covers every staged tile.
		// Restricting it to the tiles this viewport has sprites on would not deepen with distance.
		for(const auto &[x,y]:coverage)
			{
			if(inside_clip(viewport.viewport,x,y))
				draw_interface_only(canvas,viewport.viewport,x,y);
			}
		}
}

template<typename Canvas,typename Manager,typename Texture>
void old_render(
	Canvas &canvas,
	const std::vector<oracle_viewport *> &viewports,
	oracle_viewport *vp,
	const Manager &animation_manager,
	bool flip_enabled,
	bool bob_enabled,
	const Texture &texture_of,
	int32_t glide_x,
	int32_t glide_y,
	tile_coveragest &previous_coverage)
{
	const bool glide=glide_x!=0||glide_y!=0;
	std::vector<viewport_renderst> viewport_renders=
		collect_viewport_renders(canvas,viewports,animation_manager,flip_enabled,bob_enabled,texture_of);
	tile_coveragest coverage=collect_viewport_coverage(viewport_renders);
	const int32_t zoom=canvas.zoom();
	const int32_t tile_size=zoom==128?32:std::max(1,zoom*32/128);

	if(glide)
		{
		// Camera mid-glide: repaint the WHOLE map rect at the shifted origin so the world (and
		// the creature proxies, which read origin at draw time) renders between tiles. The engine
		// already drew this frame at the snapped position; everything here overdraws it, clipped
		// to the map rect so shifted tiles never spill over the UI. The uncovered strip on the
		// trailing edge stays black until the glide lands.
		const pixel_rectst map_rect=
			{
			old_tile_pixel(vp->clipx[0],canvas.origin_x(),zoom),
			old_tile_pixel(vp->clipy[0],canvas.origin_y(),zoom),
			old_tile_pixel(vp->clipx[1]+1,canvas.origin_x(),zoom)-
				old_tile_pixel(vp->clipx[0],canvas.origin_x(),zoom),
			old_tile_pixel(vp->clipy[1]+1,canvas.origin_y(),zoom)-
				old_tile_pixel(vp->clipy[0],canvas.origin_y(),zoom)
			};
		canvas.set_clip(map_rect);
		canvas.fill_black(map_rect);

		canvas.offset_origin(glide_x,glide_y);
		for(int32_t x=vp->clipx[0];x<=vp->clipx[1];++x)
			{
			for(int32_t y=vp->clipy[0];y<=vp->clipy[1];++y)
				redraw_world_tile(canvas,viewport_renders,coverage,x,y);
			}
		draw_viewport_interpolation_stages(canvas,viewport_renders,coverage);
		canvas.offset_origin(-glide_x,-glide_y);
		canvas.clear_clip();

		// Everything was repainted; per-tile coverage bookkeeping restarts after the glide.
		previous_coverage.clear();
		return;
		}

	tile_coveragest redraw_coverage=coverage;
	redraw_coverage.insert(previous_coverage.begin(),previous_coverage.end());
	for(const auto &[x,y]:redraw_coverage)
		{
		if(!inside_clip(vp,x,y))continue;
		const pixel_rectst tile_rect=
			{
			old_tile_pixel(x,canvas.origin_x(),zoom),
			old_tile_pixel(y,canvas.origin_y(),zoom),
			tile_size,
			tile_size
			};
		canvas.fill_black(tile_rect);
		}

	for(const auto &[x,y]:redraw_coverage)
		{
		if(inside_clip(vp,x,y))
			redraw_world_tile(canvas,viewport_renders,coverage,x,y);
		}
	draw_viewport_interpolation_stages(canvas,viewport_renders,coverage);

	previous_coverage=std::move(coverage);
}

} // namespace oracle
