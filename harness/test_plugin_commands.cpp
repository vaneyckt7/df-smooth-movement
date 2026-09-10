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

// The movement the plugin follows, by name, and one of the bob movement's settings, current
// or not.
bool follows(const plugin_statest &state,const char *name)
{
	return std::string(state.render.animation_manager.movement().name())==name;
}

float bob_setting(const plugin_statest &state,const char *name)
{
	for(const movement_settingst &setting:state.movements.find("bob")->settings())
		if(setting.name==name)return setting.value;
	return -1.0f;
}

void test_settings_printout()
{
	plugin_statest state;
	const runst r=run(state,{});
	expect_ok("bare command",r,
		"smooth-movement 9.9.9: enabled\n"
		"free camera: off, offset -0 -0 (tiles east/south of the grid)\n"
		"sprite flipping: off\n"
		"interpolation: smoothstep\n"
		"linear movement: off\n"
		"time step: 150 ms\n"
		"hauled item icons: off\n"
		"walk bob: off, amount 0.1\n"
		"bob multipliers: horizontal 1, diagonal 2.4, vertical 2.7\n"
		"hops per step: 2\n"
		"frame stats: off\n");
	const runst disabled=run(state,{},disabled_host);
	expect_true("bare command, plugin disabled",
		disabled.text.rfind("smooth-movement 9.9.9: disabled\n",0)==0);
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
	expect_ok("all on",run(state,{"all","on"}),"smooth-movement: flip, linear and hauled on\n",1);
	expect_true("all on sets flip",state.flip_enabled);
	expect_true("all on sets linear",follows(state,"linear"));
	expect_true("all on sets hauled",state.hauled_enabled);
	expect_ok("all off",run(state,{"all","off"}),
		"smooth-movement: flip, linear and hauled off\n",1);
	expect_true("all off clears flip",!state.flip_enabled);
	expect_true("all off clears linear",!(follows(state,"linear")));
	expect_true("all off clears hauled",!state.hauled_enabled);
	// `all on` picks linear whatever was current; `all off` goes back to the default from
	// linear and leaves another interpolation alone.
	run(state,{"bob","on"});
	expect_ok("all on from bob",run(state,{"all","on"}),
		"smooth-movement: flip, linear and hauled on\n",1);
	expect_true("all on from bob gives linear",follows(state,"linear"));
	run(state,{"bob","on"});
	expect_ok("all off from bob",run(state,{"all","off"}),
		"smooth-movement: flip, linear and hauled off\n",1);
	expect_true("all off leaves bob",follows(state,"bob"));
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

void test_flip_linear_hauled()
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

	expect_ok("linear",run(state,{"linear"}),"linear movement: off\n");
	expect_ok("linear on",run(state,{"linear","on"}),"smooth-movement: linear movement on\n");
	expect_true("linear on sets",follows(state,"linear"));
	expect_ok("linear printout",run(state,{"linear"}),"linear movement: on\n");
	expect_ok("linear off",run(state,{"linear","off"}),"smooth-movement: linear movement off\n");
	expect_true("linear off clears",!(follows(state,"linear")));
	expect_usage("linear bogus",run(state,{"linear","maybe"}));
	expect_usage("linear on extra",run(state,{"linear","on","now"}));

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

void test_interpolation()
{
	plugin_statest state;
	const auto current=[&]{return state.render.animation_manager.movement().name();};
	expect_ok("interpolation",run(state,{"interpolation"}),
		"interpolation: smoothstep\ninterpolations: smoothstep, linear, bob\n");
	expect_ok("interpolation bob",run(state,{"interpolation","bob"}),
		"smooth-movement: interpolation bob\n",1);
	expect_true("bob is set",std::string(current())=="bob");
	expect_ok("linear printout follows",run(state,{"linear"}),"linear movement: off\n");
	expect_ok("bob printout follows",run(state,{"bob"}),"walk bob: on, amount 0.1\n");
	// Each switch picks its interpolation with on; off goes back to the default from that
	// one and leaves any other alone.
	expect_ok("bob off",run(state,{"bob","off"}),"smooth-movement: walk bob off\n",1);
	expect_true("bob off gives smoothstep",std::string(current())=="smoothstep");
	expect_ok("bob on from smoothstep",run(state,{"bob","on"}),"smooth-movement: walk bob on\n",1);
	expect_true("bob on gives bob",std::string(current())=="bob");
	expect_ok("linear on from bob",run(state,{"linear","on"}),
		"smooth-movement: linear movement on\n");
	expect_true("linear on gives linear",std::string(current())=="linear");
	expect_ok("bob off from linear",run(state,{"bob","off"}),"smooth-movement: walk bob off\n",1);
	expect_true("bob off leaves linear",std::string(current())=="linear");
	expect_ok("bob on from linear",run(state,{"bob","on"}),"smooth-movement: walk bob on\n",1);
	expect_true("bob on gives bob again",std::string(current())=="bob");
	expect_ok("linear off from bob",run(state,{"linear","off"}),
		"smooth-movement: linear movement off\n");
	expect_true("linear off leaves bob",std::string(current())=="bob");
	expect_ok("interpolation smoothstep",run(state,{"interpolation","smoothstep"}),
		"smooth-movement: interpolation smoothstep\n",1);
	expect_true("smoothstep is the default",follows(state,"smoothstep"));
	expect_ok("interpolation printout follows",run(state,{"interpolation"}),
		"interpolation: smoothstep\ninterpolations: smoothstep, linear, bob\n");
	expect_usage("interpolation bogus",run(state,{"interpolation","bounce"}));
	expect_usage("interpolation extra",run(state,{"interpolation","linear","now"}));
	expect_usage("interpolation bob suffix",run(state,{"interpolation","smoothstep-bob"}));
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
	expect_ok("timestep with leading zeros",run(state,{"timestep","0300"}),
		"smooth-movement: time step 300 ms\n");
	expect_ok("timestep printout",run(state,{"timestep"}),"time step: 300 ms\n");
	expect_usage("timestep 19",run(state,{"timestep","19"}));
	expect_usage("timestep 2001",run(state,{"timestep","2001"}));
	expect_usage("timestep five digits",run(state,{"timestep","00300"}));
	expect_usage("timestep not a number",run(state,{"timestep","fast"}));
	expect_usage("timestep negative",run(state,{"timestep","-100"}));
	expect_usage("timestep empty",run(state,{"timestep",""}));
	expect_usage("timestep extra",run(state,{"timestep","100","ms"}));
	expect_true("refused timesteps leave it",
		state.render.animation_manager.step_duration_ms()==300);
}

void test_bob()
{
	plugin_statest state;
	expect_ok("bob",run(state,{"bob"}),"walk bob: off, amount 0.1\n");
	expect_ok("bob on",run(state,{"bob","on"}),"smooth-movement: walk bob on\n",1);
	expect_true("bob on sets",follows(state,"bob"));
	expect_ok("bob printout",run(state,{"bob"}),"walk bob: on, amount 0.1\n");
	expect_ok("bob off",run(state,{"bob","off"}),"smooth-movement: walk bob off\n",1);
	expect_true("bob off clears",!(follows(state,"bob")));
	expect_ok("bob amount",run(state,{"bob","0.25"}),"smooth-movement: bob amount 0.25\n",1);
	expect_near("bob amount sets",bob_setting(state,"amount"),0.25);
	expect_true("bob amount leaves it off",!(follows(state,"bob")));
	expect_ok("bob amount without a leading digit",run(state,{"bob",".2"}),
		"smooth-movement: bob amount 0.2\n",1);
	expect_near("bob amount without a leading digit sets",bob_setting(state,"amount"),0.2f);
	// 0.9 tile is the most the amount times the largest multiplier may lift: the default
	// vertical multiplier of 2.7 caps the amount at a third of a tile.
	expect_failed("bob amount lifting too far",run(state,{"bob","0.34"}),
		"bob settings reach 0.92 tile beyond the path; the most the plugin repaints is 0.90\n");
	expect_failed("bob amount zero",run(state,{"bob","0"}),
		"bob amount must be within 0..0.90 tile\n");
	expect_failed("bob amount over a tile",run(state,{"bob","0.91"}),
		"bob amount must be within 0..0.90 tile\n");
	expect_usage("bob amount two points",run(state,{"bob","0.1.2"}));
	expect_usage("bob amount only a point",run(state,{"bob","."}));
	expect_usage("bob amount negative",run(state,{"bob","-0.1"}));
	expect_usage("bob amount text",run(state,{"bob","high"}));
	expect_usage("bob amount too long",run(state,{"bob","0.1000000"}));
	expect_usage("bob extra",run(state,{"bob","on","now"}));
	expect_near("refused amounts leave it",bob_setting(state,"amount"),0.2f);

	expect_ok("bobmult",run(state,{"bobmult"}),
		"bob multipliers: horizontal 1, diagonal 2.4, vertical 2.7\n");
	expect_ok("bobmult set",run(state,{"bobmult","0.5","1.5","2"}),
		"smooth-movement: bob multipliers horizontal 0.5, diagonal 1.5, vertical 2\n");
	expect_near("bobmult horizontal",bob_setting(state,"horizontal"),0.5);
	expect_near("bobmult diagonal",bob_setting(state,"diagonal"),1.5);
	expect_near("bobmult vertical",bob_setting(state,"vertical"),2.0);
	// The amount is 0.2, so a multiplier of 5 would lift a tile; 0.15 lets the cap fit.
	expect_ok("bob amount for the cap",run(state,{"bob","0.15"}),
		"smooth-movement: bob amount 0.15\n",1);
	expect_ok("bobmult at the cap",run(state,{"bobmult","5","4.5","4"}),
		"smooth-movement: bob multipliers horizontal 5, diagonal 4.5, vertical 4\n");
	expect_ok("bob amount at the cap",run(state,{"bob","0.18"}),
		"smooth-movement: bob amount 0.18\n",1);
	expect_failed("bob amount past the cap",run(state,{"bob","0.19"}),
		"bob settings reach 0.95 tile beyond the path; the most the plugin repaints is 0.90\n");
	expect_failed("bobmult horizontal over the cap",run(state,{"bobmult","5.1","1","1"}),
		"bob multipliers must be within 0..5\n");
	expect_failed("bobmult diagonal over the cap",run(state,{"bobmult","1","6","1"}),
		"bob multipliers must be within 0..5\n");
	expect_failed("bobmult vertical over the cap",run(state,{"bobmult","1","1","5.5"}),
		"bob multipliers must be within 0..5\n");
	// At an amount of 0.2 a multiplier of 4.6 lifts 0.92 tile.
	expect_ok("bobmult one",run(state,{"bobmult","1","1","1"}),
		"smooth-movement: bob multipliers horizontal 1, diagonal 1, vertical 1\n");
	expect_ok("bob amount for the lift",run(state,{"bob","0.2"}),
		"smooth-movement: bob amount 0.2\n",1);
	expect_failed("bobmult lifting too far",run(state,{"bobmult","1","4.6","1"}),
		"bob settings reach 0.92 tile beyond the path; the most the plugin repaints is 0.90\n");
	expect_near("refused multipliers leave horizontal",bob_setting(state,"horizontal"),1.0);
	expect_near("refused multipliers leave diagonal",bob_setting(state,"diagonal"),1.0);
	expect_near("refused multipliers leave vertical",bob_setting(state,"vertical"),1.0);
	expect_usage("bobmult two values",run(state,{"bobmult","1","2"}));
	expect_usage("bobmult four values",run(state,{"bobmult","1","2","3","4"}));
	expect_usage("bobmult text",run(state,{"bobmult","1","two","3"}));
	expect_usage("bobmult negative",run(state,{"bobmult","-1","2","3"}));

	expect_ok("hops",run(state,{"hops"}),"hops per step: 2\n");
	expect_ok("hops 1",run(state,{"hops","1"}),"smooth-movement: hops per step 1\n");
	expect_true("hops 1 sets",bob_setting(state,"hops")==1.0f);
	expect_ok("hops printout",run(state,{"hops"}),"hops per step: 1\n");
	expect_ok("hops 2",run(state,{"hops","2"}),"smooth-movement: hops per step 2\n");
	expect_true("hops 2 sets",bob_setting(state,"hops")==2.0f);
	expect_usage("hops 3",run(state,{"hops","3"}));
	expect_usage("hops 0",run(state,{"hops","0"}));
	expect_usage("hops text",run(state,{"hops","two"}));
	expect_usage("hops extra",run(state,{"hops","1","2"}));
	expect_true("refused hops leave it",bob_setting(state,"hops")==2.0f);
}

// The settings go out as one value and come back the same, with each landing where its
// owner reads it. The four switches are set to a pattern and then its complement, so two of
// them swapped would show. The camera's rest offset survives its switch, which zeroes the
// offset when it changes.
void test_settings_round_trip()
{
	plugin_statest state;
	plugin_settingsst s;
	s.flip=true;s.hauled=false;s.camera=true;s.rest_x=-0.25;s.rest_y=0.5;s.movement="bob";
	s.step_ms=400;
	const std::vector<movement_settingst> bob_settings={{"amount",0.2f},{"horizontal",1.1f},
		{"diagonal",1.2f},{"vertical",1.3f},{"hops",1.0f}};
	s.movement_settings=bob_settings;
	expect_true("apply accepts",state.apply_settings(s).empty());
	expect_true("apply sets flip",state.flip_enabled);
	expect_true("apply leaves hauled",!state.hauled_enabled);
	expect_true("apply turns the camera on",state.render.camera.is_enabled());
	expect_near("apply sets the rest x",state.render.camera.rest_offset_x(),-0.25);
	expect_near("apply sets the rest y",state.render.camera.rest_offset_y(),0.5);
	expect_true("apply leaves linear",!(follows(state,"linear")));
	expect_true("apply sets the step",state.render.animation_manager.step_duration_ms()==400);
	expect_true("apply sets the interpolation",
		follows(state,"bob"));
	expect_true("apply sets the bob",bob_setting(state,"hops")==1.0f);
	expect_near("apply sets the bob amount",bob_setting(state,"amount"),0.2f);
	expect_near("apply sets the bob multipliers",bob_setting(state,"vertical"),1.3f);
	const plugin_settingsst back=state.settings();
	expect_true("settings read back the switches",
		back.flip&&!back.hauled&&back.camera&&back.movement=="bob"&&
		back.step_ms==400);
	expect_near("settings read back the rest x",back.rest_x,-0.25);
	expect_near("settings read back the rest y",back.rest_y,0.5);
	expect_true("settings read back the bob",back.movement_settings==bob_settings);
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
	// default movement and leave the bob's own settings as they are.
	expect_true("defaults restore the movement",
		!follows(state,"bob")&&bob_setting(state,"hops")==1.0f);
	// Settings that name a movement the plugin lacks, or values the movement refuses, are
	// reported and leave the plugin on the default movement with the bob's settings as
	// they were; the switches still apply.
	plugin_settingsst unknown=s;unknown.movement="bounce";unknown.movement_settings.clear();
	expect_true("an unknown movement is reported",
		state.apply_settings(unknown)=="unknown movement bounce");
	expect_true("an unknown movement gives the default",follows(state,"smoothstep")&&state.flip_enabled);
	plugin_settingsst refused=s;refused.movement_settings={{"amount",0.95f}};
	expect_true("refused settings are reported",!state.apply_settings(refused).empty());
	expect_true("refused settings leave the bob",follows(state,"bob")&&
		bob_setting(state,"amount")==0.2f&&bob_setting(state,"hops")==1.0f);
	// A frame's list is the movement's whole state: a setting the list leaves out is back at
	// its default, as the recording reader checked the list.
	plugin_settingsst partial=s;partial.movement_settings={{"amount",0.3f}};
	expect_true("a partial list is accepted",state.apply_settings(partial).empty());
	expect_true("a partial list resets the rest",follows(state,"bob")&&
		bob_setting(state,"amount")==0.3f&&bob_setting(state,"hops")==2.0f);
}

void test_reset()
{
	plugin_statest state;
	run(state,{"all","on"});
	run(state,{"camera","0.25","0"});
	run(state,{"timestep","400"});
	run(state,{"bob","0.2"});
	run(state,{"bobmult","1","1","1"});
	run(state,{"hops","1"});
	run(state,{"stats","on"});
	state.reset();
	expect_true("reset clears flip",!state.flip_enabled);
	expect_true("reset clears hauled",!state.hauled_enabled);
	// The interpolation survives a reset, `bob` included: the plugin has kept it across
	// disable and enable since the setting was added (as the linear switch then).
	expect_true("reset keeps linear",
		follows(state,"linear"));
	run(state,{"interpolation","bob"});
	state.reset();
	expect_true("reset keeps the bob interpolation",
		follows(state,"bob"));
	expect_true("reset restores the step time",
		state.render.animation_manager.step_duration_ms()==150);
	expect_true("reset turns the camera off",!state.render.camera.is_enabled());
	expect_near("reset zeroes the camera offset",state.render.camera.rest_offset_x(),0.0);
	expect_near("reset restores the bob amount",bob_setting(state,"amount"),0.1f);
	expect_near("reset restores the bob multipliers",bob_setting(state,"diagonal"),2.4f);
	expect_true("reset restores the hops",bob_setting(state,"hops")==2.0f);
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
	test_flip_linear_hauled();
	test_interpolation();
	test_timestep();
	test_bob();
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
