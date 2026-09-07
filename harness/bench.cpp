// Offline harness for smooth-movement.cpp: compiles the plugin against stub DFHack/SDL headers,
// replays frames recorded in the game with `smooth-movement record`, and writes every tile
// repaint and SDL draw the plugin asks for as a trace, for byte-for-byte comparison between
// plugin versions.
//
// Build: c++ -std=c++17 -O2 -Istubs -I<plugin dir> -DPLUGIN_SOURCE='"<plugin dir>/smooth-movement.cpp"' bench.cpp
// Run:   ./bench replay <recording> [trace-out]
//        Reads <recording>. Writes <trace-out>, one line per renderer call. Leave <trace-out>
//        out, or pass `-`, to replay without a trace.
// Exit:  0 every frame matched the game, 1 some frames differed, 2 the recording could not be
//        read or has no frames, the trace could not be written or the plugin could not be
//        enabled, 3 usage error.
//
// Trace lines, one per renderer call the plugin makes, in order:
//   # frame replay N t=<clock ms> w=<window x>,<window y>   start of frame N
//   R vp x y hash i<interface entry> o<origin x>,<origin y>  tile repaint; the hash covers the tile's
//                                                            current entries in the 24 arrays other
//                                                            than the interface array
//   C texture x y w h t<transparent>   copy a texture to a screen rectangle (pixels); t1 when the
//                                      plugin looked the texture up with a transparent background
//   X texture x y w h f<flip> t<transparent>   the same, mirrored
//   K x y w h | K none           set or clear the clip rectangle
//   D r g b a                    set the draw colour
//   F x y w h                    fill a rectangle
#include PLUGIN_SOURCE

#include <chrono>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace {

FILE *trace=nullptr;
uint64_t repaint_calls=0,copy_calls=0,fill_calls=0,fill_rects=0;
// Replay: a repaint caches the tile's background_two entry like the game's renderer does, because
// the plugin stages a hauled item's texture there so the game's texture cache has it on the next
// lookup (cached_viewport_texture in the plugin).
bool cache_on_repaint=false;

// Model of the game's tile repaint: a tile whose per-tile entries are all zero paints nothing, so
// such a repaint is counted (blank) but leaves no mark in the trace.
uint64_t blank_repaint_calls=0;
int32_t interface_entry(const df::graphic_viewportst *vp,int32_t index)
{return vp->screentexpos_interface?vp->screentexpos_interface[index]:0;}
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
	// The interface (shading) layer is reported on its own: the game paints it last, so a
	// repaint with it equals a repaint without it followed by an interface-only repaint.
	// The plugin treats that array as optional, so it may be absent from a recording.
	any=any||interface_entry(vp,index)!=0;
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
	// The stub cache hands out a fake pointer that encodes the texpos and whether the lookup asked
	// for a transparent background, so a trace line tells the two variants apart.
	static void *fake_texture(int32_t texpos,bool transparent){return reinterpret_cast<void*>(intptr_t(texpos)*2+(transparent?1:0));}
	static long long fake_texpos(const void *t){return (long long)((intptr_t)t>>1);}
	static int fake_transparent(const void *t){return int((intptr_t)t&1);}
	void cache_texture(int32_t texpos)
		{
		if(texpos==0)return;
		for(uint32_t flag:{uint32_t(df::texture_fullid_flag::mask_transparent_background),0u})
			{
			df::texture_fullid id;id.texpos=texpos;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=flag;
			tile_cache.tile_cache.emplace(id,fake_texture(texpos,flag!=0));
			}
		}
	// The recording says the game had no texture for this texpos: make that true here too.
	void uncache_texture(int32_t texpos)
		{
		for(uint32_t flag:{uint32_t(df::texture_fullid_flag::mask_transparent_background),0u})
			{
			df::texture_fullid id;id.texpos=texpos;id.r=id.g=id.b=1.0f;id.br=id.bg=id.bb=0.0f;id.flag=flag;
			tile_cache.tile_cache.erase(id);
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
			(unsigned long long)hash,interface_entry(vp,x*vp->dim_y+y),origin_x,origin_y);
		}
};

} // namespace

