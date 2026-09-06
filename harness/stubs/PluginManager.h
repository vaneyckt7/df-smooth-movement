#pragma once
#include <functional>
#include <string>
#include <vector>
#include "Core.h"
#define DFHACK_PLUGIN(name)
#define DFHACK_PLUGIN_IS_ENABLED(v) bool v=false;
#define DFhackCExport
#define REQUIRE_GLOBAL(g) using df::global::g;
#include "df/enabler.h"
#include "df/graphic.h"
#include "df/plotinfost.h"
namespace df { namespace global {
	inline df::enabler *enabler=nullptr; inline df::graphic *gps=nullptr; inline bool *pause_state=nullptr;
	inline df::plotinfost *plotinfo=nullptr; inline int32_t *window_x=nullptr; inline int32_t *window_y=nullptr; inline int32_t *window_z=nullptr; } }
namespace DFHack {
using command_function=command_result(*)(color_ostream&,std::vector<std::string>&);
struct PluginCommand { std::string name,desc; command_function fn;
	PluginCommand(const char *n,const char *d,command_function f):name(n),desc(d),fn(f){} };
}
