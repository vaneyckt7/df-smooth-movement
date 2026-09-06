// Offline harness for smooth-movement.cpp: compiles the plugin against stub DFHack/SDL headers,
// drives synthetic viewports through busy, idle and scrolling phases, records every engine
// repaint and SDL draw as a trace (for byte-for-byte comparison between plugin versions) and
// times the render hook per frame.
//
// Build: c++ -std=c++17 -O2 -Istubs -I<plugin dir> -DPLUGIN_SOURCE='"<plugin dir>/smooth-movement.cpp"' bench.cpp
// Run:   ./bench [trace-file] [creatures] [levels] [dim_x] [dim_y]        synthetic scene
//        ./bench replay <recording> [trace-file]                          frames recorded in the game
#include PLUGIN_SOURCE

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <random>
#include <set>
#include <string>

namespace {

FILE *trace=nullptr;
uint64_t repaint_calls=0,copy_calls=0,fill_calls=0,fill_rects=0;
// Replay: a repaint caches the tile's textures like the engine does, so a tile the plugin
// stages to populate the cache (hauled item icons) is found on the next lookup.
bool cache_on_repaint=false;

// Model of the engine's tile repaint: a tile whose buffers are all zero paints nothing, so
// such a repaint is counted (blank) but leaves no mark in the trace.
uint64_t blank_repaint_calls=0;
uint64_t tile_hash(const df::graphic_viewportst *vp,int32_t index,bool &any)
{
	uint64_t h=0xcbf29ce484222325ULL;
	const auto mix=[&](uint64_t v){any=any||v!=0;h=(h^v)*0x100000001b3ULL;};
	mix(vp->screentexpos_background[index]);mix(vp->screentexpos_floor_flag[index]);
	mix(vp->screentexpos_background_two[index]);mix(vp->screentexpos_liquid_flag[index]);
	mix(vp->screentexpos_spatter_flag[index]);mix(vp->screentexpos_spatter[index]);
	mix(vp->screentexpos_ramp_flag[index]);mix(vp->screentexpos_shadow_flag[index]);
	mix(vp->screentexpos_building_one[index]);mix(vp->screentexpos_item[index]);
	mix(vp->screentexpos_vehicle[index]);mix(vp->screentexpos_vermin[index]);
	mix(vp->screentexpos_left_creature[index]);mix(vp->screentexpos[index]);
	mix(vp->screentexpos_right_creature[index]);mix(vp->screentexpos_building_two[index]);
	mix(vp->screentexpos_projectile[index]);mix(vp->screentexpos_high_flow[index]);
	mix(vp->screentexpos_top_shadow[index]);mix(vp->screentexpos_signpost[index]);
	mix(vp->screentexpos_upleft_creature[index]);mix(vp->screentexpos_up_creature[index]);
	mix(vp->screentexpos_upright_creature[index]);mix(vp->screentexpos_designation[index]);
	// The interface (shading) layer is reported on its own: the engine paints it last, so a
	// repaint with it equals a repaint without it followed by an interface-only repaint.
	any=any||vp->screentexpos_interface[index]!=0;
	return h;
}

struct bench_rendererst : df::renderer_2d_base
{
	std::vector<df::graphic_viewportst*> ids;
	int vp_id(const df::graphic_viewportst *vp) const
		{
		for(size_t i=0;i<ids.size();++i)if(ids[i]==vp)return int(i);
		return -1;
		}
	void cache_texture(int32_t texpos)
		{
		if(texpos==0)return;
		for(uint32_t flag:{uint32_t(df::texture_fullid_flag::mask_transparent_background),0u})
			{
			df::texture_fullid id;id.texpos=texpos;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=flag;
			tile_cache.tile_cache.emplace(id,reinterpret_cast<void*>(intptr_t(texpos)));
			}
		}
	void update_viewport_tile(df::graphic_viewportst *vp,int32_t x,int32_t y) override
		{
		++repaint_calls;
		bool any=false;
		const uint64_t hash=tile_hash(vp,x*vp->dim_y+y,any);
		if(cache_on_repaint)cache_texture(vp->screentexpos_background_two[x*vp->dim_y+y]);
		if(!any){++blank_repaint_calls;return;}
		if(trace)fprintf(trace,"R %d %d %d %llx i%d o%d,%d\n",vp_id(vp),x,y,
			(unsigned long long)hash,vp->screentexpos_interface[x*vp->dim_y+y],origin_x,origin_y);
		}
};

} // namespace

