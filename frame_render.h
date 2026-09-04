// SPDX-License-Identifier: MIT

#ifndef FRAME_RENDER_H
#define FRAME_RENDER_H

#include "sprite_proxies.h"
#include "tile_coverage.h"
#include "tile_repaint.h"
#include "visual_layers.h"

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

// Paints one frame's smooth movement over the engine's own frame. Generic over the viewport
// type and over a canvas, so the whole pass runs against test doubles:
//
//   canvas.origin_x(), origin_y(), zoom()   the engine's map origin in pixels and zoom factor
//   canvas.offset_origin(dx,dy)             shifts the origin the engine repaints with
//   canvas.repaint(vp,x,y)                  the engine's own tile repaint
//   canvas.draw_sprite(texture,x,y,size,mirrored)
//   canvas.fill_black(rect)
//   canvas.set_clip(rect) / clear_clip()

struct walk_bob_settingst
{
	bool enabled=false;
	float amplitude=0.10f;
	float horizontal_mult=1.0f;
	float diagonal_mult=2.4f;
	float vertical_mult=2.7f;
	int hops=2;
};

struct render_settingst
{
	bool flip=false;
	walk_bob_settingst bob;

	sprite_settingst sprite() const
		{
		return {flip,bob.enabled};
		}
};

struct pixel_rectst
{
	int32_t x=0;
	int32_t y=0;
	int32_t w=0;
	int32_t h=0;
};

inline int32_t tile_pixel(int32_t tile,int32_t origin,int32_t zoom)
{
	return zoom==128?32*tile+origin:(zoom*32*tile)/128+origin;
}

inline int32_t tile_pixel_size(int32_t zoom)
{
	return zoom==128?32:std::max(1,zoom*32/128);
}

template<typename Viewport>
viewport_viewst view_of(const Viewport *vp)
{
	using table=viewport_layer_tablest<Viewport>;
	return {
		vp,
		{vp->dim_x,vp->dim_y},
		vp->clipx[0],
		vp->clipx[1],
		vp->clipy[0],
		vp->clipy[1],
		table::current(vp),
		table::previous(vp),
		vp->screentexpos_spatter_flag};
}

template<typename Viewport>
struct viewport_renderst
{
	Viewport *viewport=nullptr;
	viewport_viewst view;
	std::vector<sprite_proxyst> proxies;
	tile_coveragest coverage;
};

template<typename Viewport>
class frame_rendererst
{
	render_settingst settings;
	sprite_collectorst collector;
	std::vector<viewport_renderst<Viewport>> renders;
	size_t render_count=0;
	// This frame's tiles, last frame's, and their union: the union is blacked out and
	// repainted, since last frame's sprites are still on screen where nothing else paints.
	tile_coveragest coverage;
	tile_coveragest previous_coverage;
	tile_coveragest redraw_coverage;
	std::vector<int32_t> mirrored_scratch;

	template<typename Canvas>
	void draw_proxy(Canvas &canvas,const sprite_proxyst &proxy) const
		{
		const int32_t zoom=canvas.zoom();
		const int32_t target_x=tile_pixel(proxy.target_x,canvas.origin_x(),zoom);
		const int32_t target_y=tile_pixel(proxy.target_y,canvas.origin_y(),zoom);
		const float tile_size=float(tile_pixel_size(zoom));
		const float source_x=target_x+(proxy.source_x-proxy.target_x)*tile_size;
		const float source_y=target_y+(proxy.source_y-proxy.target_y)*tile_size;
		const float mirror_offset=float(proxy.mirror_shift)*tile_size;
		const walk_bob_directionst bob_direction=walk_bob_direction(
			proxy.source_x,proxy.source_y,proxy.target_x,proxy.target_y);
		const walk_bob_settingst &bob=settings.bob;
		const float bob_mult=
			bob_direction==walk_bob_directionst::horizontal?bob.horizontal_mult:
			bob_direction==walk_bob_directionst::vertical?bob.vertical_mult:
			bob.diagonal_mult;
		const float bob_offset=proxy.bob?
			-walk_bob_lift(proxy.progress,bob.hops,bob.amplitude,bob_mult)*tile_size:0.0f;
		const float base_x=source_x+(target_x-source_x)*proxy.progress+mirror_offset;
		const float base_y=source_y+(target_y-source_y)*proxy.progress+bob_offset;
		canvas.draw_sprite(proxy.texture,base_x,base_y,tile_size,proxy.mirrored);
		}

