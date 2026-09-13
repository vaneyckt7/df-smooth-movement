// SPDX-License-Identifier: MIT
//
// The `smooth-movement` console command: parses the words the console hands the plugin,
// reads and sets the plugin's settings and reports through the output it is given. The
// plugin file maps the outcome to DFHack's result codes and provides what the commands need
// from the game: whether the plugin is enabled, a full redraw, and scrolling the window.

#pragma once

#include "plugin_state.h"
#include "visual_animation.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

enum class command_outcomest
{
	ok,          // done, or reported
	failed,      // refused with a printed reason
	wrong_usage  // the words do not form a command; the console prints the usage
};

// What the commands need from the plugin around them.
struct command_hostst
{
	const char *plugin_version;
	bool plugin_enabled;
	// Asks the game to repaint everything on its next frame: a setting that changes the
	// screen without changing anything the game knows would otherwise leave the last frame
	// the plugin drew on screen.
	void (*full_redraw)();
	// Scrolls the game's window by whole tiles, for the camera; see free_camerast.
	std::array<bool,2> (*scroll_window)(int32_t kx,int32_t ky);
};

// The step time a `timestep` argument names, or -1 when it is not a whole number of
// milliseconds from 20 to 2000.
inline int32_t parse_step_ms(const std::string &text)
{
	if(text.empty()||text.size()>4||
		text.find_first_not_of("0123456789")!=std::string::npos)return -1;
	const int32_t ms=int32_t(std::stoul(text));
	return ms>=20&&ms<=2000?ms:-1;
}

// A hop amount or multiplier: decimal digits with at most one point, so that a stray
// character is a usage error rather than a number cut short at it. Negative when the text
// is not one.
inline float parse_hop_value(const std::string &text)
{
	if(text.empty()||text.size()>8||
		text.find_first_not_of("0123456789.")!=std::string::npos||
		text.find('.')!=text.rfind('.')||
		text.find_first_of("0123456789")==std::string::npos)return -1.0f;
	return std::stof(text);
}

// Whether the words are a setting's name followed by `on` or `off`, and which; the shape
// every switch in the grammar takes.
inline bool parse_on_off(const std::vector<std::string> &parameters,bool &on)
{
	if(parameters.size()!=2||(parameters[1]!="on"&&parameters[1]!="off"))return false;
	on=parameters[1]=="on";
	return true;
}

inline const char *on_off(bool on)
{
	return on?"on":"off";
}

// One line per setting, named by the setting's command word. Printed by the setting's bare
// command and by `smooth-movement status`, which lists them all.
template<typename Output>
void print_setting(Output &out,const plugin_statest &state,const std::string &word)
{
	if(word=="flip")out.print("sprite flipping: {}\n",on_off(state.flip_enabled));
	if(word=="movement")
		out.print("movement: {}\n",state.render.animation_manager.movement().name());
	if(word=="timestep")
		out.print("time step: {} ms\n",state.render.animation_manager.step_duration_ms());
	if(word=="hauled")out.print("hauled item icons: {}\n",on_off(state.hauled_enabled));
}

// Prints every setting, for `status`.
template<typename Output>
void print_settings(Output &out,const plugin_statest &state,const command_hostst &host)
{
	out.print(
		"smooth-movement {}: {}\n",
		host.plugin_version,
		host.plugin_enabled?"enabled":"disabled");
	out.print("free camera: {}, offset {:.3f} {:.3f} (tiles east/south of the grid)\n",
		on_off(state.render.camera.is_enabled()),
		-state.render.camera.requested_offset_x(),-state.render.camera.requested_offset_y());
	for(const char *word:{"flip","movement","timestep","hauled"})
		print_setting(out,state,word);
	out.print("frame stats: {}\n",on_off(state.stats.enabled));
}