int SDL_RenderCopyF(SDL_Renderer*,SDL_Texture *t,const SDL_Rect*,const SDL_FRect *d)
{
	++copy_calls;
	if(trace)fprintf(trace,"C %lld %.3f %.3f %.3f %.3f\n",(long long)(intptr_t)t,d->x,d->y,d->w,d->h);
	return 0;
}
int SDL_RenderCopyExF(SDL_Renderer*,SDL_Texture *t,const SDL_Rect*,const SDL_FRect *d,double,const SDL_FPoint*,SDL_RendererFlip f)
{
	++copy_calls;
	if(trace)fprintf(trace,"X %lld %.3f %.3f %.3f %.3f f%d\n",(long long)(intptr_t)t,d->x,d->y,d->w,d->h,int(f));
	return 0;
}
int SDL_RenderFillRect(SDL_Renderer*,const SDL_Rect *r)
{
	++fill_calls;++fill_rects;
	if(trace)fprintf(trace,"F %d %d %d %d\n",r->x,r->y,r->w,r->h);
	return 0;
}
int SDL_RenderFillRects(SDL_Renderer*,const SDL_Rect *r,int n)
{
	++fill_calls;fill_rects+=n;
	if(trace)for(int i=0;i<n;++i)fprintf(trace,"F %d %d %d %d\n",r[i].x,r[i].y,r[i].w,r[i].h);
	return 0;
}
int SDL_RenderSetClipRect(SDL_Renderer*,const SDL_Rect *r)
{
	if(trace){if(r)fprintf(trace,"K %d %d %d %d\n",r->x,r->y,r->w,r->h);else fprintf(trace,"K none\n");}
	return 0;
}
int SDL_GetRenderDrawColor(SDL_Renderer*,Uint8 *r,Uint8 *g,Uint8 *b,Uint8 *a){*r=1;*g=2;*b=3;*a=255;return 0;}
int SDL_SetRenderDrawColor(SDL_Renderer*,Uint8 r,Uint8 g,Uint8 b,Uint8 a)
{
	if(trace)fprintf(trace,"D %d %d %d %d\n",r,g,b,a);
	return 0;
}
void *DFHack::harness_lookup_sdl(const char *name)
{
	#define entry(n) if(!strcmp(name,#n))return reinterpret_cast<void*>(&n);
	entry(SDL_RenderCopyF)entry(SDL_RenderCopyExF)entry(SDL_RenderFillRect)entry(SDL_RenderFillRects)
	entry(SDL_RenderSetClipRect)entry(SDL_GetRenderDrawColor)entry(SDL_SetRenderDrawColor)
	#undef entry
	return nullptr;
}

namespace {

// One synthetic level: world-anchored content, drawn into a viewport at the current window.
struct levelst
{
	df::graphic_viewportst vp;
	std::vector<int32_t> i32[40];
	std::vector<uint64_t> u64[4];
	std::vector<uint32_t> u32[6];
	// World content (world coords = screen + window). Sparse maps keyed by world index.
	std::set<std::pair<int32_t,int32_t>> open;      // lower levels: tiles visible from above
	std::vector<std::pair<std::pair<int32_t,int32_t>,int32_t>> items,designations;
	struct creaturest{int32_t x,y,t;bool r,l,u,ur,ul,item;int kind;bool hauls=false;};// kind 0 creature, 1 vehicle, 2 lone item
	// Units backing the hauling creatures, positioned from the creatures at every redraw.
	std::vector<df::unit> units;
	df::item boulder{df::item_type::BOULDER};
	df::unit_inventory_item hauled{&boulder,df::inv_item_role_type::Hauled};
	std::vector<creaturest> creatures;
	bool is_main=false;

