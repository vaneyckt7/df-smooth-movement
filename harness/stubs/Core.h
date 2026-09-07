#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>
#include <sstream>
#include <string>
#include <vector>
#include "MemAccess.h"
namespace DFHack {
enum command_result { CR_LINK_FAILURE=-3, CR_NEEDS_CONSOLE=-2, CR_NOT_IMPLEMENTED=-1, CR_OK=0, CR_FAILURE=1, CR_WRONG_USAGE=2, CR_NOT_FOUND=3 };
struct color_ostream {
	std::string captured;
	// Crude std::format stand-in: each {...} takes the next argument. Of the specs only a
	// precision on a number, `{:.1f}`, is honoured; anything else is ignored.
	struct argst{std::string text;double number;bool numeric;};
	template<typename... A> void print(const char *f,A&&... a){
		std::vector<argst> args;
		(args.push_back(arg(a)),...);
		size_t n=0;
		for(const char *c=f;*c;++c){
			if(*c!='{'){captured+=*c;continue;}
			const char *spec=c;while(*c&&*c!='}')++c;
			if(n>=args.size()){captured+="?";continue;}
			const argst &v=args[n++];
			const char *dot=std::strchr(spec,'.');
			if(v.numeric&&dot&&dot<c){char buf[64];std::snprintf(buf,sizeof buf,"%.*f",std::atoi(dot+1),v.number);captured+=buf;}
			else captured+=v.text;}
	}
	template<typename... A> void printerr(const char *f,A&&... a){print(f,a...);}
	template<typename T> static argst arg(const T &v){
		std::ostringstream o;o<<v;
		if constexpr(std::is_arithmetic_v<T>)return {o.str(),double(v),true};
		else return {o.str(),0.0,false};}
};
struct Core { Process *p; static Core &getInstance(){static Process proc; static Core core{&proc}; return core;} };
}
