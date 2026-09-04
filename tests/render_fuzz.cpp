// SPDX-License-Identifier: MIT

// Drives the real animation manager over random viewport histories and checks every frame the
// pass paints against the properties it owes the engine, with no reference implementation:
// what it blanks, what it repaints, in which order, with which buffers hidden, and that it
// leaves the buffers as it found them. Usage: render_fuzz [trials] [frames]

#include "frame_render.h"
#include "visual_animation.h"
#include "fake_viewport.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <set>
#include <vector>

namespace {

using table=viewport_layer_tablest<fake_viewportst>;
using tilest=std::array<int32_t,2>;

struct paint_opst
{
	enum kindst{repaint,sprite,fill,clip,unclip};
	kindst kind;
	size_t level=0;                       // repaint: index into the viewport list
	int32_t x=0,y=0,w=0,h=0;              // repaint: tile; fill/clip: pixel rect
	int32_t origin_x=0,origin_y=0;        // the origin the engine would paint with
	repaint_passst pass{};                // repaint: the pass the renderer said it was
	// repaint: the buffers as the engine would read them, visual layers by layer index.
	std::array<int32_t,visual_layer_count> visual{};
	int64_t base=0;                       // sum of the terrain buffers beneath every group
	int32_t vermin=0;
	int64_t upper=0;                      // sum of the buffers above the main group
	int32_t interface=0;
	bool has_interface=false;
	bool blank=true;                      // every buffer read as zero
	const void *texture=nullptr;          // sprite
	float px=0,py=0,size=0;
	bool mirrored=false;
};

struct recording_canvasst
{
	int32_t ox=0,oy=0,zoom_=128;
	const std::vector<fake_viewportst *> *viewports=nullptr;
	std::vector<paint_opst> ops;

	int32_t origin_x() const {return ox;}
	int32_t origin_y() const {return oy;}
	int32_t zoom() const {return zoom_;}
	void offset_origin(int32_t dx,int32_t dy){ox+=dx;oy+=dy;}
	const void *texture(int32_t texpos) const
		{
		return texpos%7==0?nullptr:reinterpret_cast<const void *>(intptr_t(texpos));
		}
	void repaint(fake_viewportst *vp,int32_t x,int32_t y,repaint_passst pass)
		{
		paint_opst op{paint_opst::repaint};
		op.level=size_t(std::find(viewports->begin(),viewports->end(),vp)-viewports->begin());
		op.x=x;op.y=y;op.origin_x=ox;op.origin_y=oy;op.pass=pass;
		const size_t i=size_t(x)*size_t(vp->dim_y)+size_t(y);
		const visual_layer_pointerst layers=table::current(vp);
		for(size_t l=0;l<visual_layer_count;++l)op.visual[l]=layers[l][i];
		op.base=int64_t(vp->screentexpos_background[i])+int64_t(vp->screentexpos_floor_flag[i])+
			vp->screentexpos_background_two[i]+vp->screentexpos_liquid_flag[i]+
			vp->screentexpos_spatter_flag[i]+vp->screentexpos_spatter[i]+
			int64_t(vp->screentexpos_ramp_flag[i])+vp->screentexpos_shadow_flag[i]+
			vp->screentexpos_building_one[i];
		op.vermin=vp->screentexpos_vermin[i];
		op.upper=int64_t(vp->screentexpos_building_two[i])+vp->screentexpos_projectile[i]+
			vp->screentexpos_high_flow[i]+vp->screentexpos_top_shadow[i]+vp->screentexpos_signpost[i];
		op.has_interface=vp->screentexpos_interface!=nullptr;
		op.interface=op.has_interface?vp->screentexpos_interface[i]:0;
		op.blank=tile_paints_nothing(vp,int32_t(i));
		ops.push_back(op);
		}
	void draw_sprite(const void *texture,float x,float y,float size,bool mirrored)
		{
		paint_opst op{paint_opst::sprite};
		op.texture=texture;op.px=x;op.py=y;op.size=size;op.mirrored=mirrored;
		op.origin_x=ox;op.origin_y=oy;
		ops.push_back(op);
		}
	void fill_black(pixel_rectst r)
		{
		paint_opst op{paint_opst::fill};
		op.x=r.x;op.y=r.y;op.w=r.w;op.h=r.h;
		ops.push_back(op);
		}
	void set_clip(pixel_rectst r)
		{
		paint_opst op{paint_opst::clip};
		op.x=r.x;op.y=r.y;op.w=r.w;op.h=r.h;
		ops.push_back(op);
		}
	void clear_clip(){ops.push_back({paint_opst::unclip});}
};

struct creaturest{int32_t x,y,texpos;bool right,left,up,item,designation;};

// One frame's expectations, checked op by op.
struct frame_checkst
{
	const std::vector<fake_viewportst *> &viewports;
	const std::vector<std::vector<sprite_proxyst>> &proxies;   // per level, as collected
	const recording_canvasst &canvas;
	int32_t ox,oy,zoom,glide_x,glide_y;
	std::set<tilest> covered;   // the tiles the frame may paint on, in the main clip
	int failures=0;
	int trial=0,frame=0;