	void allocate(int32_t dim_x,int32_t dim_y)
		{
		const size_t n=size_t(dim_x)*size_t(dim_y);
		for(auto &v:i32)v.assign(n,0);
		for(auto &v:u64)v.assign(n,0);
		for(auto &v:u32)v.assign(n,0);
		vp.flag.bits.active=1;vp.dim_x=dim_x;vp.dim_y=dim_y;
		vp.clipx={0,dim_x-1};vp.clipy={0,dim_y-1};
		size_t a=0,b=0,c=0;
		auto I=[&]{return i32[a++].data();};auto L=[&]{return u64[b++].data();};auto U=[&]{return u32[c++].data();};
		vp.screentexpos_background=I();vp.screentexpos_floor_flag=L();vp.screentexpos_background_two=I();
		vp.screentexpos_liquid_flag=U();vp.screentexpos_spatter_flag=U();vp.screentexpos_spatter=I();
		vp.screentexpos_ramp_flag=L();vp.screentexpos_shadow_flag=U();vp.screentexpos_building_one=I();
		vp.screentexpos_item=I();vp.screentexpos_vehicle=I();vp.screentexpos_vermin=I();
		vp.screentexpos_left_creature=I();vp.screentexpos=I();vp.screentexpos_right_creature=I();
		vp.screentexpos_building_two=I();vp.screentexpos_projectile=I();vp.screentexpos_high_flow=I();
		vp.screentexpos_top_shadow=I();vp.screentexpos_signpost=I();vp.screentexpos_upleft_creature=I();
		vp.screentexpos_up_creature=I();vp.screentexpos_upright_creature=I();vp.screentexpos_designation=I();
		vp.screentexpos_interface=I();
		vp.screentexpos_background_old=I();vp.screentexpos_floor_flag_old=L();vp.screentexpos_background_two_old=I();
		vp.screentexpos_liquid_flag_old=U();vp.screentexpos_spatter_flag_old=U();vp.screentexpos_spatter_old=I();
		vp.screentexpos_ramp_flag_old=L();vp.screentexpos_shadow_flag_old=U();vp.screentexpos_building_one_old=I();
		vp.screentexpos_item_old=I();vp.screentexpos_vehicle_old=I();vp.screentexpos_vermin_old=I();
		vp.screentexpos_left_creature_old=I();vp.screentexpos_old=I();vp.screentexpos_right_creature_old=I();
		vp.screentexpos_building_two_old=I();vp.screentexpos_projectile_old=I();vp.screentexpos_high_flow_old=I();
		vp.screentexpos_top_shadow_old=I();vp.screentexpos_signpost_old=I();vp.screentexpos_upleft_creature_old=I();
		vp.screentexpos_up_creature_old=I();vp.screentexpos_upright_creature_old=I();vp.screentexpos_designation_old=I();
		vp.screentexpos_interface_old=I();
		if(a!=40||b!=4||c!=6){fprintf(stderr,"buffer count mismatch %zu %zu %zu\n",a,b,c);abort();}
		}

	// The engine's redraw: current becomes old, then current is drawn from the world.
	void sync_units()
		{
		size_t n=0;
		for(const auto &c:creatures)if(c.hauls)++n;
		units.resize(n);
		n=0;
		for(const auto &c:creatures)
			if(c.hauls)
				{
				units[n].pos.x=int16_t(c.x);units[n].pos.y=int16_t(c.y);units[n].pos.z=100;
				units[n].inventory={&hauled};
				++n;
				}
		}

	void redraw(int32_t wx,int32_t wy)
		{
		sync_units();
		const int32_t dx=vp.dim_x,dy=vp.dim_y;const size_t n=size_t(dx)*size_t(dy);
		for(int k=0;k<20;++k)i32[20+k]=i32[k];
		for(int k=0;k<2;++k)u64[2+k]=u64[k];
		for(int k=0;k<3;++k)u32[3+k]=u32[k];
		for(int k=0;k<20;++k)std::fill(i32[k].begin(),i32[k].end(),0);
		for(int k=0;k<2;++k)std::fill(u64[k].begin(),u64[k].end(),0);
		for(int k=0;k<3;++k)std::fill(u32[k].begin(),u32[k].end(),0);
		auto at=[&](int32_t x,int32_t y)->int32_t{x-=wx;y-=wy;return (x>=0&&x<dx&&y>=0&&y<dy)?x*dy+y:-1;};
		if(is_main)
			{
			for(int32_t x=0;x<dx;++x)for(int32_t y=0;y<dy;++y)
				{
				const int32_t i=x*dy+y;const uint32_t w=uint32_t((x+wx)*73856093)^uint32_t((y+wy)*19349663);
				vp.screentexpos_background[i]=1000+int32_t(w%7);
				if(w%5==0)vp.screentexpos_floor_flag[i]=1;
				if(w%23==0)vp.screentexpos_building_one[i]=2000+int32_t(w%3);
				// A third of the main level carries shading, so folded shading gets exercised there too.
				if(w%3==0)vp.screentexpos_interface[i]=3000;
				if(w%11==0)vp.screentexpos_top_shadow[i]=7000;
				}
			}
		else
			{
			for(const auto &t:open){const int32_t i=at(t.first,t.second);if(i<0)continue;
				vp.screentexpos_background[i]=1000+int32_t(uint32_t(t.first*7+t.second*13)%7);vp.screentexpos_interface[i]=3000;
				if((t.first+t.second)%4==0)vp.screentexpos_top_shadow[i]=7000;}
			}
		for(const auto &it:items){const int32_t i=at(it.first.first,it.first.second);if(i>=0)vp.screentexpos_item[i]=it.second;}
		for(const auto &it:designations){const int32_t i=at(it.first.first,it.first.second);if(i>=0)vp.screentexpos_designation[i]=it.second;}
		for(const auto &c:creatures)
			{
			int32_t i=at(c.x,c.y);
			if(c.kind==1){if(i>=0)vp.screentexpos_vehicle[i]=c.t;continue;}
			if(c.kind==2){if(i>=0)vp.screentexpos_item[i]=c.t;continue;}
			if(i>=0){vp.screentexpos[i]=c.t;if(c.item)vp.screentexpos_item[i]=c.t+4;}
			if(c.r){i=at(c.x+1,c.y);if(i>=0)vp.screentexpos_right_creature[i]=c.t+1;}
			if(c.l){i=at(c.x-1,c.y);if(i>=0)vp.screentexpos_left_creature[i]=c.t+2;}
			if(c.u){i=at(c.x,c.y-1);if(i>=0)vp.screentexpos_up_creature[i]=c.t+3;}
			if(c.ur){i=at(c.x+1,c.y-1);if(i>=0)vp.screentexpos_upright_creature[i]=c.t+5;}
			if(c.ul){i=at(c.x-1,c.y-1);if(i>=0)vp.screentexpos_upleft_creature[i]=c.t+6;}
			}
		(void)n;
		}
};

// One viewport slot of a recording: the buffers are decoded in place, frame after frame.
struct replay_slotst
{
	df::graphic_viewportst vp;
	size_t bytes=0;
	std::vector<uint8_t> storage[frame_record::buffer_count];

