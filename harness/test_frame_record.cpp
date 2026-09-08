// Round-trip test of the frame recording codec: random per-tile arrays with zero runs and repeats
// across frames, written and read back through frame_record.h.
#include "frame_record.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <random>

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
			f.settings.bob.enabled=i==2;
			f.settings.bob.amplitude=0.15f;
			f.settings.bob.horizontal_mult=1.0f;f.settings.bob.diagonal_mult=2.0f;
			f.settings.bob.vertical_mult=3.0f;
			f.settings.bob.hops=1;
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
	{
	frame_record::writerst w;
	frame_record::write_file_header(w);
	frame_record::frame_headerst f;
	f.settings.flip=true;f.settings.camera=true;f.settings.step_ms=300;
	f.simulation_tick=int64_t(3)<<33;f.tick_ms=123456;
	f.settings.bob.enabled=true;f.settings.bob.amplitude=0.25f;f.settings.bob.hops=1;
	f.settings.bob.horizontal_mult=1.5f;f.settings.bob.diagonal_mult=2.0f;
	f.settings.bob.vertical_mult=3.0f;
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
	if(!ok||r.version!=frame_record::version||f2.settings.flip!=f.settings.flip||f2.settings.hauled!=f.settings.hauled||f2.settings.camera!=f.settings.camera||f2.settings.linear!=f.settings.linear||f2.settings.step_ms!=300||f2.simulation_tick!=f.simulation_tick||f2.tick_ms!=f.tick_ms||f2.window_x!=f.window_x||f2.window_z!=f.window_z||f2.paused!=f.paused||f2.follow_unit!=f.follow_unit||f2.mouse_x!=f.mouse_x||f2.mouse_mbut!=f.mouse_mbut||f2.zoom!=f.zoom||f2.origin_x!=f.origin_x||f2.dimx!=f.dimx||f2.settings.rest_x!=f.settings.rest_x||f2.settings.rest_y!=f.settings.rest_y
		||v2.slot!=v.slot||v2.dim_x!=v.dim_x||v2.dim_y!=v.dim_y||v2.clipx0!=v.clipx0||v2.clipx1!=v.clipx1||v2.clipy0!=v.clipy0||v2.clipy1!=v.clipy1||v2.screen_x!=v.screen_x||v2.screen_y!=v.screen_y
		||u2.size()!=2||u2[0].x!=-1||u2[0].y!=2||u2[0].z!=3||u2[0].texpos!=6000||!u2[0].cached||u2[1].x!=4||u2[1].texpos!=0||u2[1].cached||res.repaints!=42||!res.painted||res.changed_words!=7)
		{puts("header round trip failed");++failures;}
	if(!f2.settings.bob.enabled||f2.settings.bob.amplitude!=0.25f||
		f2.settings.bob.horizontal_mult!=1.5f||f2.settings.bob.diagonal_mult!=2.0f||
		f2.settings.bob.vertical_mult!=3.0f||f2.settings.bob.hops!=1)
		{puts("bob settings round trip failed");++failures;}
	}
	// A version 2 file has no step field in its frame header and reads back at the 150 ms
	// every such recording was made with, a version 3 file no simulation tick and reads
	// back as unknown, a version 4 file no walk bob settings and reads back with the bob
	// off; a version past the current one is rejected, as is a step of zero, a tick below
	// -1 and walk bob settings the commands would refuse.
	{
	frame_record::writerst w;
	w.raw("SMRC",4);w.u32(2);
	frame_record::frame_headerst f;
	f.settings.linear=true;f.settings.step_ms=999;f.simulation_tick=77;
	f.settings.bob.enabled=true;f.settings.bob.amplitude=0.3f;f.settings.bob.hops=1;
	f.tick_ms=5;f.zoom=64;
	frame_record::write_frame_header(w,f);
	// Drop the step, tick and bob fields: 4, 8 and 18 bytes after the 'F' and four flags.
	w.bytes.erase(w.bytes.begin()+8+5,w.bytes.begin()+8+35);
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
	frame_record::frame_headerst f2;
	const bool ok=frame_record::read_file_header(r)&&frame_record::read_frame_header(r,f2)&&r.at_end();
	if(!ok||r.version!=2||!f2.settings.linear||f2.settings.step_ms!=150||f2.simulation_tick!=-1||
		f2.settings.bob.enabled||f2.settings.bob.amplitude!=walk_bob_settingst{}.amplitude||
		f2.settings.bob.hops!=2||
		f2.tick_ms!=5||f2.zoom!=64)
		{puts("version 2 header read failed");++failures;}
	frame_record::writerst w1;
	w1.raw("SMRC",4);w1.u32(3);
	frame_record::write_frame_header(w1,f);
	w1.bytes.erase(w1.bytes.begin()+8+9,w1.bytes.begin()+8+35); // drop the tick and bob fields
	frame_record::readerst r1;r1.data=w1.bytes.data();r1.size=w1.bytes.size();
	frame_record::frame_headerst f3;
	const bool ok1=frame_record::read_file_header(r1)&&
		frame_record::read_frame_header(r1,f3)&&r1.at_end();
	if(!ok1||r1.version!=3||!f3.settings.linear||f3.settings.step_ms!=999||f3.simulation_tick!=-1||
		f3.settings.bob.enabled||f3.settings.bob.hops!=2||f3.tick_ms!=5||f3.zoom!=64)
		{puts("version 3 header read failed");++failures;}
	frame_record::writerst w5;
	w5.raw("SMRC",4);w5.u32(4);
	frame_record::write_frame_header(w5,f);
	w5.bytes.erase(w5.bytes.begin()+8+17,w5.bytes.begin()+8+35); // drop the bob fields
	frame_record::readerst r5;r5.data=w5.bytes.data();r5.size=w5.bytes.size();
	// Read into a header that already holds the bob on, as a replay that reuses one header
	// per frame would: a version 4 read must still reset it.
	frame_record::frame_headerst f5=f;
	const bool ok5=frame_record::read_file_header(r5)&&
		frame_record::read_frame_header(r5,f5)&&r5.at_end();
	if(!ok5||r5.version!=4||!f5.settings.linear||f5.settings.step_ms!=999||f5.simulation_tick!=77||
		f5.settings.bob.enabled||f5.settings.bob.amplitude!=walk_bob_settingst{}.amplitude||
		f5.settings.bob.hops!=2||
		f5.tick_ms!=5||f5.zoom!=64)
		{puts("version 4 header read failed");++failures;}
	// The current version reads the bob settings back as written.
	frame_record::writerst w6;frame_record::write_file_header(w6);
	frame_record::write_frame_header(w6,f);
	frame_record::readerst r6;r6.data=w6.bytes.data();r6.size=w6.bytes.size();
	frame_record::frame_headerst f6;
	if(!frame_record::read_file_header(r6)||!frame_record::read_frame_header(r6,f6)||
		!r6.at_end()||!f6.settings.bob.enabled||f6.settings.bob.amplitude!=0.3f||
		f6.settings.bob.hops!=1||
		f6.settings.bob.vertical_mult!=walk_bob_settingst{}.vertical_mult)
		{puts("version 5 bob settings read failed");++failures;}
	// Settings the commands would refuse: a zero amount, three hops, an amount whose
	// product with a multiplier passes the cap, and a NaN multiplier.
	const auto rejects=[&](const walk_bob_settingst &bob,const char *what)
		{
		frame_record::frame_headerst g=f;g.settings.bob=bob;
		frame_record::writerst wb;frame_record::write_file_header(wb);
		frame_record::write_frame_header(wb,g);
		frame_record::readerst rb;rb.data=wb.bytes.data();rb.size=wb.bytes.size();
		frame_record::frame_headerst gb;
		if(!frame_record::read_file_header(rb)||frame_record::read_frame_header(rb,gb)||rb.ok())
			{printf("%s accepted\n",what);++failures;}
		};
	walk_bob_settingst bad;bad.amplitude=0.0f;rejects(bad,"zero bob amount");
	bad=walk_bob_settingst{};bad.hops=3;rejects(bad,"three hops");
	bad=walk_bob_settingst{};bad.amplitude=0.4f;rejects(bad,"bob lift past the cap");
	bad=walk_bob_settingst{};bad.diagonal_mult=std::nanf("");rejects(bad,"NaN bob multiplier");
	// A multiplier past 5 on each field, with an amount small enough that the lift still fits.
	bad=walk_bob_settingst{};bad.amplitude=0.1f;bad.horizontal_mult=6.0f;
	rejects(bad,"horizontal multiplier past 5");
	bad=walk_bob_settingst{};bad.amplitude=0.1f;bad.diagonal_mult=6.0f;
	rejects(bad,"diagonal multiplier past 5");
	bad=walk_bob_settingst{};bad.amplitude=0.1f;bad.vertical_mult=6.0f;
	rejects(bad,"vertical multiplier past 5");
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
