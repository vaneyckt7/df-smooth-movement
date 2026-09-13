// SPDX-License-Identifier: MIT
//
// Movements: how one tile step, which the game makes as a jump, is spread over frames. The
// console calls the current movement the interpolation (`interpolation <name>`).
//
// A step's time progress runs from 0 at its start to 1 at its end. A movement is asked two
// things about a step: how far along it the creature has travelled (a fraction of the step,
// for what continues from where a creature is: a retarget in flight, the camera following
// it) and where its sprite is drawn, as an offset in tiles from the source tile, given the
// step as a tile delta. Whatever the movement does between the tiles, a hop, a dip, a
// swerve, is its own business: the render code draws the sprite at the offset and knows
// nothing else about it.
//
// One fact the render code does read, for every movement alike: `overshoot`, how far beyond
// the straight path the sprite can get, in tiles, in each of four directions (above, below,
// left, right), and the render code erases and repaints that many extra tiles around the
// moving sprite.
//
// A movement owns its settings: named numbers the console sets by name and the recordings
// store by name. `set` checks one value's own range; `apply_movement_settings` below applies
// a whole list at once, on a copy first, so a refused list changes nothing. A movement with
// no settings has none to set.
//
// Every movement's offset is zero at progress 0 and the step itself at progress 1 (within
// float rounding), so the sprite always leaves from and lands on the grid. A movement need
// not still be moving at progress 1: a curve may arrive early and hold, so a movement that
// lingers on the tile it reached before the next step is a curve like any other. Once
// `travelled_pct` returns 1 the sprite is on the target tile, where the game draws it itself,
// and the plugin stops drawing the step; so a movement's offset must be the whole step
// whenever its travelled fraction is 1.
//
// A sprite that cannot follow the movement beyond the straight path (a tile it would cross
// is off the screen or burning) is drawn on the straight path instead, at the fraction
// travelled: the movement's own pace, without its excursion.
//
// How long a step lasts is the animation manager's business, not the movement's: a step
// that continues an earlier one takes that step's cadence, so a creature's walk keeps its
// own pace whatever the movement (the rules are in visual_animation.h).
//
// Names say their unit: `_ms` for milliseconds, `_tiles` for tiles, `_px` for pixels, `_pct`
// for a fraction from 0 to 1. A setting's console and recording name (`amount`) has no suffix.
//
// To add a movement: derive from `movementst`, and add it to `make_movements()`.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

// A tile step as a movement sees it: the target tile minus the source, in tiles. The
// source can be fractional: when the game moves a creature again before its previous step
// has finished, the new step starts from where the sprite is, the previous source plus the
// previous delta times the fraction travelled, so the delta is not a whole tile either.
struct tile_stepst
{
	float x_tiles;
	float y_tiles;
};

// Where the sprite is drawn: an offset from the source tile, in tiles.
struct sprite_offsetst
{
	float x_tiles;
	float y_tiles;
};

// How far beyond the straight path a movement can take the sprite in each direction,
// in tiles: `above_tiles` towards the row above (smaller y), `below_tiles` towards the row
// below, `left_tiles` towards smaller x, `right_tiles` towards larger x.
struct movement_overshootst
{
	float above_tiles=0.0f;
	float below_tiles=0.0f;
	float left_tiles=0.0f;
	float right_tiles=0.0f;

	bool any() const
		{
		return above_tiles>0.0f||below_tiles>0.0f||left_tiles>0.0f||right_tiles>0.0f;
		}
};

// One named setting of a movement and its value.
struct movement_settingst
{
	std::string name;
	float value;

	bool operator==(const movement_settingst &other) const
		{
		return name==other.name&&value==other.value;
		}

	bool operator!=(const movement_settingst &other) const
		{
		return !(*this==other);
		}
};

class movementst
{
	public:
		virtual ~movementst()=default;

		virtual const char *name() const=0;

