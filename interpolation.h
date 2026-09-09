// SPDX-License-Identifier: MIT
//
// Interpolation: how one tile step, which the game makes as a jump, is spread over frames.
// A step's time progress runs from 0 at its start to 1 at its end. An interpolation is a
// movement: given the time progress and the direction of the step (horizontal, diagonal or
// vertical), it gives the sprite's place in the step's frame: how far along the line from
// the source tile to the target tile it is, and how high above that line, in tiles. It also
// says whether steps are paced (the cadence rules below) and whether it lifts the sprite off
// the line at all, which the render code must know: a lifted sprite reaches into the row
// above its path, which must be erasable and repaintable too (see sprite_proxies.h).
//
// Every movement's lift is zero at both ends of the step, so the sprite always lands on the
// grid. The walk bob settings are the parameters of the movements that lift: the hops per
// step, the height of a hop and how the direction of the step scales it.
//
// The console names an interpolation, `interpolation <name>`; `linear on|off` and `bob on|off`
// switch to that one and back to the default. To add one, add one entry to
// `interpolations()`.

#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <string>
#include <string_view>

// The direction of a tile step, as the movements see it. The source can be fractional when
// a step retargets from an in-flight position, so it is rounded back to the tile the
// creature was last seen on; the direction is that of the whole tile step, not of the
// remaining fraction.
enum class step_directionst{horizontal,diagonal,vertical};

inline step_directionst step_direction(
	float source_x,float source_y,int32_t target_x,int32_t target_y)
{
	const bool same_x=std::lround(source_x)==target_x;
	const bool same_y=std::lround(source_y)==target_y;
	if(same_y)return step_directionst::horizontal;
	if(same_x)return step_directionst::vertical;
	return step_directionst::diagonal;
}

// The walk bob's settings, the parameters of the movements that lift (`bob` below): the hops
// per step, and how far a hop raises the sprite: the amplitude (a fraction of a tile) times
// the multiplier for the step's direction. A step with a vertical component glides the
// sprite a whole tile up or down, which drowns a small hop, so those steps get more; on a
// straight up or down step the hop is parallel to the travel and shows only as a stall, so
// it needs more still. Two hops per step read as two footfalls, one as a single bounce.
struct walk_bob_settingst
{
	float amplitude=0.10f;
	float horizontal_mult=1.0f;
	float diagonal_mult=2.4f;
	float vertical_mult=2.7f;
	int hops=2;

	float multiplier(step_directionst direction) const
	{
		return direction==step_directionst::horizontal?horizontal_mult:
			direction==step_directionst::vertical?vertical_mult:diagonal_mult;
	}
};

// Where a sprite is at one moment of its step, in the step's frame: `along`, 0 at the source
// tile and 1 at the target tile, and `lift`, the height above the line between them in
// tiles.
struct sprite_positionst
{
	float along;
	float lift;
};

// A movement: the sprite's position at a time progress, for a step in a direction, given
// the walk bob settings.
using interpolation_fnt=sprite_positionst (*)(
	float progress,step_directionst direction,const walk_bob_settingst &bob);

// The default: slow at both ends, so a sprite leaves and lands softly, and stays on the line.
inline sprite_positionst smoothstep_interpolation(
	float progress,step_directionst,const walk_bob_settingst &)
{
	return {progress*progress*(3.0f-2.0f*progress),0.0f};
}

// Constant speed on the line. Paced, so a creature walking tile after tile keeps one
// cadence.
inline sprite_positionst linear_interpolation(
	float progress,step_directionst,const walk_bob_settingst &)
{
	return {progress,0.0f};
}

// The walk bob: the soft start and landing of the default, and a hop per step (or two, like
// footfalls): |sin(pi*hops*along)| rises and falls once per hop and is zero at both ends,
// scaled by the amount and the direction's multiplier. The hop follows the distance along
// the line rather than the time, so its peaks sit between the tiles and the sprite is on
// the line exactly when it is on a tile.
inline sprite_positionst bob_interpolation(
	float progress,step_directionst direction,const walk_bob_settingst &bob)
{
	const float along=smoothstep_interpolation(progress,direction,bob).along;
	return {along,std::fabs(std::sin(along*3.14159265f*float(bob.hops)))*bob.amplitude*
		bob.multiplier(direction)};
}

struct interpolationst
{
	const char *name;
	interpolation_fnt at;
	// Whether the movement ever leaves the line: the render code then erases and repaints
	// the row above the path (see sprite_proxies.h). A movement that never lifts says no.
	bool lifts;
	// Paced: a step that continues an earlier step takes that step's cadence as its
	// duration (within the animation manager's limits), and a finished step stays usable as
	// the predecessor of the next for up to those limits, so a walk does not stutter.
	bool paced;
};

// Every interpolation, the default first.
inline const std::array<interpolationst,3> &interpolations()
{
	static const std::array<interpolationst,3> all={{
		{"smoothstep",smoothstep_interpolation,false,false},
		{"linear",linear_interpolation,false,true},
		{"bob",bob_interpolation,true,false},
	}};
	return all;
}

inline const interpolationst &default_interpolation()
{
	return interpolations()[0];
}

inline const interpolationst *find_interpolation(std::string_view name)
{
	for(const interpolationst &interpolation:interpolations())
		if(name==interpolation.name)return &interpolation;
	return nullptr;
}

// The names the console accepts, for its printouts.
inline std::string interpolation_names()
{
	std::string names;
	for(const interpolationst &interpolation:interpolations())
		{
		if(!names.empty())names+=", ";
		names+=interpolation.name;
		}
	return names;
}