	// Repaints a tile through every viewport that shows it, lowest first. A tile with
	// sprites stops at the first: the stage pass repaints what lies above them afterwards.
	template<typename Canvas>
	void repaint_world_tile(Canvas &canvas,int32_t x,int32_t y) const
		{
		const bool staged=coverage.covers(x,y);
		const auto repaint=[&](Viewport *vp,int32_t tx,int32_t ty){canvas.repaint(vp,tx,ty);};
		for(size_t i=0;i<render_count;++i)
			{
			const viewport_renderst<Viewport> &render=renders[i];
			if(render.view.inside_clip(x,y))
				repaint_staged(
					render.viewport,x,y,
					render.coverage.proxied_layers(render.view.grid.index(x,y)),
					staged,repaint);
			if(staged)break;
			}
		}

	// Sprites, group by group, each group followed by a repaint of what sits above it.
	template<typename Canvas>
	void draw_stages(Canvas &canvas) const
		{
		const auto repaint=[&](Viewport *vp,int32_t tx,int32_t ty){canvas.repaint(vp,tx,ty);};
		for(size_t i=0;i<render_count;++i)
			{
			const viewport_renderst<Viewport> &render=renders[i];
			Viewport *vp=render.viewport;
			// A lower level's sprite must be covered by the next level's fog and terrain, so
			// that level is reapplied before its own sprites, in the engine's draw order.
			if(i>0)
				{
				coverage.for_each([&](int32_t x,int32_t y)
					{
					if(!render.view.inside_clip(x,y))return;
					repaint_staged(
						vp,x,y,
						render.coverage.proxied_layers(render.view.grid.index(x,y)),
						true,repaint);
					});
				}
			for(uint8_t g=0;g<static_cast<uint8_t>(visual_render_groupst::count);++g)
				{
				const auto group=static_cast<visual_render_groupst>(g);
				for(const sprite_proxyst &proxy:render.proxies)
					if(visual_render_group(proxy.layer)==group)draw_proxy(canvas,proxy);
				if(group==visual_render_groupst::designation)continue;
				render.coverage.for_each_in_group(group,[&](int32_t x,int32_t y)
					{
					repaint_above(
						vp,x,y,group,
						render.coverage.proxied_layers(render.view.grid.index(x,y)),
						repaint);
					});
				}
			// A level shades everything drawn beneath it, so this covers every staged tile,
			// not only the ones this level has sprites on.
			coverage.for_each([&](int32_t x,int32_t y)
				{
				if(render.view.inside_clip(x,y))repaint_interface_only(vp,x,y,repaint);
				});
			}
		}

	public:
		render_settingst &get_settings()
			{
			return settings;
			}

		const render_settingst &get_settings() const
			{
			return settings;
			}

		// Last frame's sprites are gone from the screen (the engine repainted everything).
		void forget_coverage()
			{
			previous_coverage.clear();
			}

		// A frame without movement still needs painting when the engine repainted a tile that a
		// resting mirrored sprite covers: the sprite stays on screen until then. The reach
		// covers the sprite's fragments and the largest mirror shift.
		template<typename Manager>
		bool resting_sprites_disturbed(
			const std::vector<Viewport *> &viewports,
			const Manager &manager)
			{
			if(!settings.flip)return false;
			constexpr int32_t reach_x=2;
			constexpr int32_t reach_y=1;
			for(const Viewport *vp:viewports)
				{
				manager.mirrored_tiles(vp,mirrored_scratch);
				if(mirrored_scratch.empty())continue;
				const visual_gridst grid{vp->dim_x,vp->dim_y};
				for(const int32_t index:mirrored_scratch)
					{
					const int32_t x=index/vp->dim_y;
					const int32_t y=index%vp->dim_y;
					for(int32_t tx=x-reach_x;tx<=x+reach_x;++tx)
						for(int32_t ty=y-reach_y;ty<=y+reach_y;++ty)
							{
							if(!grid.contains(tx,ty))continue;
							for(const Viewport *level:viewports)
								if(engine_repainted_tile(level,grid.index(tx,ty)))return true;
							}
					}
				}
			return false;
			}

