// SPDX-License-Identifier: MIT

#ifndef TILE_REPAINT_H
#define TILE_REPAINT_H

#include "visual_layers.h"

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>

// Repaints one viewport tile through the engine with chosen buffers blanked for the duration,
// so a redraw paints only the parts of a tile the plugin has not painted itself. Generic over
// the viewport type: the engine's graphic_viewportst or a test double with the same members.

template<typename T>
class scoped_value_restorest
{
	T &value;
	T saved;

	public:
		explicit scoped_value_restorest(T &value,T replacement=T{}):
			value(value),
			saved(std::exchange(value,std::move(replacement)))
			{
			static_assert(std::is_nothrow_move_assignable_v<T>);
			}

		~scoped_value_restorest() noexcept
			{
			value=std::move(saved);
			}

		scoped_value_restorest(const scoped_value_restorest &)=delete;
		scoped_value_restorest &operator=(const scoped_value_restorest &)=delete;
		scoped_value_restorest(scoped_value_restorest &&)=delete;
		scoped_value_restorest &operator=(scoped_value_restorest &&)=delete;
};

template<typename Callback>
void with_zeroed_values(const Callback &callback)
{
	callback();
}

template<typename Callback,typename T,typename... Values>
void with_zeroed_values(const Callback &callback,T &value,Values &...values)
{
	scoped_value_restorest<T> zero(value);
	with_zeroed_values(callback,values...);
}

// Which viewport buffer holds each visual layer, current and previous.
template<typename Viewport>
struct viewport_layer_tablest
{
	using memberst=int32_t *Viewport::*;

	struct bufferst
	{
		viewport_visual_layer layer;
		memberst current;
		memberst previous;
	};

	static constexpr std::array<bufferst,visual_layer_count> buffers=
		{
		bufferst{viewport_visual_layer::right,
			&Viewport::screentexpos_right_creature,
			&Viewport::screentexpos_right_creature_old},
		bufferst{viewport_visual_layer::center,
			&Viewport::screentexpos,
			&Viewport::screentexpos_old},
		bufferst{viewport_visual_layer::left,
			&Viewport::screentexpos_left_creature,
			&Viewport::screentexpos_left_creature_old},
		bufferst{viewport_visual_layer::upright,
			&Viewport::screentexpos_upright_creature,
			&Viewport::screentexpos_upright_creature_old},
		bufferst{viewport_visual_layer::up,
			&Viewport::screentexpos_up_creature,
			&Viewport::screentexpos_up_creature_old},
		bufferst{viewport_visual_layer::upleft,
			&Viewport::screentexpos_upleft_creature,
			&Viewport::screentexpos_upleft_creature_old},
		bufferst{viewport_visual_layer::vehicle,
			&Viewport::screentexpos_vehicle,
			&Viewport::screentexpos_vehicle_old},
		bufferst{viewport_visual_layer::item,
			&Viewport::screentexpos_item,
			&Viewport::screentexpos_item_old},
		bufferst{viewport_visual_layer::designation,
			&Viewport::screentexpos_designation,
			&Viewport::screentexpos_designation_old}
		};

	static constexpr bool valid()
		{
		uint16_t layers=0;
		for(const bufferst &buffer:buffers)
			{
			if(layers&visual_layer_bit(buffer.layer))return false;
			layers|=visual_layer_bit(buffer.layer);
			}
		return layers==all_visual_layers_mask;
		}
	static_assert(valid());

	static constexpr memberst current_of(viewport_visual_layer layer)
		{
		for(const bufferst &buffer:buffers)
			if(buffer.layer==layer)return buffer.current;
		return nullptr;
		}

	static visual_layer_pointerst current(const Viewport *vp)
		{
		visual_layer_pointerst layers{};
		for(const bufferst &buffer:buffers)
			layers[static_cast<size_t>(buffer.layer)]=vp->*buffer.current;
		return layers;
		}

	static visual_layer_pointerst previous(const Viewport *vp)
		{
		visual_layer_pointerst layers{};
		for(const bufferst &buffer:buffers)
			layers[static_cast<size_t>(buffer.layer)]=vp->*buffer.previous;
		return layers;
		}
};