	void resize(size_t tiles)
		{
		int index=0;
		frame_record::for_each_buffer(vp,[&](auto &pointer)
			{
			using T=std::remove_pointer_t<std::remove_reference_t<decltype(pointer)>>;
			std::vector<uint8_t> &store=storage[index++];
			if(store.size()!=tiles*sizeof(T))store.assign(tiles*sizeof(T),0);
			});
		bytes=tiles;
		}
};

bool read_whole_file(const char *path,std::vector<uint8_t> &out)
{
	FILE *f=fopen(path,"rb");
	if(!f)return false;
	uint8_t chunk[1<<16];
	size_t n;
	while((n=fread(chunk,1,sizeof chunk,f))>0)out.insert(out.end(),chunk,chunk+n);
	fclose(f);
	return true;
}

// Decodes one frame into the slots and globals. Returns false at end of file or on error.
struct replay_framest
{
	frame_record::frame_headerst header;
	std::vector<frame_record::unit_recordst> units;
	frame_record::frame_resultst result;
};

bool decode_frame(frame_record::readerst &reader,replay_slotst *slots,df::graphic &graphics,
	replay_framest &frame,std::set<int32_t> *texposes)
{
	if(reader.at_end())return false;
	if(!frame_record::read_frame_header(reader,frame.header))return false;
	const int count=reader.u8();
	bool present[frame_record::slot_count]={};
	for(int v=0;v<count&&reader.ok();++v)
		{
		frame_record::viewport_headerst vh;
		if(!frame_record::read_viewport_header(reader,vh))return false;
		replay_slotst &slot=slots[vh.slot];
		present[vh.slot]=true;
		const size_t tiles=size_t(vh.dim_x)*size_t(vh.dim_y);
		slot.resize(tiles);
		slot.vp.flag.bits.active=1;
		slot.vp.dim_x=vh.dim_x;slot.vp.dim_y=vh.dim_y;
		slot.vp.clipx={vh.clipx0,vh.clipx1};slot.vp.clipy={vh.clipy0,vh.clipy1};
		slot.vp.screen_x=vh.screen_x;slot.vp.screen_y=vh.screen_y;
		int index=0;
		frame_record::for_each_buffer(slot.vp,[&](auto &pointer)
			{
			using T=std::remove_pointer_t<std::remove_reference_t<decltype(pointer)>>;
			std::vector<uint8_t> &store=slot.storage[index++];
			if(!reader.ok())return;
			if(reader.u8()==0){pointer=nullptr;return;}
			pointer=reinterpret_cast<T*>(store.data());
			reader.words(pointer,tiles);
			if(texposes&&std::is_same_v<T,int32_t>)
				for(size_t i=0;i<tiles;++i)if(pointer[i]!=0)texposes->insert(int32_t(pointer[i]));
			});
		}
	for(int s=0;s<frame_record::main_slot;++s)graphics.lower_viewport[size_t(s)]=present[s]?&slots[s].vp:nullptr;
	graphics.main_viewport=present[frame_record::main_slot]?&slots[frame_record::main_slot].vp:nullptr;
	if(!frame_record::read_units(reader,frame.units))return false;
	if(!frame_record::read_frame_result(reader,frame.result))return false;
	return reader.ok();
}

int run_replay(const char *record_path)
{
	std::vector<uint8_t> file;
	if(!read_whole_file(record_path,file)){fprintf(stderr,"cannot read %s\n",record_path);return 1;}
	frame_record::readerst reader;reader.data=file.data();reader.size=file.size();
	if(!frame_record::read_file_header(reader)){fprintf(stderr,"%s: %s\n",record_path,reader.error.c_str());return 1;}
	const size_t frames_start=reader.pos;

	bench_rendererst renderer;
	renderer.sdl_renderer=reinterpret_cast<void*>(0x1);
	df::graphic graphics;df::enabler enable;df::plotinfost plot;
	int32_t wx=0,wy=0,wz=0;bool paused=false;
	df::global::gps=&graphics;df::global::enabler=&enable;df::global::plotinfo=&plot;
	df::global::pause_state=&paused;df::global::window_x=&wx;df::global::window_y=&wy;df::global::window_z=&wz;
	replay_slotst slots[frame_record::slot_count];
	for(auto &slot:slots)renderer.ids.push_back(&slot.vp);

	// Pass one: every texpos the engine had on screen is in its texture cache before the hook runs.
	std::set<int32_t> texposes;
	replay_framest frame;
	while(decode_frame(reader,slots,graphics,frame,&texposes)){}
	if(!reader.ok()){fprintf(stderr,"%s: %s at byte %zu\n",record_path,reader.error.c_str(),reader.pos);return 1;}
	for(int32_t t:texposes)renderer.cache_texture(t);
	for(auto &slot:slots){for(auto &store:slot.storage)std::fill(store.begin(),store.end(),0);}
	reader.pos=frames_start;
	cache_on_repaint=true;

	DFHack::color_ostream out;
	const auto rc=plugin_enable(out,true);
	if(rc!=DFHack::CR_OK){fprintf(stderr,"plugin_enable failed: %s",out.captured.c_str());return 1;}

	std::vector<df::unit> units;
	std::vector<df::item> items;
	std::vector<df::unit_inventory_item> inventory;
	std::map<int32_t,df::material> materials;
	using clock=std::chrono::steady_clock;
	double us=0,us_max=0;
	uint64_t recorded_painted=0,recorded_repaints=0,replayed_painted=0,mismatched=0,shown=0,unstable=0,unstable_mismatched=0;
	size_t n=0;
	while(decode_frame(reader,slots,graphics,frame,nullptr))
		{
		const auto &h=frame.header;
		flip_enabled=h.flip;
		hauled_enabled=h.hauled;
		set_camera_enabled(h.camera);
		animation_manager.set_linear(h.linear);
		wx=h.window_x;wy=h.window_y;wz=h.window_z;paused=h.paused;
		plot.follow_unit=h.follow_unit;
		graphics.precise_mouse_x=h.mouse_x;graphics.precise_mouse_y=h.mouse_y;
		graphics.dimx=h.dimx;graphics.dimy=h.dimy;
		enable.mouse_mbut=h.mouse_mbut;
		renderer.viewport_zoom_factor=h.zoom;renderer.origin_x=h.origin_x;renderer.origin_y=h.origin_y;
		DFHack::Core::getInstance().p->tick_ms=h.tick_ms;
		units.clear();items.clear();inventory.clear();
		units.reserve(frame.units.size());items.reserve(frame.units.size());inventory.reserve(frame.units.size());
		DFHack::Units::harness_units.clear();
		for(const auto &u:frame.units)
			{
			units.push_back({});
			df::unit &unit=units.back();
			unit.pos.x=u.x;unit.pos.y=u.y;unit.pos.z=u.z;
			if(u.cached)renderer.cache_texture(u.texpos);
			if(u.texpos!=0)
				{
				df::material &m=materials[u.texpos];
				m.boulder_texpos1=u.texpos;
				items.push_back({df::item_type::BOULDER,&m});
				inventory.push_back({&items.back(),df::inv_item_role_type::Hauled});
				unit.inventory={&inventory.back()};
				}
			DFHack::Units::harness_units.push_back(&unit);
			}

		const uint64_t r0=repaint_calls,p0=frame_stats.painted;
		if(trace)fprintf(trace,"# frame replay %zu t=%u w=%d,%d\n",n,h.tick_ms,wx,wy);
		const auto t0=clock::now();
		render_interpolated_world(&renderer);
		const auto t1=clock::now();
		const double frame_us=std::chrono::duration<double,std::micro>(t1-t0).count();
		us+=frame_us;us_max=std::max(us_max,frame_us);
		const uint64_t repaints=repaint_calls-r0;
		const bool painted=frame_stats.painted!=p0;
		recorded_painted+=frame.result.painted;recorded_repaints+=frame.result.repaints;
		replayed_painted+=painted;
		unstable+=frame.result.changed_words!=0;
		if(repaints!=frame.result.repaints||painted!=frame.result.painted)
			{
			++mismatched;
			unstable_mismatched+=frame.result.changed_words!=0;
			if(shown<10){++shown;printf("frame %zu: game %s %u repaints, replay %s %llu repaints, %u buffer words changed under the hook\n",n,
				frame.result.painted?"painted":"skipped",frame.result.repaints,painted?"painted":"skipped",(unsigned long long)repaints,frame.result.changed_words);}
			}
		++n;
		}
	if(!reader.ok()){fprintf(stderr,"%s: %s at byte %zu\n",record_path,reader.error.c_str(),reader.pos);return 1;}
	printf("replay %-12s %6.1f us/frame (max %.0f)  repaints %7.1f (blank %7.1f)  copies %6.1f  fill calls %5.1f  rects %6.1f  (frames %zu)\n",
		"all",us/double(n),us_max,double(repaint_calls)/double(n),double(blank_repaint_calls)/double(n),
		double(copy_calls)/double(n),double(fill_calls)/double(n),double(fill_rects)/double(n),n);
	printf("game:   painted %llu of %zu frames, repaints %llu\n",(unsigned long long)recorded_painted,n,(unsigned long long)recorded_repaints);
	printf("replay: painted %llu of %zu frames, repaints %llu\n",(unsigned long long)replayed_painted,n,(unsigned long long)repaint_calls);
	printf("frames whose painted flag or repaint count differ from the game: %llu, of which %llu had buffers change under the hook\n",(unsigned long long)mismatched,(unsigned long long)unstable_mismatched);
	printf("frames whose buffers changed under the hook in the game: %llu\n",(unsigned long long)unstable);
	if(trace)fclose(trace);
	plugin_enable(out,false);
	return mismatched==0?0:2;
}

} // namespace

