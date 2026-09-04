// SPDX-License-Identifier: MIT

#ifndef SDL_CANVAS_H
#define SDL_CANVAS_H

#include "frame_render.h"
#include "frame_stats.h"
#include "tile_repaint.h"

#include <cstdint>
#include <vector>

// The engine's 2D renderer as the canvas frame_rendererst paints on. Generic over the renderer,
// the viewport and the SDL entry points so the batching and colour bookkeeping run against
// test doubles. The renderer provides origin_x, origin_y, viewport_zoom_factor, sdl_renderer
// and update_viewport_tile(vp,x,y). The API object provides:
//
//   renderer_type, texture_type, rect_type {x,y,w,h}, frect_type {float x,y,w,h}, color_type
//   flip_horizontal                          the renderer-flip flag for mirrored sprites
//   render_copy_f(sdl,texture,src,dst)       SDL_RenderCopyF
//   render_copy_ex_f(sdl,texture,src,dst,angle,center,flip)
//   render_fill_rects(sdl,rects,count)
//   render_set_clip_rect(sdl,rect)           nullptr clears the clip
//   get_render_draw_color(sdl,&r,&g,&b,&a) / set_render_draw_color(sdl,r,g,b,a)
//   texture_of(renderer,texpos)              the cached texture for a texpos, or nullptr
//
// Black fills are collected and issued as one call right before anything could paint over them
// (a repaint, a sprite, a clip change or the end of the frame). The draw colour is switched to
// black on the first fill of the frame and put back when the canvas goes away, not per tile.
template<typename Renderer,typename Viewport,typename Api>
class sdl_canvasst
{
	using rect_type=typename Api::rect_type;
	using frect_type=typename Api::frect_type;
	using color_type=typename Api::color_type;

	Renderer *renderer;
	const Api &api;
	typename Api::renderer_type *sdl;
	const Viewport *main_viewport;
	frame_statisticst &stats;
	std::vector<rect_type> &fills;
	bool filling=false;
	color_type saved_r=0,saved_g=0,saved_b=0,saved_a=255;
	// Time from construction to the first canvas call is the sprite collection.
	uint64_t start_us;
	bool first_call=true;

	void flush_fills()
		{
		if(fills.empty())return;
		const uint64_t t=stats.detail_clock();
		api.render_fill_rects(sdl,fills.data(),int(fills.size()));
		if(t)stats.add(stats.fill_us,stats.now_us()-t);
		fills.clear();
		}

	void note_call()
		{
		if(!first_call)return;
		first_call=false;
		if(start_us)stats.add(stats.collect_us,stats.now_us()-start_us);
		}

	// Where the repaints land: the ones the pass could in principle avoid are counted.
	void classify_repaint(const Viewport *vp,int32_t x,int32_t y) const
		{
		stats.add(stats.tile_repaints);
		const int32_t index=x*vp->dim_y+y;
		if(tile_paints_nothing(vp,index))stats.add(stats.blank_repaints);
		if(vp==main_viewport)stats.add(stats.main_repaints);
		else if(main_viewport!=nullptr&&main_viewport->dim_x==vp->dim_x&&
			main_viewport->dim_y==vp->dim_y&&main_viewport->screentexpos_background&&
			main_viewport->screentexpos_background[index]!=0)
			stats.add(stats.occluded_repaints);
		}

	public:
		sdl_canvasst(
			Renderer *renderer,
			const Api &api,
			const Viewport *main_viewport,
			frame_statisticst &stats,
			std::vector<rect_type> &fill_scratch):
			renderer(renderer),
			api(api),
			sdl(static_cast<typename Api::renderer_type *>(renderer->sdl_renderer)),
			main_viewport(main_viewport),
			stats(stats),
			fills(fill_scratch),
			start_us(stats.clock())
			{
			fills.clear();
			}

		~sdl_canvasst()
			{
			flush_fills();
			if(filling)api.set_render_draw_color(sdl,saved_r,saved_g,saved_b,saved_a);
			}

		sdl_canvasst(const sdl_canvasst &)=delete;
		sdl_canvasst &operator=(const sdl_canvasst &)=delete;

		int32_t origin_x() const
			{
			return renderer->origin_x;
			}

		int32_t origin_y() const
			{
			return renderer->origin_y;
			}

		int32_t zoom() const
			{
			return renderer->viewport_zoom_factor;
			}

		void offset_origin(int32_t dx,int32_t dy)
			{
			renderer->origin_x+=dx;
			renderer->origin_y+=dy;
			}

		void repaint(Viewport *vp,int32_t x,int32_t y,repaint_passst)
			{
			flush_fills();
			note_call();
			if(stats.enabled)classify_repaint(vp,x,y);
			const uint64_t t=stats.detail_clock();
			renderer->update_viewport_tile(vp,x,y);
			if(t)stats.add(stats.engine_us,stats.now_us()-t);
			}

		const void *texture(int32_t texpos) const
			{
			return api.texture_of(renderer,texpos);
			}

		void draw_sprite(const void *texture,float x,float y,float size,bool mirrored)
			{
			flush_fills();
			note_call();
			stats.add(stats.sprites);
			const uint64_t t=stats.detail_clock();
			auto *sdl_texture=
				static_cast<typename Api::texture_type *>(const_cast<void *>(texture));
			const frect_type destination={x,y,size,size};
			if(mirrored)
				api.render_copy_ex_f(
					sdl,sdl_texture,nullptr,&destination,0.0,nullptr,Api::flip_horizontal);
			else api.render_copy_f(sdl,sdl_texture,nullptr,&destination);
			if(t)stats.add(stats.sprite_us,stats.now_us()-t);
			}

		void fill_black(const pixel_rectst &rect)
			{
			note_call();
			stats.add(stats.fills);
			if(!filling)
				{
				api.get_render_draw_color(sdl,&saved_r,&saved_g,&saved_b,&saved_a);
				api.set_render_draw_color(sdl,0,0,0,255);
				filling=true;
				}
			fills.push_back({rect.x,rect.y,rect.w,rect.h});
			}

		void set_clip(const pixel_rectst &rect)
			{
			flush_fills();
			note_call();
			stats.add(stats.glides);
			const rect_type sdl_rect={rect.x,rect.y,rect.w,rect.h};
			api.render_set_clip_rect(sdl,&sdl_rect);
			}

		void clear_clip()
			{
			flush_fills();
			api.render_set_clip_rect(sdl,nullptr);
			}
};

#endif
