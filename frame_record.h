// Frame recording: everything the render hook reads from the game, captured once per frame so
// the offline harness can replay a real fortress through the plugin. Shared between the plugin
// (writer, `smooth-movement record`) and the harness (reader, `bench replay`).
//
// File layout (little-endian, byte packed):
//   "SMRC" u32 version
//   per frame:
//     'F' u8 flip, u8 hauled, u8 camera (the plugin's settings for this frame),
//         u8 length and that many bytes: the name of the movement (movement.h; before
//         version 6 one byte, linear, which with the hop byte below names one of the three:
//         smoothstep, linear or hop; both set names none),
//         u8 count, then that many settings of the movement, each u8 length and that many
//         bytes, the setting's name, and f32 its value (from version 7; before it, see the
//         walk hop settings below),
//         u32 step_ms (the one-tile step time; absent in version 2, where it was 150),
//         i64 simulation_tick (the simulation's frame counter when the game last filled the
//         per-tile arrays, -1 when no fill was seen since the previous frame; absent before
//         version 4, which reads as -1 on every frame),
//         before version 7: f32 hop amount, f32 horizontal diagonal vertical multipliers,
//         u8 hops (the walk hop settings, which read as the `hop` movement's settings
//         amount, horizontal, diagonal, vertical and hops when that movement is named, and
//         are dropped otherwise; absent before version 5, which reads as the defaults;
//         before version 6 a byte, hop, comes first, whether the interpolation is `hop`),
//         u32 tick_ms, i32 window x y z, u8 paused, i32 follow_unit, i32 mouse x y, u8 mbut,
//         i32 zoom origin_x origin_y dimx dimy, f64 free-camera rest offset x y (tiles)
//     u8 viewports; per viewport: u8 slot (0..7 lower, 8 main), i32 dim_x dim_y clipx0 clipx1
//         clipy0 clipy1 screen_x screen_y, then every per-tile array in `for_each_tile_array` order as
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

constexpr uint32_t version=7;
// The oldest version the reader accepts; a version 2 frame header has no step field, a
// version 3 header no simulation tick, a version 4 header no walk hop settings, every
// header before version 6 a linear switch (and from version 5 a hop switch) instead of the
// movement's name, and every header before version 7 the walk hop settings in fixed fields
// instead of the movement's settings by name.
constexpr uint32_t oldest_version=2;
constexpr int main_slot=8;
constexpr int slot_count=9;

