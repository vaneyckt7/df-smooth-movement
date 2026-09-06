#pragma once
#include <cstdint>
#include <functional>
namespace df {
union texture_fullid_flag {
	uint32_t whole; struct { uint32_t transparent_background:1; } bits;
	enum Mask : uint32_t { mask_transparent_background=0x1U };
	texture_fullid_flag(uint32_t w=0):whole(w){}
};
struct texture_fullid {
	int32_t texpos=0; float r=0,g=0,b=0,br=0,bg=0,bb=0; texture_fullid_flag flag{};
	bool operator==(const texture_fullid &o) const
		{return texpos==o.texpos&&r==o.r&&g==o.g&&b==o.b&&br==o.br&&bg==o.bg&&bb==o.bb&&flag.whole==o.flag.whole;}
};
}
namespace std { template<> struct hash<df::texture_fullid> {
	size_t operator()(const df::texture_fullid &t) const {return size_t(t.texpos)*1315423911u^size_t(t.flag.whole);} }; }