		// The fraction of the step travelled at a time progress, 0 to 1.
		virtual float travelled_pct(float progress_pct) const=0;

		// The sprite's offset from the source tile at a time progress, for a step.
		virtual sprite_offsetst position(float progress_pct,tile_stepst step) const=0;

		virtual movement_overshootst overshoot() const=0;

		// The movement's settings and their values, in a fixed order.
		virtual std::vector<movement_settingst> settings() const
			{
			return {};
			}

		// Sets one setting. Returns an error message when the movement has no setting of
		// that name or the value is outside the setting's own range, else an empty string.
		virtual std::string set(std::string_view name,float value)
			{
			(void)value;
			return std::string(this->name())+" has no setting named "+std::string(name);
			}

		virtual std::unique_ptr<movementst> clone() const=0;
};

// The default: slow at both ends of the step, so a sprite leaves and lands softly, on the
// straight path.
class smoothstep_movementst:public movementst
{
	public:
		const char *name() const override
			{
			return "smoothstep";
			}

		float travelled_pct(float progress_pct) const override
			{
			return progress_pct*progress_pct*(3.0f-2.0f*progress_pct);
			}

		sprite_offsetst position(float progress_pct,tile_stepst step) const override
			{
			const float along_pct=travelled_pct(progress_pct);
			return {step.x_tiles*along_pct,step.y_tiles*along_pct};
			}

		movement_overshootst overshoot() const override
			{
			return {};
			}

		std::unique_ptr<movementst> clone() const override
			{
			return std::make_unique<smoothstep_movementst>(*this);
			}
};

// Constant speed on the straight path.
class linear_movementst:public movementst
{
	public:
		const char *name() const override
			{
			return "linear";
			}

		float travelled_pct(float progress_pct) const override
			{
			return progress_pct;
			}

		sprite_offsetst position(float progress_pct,tile_stepst step) const override
			{
			return {step.x_tiles*progress_pct,step.y_tiles*progress_pct};
			}

		movement_overshootst overshoot() const override
			{
			return {};
			}

		std::unique_ptr<movementst> clone() const override
			{
			return std::make_unique<linear_movementst>(*this);
			}
};

// The walk bob: the soft start and landing of the default, and a hop above the path per
// step, or two, like footfalls. The hop follows the distance travelled rather than the time,
// so its peaks sit between the tiles and the sprite is on the path exactly when it is on a
// tile: |sin(pi*hops*travelled)| rises and falls once per hop and is zero at both ends. Its
// height is the amount times a multiplier for the direction of the step: a step with a
// vertical part glides the sprite a whole tile up or down, which drowns a small hop, so
// diagonal steps get more; on a straight up or down step the hop is parallel to the travel
// and shows only as a stall, so it needs more still.
//
// Settings: `amount`, the height of a hop in tiles; `horizontal`, `diagonal` and `vertical`,
// the multipliers; `hops`, 1 or 2 per step.

// Every movement, one instance each with its settings, the default first.
struct movement_sett
{
	std::vector<std::unique_ptr<movementst>> all;

	movementst &default_movement() const
		{
		return *all.front();
		}

	movementst *find(std::string_view name) const
		{
		for(const std::unique_ptr<movementst> &movement:all)
			if(name==movement->name())return movement.get();
		return nullptr;
		}

	// The names the console accepts, for its printouts.
	std::string names() const
		{
		std::string names;
		for(const std::unique_ptr<movementst> &movement:all)
			{
			if(!names.empty())names+=", ";
			names+=movement->name();
			}
		return names;
		}
};

inline movement_sett make_movements()
{
	movement_sett set;
	set.all.push_back(std::make_unique<smoothstep_movementst>());
	set.all.push_back(std::make_unique<linear_movementst>());
	return set;
}

// A movement a manager without a set of its own follows: the default, with no settings.
inline const movementst &default_movement()
{
	static const smoothstep_movementst movement;
	return movement;
}
