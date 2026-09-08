// Frame recording: everything the render hook reads from the game, captured once per frame so
// the offline harness can replay a real fortress through the plugin. Shared between the plugin
// (writer, `smooth-movement record`) and the harness (reader, `bench replay`).
//
// File layout (little-endian, byte packed):
//   "SMRC" u32 version
//   per frame:
//     'F' u8 flip, u8 hauled, u8 camera, u8 linear (the plugin's settings for this frame),
//         u32 step_ms (the one-tile step time; absent in version 2, where it was 150),
//         i64 simulation_tick (the simulation's frame counter when the game last filled the
//         per-tile arrays, -1 when no fill was seen since the previous frame; absent before
//         version 4, which reads as -1 on every frame),
//         u8 bob, f32 bob amount, f32 horizontal diagonal vertical multipliers, u8 hops (the
//         walk bob settings; absent before version 5, which reads as the bob off),
//         u32 tick_ms, i32 window x y z, u8 paused, i32 follow_unit, i32 mouse x y, u8 mbut,
//         i32 zoom origin_x origin_y dimx dimy, f64 free-camera rest offset x y (tiles)
//     u8 viewports; per viewport: u8 slot (0..7 lower, 8 main), i32 dim_x dim_y clipx0 clipx1
//         clipy0 clipy1 screen_x screen_y, then every per-tile array in `for_each_buffer` order as
//         u8 present and, when present, a run-coded array
//     u32 units; per unit: i32 x y z, i32 texture id (texpos) of the hauled item, u8 texture
//         already cached.
//         Captured where the hook looks units up, after the camera update; empty when the hook
//         returned before that point.
//     'E' u32 tiles the hook asked the game to repaint, u8 painted, u32 array entries the game
//         had changed under the hook by the time it returned (a frame the replay cannot
//         reproduce exactly)
// Run coding of an array of N entries: varint (length<<2 | op) where op 0 = zeros, 1 = same
// entries as this array had in the previous frame, 2 = literal entries follow.
#pragma once

#include "df/graphic_viewportst.h"
#include "plugin_settings.h"
#include "visual_animation.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>

namespace frame_record {

constexpr uint32_t version=5;
// The oldest version the reader accepts; a version 2 frame header has no step field, a
// version 3 header no simulation tick and a version 4 header no walk bob settings.
constexpr uint32_t oldest_version=2;
constexpr int main_slot=8;
constexpr int slot_count=9;

// Every per-tile array of a viewport, current then old, in declaration order.
template<typename Viewport,typename Callback>
void for_each_buffer(Viewport &vp,Callback callback)
{
	#define buffer(name) callback(vp.name);
	buffer(screentexpos_background) buffer(screentexpos_floor_flag)
	buffer(screentexpos_background_two) buffer(screentexpos_liquid_flag)
	buffer(screentexpos_spatter_flag) buffer(screentexpos_spatter)
	buffer(screentexpos_ramp_flag) buffer(screentexpos_shadow_flag)
	buffer(screentexpos_building_one) buffer(screentexpos_item)
	buffer(screentexpos_vehicle) buffer(screentexpos_vermin)
	buffer(screentexpos_left_creature) buffer(screentexpos)
	buffer(screentexpos_right_creature) buffer(screentexpos_building_two)
	buffer(screentexpos_projectile) buffer(screentexpos_high_flow)
	buffer(screentexpos_top_shadow) buffer(screentexpos_signpost)
	buffer(screentexpos_upleft_creature) buffer(screentexpos_up_creature)
	buffer(screentexpos_upright_creature) buffer(screentexpos_designation)
	buffer(screentexpos_interface)
	buffer(screentexpos_background_old) buffer(screentexpos_floor_flag_old)
	buffer(screentexpos_background_two_old) buffer(screentexpos_liquid_flag_old)
	buffer(screentexpos_spatter_flag_old) buffer(screentexpos_spatter_old)
	buffer(screentexpos_ramp_flag_old) buffer(screentexpos_shadow_flag_old)
	buffer(screentexpos_building_one_old) buffer(screentexpos_item_old)
	buffer(screentexpos_vehicle_old) buffer(screentexpos_vermin_old)
	buffer(screentexpos_left_creature_old) buffer(screentexpos_old)
	buffer(screentexpos_right_creature_old) buffer(screentexpos_building_two_old)
	buffer(screentexpos_projectile_old) buffer(screentexpos_high_flow_old)
	buffer(screentexpos_top_shadow_old) buffer(screentexpos_signpost_old)
	buffer(screentexpos_upleft_creature_old) buffer(screentexpos_up_creature_old)
	buffer(screentexpos_upright_creature_old) buffer(screentexpos_designation_old)
	buffer(screentexpos_interface_old)
	#undef buffer
}
constexpr int buffer_count=50;

struct writerst
{
	std::vector<uint8_t> bytes;

