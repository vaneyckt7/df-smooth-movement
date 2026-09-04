// Times frame_rendererst::render with a no-op canvas: the plugin-side cost without the engine.
#include "frame_render.h"
#include "visual_animation.h"
#include "fake_viewport.h"
#include <chrono>
#include <cstdio>
#include <random>
struct noop_canvasst
{
	int32_t ox=5,oy=7,zoom_=128;
	size_t repaints=0,sprites=0,fills=0;
	int32_t origin_x() const {return ox;}
	int32_t origin_y() const {return oy;}
	int32_t zoom() const {return zoom_;}
	void offset_origin(int32_t dx,int32_t dy){ox+=dx;oy+=dy;}
	const void *texture(int32_t t) const {return t?reinterpret_cast<const void*>(intptr_t(t)):nullptr;}
	void repaint(fake_viewportst *vp,int32_t x,int32_t y,repaint_passst){asm volatile(""::"r"(vp),"r"(x),"r"(y):"memory");++repaints;}
	void draw_sprite(const void *,int32_t,int32_t,int32_t,bool){++sprites;}
	void fill_black(pixel_rectst){++fills;}
	void set_clip(pixel_rectst){}
	void clear_clip(){}
};
int main(int argc,char **argv)
{
	const int32_t dim_x=argc>1?atoi(argv[1]):120,dim_y=argc>2?atoi(argv[2]):70;
	const int creatures=argc>3?atoi(argv[3]):40;
	const int levels=argc>4?atoi(argv[4]):3;
	const int iterations=argc>5?atoi(argv[5]):3000;
	std::mt19937 rng(7);
	std::vector<fake_viewportst> store{size_t(levels)};
	std::vector<fake_viewportst*> vps;
	for(auto &vp:store){vp.allocate(dim_x,dim_y);vp.clipx={0,dim_x-1};vp.clipy={0,dim_y-1};vps.push_back(&vp);}
	const size_t n=size_t(dim_x)*size_t(dim_y);
	for(auto &vp:store)for(size_t i=0;i<n;++i){vp.screentexpos_background[i]=1+int32_t(rng()%4);vp.screentexpos_interface[i]=(&vp==&store.back())?0:3;}
	struct cst{int32_t x,y,t;bool r,l,u,item;};
	std::vector<cst> cs;
	for(int i=0;i<creatures;++i)cs.push_back({int32_t(1+rng()%(dim_x-2)),int32_t(1+rng()%(dim_y-2)),int32_t(10+rng()%90),rng()%3==0,rng()%3==0,rng()%3==0,rng()%3==0});
	fake_viewportst &main=store.back();
	auto paint=[&]{
		for(int32_t *b:{main.screentexpos,main.screentexpos_right_creature,main.screentexpos_left_creature,main.screentexpos_up_creature,main.screentexpos_item})std::fill(b,b+n,0);
		auto put=[&](int32_t *b,int32_t x,int32_t y,int32_t t){if(x>=0&&x<dim_x&&y>=0&&y<dim_y)b[size_t(x)*size_t(dim_y)+size_t(y)]=t;};
		for(auto &c:cs){put(main.screentexpos,c.x,c.y,c.t);if(c.r)put(main.screentexpos_right_creature,c.x+1,c.y,c.t+1);if(c.l)put(main.screentexpos_left_creature,c.x-1,c.y,c.t+2);if(c.u)put(main.screentexpos_up_creature,c.x,c.y-1,c.t+3);if(c.item)put(main.screentexpos_item,c.x,c.y,c.t+4);}
	};
	using tbl=viewport_layer_tablest<fake_viewportst>;
	auto shift=[&]{
		auto cur=tbl::current(&main);auto old=tbl::previous(&main);
		for(size_t l=0;l<visual_layer_count;++l)std::copy(cur[l],cur[l]+n,const_cast<int32_t*>(old[l]));
		for(auto &c:cs)if(rng()%2){c.x+=int32_t(rng()%3)-1;c.y+=int32_t(rng()%3)-1;c.x=std::clamp(c.x,1,dim_x-2);c.y=std::clamp(c.y,1,dim_y-2);}
		paint();
	};
	visual_animation_managerst manager;
	frame_rendererst<fake_viewportst> renderer;
	renderer.get_settings().flip=true;renderer.get_settings().bob.enabled=true;
	auto sync=[&](uint32_t now){manager.begin_frame(now);for(auto *vp:vps)manager.synchronize_viewport({vp,dim_x,dim_y,1,tbl::current(static_cast<const fake_viewportst*>(vp)),tbl::previous(vp),0,0});manager.end_frame();};
	paint();sync(1000);shift();sync(1100);shift();sync(1200);sync(1240);
	noop_canvasst canvas;
	using clock=std::chrono::steady_clock;
	renderer.render(canvas,vps,&main,manager,0,0);
	canvas.repaints=canvas.sprites=canvas.fills=0;
	auto t0=clock::now();
	for(int i=0;i<iterations;++i)renderer.render(canvas,vps,&main,manager,0,0);
	auto t1=clock::now();
	const double us=std::chrono::duration<double,std::micro>(t1-t0).count()/iterations;
	printf("%dx%d creatures %d levels %d: render %.1f us/frame, repaints %.1f, sprites %.1f, fills %.1f\n",dim_x,dim_y,creatures,levels,us,double(canvas.repaints)/iterations,double(canvas.sprites)/iterations,double(canvas.fills)/iterations);
	auto t2=clock::now();
	for(int i=0;i<iterations;++i)renderer.render(canvas,vps,&main,manager,3,2);
	auto t3=clock::now();
	printf("  glide frame %.1f us\n",std::chrono::duration<double,std::micro>(t3-t2).count()/iterations);
}
