#pragma once
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>
#include "MemAccess.h"
namespace DFHack {
enum command_result { CR_LINK_FAILURE=-3, CR_NEEDS_CONSOLE=-2, CR_NOT_IMPLEMENTED=-1, CR_OK=0, CR_FAILURE=1, CR_WRONG_USAGE=2, CR_NOT_FOUND=3 };
struct color_ostream {
	std::string captured;
	// Crude std::format stand-in: each {...} takes the next argument, specs are ignored.
	template<typename... A> void print(const char *f,A&&... a){
		std::vector<std::string> args;
		(args.push_back(str(a)),...);
		size_t n=0;
		for(const char *c=f;*c;++c){
			if(*c=='{'){while(*c&&*c!='}')++c;captured+=n<args.size()?args[n++]:"?";}
			else captured+=*c;}
	}
	template<typename... A> void printerr(const char *f,A&&... a){print(f,a...);}
	template<typename T> static std::string str(const T &v){std::ostringstream o;o<<v;return o.str();}
};
struct Core { Process *p; static Core &getInstance(){static Process proc; static Core core{&proc}; return core;} };
}