	void u8(uint8_t v){bytes.push_back(v);}
	void u32(uint32_t v){for(int i=0;i<4;++i)bytes.push_back(uint8_t(v>>(8*i)));}
	void i32(int32_t v){u32(uint32_t(v));}
	void f64(double v){uint64_t bits;std::memcpy(&bits,&v,8);u32(uint32_t(bits));u32(uint32_t(bits>>32));}
	void f32(float v){uint32_t bits;std::memcpy(&bits,&v,4);u32(bits);}
	void i64(int64_t v)
		{const uint64_t bits=uint64_t(v);u32(uint32_t(bits));u32(uint32_t(bits>>32));}
	void varint(uint64_t v)
		{
		while(v>=0x80){bytes.push_back(uint8_t(v|0x80));v>>=7;}
		bytes.push_back(uint8_t(v));
		}
	void raw(const void *data,size_t size)
		{
		const uint8_t *p=static_cast<const uint8_t *>(data);
		bytes.insert(bytes.end(),p,p+size);
		}

	// Run-codes `count` entries of `current` against `previous` (the same array one frame
	// earlier, or null when there is no comparable previous frame).
	template<typename T>
	void words(const T *current,const T *previous,size_t count)
		{
		static_assert(std::is_trivially_copyable_v<T>);
		const T zero{};
		const auto is_zero=[&](size_t i){return std::memcmp(&current[i],&zero,sizeof(T))==0;};
		const auto is_same=[&](size_t i)
			{return previous!=nullptr&&std::memcmp(&current[i],&previous[i],sizeof(T))==0;};
		size_t i=0;
		while(i<count)
			{
			size_t j=i;
			if(is_zero(i)){while(j<count&&is_zero(j))++j;varint((uint64_t(j-i)<<2)|0);}
			else if(is_same(i)){while(j<count&&is_same(j))++j;varint((uint64_t(j-i)<<2)|1);}
			else
				{
				while(j<count&&!is_zero(j)&&!is_same(j))++j;
				varint((uint64_t(j-i)<<2)|2);
				raw(&current[i],(j-i)*sizeof(T));
				}
			i=j;
			}
		}
};

struct readerst
{
	const uint8_t *data=nullptr;
	size_t size=0;
	size_t pos=0;
	std::string error;
	uint32_t version=0; // the file's version, set by read_file_header

	bool ok() const{return error.empty();}
	bool at_end() const{return pos>=size;}
	void fail(const char *what){if(error.empty())error=what;}
	bool need(size_t n)
		{
		if(pos+n>size){fail("truncated");return false;}
		return true;
		}
	uint8_t u8(){if(!need(1))return 0;return data[pos++];}
	uint32_t u32()
		{
		if(!need(4))return 0;
		uint32_t v=0;
		for(int i=0;i<4;++i)v|=uint32_t(data[pos+size_t(i)])<<(8*i);
		pos+=4;
		return v;
		}
	int32_t i32(){return int32_t(u32());}
	double f64(){const uint64_t lo=u32(),hi=u32();const uint64_t bits=lo|hi<<32;double v;std::memcpy(&v,&bits,8);return v;}
	int64_t i64(){const uint64_t lo=u32(),hi=u32();return int64_t(lo|hi<<32);}
	float f32(){const uint32_t bits=u32();float v;std::memcpy(&v,&bits,4);return v;}
	uint64_t varint()
		{
		uint64_t v=0;
		for(int shift=0;shift<64;shift+=7)
			{
			if(!need(1))return 0;
			const uint8_t b=data[pos++];
			v|=uint64_t(b&0x7f)<<shift;
			if(!(b&0x80))return v;
			}
		fail("bad varint");
		return 0;
		}

