// SPDX-License-Identifier: MIT

#ifndef TILE_REPAINT_H
#define TILE_REPAINT_H

#include "visual_animation.h"

#include <array>
#include <cstdint>
#include <type_traits>
#include <utility>
#include <vector>

// Repaints one viewport tile through the game's renderer with chosen per-tile entries zeroed
// for the duration, so a redraw paints only the parts of a tile the plugin has not painted
// itself. Generic over the viewport type: the game's graphic_viewportst or a test double with
// the same members. The repaint itself is a callable the plugin supplies, so this header
// needs nothing from DFHack.

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

constexpr size_t visual_layer_count=static_cast<size_t>(viewport_visual_layer::count);

constexpr uint16_t visual_layer_bit(viewport_visual_layer layer)
{
	return uint16_t(1U<<static_cast<uint8_t>(layer));
}

constexpr uint16_t all_visual_layers_mask=uint16_t((1U<<visual_layer_count)-1);

// Which per-tile array of the viewport holds each visual layer, current and previous.
template<typename Viewport>
struct viewport_layer_tablest
{
	using memberst=int32_t *Viewport::*;

	struct entryst
	{
		viewport_visual_layer layer;
		memberst current;
		memberst previous;
	};

	static constexpr std::array<entryst,visual_layer_count> entries=
		{
		entryst{viewport_visual_layer::right,
			&Viewport::screentexpos_right_creature,
			&Viewport::screentexpos_right_creature_old},
		entryst{viewport_visual_layer::center,
			&Viewport::screentexpos,
			&Viewport::screentexpos_old},
		entryst{viewport_visual_layer::left,
			&Viewport::screentexpos_left_creature,
			&Viewport::screentexpos_left_creature_old},
		entryst{viewport_visual_layer::upright,
			&Viewport::screentexpos_upright_creature,
			&Viewport::screentexpos_upright_creature_old},
		entryst{viewport_visual_layer::up,
			&Viewport::screentexpos_up_creature,
			&Viewport::screentexpos_up_creature_old},
		entryst{viewport_visual_layer::upleft,
			&Viewport::screentexpos_upleft_creature,
			&Viewport::screentexpos_upleft_creature_old},
		entryst{viewport_visual_layer::vehicle,
			&Viewport::screentexpos_vehicle,
			&Viewport::screentexpos_vehicle_old},
		entryst{viewport_visual_layer::item,
			&Viewport::screentexpos_item,
			&Viewport::screentexpos_item_old},
		entryst{viewport_visual_layer::designation,
			&Viewport::screentexpos_designation,
			&Viewport::screentexpos_designation_old}
		};

	static constexpr bool valid()
		{
		uint16_t layers=0;
		for(const entryst &entry:entries)
			{
			if(layers&visual_layer_bit(entry.layer))return false;
			layers|=visual_layer_bit(entry.layer);
			}
		return layers==all_visual_layers_mask;
		}
	static_assert(valid());
};

// The viewport's layer arrays indexed by layer, current or previous.
template<typename Viewport>
auto visual_layers(Viewport *vp,bool previous=false)
{
	using layer_pointer=std::conditional_t<
		std::is_const_v<Viewport>,const int32_t *,int32_t *>;
	std::array<layer_pointer,visual_layer_count> layers{};
	for(const auto &entry:viewport_layer_tablest<std::remove_const_t<Viewport>>::entries)
		layers[static_cast<size_t>(entry.layer)]=vp->*(previous?
			entry.previous:entry.current);
	return layers;
}

// Runs `callback` with the tile's entry zeroed in every layer array `mask` selects.
template<size_t Layer=0,typename Callback>
void with_hidden_visual_layers(
	const std::array<int32_t *,visual_layer_count> &layers,
	int32_t index,
	uint16_t mask,
	const Callback &callback)
{
	if constexpr(Layer==visual_layer_count)
		callback();
	else if(mask&(1U<<Layer))
		{
		scoped_value_restorest<int32_t> zero(layers[Layer][index]);
		with_hidden_visual_layers<Layer+1>(layers,index,mask,callback);
		}
	else
		with_hidden_visual_layers<Layer+1>(layers,index,mask,callback);
}

// The arrays below every sprite group: terrain, spatter and low furniture.
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

// Calls `visit` on each of the 25 per-tile arrays the game draws a tile from, the 20
// texture slots and the 5 flag words, in the game's order, until one call returns false.
template<typename Viewport,typename Visit>
bool visit_tile_arrays(const Viewport *vp,const Visit &visit)
{
	return visit(vp->screentexpos_background)&&
		visit(vp->screentexpos_floor_flag)&&
		visit(vp->screentexpos_background_two)&&
		visit(vp->screentexpos_liquid_flag)&&
		visit(vp->screentexpos_spatter_flag)&&
		visit(vp->screentexpos_spatter)&&
		visit(vp->screentexpos_ramp_flag)&&
		visit(vp->screentexpos_shadow_flag)&&
		visit(vp->screentexpos_building_one)&&
		visit(vp->screentexpos_item)&&
		visit(vp->screentexpos_vehicle)&&
		visit(vp->screentexpos_vermin)&&
		visit(vp->screentexpos_left_creature)&&
		visit(vp->screentexpos)&&
		visit(vp->screentexpos_right_creature)&&
		visit(vp->screentexpos_building_two)&&
		visit(vp->screentexpos_projectile)&&
		visit(vp->screentexpos_high_flow)&&
		visit(vp->screentexpos_top_shadow)&&
		visit(vp->screentexpos_signpost)&&
		visit(vp->screentexpos_upleft_creature)&&
		visit(vp->screentexpos_up_creature)&&
		visit(vp->screentexpos_upright_creature)&&
		visit(vp->screentexpos_designation)&&
		visit(vp->screentexpos_interface);
}

