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
// Two facts the render code does read, for every movement alike: `reach`, how far beyond
// the straight path between the tiles the sprite can ever get, above and below, in tiles,
// which is the box of extra rows it erases and repaints around a moving sprite; and
// `keeps_cadence`, whether consecutive steps keep one cadence (the rules in
// visual_animation.h). The render code repaints one row beyond the path on each side, so a
// movement's reach must stay under a tile: `max_movement_reach`.
//
// A movement owns its settings: named numbers the console sets by name and the recordings
// store by name. `set` checks one value's own range; `apply_movement_settings` below applies
// a whole list at once and checks the reach the settings give against `max_movement_reach`.
// A movement with no settings has none to set.
//
// Every movement's offset is zero at progress 0 and the step itself at progress 1 (within
// float rounding), so the sprite always leaves from and lands on the grid.
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

// The most a movement may reach beyond the straight path, in tiles, above or below: the
// render code erases and repaints exactly one row on each side of the path, and a sprite
// reaching a whole tile would leave stale pixels at its apex.
constexpr float max_movement_reach=0.9f;

// A tile step as a movement sees it: the target tile minus the source, in tiles. The
// source can be fractional when a step retargets from a position in flight.
struct tile_stepst
{
	float x;
	float y;
};

// Where the sprite is drawn: an offset from the source tile, in tiles.
struct sprite_offsetst
{
	float x;
	float y;
};

// How far beyond the straight path a movement can take the sprite, in tiles: `above` is
// towards the row above (smaller y), `below` towards the row below.
struct movement_reachst
{
	float above;
	float below;

	// Whether the movement leaves the path at all.
	bool any() const
		{
		return above>0.0f||below>0.0f;
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
		virtual float travelled(float progress) const=0;

		// The sprite's offset from the source tile at a time progress, for a step.
		virtual sprite_offsetst position(float progress,tile_stepst step) const=0;

		virtual movement_reachst reach() const=0;

		// Whether a step that continues an earlier step takes that step's cadence as its
		// duration (within the animation manager's limits), and a finished step stays usable
		// as the predecessor of the next for up to those limits, so a walk does not stutter.
		virtual bool keeps_cadence() const=0;

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

		float travelled(float progress) const override
			{
			return progress*progress*(3.0f-2.0f*progress);
			}

		sprite_offsetst position(float progress,tile_stepst step) const override
			{
			const float along=travelled(progress);
			return {step.x*along,step.y*along};
			}

		movement_reachst reach() const override
			{
			return {0.0f,0.0f};
			}

		bool keeps_cadence() const override
			{
			return false;
			}

		std::unique_ptr<movementst> clone() const override
			{
			return std::make_unique<smoothstep_movementst>(*this);
			}
};

// Constant speed on the straight path, keeping one cadence from step to step.
class linear_movementst:public movementst
{
	public:
		const char *name() const override
			{
			return "linear";
			}

		float travelled(float progress) const override
			{
			return progress;
			}

		sprite_offsetst position(float progress,tile_stepst step) const override
			{
			return {step.x*progress,step.y*progress};
			}

		movement_reachst reach() const override
			{
			return {0.0f,0.0f};
			}

		bool keeps_cadence() const override
			{
			return true;
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
class bob_movementst:public movementst
{
	public:
		const char *name() const override
			{
			return "bob";
			}

		float travelled(float progress) const override
			{
			return path.travelled(progress);
			}

		sprite_offsetst position(float progress,tile_stepst step) const override
			{
			const float along=travelled(progress);
			const float hop=std::fabs(std::sin(along*3.14159265f*float(hops)))*amount*
				multiplier(step);
			return {step.x*along,step.y*along-hop};
			}

		movement_reachst reach() const override
			{
			return {amount*std::max({horizontal,diagonal,vertical}),0.0f};
			}

		bool keeps_cadence() const override
			{
			return false;
			}

		std::vector<movement_settingst> settings() const override
			{
			return {
				{"amount",amount},{"horizontal",horizontal},{"diagonal",diagonal},
				{"vertical",vertical},{"hops",float(hops)}};
			}

		std::string set(std::string_view name,float value) override
			{
			// Written so that NaN fails too.
			if(name=="amount")
				{
				if(!(value>0.0f&&value<=max_movement_reach))
					return format("bob amount must be within 0..%.2f tile",max_movement_reach);
				amount=value;
				return {};
				}
			if(name=="horizontal"||name=="diagonal"||name=="vertical")
				{
				if(!(value>=0.0f&&value<=5.0f))return "bob multipliers must be within 0..5";
				(name=="horizontal"?horizontal:name=="diagonal"?diagonal:vertical)=value;
				return {};
				}
			if(name=="hops")
				{
				if(value!=1.0f&&value!=2.0f)return "hops per step must be 1 or 2";
				hops=int(value);
				return {};
				}
			return movementst::set(name,value);
			}

		std::unique_ptr<movementst> clone() const override
			{
			return std::make_unique<bob_movementst>(*this);
			}

	private:
		smoothstep_movementst path;
		float amount=0.10f;
		float horizontal=1.0f;
		float diagonal=2.4f;
		float vertical=2.7f;
		int hops=2;

		// The multiplier for a step's direction. A retargeted step starts from a fractional
		// source, so a delta within (-0.5, 0.5] counts as no move on that axis: the tile the
		// source rounds to (halves up, as the plugin has always rounded it) is the target.
		float multiplier(tile_stepst step) const
			{
			if(still(step.y))return horizontal;
			if(still(step.x))return vertical;
			return diagonal;
			}

		static bool still(float delta)
			{
			return delta>-0.5f&&delta<=0.5f;
			}

		static std::string format(const char *pattern,float value)
			{
			char text[96];
			std::snprintf(text,sizeof text,pattern,double(value));
			return text;
			}
};

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
	set.all.push_back(std::make_unique<bob_movementst>());
	return set;
}

// A movement a manager without a set of its own follows: the default, with no settings.
inline const movementst &default_movement()
{
	static const smoothstep_movementst movement;
	return movement;
}

// Applies settings to a movement all at once: every value is set on a copy first, and the
// copy's reach checked against what the render code repaints, so a refused list leaves the
// movement as it was. Returns the error, or an empty string.
inline std::string apply_movement_settings(
	movementst &movement,const std::vector<movement_settingst> &settings)
{
	std::unique_ptr<movementst> changed=movement.clone();
	for(const movement_settingst &setting:settings)
		{
		const std::string error=changed->set(setting.name,setting.value);
		if(!error.empty())return error;
		}
	const movement_reachst reach=changed->reach();
	// A little slack so a product landing on the cap (0.3 x 3.0) is not rejected by rounding.
	if(!(reach.above<=max_movement_reach+1e-4f&&reach.below<=max_movement_reach+1e-4f))
		{
		char text[128];
		std::snprintf(text,sizeof text,
			"%s settings reach %.2f tile beyond the path; the most the plugin repaints is %.2f",
			movement.name(),double(std::max(reach.above,reach.below)),
			double(max_movement_reach));
		return text;
		}
	for(const movement_settingst &setting:settings)movement.set(setting.name,setting.value);
	return {};
}

// Whether a list of settings would be accepted by the movement of that name, fresh from its
// defaults: what a recording's frame header is checked against. Returns the error, or an
// empty string; an unknown movement is an error too.
inline std::string check_movement_settings(
	std::string_view name,const std::vector<movement_settingst> &settings)
{
	const movement_sett set=make_movements();
	movementst *movement=set.find(name);
	if(movement==nullptr)return "unknown movement "+std::string(name);
	return apply_movement_settings(*movement,settings);
}
