#pragma once
#include <cstdint>
namespace DFHack { struct Process { uint32_t tick_ms=0; uint32_t getTickCount(){return tick_ms;} }; }