// Whether a repaint of the tile would paint anything. The game draws only the per-tile
// arrays whose entry for the tile holds a texture or flag, so a tile whose 25 entries are all
// zero paints nothing. Tiles of a level below the camera are mostly like that already, and a
// tile on the camera's level becomes like that once the layers a stage hides are zeroed. Read
// with the layers hidden, right before the repaint it can save.
template<typename Viewport>
bool tile_paints_nothing(const Viewport *vp,int32_t index)
{
	return visit_tile_arrays(
		vp,[index](const auto *array){return array==nullptr||array[index]==0;});
}

// While the camera glide repaints every tile of every viewport, one word per tile of each
// viewport holds the bitwise OR of its 25 entries, so the word is zero exactly when the tile
// paints nothing. Such a tile still paints nothing whatever layers a stage hides, since
// hiding a layer only zeroes entries, so the stages skip it before hiding anything. The
// words are filled once per viewport, array by array, and hold only for the glide's frame:
// the game writes the arrays between frames, and the plugin restores every entry it writes.
template<typename Viewport>
struct blank_summariest
{
	struct summaryst
	{
		const Viewport *viewport;
		std::vector<uint64_t> nonzero;
	};
	std::vector<summaryst> summaries;

	// Fills one summary per viewport `viewport_of` picks out of `items`.
	template<typename Items,typename ViewportOf>
	void summarize(const Items &items,const ViewportOf &viewport_of)
		{
		summaries.clear();
		for(const auto &item:items)
			{
			const Viewport *vp=viewport_of(item);
			const size_t tile_count=size_t(vp->dim_x)*size_t(vp->dim_y);
			summaryst &summary=summaries.emplace_back();
			summary.viewport=vp;
			summary.nonzero.assign(tile_count,0);
			visit_tile_arrays(vp,[&](const auto *array)
				{
				if(array!=nullptr)
					for(size_t i=0;i<tile_count;++i)summary.nonzero[i]|=uint64_t(array[i]);
				return true;
				});
			}
		}

	void clear()
		{
		summaries.clear();
		}

	// Whether the summary, if there is one for the viewport, knows the tile paints nothing.
	bool known_blank(const Viewport *vp,int32_t index) const
		{
		for(const summaryst &summary:summaries)
			if(summary.viewport==vp)
				return size_t(index)<summary.nonzero.size()&&
					summary.nonzero[size_t(index)]==0;
		return false;
		}
};

// The layers a redraw through `group` has already painted as sprites, and so must hide.
// Designations are drawn last and stand alone.
constexpr uint16_t visual_layers_through_group(visual_render_groupst group)
{
	uint16_t mask=0;
	for(const auto &descriptor:visual_layer_descriptors)
		if(descriptor.render_group!=visual_render_groupst::designation&&
			static_cast<uint8_t>(descriptor.render_group)<=static_cast<uint8_t>(group))
			mask|=visual_layer_bit(descriptor.layer);
	return mask;
}

// A full repaint of the tile with the layers in `hidden_layers` blank. The interface layer is
// the shading for levels below the camera; a staged tile has sprites drawn over it
// afterwards, so `defer_interface` leaves the shading to repaint_interface_only. `repaint`
// is called as repaint(vp,x,y) with the entries zeroed.
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
		with_hidden_visual_layers(
			visual_layers(vp),index,hidden_layers,[&]{repaint(vp,x,y);});
		};
	if(!defer_interface||vp->screentexpos_interface==nullptr)stage();
	else with_zeroed_values(stage,vp->screentexpos_interface[index]);
}

// Repaints only what sits above `group`, after that group's sprites were drawn, with the
// layers in `hidden_layers` blank as well. The interface layer sits above every group, so
// each group's redraw would paint it again; repaint_interface_only places it once, after the
// sprites.
template<typename Viewport,typename Repaint>
void repaint_above(
	Viewport *vp,
	int32_t x,
	int32_t y,
	visual_render_groupst group,
	uint16_t hidden_layers,
	const Repaint &repaint)
{
	const int32_t index=x*vp->dim_y+y;
	const auto without_visuals=[&]
		{
		const auto stage=[&]
			{
			with_hidden_visual_layers(
				visual_layers(vp),
				index,
				uint16_t(hidden_layers|visual_layers_through_group(group)),
				[&]{repaint(vp,x,y);});
			};
		if(vp->screentexpos_interface==nullptr)stage();
		else with_zeroed_values(stage,vp->screentexpos_interface[index]);
		};
	if(group==visual_render_groupst::item||group==visual_render_groupst::vehicle)
		with_base_hidden(vp,index,without_visuals);
	else if(group==visual_render_groupst::main)
		with_main_hidden(vp,index,without_visuals);
	else
		with_upper_hidden(vp,index,without_visuals);
}

// Every array the interface-only pass zeroes has to exist before it can be zeroed.
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
		vp->screentexpos_signpost!=nullptr;
}

// Paints the shading alone, after the sprites, so it covers them instead of lying beneath.
template<typename Viewport,typename Repaint>
void repaint_interface_only(Viewport *vp,int32_t x,int32_t y,const Repaint &repaint)
{
	const int32_t index=x*vp->dim_y+y;
	const auto without_visuals=[&]
		{
		with_hidden_visual_layers(
			visual_layers(vp),index,all_visual_layers_mask,[&]{repaint(vp,x,y);});
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
		vp->screentexpos_signpost[index]);
}

#endif
