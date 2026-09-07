#pragma once
#include <cstring>
namespace DFHack {
struct DFLibrary;
namespace DFSDL { inline DFLibrary *obtain_library_handle(){return reinterpret_cast<DFLibrary*>(1);} }
void *harness_lookup_sdl(const char *name);
inline void *LookupPlugin(DFLibrary *,const char *name){return harness_lookup_sdl(name);}
}