template<size_t Layer=0,typename Viewport,typename Callback>
void with_hidden_visual_layers(
	Viewport *vp,
	int32_t index,
	uint16_t mask,
	const Callback &callback)
{
	if constexpr(Layer==visual_layer_count)
		callback();
	else if(mask&(1U<<Layer))
		{
		constexpr auto member=viewport_layer_tablest<Viewport>::current_of(
			static_cast<viewport_visual_layer>(Layer));
		scoped_value_restorest<int32_t> zero((vp->*member)[index]);
		with_hidden_visual_layers<Layer+1>(vp,index,mask,callback);
		}
	else
		with_hidden_visual_layers<Layer+1>(vp,index,mask,callback);
}

// The buffers below every sprite group: terrain, spatter and low furniture.
template<typename Viewport,typename Callback>
void with_base_hidden(Viewport *vp,int32_t index,const Callback &callback)
{
	with_zeroed_values(
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

template<typename Viewport,typename Callback>
void with_main_hidden(Viewport *vp,int32_t index,const Callback &callback)
{
	with_base_hidden(vp,index,[&]
		{
		with_zeroed_values(callback,vp->screentexpos_vermin[index]);
		});
}

template<typename Viewport,typename Callback>
void with_upper_hidden(Viewport *vp,int32_t index,const Callback &callback)
{
	with_main_hidden(vp,index,[&]
		{
		with_zeroed_values(
			callback,
			vp->screentexpos_building_two[index],
			vp->screentexpos_projectile[index],
			vp->screentexpos_high_flow[index],
			vp->screentexpos_top_shadow[index],
			vp->screentexpos_signpost[index]);
		});
}

// A full engine repaint of the tile with the proxied layers blank. The interface layer is
// the shading for levels below the camera; a staged tile has sprites drawn over it
// afterwards, so `defer_interface` leaves the shading to repaint_interface_only.
template<typename Viewport,typename Repaint>
void repaint_staged(
	Viewport *vp,
	int32_t x,
	int32_t y,
	uint16_t hidden_layers,
	bool defer_interface,
	const Repaint &repaint)
{
	const int32_t index=x*vp->dim_y+y;
	const auto stage=[&]
		{
		with_hidden_visual_layers(vp,index,hidden_layers,[&]{repaint(vp,x,y);});
		};
	if(!defer_interface||vp->screentexpos_interface==nullptr)stage();
	else with_zeroed_values(stage,vp->screentexpos_interface[index]);
}

// Repaints only what sits above `group`, after that group's sprites were drawn. The interface
// layer sits above every group, so it is painted once, with the tile's last group.
template<typename Viewport,typename Repaint>
void repaint_above(
	Viewport *vp,
	int32_t x,
	int32_t y,
	visual_render_groupst group,
	uint16_t hidden_layers,
	bool with_interface,
	const Repaint &repaint)
{
	const int32_t index=x*vp->dim_y+y;
	const auto without_visuals=[&]
		{
		const auto stage=[&]
			{
			with_hidden_visual_layers(
				vp,index,uint16_t(hidden_layers|visual_layers_through_group(group)),
				[&]{repaint(vp,x,y);});
			};
		if(with_interface||vp->screentexpos_interface==nullptr)stage();
		else with_zeroed_values(stage,vp->screentexpos_interface[index]);
		};
	if(group==visual_render_groupst::item||group==visual_render_groupst::vehicle)
		with_base_hidden(vp,index,without_visuals);
	else if(group==visual_render_groupst::main)
		with_main_hidden(vp,index,without_visuals);
	else
		with_upper_hidden(vp,index,without_visuals);
}

// Every buffer the interface-only pass blanks has to exist before it can be blanked.
template<typename Viewport>
bool interface_pass_readable(const Viewport *vp)
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
		vp->screentexpos_top_shadow!=nullptr&&
		vp->screentexpos_signpost!=nullptr;
}

// Paints the shading alone, after the sprites, so it covers them instead of lying beneath.
template<typename Viewport,typename Repaint>
void repaint_interface_only(Viewport *vp,int32_t x,int32_t y,const Repaint &repaint)
{
	if(!interface_pass_readable(vp))return;
	const int32_t index=x*vp->dim_y+y;
	const auto without_visuals=[&]
		{
		with_hidden_visual_layers(
			vp,index,all_visual_layers_mask,[&]{repaint(vp,x,y);});
		};
	with_zeroed_values(
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
		vp->screentexpos_top_shadow[index],
		vp->screentexpos_signpost[index]);
}

// Whether a repaint of the tile would paint anything: the engine draws only the buffers that
// hold a texture or flag, so a tile whose buffers are all zero (as most tiles of a lower level
// are) paints nothing and need not be asked for. Read with the layers hidden for the repaint.
template<typename Viewport>
bool tile_paints_nothing(const Viewport *vp,int32_t index)
{
	const auto zero=[index](const auto *buffer){return buffer==nullptr||buffer[index]==0;};
	return zero(vp->screentexpos_background)&&
		zero(vp->screentexpos_floor_flag)&&
		zero(vp->screentexpos_background_two)&&
		zero(vp->screentexpos_liquid_flag)&&
		zero(vp->screentexpos_spatter_flag)&&
		zero(vp->screentexpos_spatter)&&
		zero(vp->screentexpos_ramp_flag)&&
		zero(vp->screentexpos_shadow_flag)&&
		zero(vp->screentexpos_building_one)&&
		zero(vp->screentexpos_item)&&
		zero(vp->screentexpos_vehicle)&&
		zero(vp->screentexpos_vermin)&&
		zero(vp->screentexpos_left_creature)&&
		zero(vp->screentexpos)&&
		zero(vp->screentexpos_right_creature)&&
		zero(vp->screentexpos_building_two)&&
		zero(vp->screentexpos_projectile)&&
		zero(vp->screentexpos_high_flow)&&
		zero(vp->screentexpos_top_shadow)&&
		zero(vp->screentexpos_signpost)&&
		zero(vp->screentexpos_upleft_creature)&&
		zero(vp->screentexpos_up_creature)&&
		zero(vp->screentexpos_upright_creature)&&
		zero(vp->screentexpos_designation)&&
		zero(vp->screentexpos_interface);
}

// Whether the engine repainted a tile this frame. The engine repaints a tile when any of its
// buffers changed, and copies the buffers to their `_old` twins only after the frame, so at
// hook time a difference between a buffer and its twin is exactly a repainted tile.
template<typename Viewport>
bool engine_repainted_tile(const Viewport *vp,int32_t index)
{
	const auto differs=[index](const auto *current,const auto *previous)
		{
		return current!=nullptr&&previous!=nullptr&&current[index]!=previous[index];
		};
	return differs(vp->screentexpos,vp->screentexpos_old)||
		differs(vp->screentexpos_background,vp->screentexpos_background_old)||
		differs(vp->screentexpos_floor_flag,vp->screentexpos_floor_flag_old)||
		differs(vp->screentexpos_background_two,vp->screentexpos_background_two_old)||
		differs(vp->screentexpos_liquid_flag,vp->screentexpos_liquid_flag_old)||
		differs(vp->screentexpos_spatter_flag,vp->screentexpos_spatter_flag_old)||
		differs(vp->screentexpos_spatter,vp->screentexpos_spatter_old)||
		differs(vp->screentexpos_ramp_flag,vp->screentexpos_ramp_flag_old)||
		differs(vp->screentexpos_shadow_flag,vp->screentexpos_shadow_flag_old)||
		differs(vp->screentexpos_building_one,vp->screentexpos_building_one_old)||
		differs(vp->screentexpos_item,vp->screentexpos_item_old)||
		differs(vp->screentexpos_vehicle,vp->screentexpos_vehicle_old)||
		differs(vp->screentexpos_vermin,vp->screentexpos_vermin_old)||
		differs(vp->screentexpos_left_creature,vp->screentexpos_left_creature_old)||
		differs(vp->screentexpos_right_creature,vp->screentexpos_right_creature_old)||
		differs(vp->screentexpos_building_two,vp->screentexpos_building_two_old)||
		differs(vp->screentexpos_projectile,vp->screentexpos_projectile_old)||
		differs(vp->screentexpos_high_flow,vp->screentexpos_high_flow_old)||
		differs(vp->screentexpos_top_shadow,vp->screentexpos_top_shadow_old)||
		differs(vp->screentexpos_signpost,vp->screentexpos_signpost_old)||
		differs(vp->screentexpos_upleft_creature,vp->screentexpos_upleft_creature_old)||
		differs(vp->screentexpos_up_creature,vp->screentexpos_up_creature_old)||
		differs(vp->screentexpos_upright_creature,vp->screentexpos_upright_creature_old)||
		differs(vp->screentexpos_designation,vp->screentexpos_designation_old)||
		differs(vp->screentexpos_interface,vp->screentexpos_interface_old);
}

#endif
