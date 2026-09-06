// Round-trip test of the frame recording codec: random per-tile arrays with zero runs and repeats
// across frames, written and read back through frame_record.h.
#include "frame_record.h"
#include <cstdio>
#include <cstdlib>
#include <random>

int main()
{
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
	frame_record::frame_headerst f;f.flip=true;f.camera=true;f.tick_ms=123456;f.window_x=-3;f.window_z=77;f.paused=true;f.follow_unit=9;f.mouse_x=-1;f.mouse_mbut=true;f.zoom=96;f.origin_x=5;f.dimx=200;f.rest_x=-0.25;f.rest_y=1.5;
	frame_record::write_frame_header(w,f);
	frame_record::viewport_headerst v{8,100,60,1,98,2,57,3,4};
	frame_record::write_viewport_header(w,v);
	std::vector<frame_record::unit_recordst> units{{-1,2,3,6000,true},{4,5,6,0,false}};
	frame_record::write_units(w,units);
	frame_record::write_frame_result(w,{42,true,7});
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
	frame_record::frame_headerst f2;frame_record::viewport_headerst v2;std::vector<frame_record::unit_recordst> u2;frame_record::frame_resultst res;
	const bool ok=frame_record::read_file_header(r)&&frame_record::read_frame_header(r,f2)&&frame_record::read_viewport_header(r,v2)&&frame_record::read_units(r,u2)&&frame_record::read_frame_result(r,res)&&r.at_end();
	if(!ok||f2.flip!=f.flip||f2.hauled!=f.hauled||f2.camera!=f.camera||f2.linear!=f.linear||f2.tick_ms!=f.tick_ms||f2.window_x!=f.window_x||f2.window_z!=f.window_z||f2.paused!=f.paused||f2.follow_unit!=f.follow_unit||f2.mouse_x!=f.mouse_x||f2.mouse_mbut!=f.mouse_mbut||f2.zoom!=f.zoom||f2.origin_x!=f.origin_x||f2.dimx!=f.dimx||f2.rest_x!=f.rest_x||f2.rest_y!=f.rest_y
		||v2.slot!=v.slot||v2.dim_x!=v.dim_x||v2.dim_y!=v.dim_y||v2.clipx0!=v.clipx0||v2.clipx1!=v.clipx1||v2.clipy0!=v.clipy0||v2.clipy1!=v.clipy1||v2.screen_x!=v.screen_x||v2.screen_y!=v.screen_y
		||u2.size()!=2||u2[0].x!=-1||u2[0].y!=2||u2[0].z!=3||u2[0].texpos!=6000||!u2[0].cached||u2[1].x!=4||u2[1].texpos!=0||u2[1].cached||res.repaints!=42||!res.painted||res.changed_words!=7)
		{puts("header round trip failed");++failures;}
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
