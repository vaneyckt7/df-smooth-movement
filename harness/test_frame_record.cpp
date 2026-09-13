// Round-trip test of the frame recording codec: random per-tile arrays with zero runs and repeats
// across frames, written and read back through frame_record.h.
#include "frame_record.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <tuple>
#include <vector>

int main(int argc,char **argv)
{
	// With a path: write a three-frame recording in the current format, with no viewports
	// or units, so test.sh can check that recinfo.py reads the current header layout.
	if(argc>1)
		{
		frame_record::writerst w;
		frame_record::write_file_header(w);
		for(int i=0;i<3;++i)
			{
			frame_record::frame_headerst f;
			f.settings.step_ms=250;
			f.simulation_tick=i==1?-1:int64_t(1234567)+i;
			if(i==1)f.settings.movement="linear";
			if(i==2)
				{
				f.settings.movement="hop";
				f.settings.movement_settings={{"amount",0.15f},{"horizontal",1.0f},
					{"diagonal",2.0f},{"vertical",3.0f},{"hops",1.0f}};
				}
			f.tick_ms=uint32_t(1000+16*i);
			frame_record::write_frame_header(w,f);
			w.u8(0);
			frame_record::write_units(w,{});
			frame_record::write_frame_result(w,{0,false,0});
			}
		FILE *out=fopen(argv[1],"wb");
		if(out==nullptr||fwrite(w.bytes.data(),1,w.bytes.size(),out)!=w.bytes.size())
			{puts("could not write the sample recording");return 1;}
		fclose(out);
		return 0;
		}
	std::mt19937 rng(7);
	int failures=0;
	for(int trial=0;trial<200;++trial)
		{
		const size_t n=1+rng()%300;
		std::vector<int32_t> previous(n),current(n),decoded(n);
		for(size_t i=0;i<n;++i)previous[i]=rng()%3?0:int32_t(rng());
		// Without a previous frame the decoder must not need one: start from garbage instead.
		if(trial%2==0)decoded=previous;else for(auto &v:decoded)v=int32_t(rng());
		for(size_t i=0;i<n;++i)
			{
			const int r=rng()%4;
			current[i]=r==0?0:r==1?previous[i]:int32_t(rng());
			}
		frame_record::writerst w;
		const bool with_previous=trial%2==0;
		w.words(current.data(),with_previous?previous.data():static_cast<const int32_t*>(nullptr),n);
		frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
		r.words(decoded.data(),n);
		if(!r.ok()||r.pos!=r.size||decoded!=current){printf("trial %d: mismatch (%s)\n",trial,r.error.c_str());++failures;}
		// The 'same' op only appears when a previous frame was given.
		if(!with_previous)for(size_t i=0;i<n;++i)if(current[i]!=0&&current[i]==previous[i]&&decoded[i]!=current[i]){puts("same-op without previous");++failures;}
		}
	// A 64-bit array, and a truncated stream, which must fail rather than read past the end.
	{
	std::vector<uint64_t> a(50),b(50);
	for(size_t i=0;i<50;++i)a[i]=i%4?uint64_t(rng())<<32|rng():0;
	frame_record::writerst w;w.words(a.data(),static_cast<const uint64_t*>(nullptr),50);
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
	r.words(b.data(),50);
	if(!r.ok()||a!=b){puts("64-bit round trip failed");++failures;}
	frame_record::readerst t;t.data=w.bytes.data();t.size=w.bytes.size()/2;
	t.words(b.data(),50);
	if(t.ok()){puts("truncated stream accepted");++failures;}
	}
	// A run longer than the array must be rejected, not written past it.
	{
	frame_record::writerst w;w.varint(uint64_t(51)<<2|0);
	std::vector<int32_t> out(60,-1);
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
	r.words(out.data(),50);
	if(r.ok()){puts("overlong run accepted");++failures;}
	for(size_t i=50;i<60;++i)if(out[i]!=-1){puts("overlong run wrote past the array");++failures;break;}
	}
	// Headers and unit lists round-trip field for field.
	const std::vector<movement_settingst> hop_settings={{"amount",0.25f},{"horizontal",1.5f},
		{"diagonal",2.0f},{"vertical",3.0f},{"hops",1.0f}};
	{
	frame_record::writerst w;
	frame_record::write_file_header(w);
	frame_record::frame_headerst f;
	f.settings.flip=true;f.settings.camera=true;f.settings.step_ms=300;
	f.simulation_tick=int64_t(3)<<33;f.tick_ms=123456;
	f.settings.movement="hop";f.settings.movement_settings=hop_settings;
	f.window_x=-3;f.window_z=77;f.paused=true;f.follow_unit=9;f.mouse_x=-1;f.mouse_mbut=true;
	f.zoom=96;f.origin_x=5;f.dimx=200;f.settings.rest_x=-0.25;f.settings.rest_y=1.5;
	frame_record::write_frame_header(w,f);
	frame_record::viewport_headerst v{8,100,60,1,98,2,57,3,4};
	frame_record::write_viewport_header(w,v);
	std::vector<frame_record::unit_recordst> units{{-1,2,3,6000,true},{4,5,6,0,false}};
	frame_record::write_units(w,units);
	frame_record::write_frame_result(w,{42,true,7});
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
	frame_record::frame_headerst f2;frame_record::viewport_headerst v2;std::vector<frame_record::unit_recordst> u2;frame_record::frame_resultst res;
	const bool ok=frame_record::read_file_header(r)&&frame_record::read_frame_header(r,f2)&&frame_record::read_viewport_header(r,v2)&&frame_record::read_units(r,u2)&&frame_record::read_frame_result(r,res)&&r.at_end();
	if(!ok||r.version!=frame_record::version||f2.settings.flip!=f.settings.flip||f2.settings.hauled!=f.settings.hauled||f2.settings.camera!=f.settings.camera||f2.settings.movement!=f.settings.movement||f2.settings.step_ms!=300||f2.simulation_tick!=f.simulation_tick||f2.tick_ms!=f.tick_ms||f2.window_x!=f.window_x||f2.window_z!=f.window_z||f2.paused!=f.paused||f2.follow_unit!=f.follow_unit||f2.mouse_x!=f.mouse_x||f2.mouse_mbut!=f.mouse_mbut||f2.zoom!=f.zoom||f2.origin_x!=f.origin_x||f2.dimx!=f.dimx||f2.settings.rest_x!=f.settings.rest_x||f2.settings.rest_y!=f.settings.rest_y
		||v2.slot!=v.slot||v2.dim_x!=v.dim_x||v2.dim_y!=v.dim_y||v2.clipx0!=v.clipx0||v2.clipx1!=v.clipx1||v2.clipy0!=v.clipy0||v2.clipy1!=v.clipy1||v2.screen_x!=v.screen_x||v2.screen_y!=v.screen_y
		||u2.size()!=2||u2[0].x!=-1||u2[0].y!=2||u2[0].z!=3||u2[0].texpos!=6000||!u2[0].cached||u2[1].x!=4||u2[1].texpos!=0||u2[1].cached||res.repaints!=42||!res.painted||res.changed_words!=7)
		{puts("header round trip failed");++failures;}
	if(f2.settings.movement_settings!=hop_settings)
		{puts("movement settings round trip failed");++failures;}
	}
	// A version 2 file has no step field in its frame header and reads back at the 150 ms
	// every such recording was made with, a version 3 file no simulation tick and reads
	// back as unknown, a version 4 file no walk hop settings, a version 5 file a linear
	// switch and a hop switch that name the movement and fixed walk hop fields, a version 6
	// file the movement's name and the fixed fields; a version past the current one is
	// rejected, as is a step of zero, a tick below -1 and movement settings the commands
	// would refuse.
	{
	// Writes a frame header in an older layout: the switches or the name, then the fixed
	// walk hop fields of versions 5 and 6, then the rest as the current writer lays it out.
	struct old_hopst{float amount,horizontal,diagonal,vertical;uint8_t hops;};
	const auto write_old_header=[](frame_record::writerst &w,uint32_t version,
		const frame_record::frame_headerst &g,bool linear,bool hop,const old_hopst &fields)
		{
		frame_record::writerst current;
		frame_record::write_frame_header(current,g);
		// The current header's tail, from the frame clock on: skip 'F', the switches, the
		// name, the settings list, the step and the simulation tick.
		size_t tail=4+1+g.settings.movement.size()+1;
		for(const movement_settingst &setting:g.settings.movement_settings)
			tail+=1+setting.name.size()+4;
		tail+=4+8;
		w.u8('F');w.u8(g.settings.flip);w.u8(g.settings.hauled);w.u8(g.settings.camera);
		if(version>=6)
			{
			w.u8(uint8_t(g.settings.movement.size()));
			w.raw(g.settings.movement.data(),g.settings.movement.size());
			}
		else w.u8(linear);
		if(version>=3)w.u32(g.settings.step_ms);
		if(version>=4)w.i64(g.simulation_tick);
		if(version==5)w.u8(hop);
		if(version>=5)
			{
			w.f32(fields.amount);w.f32(fields.horizontal);w.f32(fields.diagonal);
			w.f32(fields.vertical);w.u8(fields.hops);
			}
		w.raw(current.bytes.data()+tail,current.bytes.size()-tail);
		};
	const old_hopst fields{0.3f,1.0f,2.4f,2.7f,1};
	const std::vector<movement_settingst> fields_as_settings={{"amount",0.3f},{"horizontal",1.0f},
		{"diagonal",2.4f},{"vertical",2.7f},{"hops",1.0f}};
	frame_record::frame_headerst f;
	f.settings.movement="hop";f.settings.movement_settings=fields_as_settings;
	f.settings.step_ms=999;f.simulation_tick=77;
	f.tick_ms=5;f.zoom=64;
	// Reads one old header back, starting from a header that already names a movement with
	// settings, as a replay that reuses one header per frame would: every read must reset it.
	const auto read_old=[&](uint32_t version,bool linear,bool hop,
		frame_record::frame_headerst &out)
		{
		frame_record::writerst w;
		w.raw("SMRC",4);w.u32(version);
		write_old_header(w,version,f,linear,hop,fields);
		frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
		out=f;
		return frame_record::read_file_header(r)&&frame_record::read_frame_header(r,out)&&
			r.at_end()&&r.version==version;
		};
	frame_record::frame_headerst f2;
	if(!read_old(2,true,false,f2)||f2.settings.movement!="linear"||f2.settings.step_ms!=150||
		f2.simulation_tick!=-1||!f2.settings.movement_settings.empty()||
		f2.tick_ms!=5||f2.zoom!=64)
		{puts("version 2 header read failed");++failures;}
	frame_record::frame_headerst f3;
	if(!read_old(3,true,false,f3)||f3.settings.movement!="linear"||f3.settings.step_ms!=999||
		f3.simulation_tick!=-1||!f3.settings.movement_settings.empty()||f3.tick_ms!=5||f3.zoom!=64)
		{puts("version 3 header read failed");++failures;}
	frame_record::frame_headerst f4;
	if(!read_old(4,false,false,f4)||f4.settings.movement!="smoothstep"||f4.settings.step_ms!=999||
		f4.simulation_tick!=77||!f4.settings.movement_settings.empty()||f4.tick_ms!=5||f4.zoom!=64)
		{puts("version 4 header read failed");++failures;}
	// A version 5 header's two switches name the movement, and its fixed walk hop fields
	// read back as the hop's settings when the hop is named, nothing otherwise; both
	// switches on names none and is rejected. A version 6 header names the movement and
	// carries the same fixed fields.
	for(const uint32_t version:{5u,6u})
		for(const auto &[linear,hop,name]:{
			std::tuple<bool,bool,const char *>{false,false,"smoothstep"},{true,false,"linear"},
			{false,true,"hop"}})
			{
			frame_record::frame_headerst g=f;g.settings.movement=name;
			frame_record::writerst w;w.raw("SMRC",4);w.u32(version);
			write_old_header(w,version,g,linear,hop,fields);
			frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
			frame_record::frame_headerst out=f;
			const std::vector<movement_settingst> expected=
				std::string(name)=="hop"?fields_as_settings:std::vector<movement_settingst>{};
			if(!frame_record::read_file_header(r)||!frame_record::read_frame_header(r,out)||
				!r.at_end()||r.version!=version||out.settings.movement!=name||
				out.settings.movement_settings!=expected||out.settings.step_ms!=999||
				out.simulation_tick!=77||out.tick_ms!=5)
				{printf("version %u header read as %s failed\n",version,name);++failures;}
			}
	{
	frame_record::frame_headerst fb;
	if(read_old(5,true,true,fb))
		{puts("a version 5 header with linear and hop was accepted");++failures;}
	}
	// A version 5 or 6 header whose fixed fields the hop would refuse is rejected when the
	// hop is named, and read when it is not, since the fields then set nothing.
	for(const uint32_t version:{5u,6u})
		{
		const old_hopst bad{0.4f,1.0f,2.4f,2.7f,3};
		for(const auto &[linear,hop,name,accepted]:{
			std::tuple<bool,bool,const char *,bool>{false,true,"hop",false},
			{true,false,"linear",true}})
			{
			frame_record::frame_headerst g=f;g.settings.movement=name;
			g.settings.movement_settings.clear();
			frame_record::writerst w;w.raw("SMRC",4);w.u32(version);
			write_old_header(w,version,g,linear,hop,bad);
			frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
			frame_record::frame_headerst out;
			const bool read=frame_record::read_file_header(r)&&
				frame_record::read_frame_header(r,out)&&r.at_end();
			if(read!=accepted)
				{printf("version %u header with bad hop fields as %s: %s\n",version,name,
					read?"accepted":"rejected");++failures;}
			}
		}
	// The current version stores the movement by name with its settings: every movement
	// round trips with its own defaults, a name the table lacks is rejected and so is a
	// name cut short by the end of the file, or a setting's name cut short.
	for(const std::unique_ptr<movementst> &movement:make_movements().all)
		{
		frame_record::frame_headerst g=f;
		g.settings.movement=movement->name();
		g.settings.movement_settings=movement->settings();
		frame_record::writerst w7;frame_record::write_file_header(w7);
		frame_record::write_frame_header(w7,g);
		frame_record::readerst r7;r7.data=w7.bytes.data();r7.size=w7.bytes.size();
		frame_record::frame_headerst f7;
		if(!frame_record::read_file_header(r7)||!frame_record::read_frame_header(r7,f7)||
			!r7.at_end()||f7.settings.movement!=movement->name()||
			f7.settings.movement_settings!=movement->settings())
			{printf("movement %s round trip failed\n",movement->name());++failures;}
		}
	{
	frame_record::writerst w8;frame_record::write_file_header(w8);
	frame_record::write_frame_header(w8,f);
	const size_t name_at=8+4;
	const std::string bounce="bounce";
	w8.bytes[name_at]=uint8_t(bounce.size());
	w8.bytes.erase(w8.bytes.begin()+name_at+1,w8.bytes.begin()+name_at+1+3); // "hop"
	w8.bytes.insert(w8.bytes.begin()+name_at+1,bounce.begin(),bounce.end());
	frame_record::readerst r8;r8.data=w8.bytes.data();r8.size=w8.bytes.size();
	frame_record::frame_headerst f8;
	if(frame_record::read_file_header(r8)&&frame_record::read_frame_header(r8,f8))
		{puts("an unknown movement name was accepted");++failures;}
	frame_record::writerst w9;frame_record::write_file_header(w9);
	frame_record::write_frame_header(w9,f);
	w9.bytes.resize(name_at+2); // the length byte and "b" of "hop"
	frame_record::readerst r9;r9.data=w9.bytes.data();r9.size=w9.bytes.size();
	frame_record::frame_headerst f9;
	if(frame_record::read_file_header(r9)&&frame_record::read_frame_header(r9,f9))
		{puts("a truncated movement name was accepted");++failures;}
	frame_record::writerst wa;frame_record::write_file_header(wa);
	frame_record::write_frame_header(wa,f);
	wa.bytes.resize(name_at+1+3+1+1+3); // the count, then "amo" of "amount"
	frame_record::readerst ra;ra.data=wa.bytes.data();ra.size=wa.bytes.size();
	frame_record::frame_headerst fa;
	if(frame_record::read_file_header(ra)&&frame_record::read_frame_header(ra,fa))
		{puts("a truncated setting name was accepted");++failures;}
	}
	// Settings the commands would refuse: a zero amount, three hops, an amount past a tile,
	// a NaN multiplier, a multiplier past 5 on each field, a setting the movement does not
	// have, and a setting on a movement that has none.
	const auto rejects=[&](const char *name,const std::vector<movement_settingst> &settings,
		const char *what)
		{
		frame_record::frame_headerst g=f;g.settings.movement=name;g.settings.movement_settings=settings;
		frame_record::writerst wb;frame_record::write_file_header(wb);
		frame_record::write_frame_header(wb,g);
		frame_record::readerst rb;rb.data=wb.bytes.data();rb.size=wb.bytes.size();
		frame_record::frame_headerst gb;
		if(!frame_record::read_file_header(rb)||frame_record::read_frame_header(rb,gb)||rb.ok())
			{printf("%s accepted\n",what);++failures;}
		};
	rejects("hop",{{"amount",0.0f}},"zero hop amount");
	rejects("hop",{{"hops",3.0f}},"three hops");
	rejects("hop",{{"amount",1.5f}},"hop amount past a tile");
	rejects("hop",{{"diagonal",std::nanf("")}},"NaN hop multiplier");
	rejects("hop",{{"amount",0.1f},{"horizontal",6.0f}},"horizontal multiplier past 5");
	rejects("hop",{{"amount",0.1f},{"diagonal",6.0f}},"diagonal multiplier past 5");
	rejects("hop",{{"amount",0.1f},{"vertical",6.0f}},"vertical multiplier past 5");
	rejects("hop",{{"stride",1.0f}},"a setting the hop lacks");
	rejects("linear",{{"amount",0.1f}},"a setting on the linear movement");
	frame_record::writerst w4;frame_record::write_file_header(w4);f.simulation_tick=-2;
	frame_record::write_frame_header(w4,f);
	frame_record::readerst r4;r4.data=w4.bytes.data();r4.size=w4.bytes.size();
	if(!frame_record::read_file_header(r4)||frame_record::read_frame_header(r4,f3)||r4.ok())
		{puts("tick below -1 accepted");++failures;}
	f.simulation_tick=77;
	frame_record::writerst w2;w2.raw("SMRC",4);w2.u32(frame_record::version+1);
	frame_record::readerst r2;r2.data=w2.bytes.data();r2.size=w2.bytes.size();
	if(frame_record::read_file_header(r2)||r2.ok()){puts("future version accepted");++failures;}
	frame_record::writerst w3;frame_record::write_file_header(w3);f.settings.step_ms=0;
	frame_record::write_frame_header(w3,f);
	frame_record::readerst r3;r3.data=w3.bytes.data();r3.size=w3.bytes.size();
	if(!frame_record::read_file_header(r3)||frame_record::read_frame_header(r3,f2)||r3.ok())
		{puts("zero step accepted");++failures;}
	}
	// Header validation: a viewport slot past the last one and an absurd unit count are rejected.
	{
	frame_record::writerst w;frame_record::write_viewport_header(w,{frame_record::slot_count,10,10,0,9,0,9,0,0});
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();frame_record::viewport_headerst v;
	if(frame_record::read_viewport_header(r,v)||r.ok()){puts("bad viewport slot accepted");++failures;}
	frame_record::writerst w2;w2.u32(1000001);
	frame_record::readerst r2;r2.data=w2.bytes.data();r2.size=w2.bytes.size();std::vector<frame_record::unit_recordst> u;
	if(frame_record::read_units(r2,u)||r2.ok()){puts("bad unit count accepted");++failures;}
	}
	if(failures){printf("frame record tests: %d FAILED\n",failures);return 1;}
	puts("frame record tests: OK");
	return 0;
}