	void check(bool ok,const char *what,size_t op=size_t(-1))
		{
		if(ok)return;
		if(failures++==0)
			{
			printf("trial %d frame %d glide %d,%d: %s",trial,frame,glide_x,glide_y,what);
			if(op!=size_t(-1))printf(" (op %zu)",op);
			printf("\n");
			}
		}

	int32_t size() const {return tile_pixel_size(zoom);}
	int32_t paint_ox() const {return ox+glide_x;}
	int32_t paint_oy() const {return oy+glide_y;}

	// The tile whose pixel span [tile_pixel(t),tile_pixel(t+1)) holds `px`; the spans are not
	// a uniform stride below zoom 128.
	int32_t tile_at(double px,int32_t origin) const
		{
		int32_t t=int32_t(std::floor((px-origin)*128.0/(zoom*32.0)));
		while(tile_pixel(t+1,origin,zoom)<=px)++t;
		while(tile_pixel(t,origin,zoom)>px)--t;
		return t;
		}

	// The screen tiles a pixel rect overlaps under the paint origin, clipped to the main grid.
	std::vector<tilest> tiles_under(float x,float y,float w,float h) const
		{
		const fake_viewportst *main=viewports.back();
		const int32_t x0=tile_at(x,paint_ox()),x1=tile_at(std::nextafter(double(x)+w,-1e9),paint_ox());
		const int32_t y0=tile_at(y,paint_oy()),y1=tile_at(std::nextafter(double(y)+h,-1e9),paint_oy());
		std::vector<tilest> tiles;
		for(int32_t tx=std::max(x0,0);tx<=std::min(x1,main->dim_x-1);++tx)
			for(int32_t ty=std::max(y0,0);ty<=std::min(y1,main->dim_y-1);++ty)
				tiles.push_back({tx,ty});
		return tiles;
		}

	static bool inside_clip(const fake_viewportst *vp,int32_t x,int32_t y)
		{
		return x>=vp->clipx[0]&&x<=vp->clipx[1]&&y>=vp->clipy[0]&&y<=vp->clipy[1];
		}

