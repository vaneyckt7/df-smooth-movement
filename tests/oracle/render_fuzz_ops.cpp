// Drives the real animation manager with random viewport histories and renders each frame
// through the old (oracle) and new frame code, comparing the recorded canvas call streams.
#include "frame_render.h"
#include "visual_animation.h"
#include "fake_viewport.h"
using oracle_viewport=fake_viewportst;
#include "old_draw.h"

#include <cstdio>
#include <random>
#include <string>
#include <sstream>

// Per screen tile, the sequence of paint operations that touched it: a repaint contributes
// its non-blank layers in the engine's draw order, a sprite or fill every screen tile its
// pixel rect overlaps. Two protocols painting the same sequence per tile paint the same pixels.
#include <map>
struct recording_canvasst
{
	int32_t ox=0,oy=0,zoom_=128;
	int32_t screen_ox=0,screen_oy=0;
	const std::vector<fake_viewportst *> *viewports=nullptr;
	bool clipped=false;
	pixel_rectst clip{};
	size_t sprite_count=0;
	std::vector<std::string> events;

	int32_t origin_x() const {return ox;}
	int32_t origin_y() const {return oy;}
	int32_t zoom() const {return zoom_;}
	void offset_origin(int32_t dx,int32_t dy){ox+=dx;oy+=dy;}
	const void *texture(int32_t texpos) const
		{
		return texpos%7==0?nullptr:reinterpret_cast<const void *>(intptr_t(texpos));
		}
	int32_t size() const {return tile_pixel_size(zoom_);}
	// Cells are the pixel regions cut by both the screen grid and the glide-shifted grid, so
	// paints of disjoint cells commute and paints inside one cell are ordered.
	int32_t glide_x=0,glide_y=0;
	std::map<std::array<int32_t,4>,std::vector<std::string>> log;
	static void cut(std::vector<int32_t> &lines,int32_t lo,int32_t hi,int32_t origin,int32_t s)
		{
		auto first=[&](int32_t o){int32_t d=lo-o;int32_t k=d>=0?d/s:-((-d+s-1)/s);return o+k*s;};
		for(int32_t v=first(origin);v<hi;v+=s)if(v>lo)lines.push_back(v);
		}
	void touch(int32_t x,int32_t y,int32_t w,int32_t h,const std::string &op)
		{
		int32_t x0=x,y0=y,x1=x+w,y1=y+h;
		if(clipped){x0=std::max(x0,clip.x);y0=std::max(y0,clip.y);x1=std::min(x1,clip.x+clip.w);y1=std::min(y1,clip.y+clip.h);}
		events.push_back(op);
		if(x0>=x1||y0>=y1)return;
		const int32_t s=size();
		std::vector<int32_t> xs{x0},ys{y0};
		cut(xs,x0,x1,screen_ox,s);cut(xs,x0,x1,screen_ox+glide_x,s);xs.push_back(x1);
		cut(ys,y0,y1,screen_oy,s);cut(ys,y0,y1,screen_oy+glide_y,s);ys.push_back(y1);
		std::sort(xs.begin(),xs.end());std::sort(ys.begin(),ys.end());
		for(size_t i=0;i+1<xs.size();++i)for(size_t j=0;j+1<ys.size();++j)
			if(xs[i]<xs[i+1]&&ys[j]<ys[j+1])log[{xs[i],ys[j],xs[i+1],ys[j+1]}].push_back(op);
		}
	void repaint(fake_viewportst *vp,int32_t x,int32_t y)
		{
		size_t which=99;
		for(size_t i=0;i<viewports->size();++i)if((*viewports)[i]==vp)which=i;
		const size_t index=size_t(x)*size_t(vp->dim_y)+size_t(y);
		const int32_t px=tile_pixel(x,ox,zoom_),py=tile_pixel(y,oy,zoom_);
		std::string prefix="R"+std::to_string(which)+"@"+std::to_string(px)+","+std::to_string(py)+" ";
		// Engine draw order as the plugin's hide sets assume it.
		const auto emit=[&](const char *name,auto value){if(value)touch(px,py,size(),size(),prefix+name+"="+std::to_string(value));};
		emit("background",vp->screentexpos_background[index]);
		emit("floor_flag",vp->screentexpos_floor_flag[index]);
		emit("background_two",vp->screentexpos_background_two[index]);
		emit("liquid_flag",vp->screentexpos_liquid_flag[index]);
		emit("spatter_flag",vp->screentexpos_spatter_flag[index]);
		emit("spatter",vp->screentexpos_spatter[index]);
		emit("ramp_flag",vp->screentexpos_ramp_flag[index]);
		emit("shadow_flag",vp->screentexpos_shadow_flag[index]);
		emit("building_one",vp->screentexpos_building_one[index]);
		emit("item",vp->screentexpos_item[index]);
		emit("vehicle",vp->screentexpos_vehicle[index]);
		emit("vermin",vp->screentexpos_vermin[index]);
		emit("left",vp->screentexpos_left_creature[index]);
		emit("center",vp->screentexpos[index]);
		emit("right",vp->screentexpos_right_creature[index]);
		emit("building_two",vp->screentexpos_building_two[index]);
		emit("projectile",vp->screentexpos_projectile[index]);
		emit("high_flow",vp->screentexpos_high_flow[index]);
		emit("top_shadow",vp->screentexpos_top_shadow[index]);
		emit("signpost",vp->screentexpos_signpost[index]);
		emit("upleft",vp->screentexpos_upleft_creature[index]);
		emit("up",vp->screentexpos_up_creature[index]);
		emit("upright",vp->screentexpos_upright_creature[index]);
		emit("designation",vp->screentexpos_designation[index]);
		if(vp->screentexpos_interface)emit("interface",vp->screentexpos_interface[index]);
		}
	void draw_sprite(const void *texture,float x,float y,float size,bool mirrored)
		{
		++sprite_count;
		char buf[128];
		snprintf(buf,sizeof buf,"S %p %.3f %.3f %.3f %d",texture,double(x),double(y),double(size),int(mirrored));
		touch(int32_t(std::floor(x)),int32_t(std::floor(y)),int32_t(std::ceil(x+size))-int32_t(std::floor(x)),int32_t(std::ceil(y+size))-int32_t(std::floor(y)),buf);
		}
	void fill_black(pixel_rectst r)
		{
		touch(r.x,r.y,r.w,r.h,"F "+std::to_string(r.x)+" "+std::to_string(r.y)+" "+std::to_string(r.w)+" "+std::to_string(r.h));
		}
	void set_clip(pixel_rectst r){clipped=true;clip=r;}
	void clear_clip(){clipped=false;}
};

