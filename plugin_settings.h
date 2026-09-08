// SPDX-License-Identifier: MIT
//
// The settings the console sets: what the plugin does to a frame, as opposed to what the
// game shows it. The plugin state keeps them where their owners read them and hands them
// out and takes them back as one value, so a recording stores the lot with every frame and
// the harness restores the lot before replaying it.

#pragma once

#include "visual_animation.h"

#include <cstdint>

struct plugin_settingsst
{
	bool flip=false;   // mirror sprites to face their movement
	bool hauled=false; // draw the hauled item's icon with the carrier
	bool camera=false; // the free camera
	// The free camera's rest offset in tiles, positive when the view sits west/north of the
	// window; only read while the camera is on.
	double rest_x=0.0,rest_y=0.0;
	bool linear=false; // linear easing instead of the default curve
	uint32_t step_ms=visual_animation_managerst::default_step_duration_ms; // one-tile step
	walk_bob_settingst bob;
};