	// Decodes in place: `words` must still hold the previous frame's values of this array.
	template<typename T>
	void words(T *out,size_t count)
		{
		const T zero{};
		size_t i=0;
		while(i<count&&ok())
			{
			const uint64_t v=varint();
			const size_t length=size_t(v>>2);
			const int op=int(v&3);
			if(length==0||length>count-i){fail("bad run");return;}
			if(op==0)for(size_t k=0;k<length;++k)out[i+k]=zero;
			else if(op==1){}
			else if(op==2)
				{
				if(!need(length*sizeof(T)))return;
				std::memcpy(&out[i],data+pos,length*sizeof(T));
				pos+=length*sizeof(T);
				}
			else{fail("bad op");return;}
			i+=length;
			}
	}
};

struct unit_recordst
{
	int16_t x=0,y=0,z=0;
	int32_t texpos=0;
	bool cached=false;
};

struct frame_headerst
{
	// The plugin's settings for the frame. A version 2 recording reads back with the step at
	// 150 ms, what it was made with, and one before version 5 with the bob off.
	plugin_settingsst settings;
	int64_t simulation_tick=-1; // a recording before version 4 reads back as -1, unknown
	uint32_t tick_ms=0;
	int32_t window_x=0,window_y=0,window_z=0;
	bool paused=false;
	int32_t follow_unit=-1;
	int32_t mouse_x=0,mouse_y=0;
	bool mouse_mbut=false;
	int32_t zoom=128,origin_x=0,origin_y=0,dimx=0,dimy=0;
};

struct viewport_headerst
{
	int slot=0;
	int32_t dim_x=0,dim_y=0;
	int32_t clipx0=0,clipx1=0,clipy0=0,clipy1=0;
	int32_t screen_x=0,screen_y=0;
};

struct frame_resultst
{
	uint32_t repaints=0;
	bool painted=false;
	uint32_t changed_words=0;
};

inline void write_file_header(writerst &w)
{
	w.raw("SMRC",4);
	w.u32(version);
}

inline void write_frame_header(writerst &w,const frame_headerst &f)
{
	w.u8('F');
	const plugin_settingsst &s=f.settings;
	w.u8(s.flip);w.u8(s.hauled);w.u8(s.camera);w.u8(s.linear);
	w.u32(s.step_ms);
	w.i64(f.simulation_tick);
	w.u8(s.bob.enabled);
	w.f32(s.bob.amplitude);
	w.f32(s.bob.horizontal_mult);w.f32(s.bob.diagonal_mult);w.f32(s.bob.vertical_mult);
	w.u8(uint8_t(s.bob.hops));
	w.u32(f.tick_ms);
	w.i32(f.window_x);w.i32(f.window_y);w.i32(f.window_z);
	w.u8(f.paused);
	w.i32(f.follow_unit);
	w.i32(f.mouse_x);w.i32(f.mouse_y);
	w.u8(f.mouse_mbut);
	w.i32(f.zoom);w.i32(f.origin_x);w.i32(f.origin_y);w.i32(f.dimx);w.i32(f.dimy);
	w.f64(s.rest_x);w.f64(s.rest_y);
}

inline void write_viewport_header(writerst &w,const viewport_headerst &v)
{
	w.u8(uint8_t(v.slot));
	w.i32(v.dim_x);w.i32(v.dim_y);
	w.i32(v.clipx0);w.i32(v.clipx1);w.i32(v.clipy0);w.i32(v.clipy1);
	w.i32(v.screen_x);w.i32(v.screen_y);
}

inline void write_units(writerst &w,const std::vector<unit_recordst> &units)
{
	w.u32(uint32_t(units.size()));
	for(const unit_recordst &u:units){w.i32(u.x);w.i32(u.y);w.i32(u.z);w.i32(u.texpos);w.u8(u.cached);}
}

inline void write_frame_result(writerst &w,const frame_resultst &r)
{
	w.u8('E');
	w.u32(r.repaints);
	w.u8(r.painted);
	w.u32(r.changed_words);
}

inline bool read_file_header(readerst &r)
{
	if(!r.need(8))return false;
	if(std::memcmp(r.data,"SMRC",4)!=0){r.fail("not a frame recording");return false;}
	r.pos=4;
	r.version=r.u32();
	if(r.version<oldest_version||r.version>version)
		{r.fail("unsupported recording version");return false;}
	return r.ok();
}

inline bool read_frame_header(readerst &r,frame_headerst &f)
{
	if(r.u8()!='F'){r.fail("expected frame");return false;}
	plugin_settingsst &s=f.settings;
	s.flip=r.u8()!=0;s.hauled=r.u8()!=0;s.camera=r.u8()!=0;s.linear=r.u8()!=0;
	s.step_ms=r.version>=3?r.u32():150;
	if(s.step_ms==0)r.fail("bad step time");
	f.simulation_tick=r.version>=4?r.i64():-1;
	if(f.simulation_tick<-1)r.fail("bad simulation tick");
	s.bob=walk_bob_settingst{};
	if(r.version>=5)
		{
		s.bob.enabled=r.u8()!=0;
		s.bob.amplitude=r.f32();
		s.bob.horizontal_mult=r.f32();s.bob.diagonal_mult=r.f32();s.bob.vertical_mult=r.f32();
		s.bob.hops=r.u8();
		// The bounds the commands enforce, written so that NaN fails too.
		if(!(s.bob.amplitude>0.0f&&s.bob.amplitude<=max_walk_bob_lift)||
			!(s.bob.horizontal_mult>=0.0f&&s.bob.horizontal_mult<=5.0f)||
			!(s.bob.diagonal_mult>=0.0f&&s.bob.diagonal_mult<=5.0f)||
			!(s.bob.vertical_mult>=0.0f&&s.bob.vertical_mult<=5.0f)||
			!walk_bob_lift_fits(s.bob.amplitude,s.bob.horizontal_mult,s.bob.diagonal_mult,
				s.bob.vertical_mult)||
			(s.bob.hops!=1&&s.bob.hops!=2))r.fail("bad walk bob settings");
		}
	f.tick_ms=r.u32();
	f.window_x=r.i32();f.window_y=r.i32();f.window_z=r.i32();
	f.paused=r.u8()!=0;
	f.follow_unit=r.i32();
	f.mouse_x=r.i32();f.mouse_y=r.i32();
	f.mouse_mbut=r.u8()!=0;
	f.zoom=r.i32();f.origin_x=r.i32();f.origin_y=r.i32();f.dimx=r.i32();f.dimy=r.i32();
	s.rest_x=r.f64();s.rest_y=r.f64();
	return r.ok();
}

inline bool read_viewport_header(readerst &r,viewport_headerst &v)
{
	v.slot=r.u8();
	v.dim_x=r.i32();v.dim_y=r.i32();
	v.clipx0=r.i32();v.clipx1=r.i32();v.clipy0=r.i32();v.clipy1=r.i32();
	v.screen_x=r.i32();v.screen_y=r.i32();
	if(v.slot<0||v.slot>=slot_count)r.fail("bad viewport slot");
	if(v.dim_x<=0||v.dim_y<=0||v.dim_x>4096||v.dim_y>4096)r.fail("bad viewport size");
	return r.ok();
}

inline bool read_units(readerst &r,std::vector<unit_recordst> &units)
{
	const uint32_t n=r.u32();
	if(n>1000000){r.fail("bad unit count");return false;}
	units.resize(n);
	for(unit_recordst &u:units){u.x=int16_t(r.i32());u.y=int16_t(r.i32());u.z=int16_t(r.i32());u.texpos=r.i32();u.cached=r.u8()!=0;}
	return r.ok();
}

inline bool read_frame_result(readerst &r,frame_resultst &res)
{
	if(r.u8()!='E'){r.fail("expected frame result");return false;}
	res.repaints=r.u32();
	res.painted=r.u8()!=0;
	res.changed_words=r.u32();
	return r.ok();
}

} // namespace frame_record