	void run()
		{
		const std::vector<paint_opst> &ops=canvas.ops;
		const fake_viewportst *main=viewports.back();
		const bool glide=glide_x||glide_y;
		// The frame's coverage: every tile a collected sprite may paint over, or the whole clip
		// on a glide, when the map is redrawn between tiles.
		if(glide)
			{
			for(int32_t x=main->clipx[0];x<=main->clipx[1];++x)
				for(int32_t y=main->clipy[0];y<=main->clipy[1];++y)covered.insert({x,y});
			}
		else
			for(const auto &level:proxies)
				for(const sprite_proxyst &proxy:level)
					proxy.for_each_covered_tile([&](int32_t x,int32_t y)
						{
						if(x>=0&&x<main->dim_x&&y>=0&&y<main->dim_y&&inside_clip(main,x,y))
							covered.insert({x,y});
						});

		// Fills and clipping: a glide clips to the map rect and fills it once, before anything
		// else, then restores the origin; a still frame fills each covered tile exactly once,
		// all fills first, and never clips.
		size_t first_paint=0;
		if(glide)
			{
			check(ops.size()>=3,"glide frame too short");
			if(failures)return;
			const int32_t left=tile_pixel(main->clipx[0],ox,zoom),top=tile_pixel(main->clipy[0],oy,zoom);
			const int32_t right=tile_pixel(main->clipx[1]+1,ox,zoom),bottom=tile_pixel(main->clipy[1]+1,oy,zoom);
			const auto is_rect=[&](const paint_opst &op,paint_opst::kindst kind)
				{return op.kind==kind&&op.x==left&&op.y==top&&op.w==right-left&&op.h==bottom-top;};
			check(is_rect(ops[0],paint_opst::clip),"glide does not start by clipping to the map rect");
			check(is_rect(ops[1],paint_opst::fill),"glide does not fill the map rect first");
			check(ops.back().kind==paint_opst::unclip,"glide does not end by clearing the clip");
			check(canvas.ox==ox&&canvas.oy==oy,"origin not restored after the glide");
			for(size_t i=2;i+1<ops.size();++i)
				check(ops[i].kind==paint_opst::repaint||ops[i].kind==paint_opst::sprite,"unexpected op inside a glide",i);
			first_paint=2;
			}
		else
			{
			std::set<tilest> filled;
			size_t i=0;
			for(;i<ops.size()&&ops[i].kind==paint_opst::fill;++i)
				{
				const paint_opst &op=ops[i];
				check(op.w==size()&&op.h==size(),"fill is not one tile",i);
				const tilest tile{tile_at(op.x,ox),tile_at(op.y,oy)};
				check(tile_pixel(tile[0],ox,zoom)==op.x&&tile_pixel(tile[1],oy,zoom)==op.y,"fill is off the tile grid",i);
				check(covered.count(tile)!=0,"fill outside the coverage",i);
				check(filled.insert(tile).second,"tile filled twice",i);
				}
			check(filled==covered,"the filled tiles are not the covered tiles");
			first_paint=i;
			for(;i<ops.size();++i)
				check(ops[i].kind==paint_opst::repaint||ops[i].kind==paint_opst::sprite,"fill or clip after the first repaint",i);
			}
		if(failures)return;

		// Levels are painted lowest first; each repaint is inside its level's clip, on a covered
		// tile, at the paint origin, and paints something.
		size_t level_so_far=0;
		std::vector<std::vector<std::vector<size_t>>> repaints(viewports.size(),
			std::vector<std::vector<size_t>>(size_t(main->dim_x)*size_t(main->dim_y)));
		std::vector<std::vector<size_t>> sprites(viewports.size());
		const auto level_of=[&](const void *texture){return size_t(intptr_t(texture)/1000);};
		for(size_t i=first_paint;i<ops.size();++i)
			{
			const paint_opst &op=ops[i];
			if(op.kind==paint_opst::unclip)continue;
			const size_t level=op.kind==paint_opst::repaint?op.level:level_of(op.texture);
			check(level<viewports.size(),"paint on an unknown level",i);
			if(failures)return;
			check(level>=level_so_far,"a lower level painted after a higher one",i);
			level_so_far=level;
			check(op.origin_x==paint_ox()&&op.origin_y==paint_oy(),"painted at the wrong origin",i);
			if(op.kind==paint_opst::repaint)
				{
				const fake_viewportst *vp=viewports[level];
				check(inside_clip(vp,op.x,op.y),"repaint outside the level's clip",i);
				check(covered.count({op.x,op.y})!=0,"repaint outside the coverage",i);
				check(!op.blank,"repaint of a tile that paints nothing",i);
				repaints[level][size_t(op.x)*size_t(main->dim_y)+size_t(op.y)].push_back(i);
				}
			else
				{
				check(op.size==float(size()),"sprite drawn at the wrong size",i);
				check(!op.mirrored||flip,"mirrored sprite with flipping off",i);
				for(const tilest &tile:tiles_under(op.px,op.py,op.size,op.size))
					check(covered.count(tile)!=0,"sprite over an uncovered tile",i);
				sprites[level].push_back(i);
				}
			}
		for(size_t level=0;level<viewports.size();++level)
			check(sprites[level].size()==proxies[level].size(),"sprite count differs from the collected proxies");
		if(failures)return;

		// Per level and tile: the first repaint stages the tile beneath its sprites, with the
		// layers that have a proxy targeting it hidden; every later repaint is a pass above the
		// group of the sprite drawn last at that level, and hides the terrain beneath every
		// group and the layers through that group; the shading (interface) is never painted
		// beneath a sprite and comes back exactly once after the tile's last sprite.
		for(size_t level=0;level<viewports.size();++level)
			{
			const fake_viewportst *vp=viewports[level];
			for(int32_t x=0;x<main->dim_x;++x)
				for(int32_t y=0;y<main->dim_y;++y)
					{
					const size_t index=size_t(x)*size_t(main->dim_y)+size_t(y);
					const std::vector<size_t> &tile_repaints=repaints[level][index];
					uint16_t proxied=0;
					for(const sprite_proxyst &proxy:proxies[level])
						if(proxy.target_x==x&&proxy.target_y==y)proxied|=visual_layer_bit(proxy.layer);
					std::vector<size_t> over;   // sprites of this level over the tile
					for(const size_t s:sprites[level])
						{
						const paint_opst &op=ops[s];
						const std::vector<tilest> tiles=tiles_under(op.px,op.py,op.size,op.size);
						if(std::find(tiles.begin(),tiles.end(),tilest{x,y})!=tiles.end())over.push_back(s);
						}
					const bool on_grid=x<vp->dim_x&&y<vp->dim_y;
					const int32_t vp_index=on_grid?x*vp->dim_y+y:0;
					const visual_layer_pointerst layers=table::current(const_cast<fake_viewportst *>(vp));
					// A covered tile the level shows something on is repainted, whether or not
					// the level has a sprite on it: the fill beneath took the engine's paint.
					if(on_grid&&covered.count({x,y})!=0&&inside_clip(vp,x,y)&&!tile_paints_nothing(vp,vp_index))
						check(!tile_repaints.empty(),"a covered tile the level shows was not repainted");
					if(!tile_repaints.empty())
						{
						const paint_opst &staged=ops[tile_repaints.front()];
						check(staged.pass.kind==repaint_passst::staged,"a tile's first repaint is not the staging pass",tile_repaints.front());
						for(size_t l=0;l<visual_layer_count;++l)
							{
							if(proxied&(1U<<l))check(staged.visual[l]==0,"staged repaint shows a proxied layer",tile_repaints.front());
							else check(staged.visual[l]==layers[l][vp_index],"staged repaint hides a layer nothing proxies",tile_repaints.front());
							}
						if(!over.empty())
							{
							check(tile_repaints.front()<over.front(),"a sprite drawn before its tile was staged",over.front());
							if(staged.has_interface)check(staged.interface==0,"shading staged beneath a sprite",tile_repaints.front());
							}
						}
					size_t shading_after_last=0;
					for(const size_t r:tile_repaints)
						{
						const paint_opst &op=ops[r];
						// A repaint after a level's sprites is the pass above the group drawn
						// last (the renderer names it; the sprite drawn last confirms it): it
						// adds what sits above that group, with the group's own layers and the
						// proxied ones hidden, and leaves the rest as the buffers hold it.
						const size_t *last_sprite=nullptr;
						for(const size_t &s:sprites[level])
							if(s<r)last_sprite=&s;
						if(last_sprite==nullptr)
							check(r==tile_repaints.front(),"a repaint before any sprite that is not the staging pass",r);
						else
							{
							visual_render_groupst group=visual_render_groupst::item;
							for(const sprite_proxyst &proxy:proxies[level])
								if(proxy.texture==ops[*last_sprite].texture)group=visual_render_group(proxy.layer);
							check(op.pass.group==group,"a repaint above a group other than the one drawn last",r);
							if(group==visual_render_groupst::designation)
								{
								// Above the designations only the shading is left to paint.
								check(op.pass.kind==repaint_passst::interface_only,"the pass over a designation is not shading only",r);
								check(op.base==0&&op.vermin==0&&op.upper==0,"a buffer repainted over a designation",r);
								for(size_t l=0;l<visual_layer_count;++l)
									check(op.visual[l]==0,"a layer repainted over a designation",r);
								}
							else
								{
								check(op.pass.kind==repaint_passst::above_group,"the pass over a sprite is not the pass above its group",r);
								const uint16_t hidden=uint16_t(proxied|visual_layers_through_group(group));
								check(op.base==0,"terrain repainted over a sprite",r);
								if(group>=visual_render_groupst::main)check(op.vermin==0,"vermin repainted over a creature",r);
								if(group>=visual_render_groupst::upper)check(op.upper==0,"an upper buffer repainted over an upper sprite",r);
								for(size_t l=0;l<visual_layer_count;++l)
									{
									if(hidden&(1U<<l))check(op.visual[l]==0,"a layer repainted over a sprite of its own group",r);
									else check(op.visual[l]==layers[l][vp_index],"a repaint over a sprite hides a layer above it",r);
									}
								}
							}
						if(op.has_interface&&op.interface!=0)
							{
							check(over.empty()||r>over.back(),"shading painted beneath a sprite",r);
							if(!over.empty()&&r>over.back())++shading_after_last;
							}
						}
					if(!over.empty()&&vp->screentexpos_interface&&vp->screentexpos_interface[index]!=0&&inside_clip(vp,x,y))
						check(shading_after_last==1,"shading not painted exactly once after the tile's last sprite");
					}
			}
		}
	bool flip=false;
};

} // namespace

