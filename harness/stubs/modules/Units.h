#pragma once
#include <vector>
#include "df/unit.h"
namespace DFHack { namespace Units {
// The harness keeps the units of the main level here.
inline std::vector<df::unit*> harness_units;
template<typename F> bool getUnitsInBox(std::vector<df::unit*> &out,int x1,int y1,int z1,int x2,int y2,int z2,F filter)
{
	for(df::unit *u:harness_units)
		if(u->pos.x>=x1&&u->pos.x<=x2&&u->pos.y>=y1&&u->pos.y<=y2&&u->pos.z>=z1&&u->pos.z<=z2&&filter(u))out.push_back(u);
	return true;
}
inline bool isHidden(const df::unit*){return false;} } }
