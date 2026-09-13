// SPDX-License-Identifier: MIT
// Checks the console command grammar in plugin_commands.h against a plugin state and a
// stand-in host: which words each command accepts, what each sets, what it prints, when it
// asks the game for a full redraw. The recordings never run a command, so the grammar is
// pinned here. The refused names below live under harness/out so a refusal that breaks
// leaves its file where the harness ignores it.

#include "plugin_commands.h"

#include <cmath>
#include <cstdio>
#include <sstream>
#include <string>
#include <vector>

namespace {

int failures=0;

// A stand-in for DFHack's console: every `{...}` takes the next argument, its spec ignored.
struct outputst
{
	std::string text;
	std::string errors;

	template<typename... Args>
	void print(const char *format,const Args &... args)
		{
		text+=render(format,{str(args)...});
		}
	template<typename... Args>
	void printerr(const char *format,const Args &... args)
		{
		errors+=render(format,{str(args)...});
		}
	template<typename T>
	static std::string str(const T &value)
		{
		std::ostringstream stream;
		stream<<value;
		return stream.str();
		}
	static std::string render(const char *format,std::vector<std::string> args)
		{
		std::string out;
		size_t next=0;
		for(const char *c=format;*c!='\0';++c)
			{
			if(*c!='{'){out+=*c;continue;}
			while(*c!='\0'&&*c!='}')++c;
			out+=next<args.size()?args[next++]:"?";
			}
		return out;
		}
};

int redraws=0;
int scrolls=0;
int32_t scrolled_x=0,scrolled_y=0;

void count_redraw(){++redraws;}

std::array<bool,2> count_scroll(int32_t kx,int32_t ky)
{
	++scrolls;
	scrolled_x+=kx;
	scrolled_y+=ky;
	return {kx!=0,ky!=0};
}

const command_hostst enabled_host={"9.9.9",true,count_redraw,count_scroll};
const command_hostst disabled_host={"9.9.9",false,count_redraw,count_scroll};

struct runst
{
	command_outcomest outcome;
	std::string text;
	std::string errors;
	int redraws;
};

runst run(plugin_statest &state,std::vector<std::string> words,
	const command_hostst &host=enabled_host)
{
	outputst out;
	const int before=redraws;
	const command_outcomest outcome=run_command(out,words,state,host);
	return {outcome,out.text,out.errors,redraws-before};
}

const char *name(command_outcomest outcome)
{
	switch(outcome)
		{
		case command_outcomest::ok:return "ok";
		case command_outcomest::failed:return "failed";
		case command_outcomest::wrong_usage:return "wrong usage";
		}
	return "?";
}

void expect_outcome(const char *what,const runst &r,command_outcomest outcome)
{
	if(r.outcome!=outcome)
		printf("%s: outcome %s, expected %s\n",what,name(r.outcome),name(outcome)),++failures;
}

void expect_ok(const char *what,const runst &r,const char *text,int redraws=0)
{
	expect_outcome(what,r,command_outcomest::ok);
	if(r.text!=text)
		printf("%s: printed \"%s\", expected \"%s\"\n",what,r.text.c_str(),text),++failures;
	if(!r.errors.empty())
		printf("%s: printed error \"%s\"\n",what,r.errors.c_str()),++failures;
	if(r.redraws!=redraws)
		printf("%s: %d redraws, expected %d\n",what,r.redraws,redraws),++failures;
}

void expect_failed(const char *what,const runst &r,const char *error)
{
	expect_outcome(what,r,command_outcomest::failed);
	if(r.errors!=error)
		printf("%s: error \"%s\", expected \"%s\"\n",what,r.errors.c_str(),error),++failures;
	if(r.redraws!=0)
		printf("%s: %d redraws, expected none\n",what,r.redraws),++failures;
}

void expect_usage(const char *what,const runst &r)
{
	expect_outcome(what,r,command_outcomest::wrong_usage);
	if(!r.text.empty()||!r.errors.empty())
		printf("%s: printed \"%s%s\", expected nothing\n",what,r.text.c_str(),
			r.errors.c_str()),++failures;
	if(r.redraws!=0)
		printf("%s: %d redraws, expected none\n",what,r.redraws),++failures;
}

void expect_true(const char *what,bool condition)
{
	if(!condition)printf("%s\n",what),++failures;
}

void expect_near(const char *what,double value,double expected)
{
	if(std::fabs(value-expected)>1e-6)
		printf("%s: %g, expected %g\n",what,value,expected),++failures;
}

// The movement the plugin follows, by name, and one of the hop movement's settings, current
// or not.
bool follows(const plugin_statest &state,const char *name)
{
	return std::string(state.render.animation_manager.movement().name())==name;
}

float hop_setting(const plugin_statest &state,const char *name)
{
	for(const movement_settingst &setting:state.movements.find("hop")->settings())
		if(setting.name==name)return setting.value;
	return -1.0f;
}

void test_settings_printout()
{
	plugin_statest state;
	const runst r=run(state,{"status"});
	expect_ok("status",r,
		"smooth-movement 9.9.9: enabled\n"
		"free camera: off, offset -0 -0 (tiles east/south of the grid)\n"
		"sprite flipping: off\n"
		"movement: none\n"
		"time step: 150 ms\n"
		"hauled item icons: off\n"
		"frame stats: off\n");
	const runst disabled=run(state,{"status"},disabled_host);
	expect_true("status, plugin disabled",
		disabled.text.rfind("smooth-movement 9.9.9: disabled\n",0)==0);
	expect_usage("bare command",run(state,{}));
	expect_usage("status extra",run(state,{"status","now"}));
	expect_usage("unknown word",run(state,{"bogus"}));
	expect_usage("unknown word with argument",run(state,{"bogus","on"}));
}

void test_stats()
{
	plugin_statest state;
	state.stats.frames=5;
	expect_ok("stats on",run(state,{"stats","on"}),"smooth-movement: frame stats on\n");
	expect_true("stats on enables",state.stats.enabled);
	expect_true("stats on clears the counters",state.stats.frames==0);
	state.stats.frames=7;
	expect_ok("stats off",run(state,{"stats","off"}),"smooth-movement: frame stats off\n");
	expect_true("stats off disables",!state.stats.enabled);
	expect_true("stats off keeps the counters",state.stats.frames==7);
	expect_ok("stats reset",run(state,{"stats","reset"}),"smooth-movement: frame stats reset\n");
	expect_true("stats reset clears the counters",state.stats.frames==0);
	state.stats.frames=3;
	const runst printed=run(state,{"stats"});
	expect_outcome("stats",printed,command_outcomest::ok);
	expect_true("stats prints the counters",printed.text.find("3")!=std::string::npos);
	expect_usage("stats bogus",run(state,{"stats","bogus"}));
	expect_usage("stats on extra",run(state,{"stats","on","now"}));
}

void test_record()
{
	plugin_statest state;
	const std::string refused="harness/out/test-commands-refused.rec";
	expect_usage("record alone",run(state,{"record"}));
	expect_usage("record status extra",run(state,{"record","status","now"}));
	expect_usage("record stop extra",run(state,{"record","stop","now"}));
	expect_usage("record too many words",run(state,{"record",refused,"10","x"}));
	expect_usage("record count not a number",run(state,{"record",refused,"ten"}));
	expect_usage("record count zero",run(state,{"record",refused,"0"}));
	expect_usage("record count too long",run(state,{"record",refused,"1000000000"}));
	expect_usage("record count empty",run(state,{"record",refused,""}));
	expect_failed("record while disabled",run(state,{"record",refused},disabled_host),
		"smooth-movement: enable the plugin before recording\n");
	expect_true("nothing recording after the refusals",!state.recorder.running());
	expect_usage("record too many words while disabled",
		run(state,{"record",refused,"10","x"},disabled_host));
	expect_ok("record status while disabled",run(state,{"record","status"},disabled_host),
		"recording: off\n");
	expect_ok("record stop while disabled",run(state,{"record","stop"},disabled_host),
		"smooth-movement: recording stopped\n");
	expect_failed("record to a directory",run(state,{"record","/"}),
		"smooth-movement: cannot write /\n");
	const std::string file="harness/out/test-commands.rec";
	expect_ok("record",run(state,{"record",file,"12"}),
		"smooth-movement: recording 12 frames to harness/out/test-commands.rec\n");
	expect_true("record starts",state.recorder.running());
	expect_failed("record while recording",run(state,{"record",refused}),
		"smooth-movement: a recording is running; `record stop` ends it\n");
	const runst status=run(state,{"record","status"});
	expect_outcome("record status",status,command_outcomest::ok);
	expect_true("record status prints the file",
		status.text.find("test-commands.rec")!=std::string::npos);
	expect_ok("record stop",run(state,{"record","stop"}),"smooth-movement: recording stopped\n");
	expect_true("record stop stops",!state.recorder.running());
	expect_ok("record default count",run(state,{"record",file}),
		"smooth-movement: recording 900 frames to harness/out/test-commands.rec\n");
	state.recorder.stop();
	std::remove(file.c_str());
}

void test_all()
{
	plugin_statest state;
	expect_ok("all on",run(state,{"all","on"}),"smooth-movement: flip and hauled on\n",1);
	expect_true("all on sets flip",state.flip_enabled);
	expect_true("all on sets hauled",state.hauled_enabled);
	expect_ok("all off",run(state,{"all","off"}),
		"smooth-movement: flip and hauled off\n",1);
	expect_true("all off clears flip",!state.flip_enabled);
	expect_true("all off clears hauled",!state.hauled_enabled);
	expect_usage("all alone",run(state,{"all"}));
	expect_usage("all bogus",run(state,{"all","maybe"}));
	expect_usage("all on extra",run(state,{"all","on","now"}));
}

void test_camera()
{
	plugin_statest state;
	expect_ok("camera",run(state,{"camera"}),"free camera: off, offset -0 -0\n");
	expect_ok("camera on",run(state,{"camera","on"}),"");
	expect_true("camera on enables",state.render.camera.is_enabled());
	expect_ok("camera off",run(state,{"camera","off"}),"");
	expect_true("camera off disables",!state.render.camera.is_enabled());
	scrolls=0;scrolled_x=0;scrolled_y=0;
	expect_ok("camera offset",run(state,{"camera","0.25","-0.125"}),"");
	expect_true("camera offset enables",state.render.camera.is_enabled());
	// The user's east/south offset is the camera's negative rest.
	expect_near("camera offset x",state.render.camera.rest_offset_x(),-0.25);
	expect_near("camera offset y",state.render.camera.rest_offset_y(),0.125);
	expect_true("camera offset normalizes through the host",scrolls==1);
	expect_true("camera offset within half a tile scrolls nothing",
		scrolled_x==0&&scrolled_y==0);
	expect_ok("camera offset past half a tile",run(state,{"camera","0.75","0"}),"");
	expect_true("camera offset past half a tile scrolls a tile",scrolled_x==1&&scrolled_y==0);
	// The printout is against the window as written, which the last command moved a tile.
	expect_ok("camera printout",run(state,{"camera"}),"free camera: on, offset -0.25 -0\n");
	expect_ok("camera reset",run(state,{"camera","reset"}),"");
	// No frame runs here, so the tile the previous command wrote has not landed: rest carries
	// it until the landing, and the offset against the written window is zero.
	expect_near("camera reset x",state.render.camera.rest_offset_x(),-1.0);
	expect_near("camera reset y",state.render.camera.rest_offset_y(),0.0);
	expect_true("camera reset keeps it on",state.render.camera.is_enabled());
	expect_failed("camera offset too far east",run(state,{"camera","1","0"}),
		"offsets must be within -0.99..0.99 tiles\n");
	expect_failed("camera offset too far north",run(state,{"camera","0","-1.5"}),
		"offsets must be within -0.99..0.99 tiles\n");
	expect_near("refused offset leaves x",state.render.camera.rest_offset_x(),-1.0);
	expect_usage("camera offset not a number",run(state,{"camera","east","0"}));
	expect_usage("camera bogus",run(state,{"camera","sideways"}));
	expect_usage("camera one number",run(state,{"camera","0.5","0","0"}));
}

void test_flip_hauled()
{
	plugin_statest state;
	expect_ok("flip",run(state,{"flip"}),"sprite flipping: off\n");
	expect_ok("flip on",run(state,{"flip","on"}),"smooth-movement: sprite flipping enabled\n",1);
	expect_true("flip on sets",state.flip_enabled);
	expect_ok("flip printout",run(state,{"flip"}),"sprite flipping: on\n");
	expect_ok("flip off",run(state,{"flip","off"}),
		"smooth-movement: sprite flipping disabled\n",1);
	expect_true("flip off clears",!state.flip_enabled);
	expect_usage("flip bogus",run(state,{"flip","maybe"}));
	expect_usage("flip on extra",run(state,{"flip","on","now"}));

	expect_ok("hauled",run(state,{"hauled"}),"hauled item icons: off\n");
	expect_ok("hauled on",run(state,{"hauled","on"}),"smooth-movement: hauled item icons on\n",1);
	expect_true("hauled on sets",state.hauled_enabled);
	expect_ok("hauled printout",run(state,{"hauled"}),"hauled item icons: on\n");
	expect_ok("hauled off",run(state,{"hauled","off"}),
		"smooth-movement: hauled item icons off\n",1);
	expect_true("hauled off clears",!state.hauled_enabled);
	expect_usage("hauled bogus",run(state,{"hauled","maybe"}));
	expect_usage("hauled on extra",run(state,{"hauled","on","now"}));
}

void test_movement()
{
	plugin_statest state;
	const auto current=[&]{return state.render.animation_manager.movement().name();};
	expect_ok("movement",run(state,{"movement"}),
		"movement: none\nmovements: none, smoothstep, linear, hop\n");
	expect_ok("movement hop",run(state,{"movement","hop"}),
		"smooth-movement: movement hop\n",1);
	expect_true("hop is set",std::string(current())=="hop");
	expect_ok("movement smoothstep",run(state,{"movement","smoothstep"}),
		"smooth-movement: movement smoothstep\n",1);
	expect_true("smoothstep is the default",follows(state,"smoothstep"));
	expect_ok("hop height",run(state,{"movement","hop","hop-height","0.25"}),
		"smooth-movement: movement hop hop-height 0.25\n",1);
	expect_near("hop height sets",hop_setting(state,"hop-height"),0.25);
	expect_true("setting does not select",follows(state,"smoothstep"));
	expect_ok("hop height printout",run(state,{"movement","hop","hop-height"}),
		"movement hop hop-height: 0.25\n");
	expect_ok("horizontal multiplier",run(state,{"movement","hop","horizontal-mult","0.5"}),
		"smooth-movement: movement hop horizontal-mult 0.5\n",1);
	expect_near("horizontal multiplier sets",hop_setting(state,"horizontal-mult"),0.5);
	expect_ok("hops per step",run(state,{"movement","hop","hops-per-step","1"}),
		"smooth-movement: movement hop hops-per-step 1\n",1);
	expect_true("hops per step sets",hop_setting(state,"hops-per-step")==1.0f);
	expect_failed("hop height zero",run(state,{"movement","hop","hop-height","0"}),
		"hop height must be within (0, 1] tiles\n");
	expect_failed("multiplier over the limit",run(state,{"movement","hop","vertical-mult","5.5"}),
		"hop multipliers must be within 0..5\n");
	expect_usage("unknown movement",run(state,{"movement","bounce"}));
	expect_usage("unknown setting",run(state,{"movement","hop","bounce"}));
	expect_usage("setting without value",run(state,{"movement","hop","hop-height","0","extra"}));
	expect_usage("negative setting",run(state,{"movement","hop","hop-height","-0.1"}));
}

void test_timestep()
{
	plugin_statest state;
	expect_ok("timestep",run(state,{"timestep"}),"time step: 150 ms\n");
	expect_ok("timestep 20",run(state,{"timestep","20"}),"smooth-movement: time step 20 ms\n");
	expect_true("timestep 20 sets",state.render.animation_manager.step_duration_ms()==20);
	expect_ok("timestep 2000",run(state,{"timestep","2000"}),
		"smooth-movement: time step 2000 ms\n");
	expect_true("timestep 2000 sets",state.render.animation_manager.step_duration_ms()==2000);
	expect_usage("timestep too short",run(state,{"timestep","19"}));
	expect_usage("timestep too long",run(state,{"timestep","2001"}));
	expect_usage("timestep text",run(state,{"timestep","fast"}));
}

// The settings go out as one value and come back the same, with each landing where its
// owner reads it. The four switches are set to a pattern and then its complement, so two of
// them swapped would show. The camera's rest offset survives its switch, which zeroes the
// offset when it changes.
void test_settings_round_trip()
{
	plugin_statest state;
	plugin_settingsst s;
	s.flip=true;s.hauled=false;s.camera=true;s.rest_x=-0.25;s.rest_y=0.5;s.movement="hop";
	s.step_ms=400;
	const std::vector<movement_settingst> hop_settings={{"hop-height",0.2f},{"horizontal-mult",1.1f},
		{"diagonal-mult",1.2f},{"vertical-mult",1.3f},{"hops-per-step",1.0f}};
	s.movement_settings=hop_settings;
	expect_true("apply accepts",state.apply_settings(s).empty());
	expect_true("apply sets flip",state.flip_enabled);
	expect_true("apply leaves hauled",!state.hauled_enabled);
	expect_true("apply turns the camera on",state.render.camera.is_enabled());
	expect_near("apply sets the rest x",state.render.camera.rest_offset_x(),-0.25);
	expect_near("apply sets the rest y",state.render.camera.rest_offset_y(),0.5);
	expect_true("apply leaves linear",!(follows(state,"linear")));
	expect_true("apply sets the step",state.render.animation_manager.step_duration_ms()==400);
	expect_true("apply sets the movement",
		follows(state,"hop"));
	expect_true("apply sets the hop",hop_setting(state,"hops-per-step")==1.0f);
	expect_near("apply sets the hop height",hop_setting(state,"hop-height"),0.2f);
	expect_near("apply sets the hop multipliers",hop_setting(state,"vertical-mult"),1.3f);
	const plugin_settingsst back=state.settings();
	expect_true("settings read back the switches",
		back.flip&&!back.hauled&&back.camera&&back.movement=="hop"&&
		back.step_ms==400);
	expect_near("settings read back the rest x",back.rest_x,-0.25);
	expect_near("settings read back the rest y",back.rest_y,0.5);
	expect_true("settings read back the hop",back.movement_settings==hop_settings);
	plugin_settingsst other;
	other.flip=false;other.hauled=true;other.camera=false;other.movement="linear";
	expect_true("the complement is accepted",state.apply_settings(other).empty());
	// A movement without settings reads back with none.
	expect_true("linear reads back without settings",state.settings().movement_settings.empty());
	expect_true("the complement applies",
		!state.flip_enabled&&state.hauled_enabled&&!state.render.camera.is_enabled()&&
		follows(state,"linear"));
	const plugin_settingsst back2=state.settings();
	expect_true("the complement reads back",
		!back2.flip&&back2.hauled&&!back2.camera&&back2.movement=="linear");
	expect_true("the camera off reads back at rest zero",back2.rest_x==0.0&&back2.rest_y==0.0);
	state.apply_settings(s);
	// The commands and the value agree: what `camera 0.25 0` set reads back as the camera's
	// own rest offset, negated by the command.
	run(state,{"camera","0.25","0"});
	expect_near("settings read the command's offset",state.settings().rest_x,-0.25);
	// The same value again changes nothing, and the camera keeps its offset: the switch
	// does not change.
	state.apply_settings(state.settings());
	expect_near("re-applying keeps the rest x",state.render.camera.rest_offset_x(),-0.25);
	// Defaults put everything back, the camera off with its offset at zero.
	state.apply_settings(plugin_settingsst{});
	expect_true("defaults clear the switches",
		!state.flip_enabled&&!state.hauled_enabled&&!state.render.camera.is_enabled()&&
		!(follows(state,"linear")));
	expect_true("defaults restore the step",
		state.render.animation_manager.step_duration_ms()==150);
	expect_near("defaults zero the rest x",state.render.camera.rest_offset_x(),0.0);
	// The settings carry the current movement's settings only: the defaults name the
	// default movement and leave the hop's own settings as they are.
	expect_true("defaults restore the movement",
		!follows(state,"hop")&&hop_setting(state,"hops-per-step")==1.0f);
	// Settings that name a movement the plugin lacks, or values the movement refuses, are
	// reported and leave the plugin on the default movement with the hop's settings as
	// they were; the switches still apply.
	plugin_settingsst unknown=s;unknown.movement="bounce";unknown.movement_settings.clear();
	expect_true("an unknown movement is reported",
		state.apply_settings(unknown)=="unknown movement bounce");
	expect_true("an unknown movement gives the default",follows(state,"none")&&state.flip_enabled);
	plugin_settingsst refused=s;refused.movement_settings={{"hop-height",1.5f}};
	expect_true("refused settings are reported",!state.apply_settings(refused).empty());
	expect_true("refused settings leave the hop",follows(state,"hop")&&
		hop_setting(state,"hop-height")==0.2f&&hop_setting(state,"hops-per-step")==1.0f);
	// A frame's list is the movement's whole state: a setting the list leaves out is back at
	// its default, as the recording reader checked the list.
	plugin_settingsst partial=s;partial.movement_settings={{"hop-height",0.3f}};
	expect_true("a partial list is accepted",state.apply_settings(partial).empty());
	expect_true("a partial list resets the rest",follows(state,"hop")&&
		hop_setting(state,"hop-height")==0.3f&&hop_setting(state,"hops-per-step")==2.0f);
}

void test_reset()
{
	plugin_statest state;
	run(state,{"all","on"});
	run(state,{"camera","0.25","0"});
	run(state,{"timestep","400"});
	run(state,{"movement","hop","hop-height","0.2"});
	run(state,{"movement","hop","horizontal-mult","1"});
	run(state,{"movement","hop","diagonal-mult","1"});
	run(state,{"movement","hop","vertical-mult","1"});
	run(state,{"movement","hop","hops-per-step","1"});
	run(state,{"stats","on"});
	state.reset();
	expect_true("reset clears flip",!state.flip_enabled);
	expect_true("reset clears hauled",!state.hauled_enabled);
	state.render.animation_manager.set_movement(*state.movements.find("linear"));
	expect_true("reset keeps linear",follows(state,"linear"));
	run(state,{"movement","hop"});
	state.reset();
	expect_true("reset keeps the hop movement",follows(state,"hop"));
	expect_true("reset restores the step time",
		state.render.animation_manager.step_duration_ms()==150);
	expect_true("reset turns the camera off",!state.render.camera.is_enabled());
	expect_near("reset zeroes the camera offset",state.render.camera.rest_offset_x(),0.0);
	expect_near("reset restores the hop height",hop_setting(state,"hop-height"),0.1f);
	expect_near("reset restores the hop multipliers",hop_setting(state,"diagonal-mult"),2.4f);
	expect_true("reset restores the hops",hop_setting(state,"hops-per-step")==2.0f);
	expect_true("reset turns the stats off",!state.stats.enabled);
}

} // namespace

int main()
{
	test_settings_printout();
	test_stats();
	test_record();
	test_all();
	test_camera();
	test_flip_hauled();
	test_movement();
	test_timestep();
	test_settings_round_trip();
	test_reset();
	if(failures!=0)
		{
		printf("plugin command tests: %d FAILED\n",failures);
		return 1;
		}
	printf("plugin command tests: OK\n");
	return 0;
}
