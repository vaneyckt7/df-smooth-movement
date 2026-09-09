// SPDX-License-Identifier: MIT

#ifndef TILE_REPAINT_H
#define TILE_REPAINT_H

#include "visual_layers.h"

#include <array>
#include <cstdint>
#include <tuple>
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

// The engine's own per-tile buffers apart from the visual layers, by the sprite group they
// sit beneath: `base` lies under every group (terrain, spatter, low furniture), `main` adds
// what lies under the creature group, `upper` what lies under the upper-body group. The
// interface buffer (the shading of lower levels) sits above all of them and is handled on
// its own. Everything that hides, checks or scans these buffers reads this one table.
template<typename Viewport>
struct engine_buffer_tablest
{
	static constexpr auto base=std::make_tuple(
		&Viewport::screentexpos_background,
		&Viewport::screentexpos_floor_flag,
		&Viewport::screentexpos_background_two,
		&Viewport::screentexpos_liquid_flag,
		&Viewport::screentexpos_spatter_flag,
		&Viewport::screentexpos_spatter,
		&Viewport::screentexpos_ramp_flag,
		&Viewport::screentexpos_shadow_flag,
		&Viewport::screentexpos_building_one);
	static constexpr auto main=std::make_tuple(
		&Viewport::screentexpos_vermin);
	static constexpr auto upper=std::make_tuple(
		&Viewport::screentexpos_building_two,
		&Viewport::screentexpos_projectile,
		&Viewport::screentexpos_high_flow,
		&Viewport::screentexpos_top_shadow,
		&Viewport::screentexpos_signpost);

	// Runs `callback` with every buffer of `members` zeroed at `index` for the duration.
	template<typename Members,typename Callback>
	static void with_hidden(
		Viewport *vp,
		int32_t index,
		const Members &members,
		const Callback &callback)
		{
		std::apply(
			[&](const auto &...member){with_zeroed_values(callback,(vp->*member)[index]...);},
			members);
		}

	template<typename Members>
	static bool all_present(const Viewport *vp,const Members &members)
		{
		return std::apply(
			[&](const auto &...member){return ((vp->*member!=nullptr)&&...);},
			members);
		}
};

// The buffers below every sprite group: terrain, spatter and low furniture.
template<typename Viewport,typename Callback>
void with_base_hidden(Viewport *vp,int32_t index,const Callback &callback)
{
	engine_buffer_tablest<Viewport>::with_hidden(
		vp,index,engine_buffer_tablest<Viewport>::base,callback);
}

template<typename Viewport,typename Callback>
void with_main_hidden(Viewport *vp,int32_t index,const Callback &callback)
{
	with_base_hidden(vp,index,[&]
		{
		engine_buffer_tablest<Viewport>::with_hidden(
			vp,index,engine_buffer_tablest<Viewport>::main,callback);
		});
}

template<typename Viewport,typename Callback>
void with_upper_hidden(Viewport *vp,int32_t index,const Callback &callback)
{
	with_main_hidden(vp,index,[&]
		{
		engine_buffer_tablest<Viewport>::with_hidden(
			vp,index,engine_buffer_tablest<Viewport>::upper,callback);
		});
}

// Which repaint of a tile the canvas is handed, so a recording canvas can check a repaint
// against the pass it belongs to instead of inferring the pass from paint order.
struct repaint_passst
{
	enum kindst : uint8_t {staged,above_group,interface_only};
	kindst kind=staged;
	visual_render_groupst group=visual_render_groupst::item;   // above_group: the group just drawn
};

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
		with_hidden_visual_layers(vp,index,hidden_layers,[&]{repaint(vp,x,y,repaint_passst{});});
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
				[&]{repaint(vp,x,y,repaint_passst{repaint_passst::above_group,group});});
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
	using table=engine_buffer_tablest<Viewport>;
	return vp!=nullptr&&
		vp->screentexpos_interface!=nullptr&&
		table::all_present(vp,table::base)&&
		table::all_present(vp,table::main)&&
		table::all_present(vp,table::upper);
}

// Paints the shading alone, after the sprites, so it covers them instead of lying beneath.
template<typename Viewport,typename Repaint>
void repaint_interface_only(Viewport *vp,int32_t x,int32_t y,const Repaint &repaint)
{
	if(!interface_pass_readable(vp))return;
	const int32_t index=x*vp->dim_y+y;
	with_upper_hidden(vp,index,[&]
		{
		with_hidden_visual_layers(
			vp,index,all_visual_layers_mask,
			[&]{repaint(vp,x,y,repaint_passst{repaint_passst::interface_only,visual_render_groupst::designation});});
		});
}

// Whether every visual layer (creature, item, vehicle, designation, body fragments) is zero
// at the tile, unrolled over the layer table.
// Kept as a plain chain: folding over the member tables above compiles to slower
// code here (measured about 10% on the glide frame), and this runs per tile per level.
// The order follows the tables: base, main, the visual layers, upper, interface.
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
		zero(vp->screentexpos_vermin)&&
		zero(vp->screentexpos_right_creature)&&
		zero(vp->screentexpos)&&
		zero(vp->screentexpos_left_creature)&&
		zero(vp->screentexpos_upright_creature)&&
		zero(vp->screentexpos_up_creature)&&
		zero(vp->screentexpos_upleft_creature)&&
		zero(vp->screentexpos_vehicle)&&
		zero(vp->screentexpos_item)&&
		zero(vp->screentexpos_designation)&&
		zero(vp->screentexpos_building_two)&&
		zero(vp->screentexpos_projectile)&&
		zero(vp->screentexpos_high_flow)&&
		zero(vp->screentexpos_top_shadow)&&
		zero(vp->screentexpos_signpost)&&
		zero(vp->screentexpos_interface);
}

#endif