int main(int argc,char **argv)
{
	const int trials=argc>1?atoi(argv[1]):300;
	const int frames=argc>2?atoi(argv[2]):50;
	// render_fuzz trials frames trial frame: dump that frame's proxies and ops.
	const int dump_trial=argc>4?atoi(argv[3]):-1,dump_frame=argc>4?atoi(argv[4]):-1;
	std::mt19937 rng(12345);
	auto chance=[&](int n){return int(rng()%uint32_t(n))==0;};
	long rendered=0,glides=0,sprites_total=0,failed=0;
	for(int trial=0;trial<trials;++trial)
		{
		const int32_t dim_x=6+int32_t(rng()%9),dim_y=5+int32_t(rng()%8);
		const size_t vp_count=1+rng()%3;
		std::vector<fake_viewportst> storage(vp_count);
		std::vector<fake_viewportst *> viewports;
		// The levels of one view share the map rect, as in the game.
		const bool full=chance(2);
		const std::array<int32_t,2> clipx={full?0:int32_t(rng()%2),full?dim_x-1:dim_x-1-int32_t(rng()%2)};
		const std::array<int32_t,2> clipy={full?0:int32_t(rng()%2),full?dim_y-1:dim_y-1-int32_t(rng()%2)};
		for(auto &vp:storage)
			{
			vp.allocate(dim_x,dim_y);
			vp.clipx=clipx;
			vp.clipy=clipy;
			if(chance(5))vp.screentexpos_interface=nullptr;
			viewports.push_back(&vp);
			}
		fake_viewportst *main=viewports.back();
		// Textures encode their level (texpos/1000) and, within it, one creature per ten so
		// a drawn sprite can be attributed to its proxy.
		std::vector<std::vector<creaturest>> creatures(vp_count);
		for(size_t v=0;v<vp_count;++v)
			for(int i=0;i<1+int(rng()%6);++i)
				creatures[v].push_back({int32_t(rng()%uint32_t(dim_x)),int32_t(rng()%uint32_t(dim_y)),
					int32_t(v)*1000+10+10*i,chance(3),chance(3),chance(3),chance(3),chance(4)});
		visual_animation_managerst manager;
		manager.set_base_duration_ms(60+rng()%200);
		frame_rendererst<fake_viewportst> renderer;
		render_settingst &settings=renderer.get_settings();
		settings.flip=chance(3)?false:true;
		settings.bob.enabled=chance(3)?false:true;
		settings.bob.amplitude=float(rng()%30)/100.f;
		settings.bob.hops=1+int(rng()%2);
		uint32_t now=1000;
		int32_t pan_x=0,pan_y=0;
		uint32_t revision=1;
		recording_canvasst canvas;
		canvas.viewports=&viewports;
		canvas.zoom_=chance(2)?128:int32_t(64+rng()%128);
		canvas.ox=int32_t(rng()%40);
		canvas.oy=int32_t(rng()%40);
		sprite_collectorst collector;
		std::vector<std::vector<sprite_proxyst>> proxies(vp_count);
		for(int frame=0;frame<frames;++frame)
			{
			now+=10+rng()%40;
			// New engine redraw: previous <- current, then rewrite current from the creatures.
			const bool redraw=chance(2);
			bool scrolled=false;
			int32_t sdx=0,sdy=0;
			if(redraw)
				{
				if(chance(12)){scrolled=true;sdx=int32_t(rng()%3)-1;sdy=int32_t(rng()%3)-1;}
				for(size_t v=0;v<vp_count;++v)
					{
					fake_viewportst &vp=storage[v];
					const size_t n=size_t(dim_x)*size_t(dim_y);
					for(const auto &buffer:table::buffers)
						{
						int32_t *current=vp.*buffer.current;
						std::copy(current,current+n,vp.*buffer.previous);
						std::fill(current,current+n,0);
						}
					for(size_t i=0;i<n;++i)
						{
						vp.screentexpos_background[i]=int32_t(rng()%5);
						vp.screentexpos_spatter_flag[i]=chance(15)?0x10000000U:uint32_t(rng()%3);
						if(vp.screentexpos_interface)vp.screentexpos_interface[i]=int32_t(rng()%3);
						vp.screentexpos_building_two[i]=int32_t(rng()%2);
						vp.screentexpos_top_shadow[i]=int32_t(rng()%2);
						vp.screentexpos_floor_flag[i]=uint64_t(rng()%2);vp.screentexpos_background_two[i]=int32_t(rng()%2);
						vp.screentexpos_liquid_flag[i]=uint32_t(rng()%2);vp.screentexpos_spatter[i]=int32_t(rng()%2);
						vp.screentexpos_ramp_flag[i]=uint64_t(rng()%2);vp.screentexpos_shadow_flag[i]=uint32_t(rng()%2);
						vp.screentexpos_building_one[i]=int32_t(rng()%2);vp.screentexpos_vermin[i]=int32_t(rng()%2);
						vp.screentexpos_projectile[i]=int32_t(rng()%2);vp.screentexpos_high_flow[i]=int32_t(rng()%2);
						vp.screentexpos_signpost[i]=int32_t(rng()%2);
						if(chance(6))vp.screentexpos_item[i]=int32_t(v)*1000+200+int32_t(rng()%20);
						if(chance(10))vp.screentexpos_vehicle[i]=int32_t(v)*1000+300+int32_t(rng()%5);
						}
					for(creaturest &c:creatures[v])
						{
						if(chance(2)){c.x+=int32_t(rng()%3)-1;c.y+=int32_t(rng()%3)-1;}
						c.x-=sdx;c.y-=sdy;
						c.x=std::clamp(c.x,0,dim_x-1);c.y=std::clamp(c.y,0,dim_y-1);
						auto put=[&](int32_t *b,int32_t x,int32_t y,int32_t t)
							{if(x>=0&&x<dim_x&&y>=0&&y<dim_y)b[size_t(x)*size_t(dim_y)+size_t(y)]=t;};
						put(vp.screentexpos,c.x,c.y,c.texpos);
						if(c.right)put(vp.screentexpos_right_creature,c.x+1,c.y,c.texpos+1);
						if(c.left)put(vp.screentexpos_left_creature,c.x-1,c.y,c.texpos+2);
						if(c.up)put(vp.screentexpos_up_creature,c.x,c.y-1,c.texpos+3);
						if(c.item)put(vp.screentexpos_item,c.x,c.y,c.texpos+4);
						if(c.designation)put(vp.screentexpos_designation,c.x,c.y,c.texpos+5);
						}
					}
				}
			if(scrolled){pan_x+=sdx;pan_y+=sdy;}
			else if(chance(30)){pan_x+=1;}   // a scroll that has not landed yet
			if(chance(40))++revision;
			manager.begin_frame(now);
			for(size_t v=0;v<vp_count;++v)
				{
				const fake_viewportst *vp=&storage[v];
				manager.synchronize_viewport({vp,vp->dim_x,vp->dim_y,revision,
					table::current(vp),table::previous(vp),pan_x,pan_y});
				}
			manager.end_frame();
			const int32_t glide_x=chance(8)?int32_t(rng()%21)-10:0;
			const int32_t glide_y=chance(8)?int32_t(rng()%21)-10:0;
			if(renderer.frame_paint(glide_x||glide_y,viewports,manager)==frame_paintst::nothing)continue;
			++rendered;
			if(glide_x||glide_y)++glides;
			// What the pass should be painting, collected independently of it.
			const auto texture_of=[&](int32_t texpos){return canvas.texture(texpos);};
			for(size_t v=0;v<vp_count;++v)
				collector.collect(view_of(viewports[v]),manager,settings.sprite(),texture_of,proxies[v]);
			const std::vector<fake_viewportst> before=storage;
			canvas.ops.clear();
			renderer.render(canvas,viewports,main,manager,glide_x,glide_y);
			frame_checkst checks{viewports,proxies,canvas,canvas.ox,canvas.oy,canvas.zoom_,glide_x,glide_y,{},0,trial,frame};
			checks.flip=settings.flip;
			checks.run();
			// The buffers are the engine's: every value blanked for a repaint is back.
			for(size_t v=0;v<vp_count;++v)
				{
				bool same=true;
				for(size_t i=0;i<29;++i)same&=before[v].i32[i]==storage[v].i32[i];
				for(size_t i=0;i<3;++i)same&=before[v].u32[i]==storage[v].u32[i];
				for(size_t i=0;i<2;++i)same&=before[v].u64[i]==storage[v].u64[i];
				checks.check(same,"viewport buffers changed by the pass");
				}
			for(const paint_opst &op:canvas.ops)sprites_total+=op.kind==paint_opst::sprite;
			if(checks.failures)++failed;
			if(trial==dump_trial&&frame==dump_frame)
				{
				printf("dims %dx%d clip x %d..%d y %d..%d zoom %d origin %d,%d flip %d bob %d\n",dim_x,dim_y,clipx[0],clipx[1],clipy[0],clipy[1],canvas.zoom_,canvas.ox,canvas.oy,int(settings.flip),int(settings.bob.enabled));
				for(size_t v=0;v<vp_count;++v)
					for(const sprite_proxyst &p:proxies[v])
						printf("proxy level %zu layer %d source %.2f,%.2f target %d,%d texpos %d progress %.2f mirror %d shift %d bob %d\n",
							v,int(p.layer),double(p.source_x),double(p.source_y),p.target_x,p.target_y,p.texpos,double(p.progress),int(p.mirrored),p.mirror_shift,int(p.bob));
				for(size_t i=0;i<canvas.ops.size();++i)
					{
					const paint_opst &op=canvas.ops[i];
					if(op.kind==paint_opst::repaint)
						{
						printf("%3zu repaint level %zu tile %d,%d origin %d,%d visual",i,op.level,op.x,op.y,op.origin_x,op.origin_y);
						for(int32_t v:op.visual)printf(" %d",v);
						printf(" base %lld vermin %d upper %lld interface %d%s\n",(long long)op.base,op.vermin,(long long)op.upper,op.interface,op.has_interface?"":" (none)");
						}
					else if(op.kind==paint_opst::sprite)
						printf("%3zu sprite tex %lld at %.2f,%.2f size %.0f mirrored %d\n",i,(long long)intptr_t(op.texture),double(op.px),double(op.py),double(op.size),int(op.mirrored));
					else printf("%3zu %s %d,%d %dx%d\n",i,op.kind==paint_opst::fill?"fill":op.kind==paint_opst::clip?"clip":"unclip",op.x,op.y,op.w,op.h);
					}
				}
			}
		}
	printf("rendered %ld frames (%ld glides), sprites %ld, failed %ld\n",rendered,glides,sprites_total,failed);
	return failed?1:0;
}