struct creaturest{int32_t x,y,texpos;bool right,left,up,item,designation;};

int main(int argc,char **argv)
{
	const int trials=argc>1?atoi(argv[1]):300;
	const int frames=argc>2?atoi(argv[2]):50;
	std::mt19937 rng(12345);
	auto chance=[&](int n){return int(rng()%uint32_t(n))==0;};
	long compared=0,differing=0,rendered=0,sprites_total=0;
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
		std::vector<std::vector<creaturest>> creatures(vp_count);
		for(auto &list:creatures)
			for(int i=0;i<1+int(rng()%6);++i)
				list.push_back({int32_t(rng()%uint32_t(dim_x)),int32_t(rng()%uint32_t(dim_y)),
					int32_t(10+rng()%90),chance(3),chance(3),chance(3),chance(3),chance(4)});
		visual_animation_managerst manager;
		manager.set_base_duration_ms(60+rng()%200);
		frame_rendererst<fake_viewportst> renderer;
		render_settingst &settings=renderer.get_settings();
		settings.flip=chance(3)?false:true;
		settings.bob.enabled=chance(3)?false:true;
		settings.bob.amplitude=float(rng()%30)/100.f;
		settings.bob.hops=1+int(rng()%2);
		oracle::bob_amplitude=settings.bob.amplitude;
		oracle::bob_hops=settings.bob.hops;
		oracle::tile_coveragest old_previous;
		uint32_t now=1000;
		int32_t pan_x=0,pan_y=0;
		uint32_t revision=1;
		recording_canvasst old_canvas,new_canvas;
		old_canvas.viewports=new_canvas.viewports=&viewports;
		old_canvas.zoom_=new_canvas.zoom_=chance(2)?128:int32_t(64+rng()%128);
		old_canvas.ox=new_canvas.ox=int32_t(rng()%40);old_canvas.screen_ox=new_canvas.screen_ox=old_canvas.ox;
		old_canvas.oy=new_canvas.oy=int32_t(rng()%40);old_canvas.screen_oy=new_canvas.screen_oy=old_canvas.oy;
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
					auto copy_old=[&](int32_t *cur,int32_t *old){for(size_t i=0;i<n;++i)old[i]=cur[i];};
					copy_old(vp.screentexpos,vp.screentexpos_old);
					copy_old(vp.screentexpos_right_creature,vp.screentexpos_right_creature_old);
					copy_old(vp.screentexpos_left_creature,vp.screentexpos_left_creature_old);
					copy_old(vp.screentexpos_up_creature,vp.screentexpos_up_creature_old);
					copy_old(vp.screentexpos_upright_creature,vp.screentexpos_upright_creature_old);
					copy_old(vp.screentexpos_upleft_creature,vp.screentexpos_upleft_creature_old);
					copy_old(vp.screentexpos_item,vp.screentexpos_item_old);
					copy_old(vp.screentexpos_vehicle,vp.screentexpos_vehicle_old);
					copy_old(vp.screentexpos_designation,vp.screentexpos_designation_old);
					for(int32_t *b:{vp.screentexpos,vp.screentexpos_right_creature,vp.screentexpos_left_creature,
						vp.screentexpos_up_creature,vp.screentexpos_upright_creature,vp.screentexpos_upleft_creature,
						vp.screentexpos_item,vp.screentexpos_vehicle,vp.screentexpos_designation})
						std::fill(b,b+n,0);
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
						if(chance(6))vp.screentexpos_item[i]=200+int32_t(rng()%20);
						if(chance(10))vp.screentexpos_vehicle[i]=300+int32_t(rng()%5);
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
				using table=viewport_layer_tablest<fake_viewportst>;
				manager.synchronize_viewport({vp,vp->dim_x,vp->dim_y,revision,
					table::current(vp),table::previous(vp),pan_x,pan_y});
				}
			manager.end_frame();
			// The engine redraws the whole map every frame, so the pass paints only this
			// frame's coverage; the old pass is given no previous frame to match.
			old_previous.clear();
			const int32_t glide_x=chance(8)?int32_t(rng()%21)-10:0;
			const int32_t glide_y=chance(8)?int32_t(rng()%21)-10:0;
			bool mirrored=false;
			for(const auto *vp:viewports)mirrored|=manager.has_mirrored_facing(vp);
			if(!(glide_x||glide_y)&&!manager.requires_full_redraw()&&!(settings.flip&&mirrored))continue;
			++rendered;
			old_canvas.log.clear();new_canvas.log.clear();old_canvas.events.clear();new_canvas.events.clear();old_canvas.glide_x=new_canvas.glide_x=glide_x;old_canvas.glide_y=new_canvas.glide_y=glide_y;
			const auto texture_of=[&](int32_t texpos){return old_canvas.texture(texpos);};
			oracle::old_render(old_canvas,viewports,main,manager,settings.flip,settings.bob.enabled,
				texture_of,glide_x,glide_y,old_previous);
			renderer.render(new_canvas,viewports,main,manager,glide_x,glide_y);
			++compared;
			sprites_total+=long(new_canvas.sprite_count);
			if(old_canvas.log!=new_canvas.log)
				{
				++differing;
				if(differing==1)
					{
					printf("dims %dx%d vps %zu flip %d bob %d glide %d,%d\n",dim_x,dim_y,vp_count,int(settings.flip),int(settings.bob.enabled),glide_x,glide_y);
					printf("trial %d frame %d differs\n",trial,frame);
					{FILE *fo=fopen("old.log","w"),*fn=fopen("new.log","w");for(auto &e:old_canvas.events)fprintf(fo,"%s\n",e.c_str());for(auto &e:new_canvas.events)fprintf(fn,"%s\n",e.c_str());fclose(fo);fclose(fn);}
					for(const auto &kv:old_canvas.log)
						{
						const auto it=new_canvas.log.find(kv.first);
						if(it!=new_canvas.log.end()&&it->second==kv.second)continue;
						printf("  cell %d,%d-%d,%d\n",kv.first[0],kv.first[1],kv.first[2],kv.first[3]);
						const auto &a=kv.second;const std::vector<std::string> none;const auto &b=it!=new_canvas.log.end()?it->second:none;
						for(size_t i=0;i<std::max(a.size(),b.size());++i)
							printf("   old %-60s new %s\n",i<a.size()?a[i].c_str():"-",i<b.size()?b[i].c_str():"-");
						}
					for(const auto &kv:new_canvas.log)if(!old_canvas.log.count(kv.first))printf("  new-only cell %d,%d\n",kv.first[0],kv.first[1]);
					}
				}
			}
		}
	printf("rendered %ld frames, compared %ld, differing %ld, sprites %ld\n",rendered,compared,differing,sprites_total);
	return differing?1:0;
}