int main(int argc,char **argv)
{
	if(argc>=3&&!strcmp(argv[1],"replay"))
		{
		if(argc>3&&strcmp(argv[3],"-"))trace=fopen(argv[3],"w");
		return run_replay(argv[2]);
		}
	const char *trace_path=argc>1&&strcmp(argv[1],"-")?argv[1]:nullptr;
	const int creatures=argc>2?atoi(argv[2]):40;
	const int levels=argc>3?atoi(argv[3]):9;
	const int32_t dim_x=argc>4?atoi(argv[4]):100;
	const int32_t dim_y=argc>5?atoi(argv[5]):60;
	if(trace_path)trace=fopen(trace_path,"w");
	std::mt19937 rng(12345);

	std::vector<levelst> store{}; store.resize(size_t(levels));
	bench_rendererst renderer;
	renderer.sdl_renderer=reinterpret_cast<void*>(0x1);
	renderer.origin_x=5;renderer.origin_y=7;
	df::graphic graphics;df::enabler enable;df::plotinfost plot;
	int32_t wx=50,wy=50,wz=100;bool paused=false;
	df::global::gps=&graphics;df::global::enabler=&enable;df::global::plotinfo=&plot;
	df::global::pause_state=&paused;df::global::window_x=&wx;df::global::window_y=&wy;df::global::window_z=&wz;
	graphics.dimx=dim_x+20;graphics.dimy=dim_y+5;

	for(int l=0;l<levels;++l)
		{
		levelst &lv=store[size_t(l)];
		lv.allocate(dim_x,dim_y);
		lv.is_main=l==levels-1;
		// Trace viewport ids are slots: lower_viewport[i] is i, the main viewport is 8.
		renderer.ids.resize(frame_record::slot_count,nullptr);
		if(lv.is_main){graphics.main_viewport=&lv.vp;renderer.ids[frame_record::main_slot]=&lv.vp;}
		else {graphics.lower_viewport[size_t(levels-2-l)]=&lv.vp;renderer.ids[size_t(levels-2-l)]=&lv.vp;}
		const int n_open=lv.is_main?0:int(dim_x*dim_y/10);
		for(int i=0;i<n_open;++i)lv.open.insert({int32_t(wx+rng()%dim_x),int32_t(wy+rng()%dim_y)});
		const int n_items=lv.is_main?dim_x*dim_y/20:dim_x*dim_y/200;
		for(int i=0;i<n_items;++i)lv.items.push_back({{int32_t(wx+rng()%dim_x),int32_t(wy+rng()%dim_y)},int32_t(4000+rng()%50)});
		const int n_des=lv.is_main?dim_x*dim_y/50:0;
		for(int i=0;i<n_des;++i)lv.designations.push_back({{int32_t(wx+rng()%dim_x),int32_t(wy+rng()%dim_y)},int32_t(5000+rng()%3)});
		const int n_c=lv.is_main?creatures:std::max(1,creatures/10);
		std::set<std::pair<int32_t,int32_t>> taken;
		for(int i=0;i<n_c;++i)
			{
			int32_t x,y;
			// A third of them crowd a 12x8 patch so neighbours share and dispute fragments.
			const bool crowded=i<n_c/3;
			do{
				x=crowded?int32_t(wx+20+rng()%12):int32_t(wx+1+rng()%(dim_x-2));
				y=crowded?int32_t(wy+20+rng()%8):int32_t(wy+1+rng()%(dim_y-2));
			}while(taken.count({x,y}));
			taken.insert({x,y});
			const int kind=i%10==9?1:i%10==8?2:0;
			lv.creatures.push_back({x,y,int32_t(100+rng()%90),rng()%3==0,rng()%3==0,rng()%3==0,rng()%6==0,rng()%6==0,rng()%4==0,kind,lv.is_main&&kind==0&&i%5==0});
			}
		}
	// Texture cache: every texpos the world can produce, transparent-background variant.
	for(int32_t t=100;t<200+10;++t){df::texture_fullid id;id.texpos=t;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=df::texture_fullid_flag::mask_transparent_background;renderer.tile_cache.tile_cache[id]=reinterpret_cast<void*>(intptr_t(t));}
	for(int32_t t=4000;t<4060;++t){df::texture_fullid id;id.texpos=t;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=df::texture_fullid_flag::mask_transparent_background;renderer.tile_cache.tile_cache[id]=reinterpret_cast<void*>(intptr_t(t));}
	{df::texture_fullid id;id.texpos=6000;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=df::texture_fullid_flag::mask_transparent_background;renderer.tile_cache.tile_cache[id]=reinterpret_cast<void*>(intptr_t(6000));}
	for(int32_t t=5000;t<5003;++t){df::texture_fullid id;id.texpos=t;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=df::texture_fullid_flag::mask_transparent_background;renderer.tile_cache.tile_cache[id]=reinterpret_cast<void*>(intptr_t(t));}

	DFHack::color_ostream out;
	renderer.viewport_zoom_factor=128;
	const auto rc=plugin_enable(out,true);
	if(rc!=DFHack::CR_OK){fprintf(stderr,"plugin_enable failed: %s",out.captured.c_str());return 1;}
	flip_enabled=true;
	hauled_enabled=true;
	// The hauling units of the main level are what Units::getUnitsInBox hands out.
	{
	levelst &main_level=store.back();
	main_level.sync_units();
	for(auto &u:main_level.units)DFHack::Units::harness_units.push_back(&u);
	}

	auto redraw_all=[&]{for(auto &lv:store)lv.redraw(wx,wy);};
	auto step=[&]
		{
		for(auto &lv:store)
			{
			std::set<std::pair<int32_t,int32_t>> taken;
			for(const auto &c:lv.creatures)taken.insert({c.x,c.y});
			for(auto &c:lv.creatures)
				{
				if(rng()%2)continue;
				const int32_t nx=std::clamp(c.x+int32_t(rng()%3)-1,wx+1,wx+dim_x-2);
				const int32_t ny=std::clamp(c.y+int32_t(rng()%3)-1,wy+1,wy+dim_y-2);
				if((nx!=c.x||ny!=c.y)&&!taken.count({nx,ny})){taken.erase({c.x,c.y});c.x=nx;c.y=ny;taken.insert({nx,ny});}
				}
			// Designations shift too: a dig designation advancing along a tunnel looks like one.
			for(auto &d:lv.designations)
				if(rng()%4==0)d.first.first=std::clamp(d.first.first+int32_t(rng()%3)-1,wx+1,wx+dim_x-2);
			}
		redraw_all();
		};

	using clock=std::chrono::steady_clock;
	uint32_t now_ms=1000;
	struct phasest{const char *name;double us=0;uint64_t frames=0,repaints=0,blank=0,copies=0,fills=0,rects=0;};
	std::vector<phasest> phases;
	auto run_frames=[&](phasest &ph,int frames,auto per_frame)
		{
		for(int f=0;f<frames;++f)
			{
			per_frame(f);
			DFHack::Core::getInstance().p->tick_ms=now_ms;
			const uint64_t r0=repaint_calls,b0=blank_repaint_calls,c0=copy_calls,f0=fill_calls,q0=fill_rects;
			if(trace)fprintf(trace,"# frame %s %d t=%u w=%d,%d\n",ph.name,f,now_ms,wx,wy);
			const auto t0=clock::now();
			render_interpolated_world(&renderer);
			const auto t1=clock::now();
			ph.us+=std::chrono::duration<double,std::micro>(t1-t0).count();
			++ph.frames;ph.repaints+=repaint_calls-r0;ph.blank+=blank_repaint_calls-b0;ph.copies+=copy_calls-c0;ph.fills+=fill_calls-f0;ph.rects+=fill_rects-q0;
			now_ms+=16;
			}
		};

#ifdef HARNESS_STATS
	{std::vector<std::string> p{"stats","on"};status_command(out,p);}
	// HARNESS_RECORD=<file>: record the whole synthetic run through the plugin's own recorder.
	if(getenv("HARNESS_RECORD"))
		{std::vector<std::string> p{"stats","record",getenv("HARNESS_RECORD"),"1872"};
		if(status_command(out,p)!=DFHack::CR_OK){fprintf(stderr,"%s",out.captured.c_str());return 1;}}
#endif
	redraw_all();
	phasest warm{"warmup"};run_frames(warm,12,[&](int f){if(f%6==0)step();});
	phasest busy{"busy"};run_frames(busy,600,[&](int f){if(f%6==0)step();});
	const int flip_scale=getenv("HARNESS_FLIP_SCALE")?atoi(getenv("HARNESS_FLIP_SCALE")):1;
	phasest idle_flip{"idle-flip"};run_frames(idle_flip,300*flip_scale,[&](int){});
	// A scroll: window moves at input time, the buffers follow two frames later.
	phasest scroll{"scroll"};
	int pending_dx=0,pending_dy=0,land_at=-1;
	run_frames(scroll,240,[&](int f)
		{
		if(f%6==0)step();
		if(f%40==5){pending_dx=int(rng()%3)-1;pending_dy=int(rng()%3)-1;if(!pending_dx&&!pending_dy)pending_dx=1;wx+=pending_dx;wy+=pending_dy;land_at=f+2;}
		if(f==land_at){redraw_all();land_at=-1;}
		});
	phasest pause_phase{"paused"};paused=true;run_frames(pause_phase,120,[&](int){});paused=false;
	flip_enabled=false;
	const int idle_scale=getenv("HARNESS_IDLE_SCALE")?atoi(getenv("HARNESS_IDLE_SCALE")):1;
	phasest idle_noflip{"idle-noflip"};run_frames(idle_noflip,300*idle_scale,[&](int){});
	flip_enabled=true;
	phasest busy2{"busy-again"};run_frames(busy2,300,[&](int f){if(f%6==0)step();});
	for(auto *ph:{&busy,&idle_flip,&scroll,&pause_phase,&idle_noflip,&busy2})
		printf("%-12s %6.1f us/frame  repaints %7.1f (blank %7.1f)  copies %6.1f  fill calls %5.1f  rects %6.1f  (frames %llu)\n",
			ph->name,ph->us/double(ph->frames),double(ph->repaints)/double(ph->frames),double(ph->blank)/double(ph->frames),double(ph->copies)/double(ph->frames),
			double(ph->fills)/double(ph->frames),double(ph->rects)/double(ph->frames),(unsigned long long)ph->frames);
#ifdef HARNESS_STATS
	{out.captured.clear();std::vector<std::string> p{"stats"};status_command(out,p);fputs(out.captured.c_str(),stdout);
	p={"stats","reset"};out.captured.clear();status_command(out,p);p={"stats"};status_command(out,p);fputs(out.captured.c_str(),stdout);
	p={"stats","bogus"};if(status_command(out,p)!=DFHack::CR_WRONG_USAGE)puts("BAD: bogus accepted");}
#endif
	if(trace)fclose(trace);
	plugin_enable(out,false);
	return 0;
}