int SDL_RenderCopyF(SDL_Renderer*,SDL_Texture *t,const SDL_Rect*,const SDL_FRect *d)
{
	++copy_calls;
	if(trace)fprintf(trace,"C %lld %.3f %.3f %.3f %.3f t%d\n",bench_rendererst::fake_texpos(t),d->x,d->y,d->w,d->h,bench_rendererst::fake_transparent(t));
	return 0;
}
int SDL_RenderCopyExF(SDL_Renderer*,SDL_Texture *t,const SDL_Rect*,const SDL_FRect *d,double,const SDL_FPoint*,SDL_RendererFlip f)
{
	++copy_calls;
	if(trace)fprintf(trace,"X %lld %.3f %.3f %.3f %.3f f%d t%d\n",bench_rendererst::fake_texpos(t),d->x,d->y,d->w,d->h,int(f),bench_rendererst::fake_transparent(t));
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

// One viewport slot of a recording: the per-tile arrays are decoded in place, frame after frame.
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

// Opens the trace for writing. Refuses a trace path that is the recording file, so a slip on the
// command line cannot destroy a recording that took game time to make.
bool open_trace(const char *record_path,const char *trace_path)
{
	struct stat rs,ts;
	if(stat(record_path,&rs)==0&&stat(trace_path,&ts)==0&&rs.st_dev==ts.st_dev&&rs.st_ino==ts.st_ino)
		{fprintf(stderr,"trace %s is the recording\n",trace_path);return false;}
	trace=fopen(trace_path,"w");
	if(!trace){fprintf(stderr,"cannot write %s\n",trace_path);return false;}
	return true;
}

int run_replay(const char *record_path,const char *trace_path)
{
	std::vector<uint8_t> file;
	if(!read_whole_file(record_path,file)){fprintf(stderr,"cannot read %s\n",record_path);return 2;}
	if(trace_path&&!open_trace(record_path,trace_path))return 2;
	frame_record::readerst reader;reader.data=file.data();reader.size=file.size();
	if(!frame_record::read_file_header(reader)){fprintf(stderr,"%s: %s\n",record_path,reader.error.c_str());return 2;}
	const size_t frames_start=reader.pos;

	bench_rendererst renderer;
	renderer.sdl_renderer=reinterpret_cast<void*>(0x1);
	df::graphic graphics;df::enabler enable;df::plotinfost plot;
	int32_t wx=0,wy=0,wz=0;bool paused=false;
	df::global::gps=&graphics;df::global::enabler=&enable;df::global::plotinfo=&plot;
	df::global::pause_state=&paused;df::global::window_x=&wx;df::global::window_y=&wy;df::global::window_z=&wz;
	replay_slotst slots[frame_record::slot_count];
	for(auto &slot:slots)renderer.ids.push_back(&slot.vp);

	// Pass one: every texture id the game had on screen is in its texture cache before the hook runs.
	std::set<int32_t> texposes;
	replay_framest frame;
	while(decode_frame(reader,slots,graphics,frame,&texposes)){}
	if(!reader.ok()){fprintf(stderr,"%s: %s at byte %zu\n",record_path,reader.error.c_str(),reader.pos);return 2;}
	for(int32_t t:texposes)renderer.cache_texture(t);
	for(auto &slot:slots){for(auto &store:slot.storage)std::fill(store.begin(),store.end(),0);}
	reader.pos=frames_start;
	cache_on_repaint=true;

	DFHack::color_ostream out;
	const auto rc=plugin_enable(out,true);
	if(rc!=DFHack::CR_OK){fprintf(stderr,"plugin_enable failed: %s",out.captured.c_str());return 2;}
	// Disable the plugin on every way out, including the error returns below.
	struct disablest{DFHack::color_ostream &out;~disablest(){plugin_enable(out,false);}} disable{out};
	{std::vector<std::string> p{"stats","on"};status_command(out,p);}

	std::vector<df::unit> units;
	std::vector<df::item> items;
	std::vector<df::unit_inventory_item> inventory;
	std::map<int32_t,df::material> materials;
	using clock=std::chrono::steady_clock;
	double us=0,us_max=0;
	uint64_t recorded_painted=0,recorded_repaints=0,replayed_painted=0,mismatched=0,shown=0,unstable=0,unstable_mismatched=0;
	size_t n=0;
	// Pass two: replay the frames in order. Each one sets the globals the hook reads to the
	// recorded values, rebuilds the units in view, runs the hook, and compares what it did
	// with what the game's plugin did on that frame.
	while(decode_frame(reader,slots,graphics,frame,nullptr))
		{
		const auto &h=frame.header;
		flip_enabled=h.flip;
		hauled_enabled=h.hauled;
		set_camera_enabled(h.camera);
		rest_x=h.rest_x;rest_y=h.rest_y;
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
			if(u.texpos!=0){if(u.cached)renderer.cache_texture(u.texpos);else renderer.uncache_texture(u.texpos);}
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
			if(shown<10){++shown;printf("frame %zu: game %s %u repaints, replay %s %llu repaints, %u array entries changed under the hook\n",n,
				frame.result.painted?"painted":"skipped",frame.result.repaints,painted?"painted":"skipped",(unsigned long long)repaints,frame.result.changed_words);}
			}
		++n;
		}
	if(!reader.ok()){fprintf(stderr,"%s: %s at byte %zu\n",record_path,reader.error.c_str(),reader.pos);return 2;}
	if(n==0){fprintf(stderr,"%s: no frames\n",record_path);return 2;}
	printf("replay %-12s %6.1f us/frame (max %.0f)  repaints %7.1f (blank %7.1f)  copies %6.1f  fill calls %5.1f  rects %6.1f  (frames %zu)\n",
		"all",us/double(n),us_max,double(repaint_calls)/double(n),double(blank_repaint_calls)/double(n),
		double(copy_calls)/double(n),double(fill_calls)/double(n),double(fill_rects)/double(n),n);
	// The plugin's own frame timers, the split the in-game `stats` command prints.
	out.captured.clear();
	{std::vector<std::string> p{"stats"};status_command(out,p);}
	for(size_t at=0;at<out.captured.size();)
		{
		const size_t end=out.captured.find('\n',at);
		const std::string line=out.captured.substr(at,end==std::string::npos?std::string::npos:end-at);
		if(line.rfind("sync:",0)==0||line.rfind("render:",0)==0)printf("plugin %s\n",line.c_str());
		if(end==std::string::npos)break;
		at=end+1;
		}
	printf("game:   painted %llu of %zu frames, repaints %llu\n",(unsigned long long)recorded_painted,n,(unsigned long long)recorded_repaints);
	printf("replay: painted %llu of %zu frames, repaints %llu\n",(unsigned long long)replayed_painted,n,(unsigned long long)repaint_calls);
	printf("frames whose painted flag or repaint count differ from the game: %llu, of which %llu had array entries change under the hook\n",(unsigned long long)mismatched,(unsigned long long)unstable_mismatched);
	printf("frames whose array entries changed under the hook in the game: %llu\n",(unsigned long long)unstable);
	return mismatched==0?0:1;
}

} // namespace

int main(int argc,char **argv)
{
	if(argc>=3&&!strcmp(argv[1],"replay"))
		{
		const int rc=run_replay(argv[2],argc>3&&strcmp(argv[3],"-")?argv[3]:nullptr);
		if(trace&&(ferror(trace)||fclose(trace)!=0)){fprintf(stderr,"trace write failed\n");return 2;}
		return rc;
		}
	fprintf(stderr,"usage: bench replay <recording> [trace-out]\n  <recording>: file written by `smooth-movement record`\n  <trace-out>: file to write the trace to (`-` or omitted: no trace)\n");
	return 3;
}
