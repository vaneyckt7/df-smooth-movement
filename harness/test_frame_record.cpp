// Round-trip test of the frame recording codec: random per-tile arrays with zero runs and repeats
// across frames, written and read back through frame_record.h.
#include "frame_record.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <limits>
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
				f.settings.movement_settings={{"hop-height",0.15f},{"horizontal-mult",1.0f},
					{"diagonal-mult",2.0f},{"vertical-mult",3.0f},{"hops-per-step",1.0f}};
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
	// A size request whose addition would wrap must still be rejected.
	{
	frame_record::readerst r;
	r.pos=std::numeric_limits<size_t>::max()-1;
	r.size=std::numeric_limits<size_t>::max();
	if(r.need(2)||r.ok()){puts("overflowing size request accepted");++failures;}
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
	const std::vector<movement_settingst> hop_settings={{"hop-height",0.25f},{"horizontal-mult",1.5f},
		{"diagonal-mult",2.0f},{"vertical-mult",3.0f},{"hops-per-step",1.0f}};
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
	frame_record::frame_headerst f;
	f.settings.movement="hop";
	f.settings.movement_settings={{"hop-height",0.3f},{"horizontal-mult",1.0f},{"diagonal-mult",2.4f},
		{"vertical-mult",2.7f},{"hops-per-step",1.0f}};
	f.settings.step_ms=999;f.simulation_tick=77;f.tick_ms=5;f.zoom=64;
	frame_record::frame_headerst f2,f3;
	// There are no pre-v7 cases here on purpose. `frame_record.h` sets `oldest_version`
	// equal to `version`, so the reader accepts one format and a recording in any older
	// one is not read at all; `harness/recinfo.py` asserts the same number. What each
	// older format held is in `CHANGELOG.md`, version by version, and in git history.
	{
	// A replay decodes every frame into the same header. Settings from one frame must not
	// remain when the next frame names a movement with none.
	frame_record::frame_headerst hop=f;
	hop.settings.movement="hop";
	frame_record::frame_headerst linear=f;
	linear.settings.movement="linear";
	linear.settings.movement_settings.clear();
	frame_record::writerst w;frame_record::write_file_header(w);
	frame_record::write_frame_header(w,hop);
	frame_record::write_frame_header(w,linear);
	frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
	frame_record::frame_headerst out;
	if(!frame_record::read_file_header(r)||!frame_record::read_frame_header(r,out)||
		out.settings.movement_settings!=hop.settings.movement_settings||
		!frame_record::read_frame_header(r,out)||out.settings.movement!="linear"||
		!out.settings.movement_settings.empty()||!r.at_end())
		{puts("reused frame header retained movement settings");++failures;}
	}
	{
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
	wa.bytes.resize(name_at+1+3+1+1+3); // the count, then part of a setting name
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
	rejects("hop",{{"hop-height",0.0f}},"zero hop height");
	rejects("hop",{{"hops-per-step",3.0f}},"three hops");
	rejects("hop",{{"hop-height",1.5f}},"hop height past a tile");
	rejects("hop",{{"diagonal-mult",std::nanf("")}},"NaN hop multiplier");
	rejects("hop",{{"hop-height",0.1f},{"horizontal-mult",6.0f}},"horizontal multiplier past 5");
	rejects("hop",{{"hop-height",0.1f},{"diagonal-mult",6.0f}},"diagonal multiplier past 5");
	rejects("hop",{{"hop-height",0.1f},{"vertical-mult",6.0f}},"vertical multiplier past 5");
	rejects("hop",{{"stride",1.0f}},"a setting the hop lacks");
	rejects("linear",{{"hop-height",0.1f}},"a setting on the linear movement");
	frame_record::writerst w4;frame_record::write_file_header(w4);f.simulation_tick=-2;
	frame_record::write_frame_header(w4,f);
	frame_record::readerst r4;r4.data=w4.bytes.data();r4.size=w4.bytes.size();
	if(!frame_record::read_file_header(r4)||frame_record::read_frame_header(r4,f3)||r4.ok())
		{puts("tick below -1 accepted");++failures;}
	f.simulation_tick=77;
	frame_record::writerst w2;w2.raw("SMRC",4);w2.u32(frame_record::version+1);
	frame_record::readerst r2;r2.data=w2.bytes.data();r2.size=w2.bytes.size();
	if(frame_record::read_file_header(r2)||r2.ok()){puts("future version accepted");++failures;}
	// The floor is checked as well as the ceiling. `oldest_version` equals `version`, so every
	// older format is refused at the file header, with the reason named there rather than as a
	// puzzling failure deeper in: an older frame header has a different layout, so a reader that
	// let one past would go on to misread the movement's name and report bad settings instead.
	// Without this case, lowering `oldest_version` or dropping the floor comparison leaves the
	// suite green.
	const auto rejects_version=[&failures](uint32_t file_version,const char *what)
		{
		frame_record::writerst w;w.raw("SMRC",4);w.u32(file_version);
		frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
		if(frame_record::read_file_header(r)||r.ok()){printf("%s accepted\n",what);++failures;}
		};
	rejects_version(frame_record::version-1,"the version below this one");
	rejects_version(2,"the oldest version there ever was");
	rejects_version(0,"version zero");
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
	// What the viewport slots claim between them is capped as well as each dimension, because
	// a reader sizes a slot's storage from these numbers before it reads anything into it and
	// keeps that storage from frame to frame. Every viewport below is well inside 4096 by
	// 4096, so only the total can refuse one.
	{
	// One viewport of a recording: which slot it is in, and its size, which is square here.
	struct casest{int slot,size;};
	// Writes these frames, then reads the viewport headers back one at a time and answers how
	// many were accepted before one was refused.
	const auto accepted=[](const std::vector<std::vector<casest>> &frames)
		{
		frame_record::frame_headerst f;
		f.settings.movement="linear";f.settings.step_ms=250;f.simulation_tick=0;f.tick_ms=1;f.zoom=64;
		frame_record::writerst w;
		frame_record::write_file_header(w);
		for(const std::vector<casest> &frame:frames)
			{
			frame_record::write_frame_header(w,f);
			for(const casest &c:frame)
				frame_record::write_viewport_header(w,{c.slot,c.size,c.size,0,c.size-1,0,c.size-1,0,0});
			}
		frame_record::readerst r;r.data=w.bytes.data();r.size=w.bytes.size();
		frame_record::frame_headerst out;frame_record::viewport_headerst vh;
		if(!frame_record::read_file_header(r))return -1;
		int count=0;
		for(const std::vector<casest> &frame:frames)
			{
			if(!frame_record::read_frame_header(r,out))return count;
			for(size_t i=0;i<frame.size();++i)
				{
				if(!frame_record::read_viewport_header(r,vh))return count;
				++count;
				}
			}
		return count;
		};
	// One frame of `count` viewports of `size`, in consecutive slots from `first`.
	const auto frame_of=[](int first,int count,int size)
		{
		std::vector<casest> frame;
		for(int i=0;i<count;++i)frame.push_back({(first+i)%frame_record::slot_count,size});
		return frame;
		};
	// 1024 square is an eighth of the cap, so the ninth of nine viewports is one too many.
	if(accepted({frame_of(0,9,1024)})!=8){puts("slots past the tile cap were accepted");++failures;}
	// Exactly the cap is allowed: the check is on more than, not on as much as.
	if(accepted({frame_of(0,8,1024)})!=8){puts("slots at the tile cap were refused");++failures;}
	// A slot claims its storage once, not once per frame, or a long recording would be refused
	// part way through however small its frames are.
	if(accepted({frame_of(0,8,1024),frame_of(0,8,1024),frame_of(0,8,1024)})!=24)
		{puts("the same slots counted again in a later frame");++failures;}
	// A slot that only a later frame uses does count: the reader is still holding the others.
	if(accepted({frame_of(0,8,1024),frame_of(8,1,1024)})!=8)
		{puts("a slot first used by a later frame was not counted");++failures;}
	// What a slot counts for is the largest it has ever been, because a reader that resizes a
	// slot downward keeps the storage it already has. Slots 0 to 7 fill the cap, then slot 0
	// shrinks to a sixteenth of what it was, and the 262,144 tiles slot 8 asks for next are
	// still too many -- which they would not be if the shrink had given the space back.
	if(accepted({frame_of(0,8,1024),{{0,512}},{{8,512}}})!=9)
		{puts("a slot counted for less than the largest it has been");++failures;}
	// A single viewport can be inside the per-dimension cap and past this one on its own.
	if(accepted({frame_of(0,1,4096)})!=0){puts("a viewport past the tile cap was accepted");++failures;}
	}
	if(failures){printf("frame record tests: %d FAILED\n",failures);return 1;}
	puts("frame record tests: OK");
	return 0;
}