		// `viewports` lowest level first, ending with `main`; `glide` is the camera's pixel
		// offset from the tile grid, zero when it sits on it.
		template<typename Canvas,typename Manager>
		void render(
			Canvas &canvas,
			const std::vector<Viewport *> &viewports,
			Viewport *main,
			const Manager &manager,
			int32_t glide_x,
			int32_t glide_y)
			{
			const visual_gridst main_grid{main->dim_x,main->dim_y};
			coverage.reset(main_grid);
			if(renders.size()<viewports.size())renders.resize(viewports.size());
			render_count=viewports.size();
			const auto texture_of=[&](int32_t texpos){return canvas.texture(texpos);};
			for(size_t i=0;i<render_count;++i)
				{
				viewport_renderst<Viewport> &render=renders[i];
				render.viewport=viewports[i];
				render.view=view_of(viewports[i]);
				collector.collect(render.view,manager,settings.sprite(),texture_of,render.proxies);
				render.coverage.reset(render.view.grid);
				for(const sprite_proxyst &proxy:render.proxies)
					{
					const visual_render_groupst group=visual_render_group(proxy.layer);
					proxy.for_each_covered_tile([&](int32_t x,int32_t y)
						{
						render.coverage.mark(x,y,group);
						});
					render.coverage.mark_proxied(proxy.target_x,proxy.target_y,proxy.layer);
					}
				coverage.merge(render.coverage);
				}

			const viewport_viewst main_view=view_of(static_cast<const Viewport *>(main));
			const int32_t zoom=canvas.zoom();
			if(glide_x!=0||glide_y!=0)
				{
				// Camera between tiles: repaint the whole map rect at the shifted origin so
				// the world and the sprites render between tiles. The engine drew this frame
				// at the snapped position; everything here overdraws it, clipped to the map
				// rect so shifted tiles never spill over the UI. The uncovered strip on the
				// trailing edge stays black until the glide lands.
				const int32_t left=tile_pixel(main_view.clip_x0,canvas.origin_x(),zoom);
				const int32_t top=tile_pixel(main_view.clip_y0,canvas.origin_y(),zoom);
				const pixel_rectst map_rect=
					{
					left,
					top,
					tile_pixel(main_view.clip_x1+1,canvas.origin_x(),zoom)-left,
					tile_pixel(main_view.clip_y1+1,canvas.origin_y(),zoom)-top
					};
				canvas.set_clip(map_rect);
				canvas.fill_black(map_rect);
				canvas.offset_origin(glide_x,glide_y);
				for(int32_t x=main_view.clip_x0;x<=main_view.clip_x1;++x)
					for(int32_t y=main_view.clip_y0;y<=main_view.clip_y1;++y)
						repaint_world_tile(canvas,x,y);
				draw_stages(canvas);
				canvas.offset_origin(-glide_x,-glide_y);
				canvas.clear_clip();
				// Everything was repainted; per-tile bookkeeping restarts after the glide.
				previous_coverage.clear();
				return;
				}

			redraw_coverage.reset(main_grid);
			redraw_coverage.merge(coverage);
			redraw_coverage.merge(previous_coverage);
			const int32_t tile_size=tile_pixel_size(zoom);
			redraw_coverage.for_each([&](int32_t x,int32_t y)
				{
				if(!main_view.inside_clip(x,y))return;
				canvas.fill_black({
					tile_pixel(x,canvas.origin_x(),zoom),
					tile_pixel(y,canvas.origin_y(),zoom),
					tile_size,
					tile_size});
				});
			redraw_coverage.for_each([&](int32_t x,int32_t y)
				{
				if(main_view.inside_clip(x,y))repaint_world_tile(canvas,x,y);
				});
			draw_stages(canvas);
			std::swap(coverage,previous_coverage);
			}
};

#endif