template<typename Output>
command_outcomest stats_command(
	Output &out,const std::vector<std::string> &parameters,plugin_statest &state)
{
	if(parameters.size()==1)
		{
		state.stats.print(out);
		return command_outcomest::ok;
		}
	bool on=false;
	if(parse_on_off(parameters,on))
		{
		if(on)state.stats.clear();
		state.stats.enabled=on;
		out.print("smooth-movement: frame stats {}\n",on_off(on));
		return command_outcomest::ok;
		}
	if(parameters.size()==2&&parameters[1]=="reset")
		{
		state.stats.clear();
		out.print("smooth-movement: frame stats reset\n");
		return command_outcomest::ok;
		}
	return command_outcomest::wrong_usage;
}

template<typename Output>
command_outcomest record_command(
	Output &out,const std::vector<std::string> &parameters,plugin_statest &state,
	const command_hostst &host)
{
	if(parameters.size()==1)return command_outcomest::wrong_usage;
	if(parameters[1]=="status")
		{
		if(parameters.size()!=2)return command_outcomest::wrong_usage;
		state.recorder.status(out);
		return command_outcomest::ok;
		}
	if(parameters[1]=="stop")
		{
		if(parameters.size()!=2)return command_outcomest::wrong_usage;
		state.recorder.stop();
		out.print("smooth-movement: recording stopped\n");
		return command_outcomest::ok;
		}
	if(parameters.size()>3)return command_outcomest::wrong_usage;
	if(!host.plugin_enabled)
		{
		out.printerr("smooth-movement: enable the plugin before recording\n");
		return command_outcomest::failed;
		}
	if(state.recorder.running())
		{
		out.printerr("smooth-movement: a recording is running; `record stop` ends it\n");
		return command_outcomest::failed;
		}
	uint32_t frames=900;
	if(parameters.size()==3)
		{
		const std::string &count=parameters[2];
		if(count.empty()||count.size()>9||
			count.find_first_not_of("0123456789")!=std::string::npos)
			return command_outcomest::wrong_usage;
		frames=uint32_t(std::stoul(count));
		if(frames==0)return command_outcomest::wrong_usage;
		}
	if(!state.recorder.start(parameters[1],frames))
		{
		out.printerr("smooth-movement: cannot write {}\n",parameters[1]);
		return command_outcomest::failed;
		}
	out.print("smooth-movement: recording {} frames to {}\n",frames,parameters[1]);
	return command_outcomest::ok;
}

template<typename Output>
command_outcomest camera_command(
	Output &out,const std::vector<std::string> &parameters,plugin_statest &state,
	const command_hostst &host)
{
	if(parameters.size()==1)
		{
		out.print("free camera: {}, offset {:.3f} {:.3f}\n",
			on_off(state.render.camera.is_enabled()),
			-state.render.camera.requested_offset_x(),-state.render.camera.requested_offset_y());
		return command_outcomest::ok;
		}
	if(parameters.size()==2&&parameters[1]=="on")
		{
		state.render.camera.set_enabled(true);
		return command_outcomest::ok;
		}
	if(parameters.size()==2&&parameters[1]=="off")
		{
		state.render.camera.set_enabled(false);
		return command_outcomest::ok;
		}
	if(parameters.size()==2&&parameters[1]=="reset")
		{
		state.render.camera.request_rest(0.0,0.0);
		return command_outcomest::ok;
		}
	if(parameters.size()==3)
		{
		try
			{
			const double fx=std::stod(parameters[1]);
			const double fy=std::stod(parameters[2]);
			if(fx<-0.99||fx>0.99||fy<-0.99||fy>0.99)
				{
				out.printerr("offsets must be within -0.99..0.99 tiles\n");
				return command_outcomest::failed;
				}
			// User-facing: positive = view sits east/south of the grid position.
			state.render.camera.set_enabled(true);
			state.render.camera.request_rest(-fx,-fy);
			state.render.camera.normalize_rest(host.scroll_window);
			return command_outcomest::ok;
			}
		catch(...)
			{
			return command_outcomest::wrong_usage;
			}
		}
	return command_outcomest::wrong_usage;
}