// Every per-tile array of a viewport, current then old, in declaration order.
template<typename Viewport,typename Callback>
void for_each_tile_array(Viewport &vp,Callback callback)
{
	#define tile_array(name) callback(vp.name);
	tile_array(screentexpos_background) tile_array(screentexpos_floor_flag)
	tile_array(screentexpos_background_two) tile_array(screentexpos_liquid_flag)
	tile_array(screentexpos_spatter_flag) tile_array(screentexpos_spatter)
	tile_array(screentexpos_ramp_flag) tile_array(screentexpos_shadow_flag)
	tile_array(screentexpos_building_one) tile_array(screentexpos_item)
	tile_array(screentexpos_vehicle) tile_array(screentexpos_vermin)
	tile_array(screentexpos_left_creature) tile_array(screentexpos)
	tile_array(screentexpos_right_creature) tile_array(screentexpos_building_two)
	tile_array(screentexpos_projectile) tile_array(screentexpos_high_flow)
	tile_array(screentexpos_top_shadow) tile_array(screentexpos_signpost)
	tile_array(screentexpos_upleft_creature) tile_array(screentexpos_up_creature)
	tile_array(screentexpos_upright_creature) tile_array(screentexpos_designation)
	tile_array(screentexpos_interface)
	tile_array(screentexpos_background_old) tile_array(screentexpos_floor_flag_old)
	tile_array(screentexpos_background_two_old) tile_array(screentexpos_liquid_flag_old)
	tile_array(screentexpos_spatter_flag_old) tile_array(screentexpos_spatter_old)
	tile_array(screentexpos_ramp_flag_old) tile_array(screentexpos_shadow_flag_old)
	tile_array(screentexpos_building_one_old) tile_array(screentexpos_item_old)
	tile_array(screentexpos_vehicle_old) tile_array(screentexpos_vermin_old)
	tile_array(screentexpos_left_creature_old) tile_array(screentexpos_old)
	tile_array(screentexpos_right_creature_old) tile_array(screentexpos_building_two_old)
	tile_array(screentexpos_projectile_old) tile_array(screentexpos_high_flow_old)
	tile_array(screentexpos_top_shadow_old) tile_array(screentexpos_signpost_old)
	tile_array(screentexpos_upleft_creature_old) tile_array(screentexpos_up_creature_old)
	tile_array(screentexpos_upright_creature_old) tile_array(screentexpos_designation_old)
	tile_array(screentexpos_interface_old)
	#undef tile_array
}
constexpr int tile_array_count=50;

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
	// 150 ms, what it was made with, and one before version 5 with the hop's settings at
	// their defaults.
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
	w.u8(s.flip);w.u8(s.hauled);w.u8(s.camera);
	w.u8(uint8_t(s.movement.size()));w.raw(s.movement.data(),s.movement.size());
	w.u8(uint8_t(s.movement_settings.size()));
	for(const movement_settingst &setting:s.movement_settings)
		{
		w.u8(uint8_t(setting.name.size()));w.raw(setting.name.data(),setting.name.size());
		w.f32(setting.value);
		}
	w.u32(s.step_ms);
	w.i64(f.simulation_tick);
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
	s.flip=r.u8()!=0;s.hauled=r.u8()!=0;s.camera=r.u8()!=0;
	s.movement=default_movement().name();
	s.movement_settings.clear();
	bool linear=false;
	auto read_name=[&](std::string &name,const char *what)
		{
		const size_t length=r.u8();
		if(r.pos+length>r.size){r.fail(what);return false;}
		name.assign(reinterpret_cast<const char *>(r.data+r.pos),length);
		r.pos+=length;
		return true;
		};
	if(r.version>=6)
		{
		if(!read_name(s.movement,"truncated movement name"))return false;
		}
	else linear=r.u8()!=0;
	if(r.version>=7)
		{
		const size_t count=r.u8();
		for(size_t i=0;i<count;++i)
			{
			movement_settingst setting;
			if(!read_name(setting.name,"truncated movement setting name"))return false;
			setting.value=r.f32();
			s.movement_settings.push_back(setting);
			}
		}
	s.step_ms=r.version>=3?r.u32():150;
	if(s.step_ms==0)r.fail("bad step time");
	f.simulation_tick=r.version>=4?r.i64():-1;
	if(f.simulation_tick<-1)r.fail("bad simulation tick");
	// Before version 7 the walk hop settings are fixed fields: the `hop` movement's settings
	// when that movement is named, nothing otherwise.
	bool hop=false;
	if(r.version>=5&&r.version<7)
		{
		if(r.version<6)hop=r.u8()!=0;
		const float amount_tiles=r.f32();
		const float horizontal=r.f32(),diagonal=r.f32(),vertical=r.f32();
		const float hops=r.u8();
		if(hop||s.movement=="hop")
			s.movement_settings={{"amount",amount_tiles},{"horizontal",horizontal},
				{"diagonal",diagonal},{"vertical",vertical},{"hops",hops}};
		}
	// Before version 6 the two switches name the movement, and both on names none.
	if(r.version<6)
		{
		if(linear&&hop){r.fail("a frame with the linear and the hop switch names no movement");return false;}
		s.movement=linear?"linear":hop?"hop":"smoothstep";
		}
	// What the console would refuse, the reader refuses: an unknown movement, a setting it
	// does not have, a value out of its range (NaN included) or settings reaching beyond what
	// the plugin repaints.
	if(!check_movement_settings(s.movement,s.movement_settings).empty())
		{r.fail("bad movement settings");return false;}
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