// Runs one console command. `parameters` are the words after `smooth-movement`.
template<typename Output>
command_outcomest run_command(
	Output &out,
	const std::vector<std::string> &parameters,
	plugin_statest &state,
	const command_hostst &host)
{
	if(parameters.empty())return command_outcomest::wrong_usage;
	const std::string &word=parameters[0];
	if(word=="status")
		{
		if(parameters.size()!=1)return command_outcomest::wrong_usage;
		print_settings(out,state,host);
		return command_outcomest::ok;
		}
	if(word=="stats")return stats_command(out,parameters,state);
	if(word=="record")return record_command(out,parameters,state,host);
	if(word=="all")
		{
		bool on=false;
		if(!parse_on_off(parameters,on))return command_outcomest::wrong_usage;
		state.flip_enabled=on;
		state.hauled_enabled=on;
		host.full_redraw();
		out.print("smooth-movement: flip and hauled {}\n",on_off(on));
		return command_outcomest::ok;
		}
	if(word=="camera")return camera_command(out,parameters,state,host);
	if(word=="flip")
		{
		if(parameters.size()==1)
			{
			print_setting(out,state,word);
			return command_outcomest::ok;
			}
		// A toggle changes the screen without changing anything the game knows, so the game
		// will not repaint. OFF matters most: the render path stops touching tiles it
		// painted every frame. The last mirrored frame would persist. Same flush
		// plugin_enable(false) uses.
		bool on=false;
		if(!parse_on_off(parameters,on))return command_outcomest::wrong_usage;
		state.flip_enabled=on;
		host.full_redraw();
		out.print("smooth-movement: sprite flipping {}\n",on?"enabled":"disabled");
		return command_outcomest::ok;
		}
	if(word=="movement")
		{
		if(parameters.size()==1)
			{
			print_setting(out,state,word);
			out.print("movements: {}\n",state.movements.names());
			return command_outcomest::ok;
			}
		movementst *movement=state.movements.find(parameters[1]);
		if(movement==nullptr)return command_outcomest::wrong_usage;
		if(parameters.size()==2)
			{
			state.render.animation_manager.set_movement(*movement);
			host.full_redraw();
			out.print("smooth-movement: movement {}\n",movement->name());
			return command_outcomest::ok;
			}
		if(parameters.size()==3)
			{
			for(const movement_settingst &setting:movement->settings())
				if(setting.name==parameters[2])
					{
					out.print("movement {} {}: {}\n",movement->name(),setting.name,setting.value);
					return command_outcomest::ok;
					}
			out.printerr("{} has no setting named {}\n",movement->name(),parameters[2]);
			return command_outcomest::failed;
			}
		if(parameters.size()!=4)return command_outcomest::wrong_usage;
		const float value=parse_hop_value(parameters[3]);
		if(value<0.0f)return command_outcomest::wrong_usage;
		const std::string error=apply_movement_settings(*movement,{{parameters[2],value}});
		if(!error.empty())
			{
			out.printerr("{}\n",error);
			return command_outcomest::failed;
			}
		host.full_redraw();
		out.print("smooth-movement: movement {} {} {}\n",movement->name(),parameters[2],value);
		return command_outcomest::ok;
		}
	if(word=="timestep")
		{
		if(parameters.size()==1)
			{
			print_setting(out,state,word);
			return command_outcomest::ok;
			}
		if(parameters.size()==2)
			{
			const int32_t ms=parse_step_ms(parameters[1]);
			if(ms<0)return command_outcomest::wrong_usage;
			state.render.animation_manager.set_step_duration_ms(uint32_t(ms));
			out.print("smooth-movement: time step {} ms\n",ms);
			return command_outcomest::ok;
			}
		return command_outcomest::wrong_usage;
		}
	if(word=="hauled")
		{
		if(parameters.size()==1)
			{
			print_setting(out,state,word);
			return command_outcomest::ok;
			}
		bool on=false;
		if(!parse_on_off(parameters,on))return command_outcomest::wrong_usage;
		state.hauled_enabled=on;
		host.full_redraw();
		out.print("smooth-movement: hauled item icons {}\n",on_off(on));
		return command_outcomest::ok;
		}
	return command_outcomest::wrong_usage;
}
