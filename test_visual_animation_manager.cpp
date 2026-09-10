// SPDX-License-Identifier: MIT

#include <array>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <tuple>
#include <limits>

#include "visual_animation.h"

namespace {

// The fraction of its step a render has travelled, read back from the offset by the step's
// tile delta: what the movement's time progress was, for a movement with constant speed.
float travelled(const visual_movement_renderst &render,int32_t target_x,int32_t target_y)
{
	const float dx=float(target_x)-render.source_x,dy=float(target_y)-render.source_y;
	return dx!=0.0f?render.offset_x/dx:render.offset_y/dy;
}

viewport_visual_animation_inputst make_input(
	const void *viewport,
	int32_t dimension,
	const int32_t *empty)
{
	viewport_visual_animation_inputst input;
	input.viewport=viewport;
	input.dim_x=dimension;
	input.dim_y=dimension;
	input.context_revision=1;
	input.current.fill(empty);
	input.previous.fill(empty);
	return input;
}

void set_layer(
	viewport_visual_animation_inputst &input,
	viewport_visual_layer layer,
	const int32_t *current,
	const int32_t *previous)
{
	const size_t index=static_cast<size_t>(layer);
	input.current[index]=current;
	input.previous[index]=previous;
}

template<size_t N>
void fill_background(std::array<int32_t,N> &background,int32_t seed)
{
	for(size_t i=0;i<N;++i)background[i]=seed+int32_t(i);
}

template<size_t N>
void shift_background(
	std::array<int32_t,N> &current,
	const std::array<int32_t,N> &previous,
	int32_t dimension,
	int32_t dx,
	int32_t dy,
	int32_t exposed_seed)
{
	for(int32_t x=0;x<dimension;++x)
		for(int32_t y=0;y<dimension;++y)
			{
			const int32_t sx=x+dx;
			const int32_t sy=y+dy;
			const size_t index=size_t(x*dimension+y);
			current[index]=sx>=0&&sx<dimension&&sy>=0&&sy<dimension?
				previous[size_t(sx*dimension+sy)]:exposed_seed+int32_t(index);
			}
}

void run_frame(
	visual_animation_managerst &manager,
	const viewport_visual_animation_inputst &input,
	uint32_t now_ms)
{
	manager.begin_frame(now_ms);
	manager.synchronize_viewport(input);
	manager.end_frame();
}

} // namespace

int main()
{
	visual_animation_managerst manager;
	manager.begin_frame(1000);
	assert(manager.get_frame_time_ms()==1000);
	assert(manager.get_frame_delta_ms()==0);

	manager.begin_frame(1020);
	assert(manager.get_frame_time_ms()==1020);
	assert(manager.get_frame_delta_ms()==20);

	manager.begin_frame(1020);
	assert(manager.get_frame_delta_ms()==0);

	visual_animation_managerst rollover;
	rollover.begin_frame(std::numeric_limits<uint32_t>::max()-5);
	rollover.begin_frame(3);
	assert(rollover.get_frame_delta_ms()==9);
	// Facing rule: only a horizontal component changes facing.
	assert(facing_after_move(1,visual_facingst::west)==visual_facingst::east);
	assert(facing_after_move(-1,visual_facingst::east)==visual_facingst::west);
	// dy is not an input, so a diagonal is only ever the sign of dx.
	// The four real diagonals run end to end through the manager further down.
	assert(facing_after_move(1,visual_facingst::east)==visual_facingst::east);
	assert(facing_after_move(-1,visual_facingst::west)==visual_facingst::west);
	// Pure vertical and idle carry the previous facing (sticky).
	assert(facing_after_move(0,visual_facingst::west)==visual_facingst::west);
	assert(facing_after_move(0,visual_facingst::east)==visual_facingst::east);

	assert(mirrored_tile_x(5,5)==5);   // anchor reflects to itself
	assert(mirrored_tile_x(6,5)==4);   // right spill -> left
	assert(mirrored_tile_x(4,5)==6);   // left spill -> right
	// The formula is a general reflection, so it holds for offsets no layer can express.
	assert(mirrored_tile_x(8,5)==2);
	assert(!camera_glide_enabled(false,false));
	assert(camera_glide_enabled(false,true));
	assert(camera_glide_enabled(true,false));
	assert(native_follow_changed(-1,42));
	assert(!native_follow_changed(42,42));
	assert(native_follow_changed(42,-1));
	// center_x is only ever -1, 0 or +1, so the real mirror shift is only ever -2, 0 or +2.
	for(const auto &descriptor:visual_layer_descriptors)
		assert(descriptor.center_x>=-1&&descriptor.center_x<=1);
	// Reflection is self-inverse.
	assert(mirrored_tile_x(mirrored_tile_x(6,5),5)==6);
	assert(mirrored_tile_x(mirrored_tile_x(8,5),5)==8);

	{
	constexpr int32_t dim=4;
	int32_t empty[dim*dim]={};
	int32_t before[dim*dim]={};
	int32_t west_after[dim*dim]={};
	const int viewport_token=0;
	const void *viewport=&viewport_token;

	before[2*dim+2]=77;
	west_after[1*dim+2]=77;   // moved west: x 2 -> 1

	// Moving west sets west facing on the target tile.
	{
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,before,empty);
	run_frame(manager,input,1000);
	set_layer(input,viewport_visual_layer::center,west_after,before);
	run_frame(manager,input,1016);
	assert(manager.get_facing(viewport,1,2)==visual_facingst::west);
	}

	// Moving east sets east facing.
	// East is neither the grid default nor the source facing, so the assertion is not vacuous.
	{
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,before,empty);
	run_frame(manager,input,1000);
	set_layer(input,viewport_visual_layer::center,west_after,before);
	run_frame(manager,input,1016);
	assert(manager.get_facing(viewport,1,2)==visual_facingst::west);
	set_layer(input,viewport_visual_layer::center,before,west_after);
	run_frame(manager,input,1032);
	assert(manager.get_facing(viewport,2,2)==visual_facingst::east);
	assert(manager.get_movement(
		viewport,viewport_visual_layer::center,2,2).active);
	manager.cancel_transitions();
	assert(!manager.get_movement(
		viewport,viewport_visual_layer::center,2,2).active);
	assert(manager.get_facing(viewport,2,2)==visual_facingst::east);
	}

	// The mirrored flag gates the render path's early return.
	// It must rise only for a genuinely mirrored creature and fall when that tile empties.
	{
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,before,empty);
	run_frame(manager,input,1000);
	assert(!manager.has_mirrored_facing(viewport));
	// Moving west matches the art, so nothing is mirrored yet.
	set_layer(input,viewport_visual_layer::center,west_after,before);
	run_frame(manager,input,1016);
	assert(manager.get_facing(viewport,1,2)==visual_facingst::west);
	assert(!manager.has_mirrored_facing(viewport));
	// Moving east faces away from the art and raises the flag.
	set_layer(input,viewport_visual_layer::center,before,west_after);
	run_frame(manager,input,1032);
	assert(manager.get_facing(viewport,2,2)==visual_facingst::east);
	assert(manager.has_mirrored_facing(viewport));
	// The creature leaves: the tile clears and so does the flag.
	set_layer(input,viewport_visual_layer::center,empty,before);
	run_frame(manager,input,1048);
	assert(manager.get_facing(viewport,2,2)==native_sprite_facing);
	assert(!manager.has_mirrored_facing(viewport));
	assert(!manager.has_mirrored_facing(nullptr));
	}

	// Pure vertical movement carries the existing facing to the new tile.
	{
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,before,empty);
	run_frame(manager,input,1000);
	set_layer(input,viewport_visual_layer::center,west_after,before);
	run_frame(manager,input,1016);
	assert(manager.get_facing(viewport,1,2)==visual_facingst::west);
	int32_t up[dim*dim]={};
	up[1*dim+1]=77;   // north: (1,2) -> (1,1), no horizontal component
	set_layer(input,viewport_visual_layer::center,up,west_after);
	run_frame(manager,input,1032);
	assert(manager.get_facing(viewport,1,1)==visual_facingst::west);
	}

	// Out-of-range and unknown viewports fall back to the native facing.
	{
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,before,empty);
	run_frame(manager,input,1000);
	assert(manager.get_facing(viewport,-1,0)==native_sprite_facing);
	assert(manager.get_facing(viewport,dim,0)==native_sprite_facing);
	assert(manager.get_facing(nullptr,0,0)==native_sprite_facing);
	}

	// Each rendered z-level has its own viewport buffers; tracking one must not suppress another.
	{
	const int lower_token=0;
	const int main_token=0;
	const void *lower_viewport=&lower_token;
	const void *main_viewport=&main_token;
	visual_animation_managerst z_levels;
	auto lower_input=make_input(lower_viewport,dim,empty);
	auto main_input=make_input(main_viewport,dim,empty);
	set_layer(lower_input,viewport_visual_layer::center,before,empty);
	set_layer(main_input,viewport_visual_layer::center,before,empty);
	z_levels.begin_frame(1000);
	z_levels.synchronize_viewport(lower_input);
	z_levels.synchronize_viewport(main_input);
	z_levels.end_frame();
	set_layer(lower_input,viewport_visual_layer::center,west_after,before);
	set_layer(main_input,viewport_visual_layer::center,west_after,before);
	z_levels.begin_frame(1016);
	z_levels.synchronize_viewport(lower_input);
	z_levels.synchronize_viewport(main_input);
	z_levels.end_frame();
	assert(z_levels.get_movement(
		lower_viewport,viewport_visual_layer::center,1,2).active);
	assert(z_levels.get_movement(
		main_viewport,viewport_visual_layer::center,1,2).active);
	}
	}

	// All four diagonals through the manager, so every step carries a real dy as well as a dx.
	// The chain alternates direction, so no assertion can pass by inheriting the previous facing.
	{
	constexpr int32_t diag_dim=5;
	int32_t diag_empty[diag_dim*diag_dim]={};
	int32_t start[diag_dim*diag_dim]={};
	int32_t north_east[diag_dim*diag_dim]={};
	int32_t south_west[diag_dim*diag_dim]={};
	int32_t south_east[diag_dim*diag_dim]={};
	int32_t north_west[diag_dim*diag_dim]={};
	const int diag_token=0;
	const void *diag_viewport=&diag_token;

	start[2*diag_dim+2]=77;        // (2,2)
	north_east[3*diag_dim+1]=77;   // (2,2) -> (3,1): dx +1, dy -1
	south_west[2*diag_dim+2]=77;   // (3,1) -> (2,2): dx -1, dy +1
	south_east[3*diag_dim+3]=77;   // (2,2) -> (3,3): dx +1, dy +1
	north_west[2*diag_dim+2]=77;   // (3,3) -> (2,2): dx -1, dy -1

	visual_animation_managerst diagonal;
	auto input=make_input(diag_viewport,diag_dim,diag_empty);
	set_layer(input,viewport_visual_layer::center,start,diag_empty);
	run_frame(diagonal,input,1000);
	assert(diagonal.get_facing(diag_viewport,2,2)==native_sprite_facing);

	set_layer(input,viewport_visual_layer::center,north_east,start);
	run_frame(diagonal,input,1016);
	assert(diagonal.get_facing(diag_viewport,3,1)==visual_facingst::east);

	// West is the grid default, so the westward legs also assert a movement was registered.
	// Otherwise an untracked step leaving the tile at its default would pass.
	set_layer(input,viewport_visual_layer::center,south_west,north_east);
	run_frame(diagonal,input,1032);
	assert(diagonal.get_movement(
		diag_viewport,viewport_visual_layer::center,2,2).active);
	assert(diagonal.get_facing(diag_viewport,2,2)==visual_facingst::west);

	set_layer(input,viewport_visual_layer::center,south_east,south_west);
	run_frame(diagonal,input,1048);
	assert(diagonal.get_facing(diag_viewport,3,3)==visual_facingst::east);

	set_layer(input,viewport_visual_layer::center,north_west,south_east);
	run_frame(diagonal,input,1064);
	assert(diagonal.get_movement(
		diag_viewport,viewport_visual_layer::center,2,2).active);
	assert(diagonal.get_facing(diag_viewport,2,2)==visual_facingst::west);
	}

	// Facing is keyed by SCREEN tile, so a scroll moves the creatures out from under it.
	// The two outcomes differ fundamentally: a landed shift has a known delta and is translated.
	// An abandoned shift never identifies a delta, so the grid can only be dropped.
	{
	constexpr int32_t pan_dim=4;
	int32_t pan_empty[pan_dim*pan_dim]={};
	int32_t at_one[pan_dim*pan_dim]={};
	int32_t at_two[pan_dim*pan_dim]={};
	int32_t unmatched_a[pan_dim*pan_dim]={};
	int32_t unmatched_b[pan_dim*pan_dim]={};
	const int pan_token=0;
	const void *pan_viewport=&pan_token;

	at_one[1*pan_dim+1]=77;
	at_two[2*pan_dim+1]=77;   // steps east, x 1 -> 2, so it faces east
	unmatched_a[2*pan_dim+1]=78;
	unmatched_b[2*pan_dim+1]=79;

	// LANDED: the shift is recognized, so facing follows the buffers.
	{
	visual_animation_managerst landed;
	auto input=make_input(pan_viewport,pan_dim,pan_empty);
	set_layer(input,viewport_visual_layer::center,at_one,pan_empty);
	run_frame(landed,input,1000);
	set_layer(input,viewport_visual_layer::center,at_two,at_one);
	run_frame(landed,input,1016);
	assert(landed.get_facing(pan_viewport,2,1)==visual_facingst::east);
	assert(landed.has_mirrored_facing(pan_viewport));

	// Frame A: the scroll is announced but the buffers have not moved yet.
	input.pan_x=1;
	set_layer(input,viewport_visual_layer::center,at_two,at_two);
	run_frame(landed,input,1032);
	assert(landed.get_facing(pan_viewport,2,1)==visual_facingst::east);

	// Frame B: the buffers shift east by one and the majority-match test recognizes it.
	set_layer(input,viewport_visual_layer::center,at_one,at_two);
	run_frame(landed,input,1048);
	assert(landed.get_facing(pan_viewport,1,1)==visual_facingst::east);
	assert(landed.get_facing(pan_viewport,2,1)==native_sprite_facing);
	assert(landed.has_mirrored_facing(pan_viewport));
	}

	// ABANDONED: the shift never shows up in the buffers, so no delta is ever identified.
	// The grid must be back at the default while the tile is still OCCUPIED.
	// The empty-tile sweep cannot reach that case, so only an explicit reset clears it.
	{
	visual_animation_managerst abandoned;
	auto input=make_input(pan_viewport,pan_dim,pan_empty);
	set_layer(input,viewport_visual_layer::center,at_one,pan_empty);
	run_frame(abandoned,input,2000);
	set_layer(input,viewport_visual_layer::center,at_two,at_one);
	run_frame(abandoned,input,2016);
	assert(abandoned.get_facing(pan_viewport,2,1)==visual_facingst::east);

	// Changed buffers keep the failed majority-match test running every frame.
	// It tolerates four before giving up on the fifth.
	input.pan_x=1;
	for(int32_t frame=0;frame<4;++frame)
		{
		set_layer(input,viewport_visual_layer::center,at_two,
			frame%2==0?unmatched_a:unmatched_b);
		run_frame(abandoned,input,2032+uint32_t(frame)*16);
		// Still pending, so the facing survives.
		// The assertion after the giving-up frame therefore tests the reset, not an empty grid.
		assert(abandoned.get_facing(pan_viewport,2,1)==visual_facingst::east);
		assert(abandoned.has_mirrored_facing(pan_viewport));
		}
	set_layer(input,viewport_visual_layer::center,at_two,unmatched_a);
	run_frame(abandoned,input,2096);
	assert(abandoned.get_facing(pan_viewport,2,1)==native_sprite_facing);
	assert(!abandoned.has_mirrored_facing(pan_viewport));
	}
	}

	// Regression: A and B move in the same frame, chained -- A's target tile is B's source tile.
	// A's write must not corrupt B's read of its own pre-frame facing.
	// A is sent east first, so its facing differs from the default B carries and a swap shows.
	{
	constexpr int32_t chase_dim=5;
	int32_t chase_empty[chase_dim*chase_dim]={};
	int32_t frame_a[chase_dim*chase_dim]={};
	int32_t frame_b[chase_dim*chase_dim]={};
	int32_t frame_c[chase_dim*chase_dim]={};
	const int chase_token=0;
	const void *chase_viewport=&chase_token;

	frame_a[1*chase_dim+1]=77;   // A at (1,1)
	frame_a[2*chase_dim+2]=88;   // B at (2,2), stationary, keeps the default facing
	frame_b[2*chase_dim+1]=77;   // A steps east: (1,1) -> (2,1), picks up an east facing
	frame_b[2*chase_dim+2]=88;   // B unchanged

	// Both step south in the same frame: shared_movement_delta needs two tiles on the same delta.
	frame_c[2*chase_dim+2]=77;   // A: (2,1) -> (2,2)
	frame_c[2*chase_dim+3]=88;   // B: (2,2) -> (2,3)

	visual_animation_managerst manager;
	auto input=make_input(chase_viewport,chase_dim,chase_empty);
	set_layer(input,viewport_visual_layer::center,frame_a,chase_empty);
	run_frame(manager,input,1000);
	set_layer(input,viewport_visual_layer::center,frame_b,frame_a);
	run_frame(manager,input,1016);
	assert(manager.get_facing(chase_viewport,2,1)==visual_facingst::east);
	assert(manager.get_facing(chase_viewport,2,2)==native_sprite_facing);

	set_layer(input,viewport_visual_layer::center,frame_c,frame_b);
	run_frame(manager,input,1032);
	assert(manager.get_facing(chase_viewport,2,2)==visual_facingst::east);
	// B carries its own default forward, not A's, though A wrote (2,2) earlier in the same pass.
	assert(manager.get_facing(chase_viewport,2,3)==native_sprite_facing);
	}

	// Regression: a vacated source tile is reoccupied the same frame by an UNTRACKED creature.
	// Nothing targets that tile, so no target write clears it, and it is not empty either.
	// Only an explicit, order-independent source clear restores the default there.
	{
	constexpr int32_t gap_dim=5;
	int32_t gap_empty[gap_dim*gap_dim]={};
	int32_t frame_a[gap_dim*gap_dim]={};
	int32_t frame_b[gap_dim*gap_dim]={};
	int32_t frame_c[gap_dim*gap_dim]={};
	const int gap_token=0;
	const void *gap_viewport=&gap_token;

	// E is a companion so that D's later departure shares a delta with E's own move.
	// shared_movement_delta engages only once two tiles move on the same delta.
	frame_a[1*gap_dim+1]=77;   // D at (1,1)
	frame_a[2*gap_dim+2]=88;   // E at (2,2), stationary companion
	frame_b[2*gap_dim+1]=77;   // D steps east: (1,1) -> (2,1), facing differs from the default
	frame_b[2*gap_dim+2]=88;   // E unchanged

	// D and E both step south, chained, and F appears at D's just-vacated tile the same frame.
	// previous[(2,1)] was occupied by D, so the empty-cell fallback cannot see F.
	// No shared-delta source matches F's texpos either, so its arrival registers no movement.
	frame_c[2*gap_dim+2]=77;   // D: (2,1) -> (2,2)
	frame_c[2*gap_dim+3]=88;   // E: (2,2) -> (2,3)
	frame_c[2*gap_dim+1]=55;   // F appears at (2,1), untracked

	visual_animation_managerst manager;
	auto input=make_input(gap_viewport,gap_dim,gap_empty);
	set_layer(input,viewport_visual_layer::center,frame_a,gap_empty);
	run_frame(manager,input,1000);
	set_layer(input,viewport_visual_layer::center,frame_b,frame_a);
	run_frame(manager,input,1016);
	assert(manager.get_facing(gap_viewport,2,1)==visual_facingst::east);

	set_layer(input,viewport_visual_layer::center,frame_c,frame_b);
	run_frame(manager,input,1032);
	assert(!manager.get_movement(
		gap_viewport,viewport_visual_layer::center,2,1).active);
	// F must not inherit D's stale east facing, though the tile is occupied rather than empty.
	assert(manager.get_facing(gap_viewport,2,1)==native_sprite_facing);
	assert(manager.get_facing(gap_viewport,2,2)==visual_facingst::east);
	assert(manager.get_facing(gap_viewport,2,3)==native_sprite_facing);
	}

	const movement_sett movements=make_movements();
	const movementst &linear=*movements.find("linear");
	const movementst &smoothstep=default_movement();
	assert(step_progress(150,0,150)==1.0f&&step_progress(75,0,150)==0.5f);
	assert(step_progress(200,0,150)==1.0f);
	assert(smoothstep.travelled(1.0f)==1.0f&&smoothstep.travelled(0.5f)==0.5f);
	assert(linear.travelled(step_progress(25,0,150))==float(1)/6);
	assert(smoothstep.travelled(step_progress(25,0,150))<float(1)/6);
	// Every movement leaves from and lands on the grid, on the step, whatever the step
	// (within float rounding of a sine at pi, for the bob).
	for(const std::unique_ptr<movementst> &movement:movements.all)
		for(const tile_stepst step:{tile_stepst{1,0},tile_stepst{0,-1},tile_stepst{-1,1}})
			{
			const sprite_offsetst start=movement->position(0.0f,step);
			const sprite_offsetst end=movement->position(1.0f,step);
			assert(start.x==0.0f&&start.y==0.0f&&end.x==step.x);
			assert(std::fabs(end.y-step.y)<1e-6f);
			}
	assert(smoothstep.position(0.5f,{1,0}).x==0.5f&&smoothstep.position(0.5f,{1,0}).y==0.0f);
	assert(linear.position(0.25f,{0,-1}).y==-0.25f&&linear.position(0.25f,{0,-1}).x==0.0f);
	const auto carried_icon=carried_item_icon_rect(100.0f,200.0f,20.0f);
	assert(carried_icon.x==101.0f&&carried_icon.y==204.0f);
	assert(carried_icon.width==14.0f&&carried_icon.height==14.0f);
	assert(inherited_visual_source_tile(0,0,1)==-1);
	assert(inherited_visual_source_tile(2,0,1)==1);
	assert(visual_layer_descriptor(viewport_visual_layer::right).center_x==-1);
	assert(visual_layer_descriptor(viewport_visual_layer::left).center_x==1);
	assert(visual_layer_descriptor(viewport_visual_layer::upright).center_x==-1&&
		visual_layer_descriptor(viewport_visual_layer::upright).center_y==1);
	assert(visual_layer_descriptor(viewport_visual_layer::up).center_x==0&&
		visual_layer_descriptor(viewport_visual_layer::up).center_y==1);
	assert(visual_layer_descriptor(viewport_visual_layer::upleft).center_x==1&&
		visual_layer_descriptor(viewport_visual_layer::upleft).center_y==1);

	std::array<int32_t,9> empty{};
	std::array<int32_t,9> current{};
	std::array<int32_t,9> previous{};
	const void *viewport=reinterpret_cast<const void *>(uintptr_t(1));
	auto input=make_input(viewport,3,empty.data());
	set_layer(input,viewport_visual_layer::center,current.data(),previous.data());

	visual_animation_managerst movement;
	run_frame(movement,input,1990);
	assert(!movement.requires_full_redraw());

	previous[0*3+1]=42;
	current[1*3+1]=42;
	run_frame(movement,input,2000);
	auto render=movement.get_movement(viewport,viewport_visual_layer::center,1,1);
	assert(render.active);
	assert(render.source_x==0&&render.source_y==1);
	assert(travelled(render,1,1)==0.0f);
	assert(movement.requires_full_redraw());

	previous=current;
	set_layer(input,viewport_visual_layer::center,current.data(),previous.data());
	run_frame(movement,input,2075);
	render=movement.get_movement(viewport,viewport_visual_layer::center,1,1);
	assert(render.active);
	assert(travelled(render,1,1)==0.5f);

	run_frame(movement,input,2150);
	assert(!movement.get_movement(
		viewport,viewport_visual_layer::center,1,1).active);
	assert(movement.requires_full_redraw());

	run_frame(movement,input,2170);
	assert(!movement.requires_full_redraw());

	visual_animation_managerst linear_movement;
	linear_movement.set_movement(linear);
	current.fill(0);
	previous.fill(0);
	set_layer(input,viewport_visual_layer::center,current.data(),previous.data());
	run_frame(linear_movement,input,2190);
	previous[0*3+1]=42;
	current[1*3+1]=42;
	run_frame(linear_movement,input,2200);
	previous=current;
	run_frame(linear_movement,input,2225);
	assert(travelled(linear_movement.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==float(1)/6);

	// Linear cadence follows the latest step, while completed movement stays silent history.
	{
	std::array<int32_t,9> at_zero{};
	std::array<int32_t,9> at_one{};
	std::array<int32_t,9> at_two{};
	at_zero[0*3+1]=42;
	at_one[1*3+1]=42;
	at_two[2*3+1]=42;

	visual_animation_managerst adaptive;
	adaptive.set_movement(linear);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(adaptive,input,1000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(adaptive,input,1010);
	run_frame(adaptive,input,1085);
	assert(travelled(adaptive.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f); // first: 150 ms
	run_frame(adaptive,input,1160);
	assert(!adaptive.get_movement(
		viewport,viewport_visual_layer::center,1,1).active);
	run_frame(adaptive,input,1161);
	assert(!adaptive.requires_full_redraw());
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(adaptive,input,1310);
	run_frame(adaptive,input,1460);
	assert(travelled(adaptive.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f); // cadence: 300 ms

	visual_animation_managerst minimum;
	minimum.set_movement(linear);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(minimum,input,2000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(minimum,input,2010);
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(minimum,input,2110);
	run_frame(minimum,input,2185);
	assert(travelled(minimum.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f); // clamped to 150 ms

	visual_animation_managerst maximum;
	maximum.set_movement(linear);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(maximum,input,3000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(maximum,input,3010);
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(maximum,input,3510);
	run_frame(maximum,input,3760);
	assert(travelled(maximum.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f); // clamped to 500 ms
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_two.data());
	run_frame(maximum,input,4111);
	run_frame(maximum,input,4186);
	assert(travelled(maximum.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f); // history expired: 150 ms

	// Reversal leaves two predecessors at B; the newer B->A step must win for A->B.
	visual_animation_managerst reversal;
	reversal.set_movement(linear);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(reversal,input,4000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(reversal,input,4010);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),at_one.data());
	run_frame(reversal,input,4310);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(reversal,input,4510);
	run_frame(reversal,input,4610);
	assert(travelled(reversal.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f); // latest cadence: 200 ms

	visual_animation_managerst smoothstep;
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(smoothstep,input,5000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(smoothstep,input,5010);
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(smoothstep,input,5110);
	run_frame(smoothstep,input,5185);
	assert(travelled(smoothstep.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f); // fixed 150 ms
	}

	// The step time is a runtime setting; a movement keeps the one it started with.
	{
	std::array<int32_t,9> at_zero{};
	std::array<int32_t,9> at_one{};
	std::array<int32_t,9> at_two{};
	at_zero[0*3+1]=42;
	at_one[1*3+1]=42;
	at_two[2*3+1]=42;

	visual_animation_managerst stepped;
	assert(stepped.step_duration_ms()==150);
	assert(visual_animation_managerst::default_step_duration_ms==150);
	stepped.set_step_duration_ms(200);
	assert(stepped.step_duration_ms()==200);
	stepped.set_step_duration_ms(0);
	assert(stepped.step_duration_ms()==1);
	stepped.set_step_duration_ms(200);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(stepped,input,6000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(stepped,input,6010);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_one.data());
	stepped.set_step_duration_ms(50); // mid-flight: the 200 ms movement is unaffected
	run_frame(stepped,input,6110);
	assert(travelled(stepped.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f);
	run_frame(stepped,input,6209);
	assert(stepped.get_movement(viewport,viewport_visual_layer::center,1,1).active);
	run_frame(stepped,input,6210);
	assert(!stepped.get_movement(viewport,viewport_visual_layer::center,1,1).active);
	assert(stepped.requires_full_redraw());
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(stepped,input,6300); // the next movement takes the new 50 ms
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_two.data());
	run_frame(stepped,input,6325);
	assert(travelled(stepped.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f);
	run_frame(stepped,input,6350);
	assert(!stepped.get_movement(viewport,viewport_visual_layer::center,2,1).active);

	// With linear on, the step time is the adaptive duration's floor, and a step longer
	// than 500 ms lifts the ceiling with it so the movement is neither clamped nor cut short.
	visual_animation_managerst long_linear;
	long_linear.set_movement(linear);
	long_linear.set_step_duration_ms(800);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(long_linear,input,7000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(long_linear,input,7010);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_one.data());
	run_frame(long_linear,input,7410);
	assert(travelled(long_linear.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f); // first: 800 ms
	run_frame(long_linear,input,7610); // past 500 ms, still in flight
	assert(travelled(long_linear.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.75f);
	// The next step follows a predecessor older than 500 ms: its visual source is where
	// that movement is, and its duration is the 600 ms cadence raised to the 800 ms floor.
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(long_linear,input,7610);
	assert(long_linear.get_movement(
		viewport,viewport_visual_layer::center,2,1).source_x==0.75f);
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_two.data());
	run_frame(long_linear,input,8010);
	assert(travelled(long_linear.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f);
	run_frame(long_linear,input,8410);
	assert(!long_linear.get_movement(viewport,viewport_visual_layer::center,2,1).active);
	// A step back at an 800 ms cadence lasts 800 ms, the floor; the predecessor filter
	// has already kept the cadence within the 800 ms limit.
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_two.data());
	run_frame(long_linear,input,8410);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_one.data());
	run_frame(long_linear,input,8810);
	assert(travelled(long_linear.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f);

	// Lowering the step time while a longer linear movement is in flight leaves that
	// movement its own duration: it is neither erased at the new 500 ms limit nor
	// dropped as a predecessor, and only the movements that start after it are shorter.
	visual_animation_managerst lowered_linear;
	lowered_linear.set_movement(linear);
	lowered_linear.set_step_duration_ms(2000);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(lowered_linear,input,10000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(lowered_linear,input,10010);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_one.data());
	run_frame(lowered_linear,input,10310);
	lowered_linear.set_step_duration_ms(150);
	run_frame(lowered_linear,input,11010); // 1000 ms in, past the 500 ms limit
	assert(travelled(lowered_linear.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.5f);
	run_frame(lowered_linear,input,11510);
	assert(travelled(lowered_linear.get_movement(viewport,viewport_visual_layer::center,1,1),1,1)==0.75f);
	// The next step still follows it as its predecessor, and its 1500 ms cadence is
	// clamped to the new 500 ms ceiling.
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(lowered_linear,input,11510);
	assert(lowered_linear.get_movement(
		viewport,viewport_visual_layer::center,2,1).source_x==0.75f);
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_two.data());
	run_frame(lowered_linear,input,11760);
	assert(travelled(lowered_linear.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f);
	run_frame(lowered_linear,input,12010);
	assert(!lowered_linear.get_movement(viewport,viewport_visual_layer::center,2,1).active);

	visual_animation_managerst short_linear;
	short_linear.set_movement(linear);
	short_linear.set_step_duration_ms(100);
	set_layer(input,viewport_visual_layer::center,at_zero.data(),empty.data());
	run_frame(short_linear,input,9000);
	set_layer(input,viewport_visual_layer::center,at_one.data(),at_zero.data());
	run_frame(short_linear,input,9010);
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_one.data());
	run_frame(short_linear,input,9040); // cadence 30 ms, clamped up to the 100 ms step
	set_layer(input,viewport_visual_layer::center,at_two.data(),at_two.data());
	run_frame(short_linear,input,9090);
	assert(travelled(short_linear.get_movement(viewport,viewport_visual_layer::center,2,1),2,1)==0.5f);
	}

	visual_animation_managerst ambiguous;
	assert(&ambiguous.movement()==&default_movement());
	ambiguous.set_movement(linear);
	assert(&ambiguous.movement()==&linear);
	assert(linear.keeps_cadence()&&!linear.reach().any());
	ambiguous.set_movement(*movements.find("bob"));
	assert(!ambiguous.movement().keeps_cadence()&&ambiguous.movement().reach().any());
	assert(!default_movement().keeps_cadence()&&!default_movement().reach().any());
	assert(movements.find("bounce")==nullptr);
	assert(movements.names()=="smoothstep, linear, bob");
	assert(&movements.default_movement()==movements.all.front().get());
	assert(std::string(movements.default_movement().name())=="smoothstep");
	ambiguous.set_movement(linear);
	set_layer(input,viewport_visual_layer::center,current.data(),previous.data());
	run_frame(ambiguous,input,2990);
	previous.fill(0);
	previous[0*3+1]=42;
	previous[1*3+0]=42;
	set_layer(input,viewport_visual_layer::center,current.data(),previous.data());
	run_frame(ambiguous,input,3000);
	assert(!ambiguous.get_movement(
		viewport,viewport_visual_layer::center,1,1).active);

	// A handler and led animal form an occupied chain: each enters the other's old space.
	visual_animation_managerst convoy;
	current.fill(0);
	previous.fill(0);
	run_frame(convoy,input,3490);
	previous[0*3+1]=41;
	previous[1*3+1]=42;
	current[1*3+1]=41;
	current[2*3+1]=42;
	run_frame(convoy,input,3500);
	const auto animal=convoy.get_movement(
		viewport,viewport_visual_layer::center,1,1);
	const auto handler=convoy.get_movement(
		viewport,viewport_visual_layer::center,2,1);
	assert(animal.active&&animal.source_x==0&&animal.source_y==1);
	assert(handler.active&&handler.source_x==1&&handler.source_y==1);

	// A multi-tile fragment can use its own movement or its mapped center tile as proof of ownership.
	for(const auto layer:{viewport_visual_layer::right,viewport_visual_layer::left,
		viewport_visual_layer::upright,viewport_visual_layer::up,
		viewport_visual_layer::upleft})
		{
		current.fill(0);
		previous.fill(0);
		previous[0*3+1]=50;
		current[1*3+1]=50;
		assert(visual_moved_between_tiles(
			layer,current.data(),previous.data(),0*3+1,1*3+1));
		previous[1*3+1]=50;
		assert(!visual_moved_between_tiles(
			layer,current.data(),previous.data(),0*3+1,1*3+1));
		}

	visual_animation_managerst context;
	current.fill(0);
	previous.fill(0);
	run_frame(context,input,4000);
	previous[0*3+1]=42;
	current[1*3+1]=42;
	run_frame(context,input,4010);
	assert(context.get_movement(
		viewport,viewport_visual_layer::center,1,1).active);

	++input.context_revision;
	run_frame(context,input,4020);
	assert(!context.get_movement(
		viewport,viewport_visual_layer::center,1,1).active);

	// Camera-pan handling. window_x/window_y change at input time but the buffers shift on a later
	// render frame, so the manager must (a) NOT create movements from the buffer shift itself (the
	// floating-sprite bug), and (b) translate in-flight movements on the frame the shift lands.
	std::array<int32_t,9> pan_current{};
	std::array<int32_t,9> pan_previous{};
	std::array<int32_t,9> pan_empty{};
	auto pan_input=make_input(viewport,3,pan_empty.data());
	set_layer(
		pan_input,
		viewport_visual_layer::center,
		pan_current.data(),
		pan_previous.data());

	// FLOAT REGRESSION: a stationary creature, pan announced at frame A, buffers shift at frame B.
	// Frame B's buffers look exactly like a real move ((1,1)->(0,1) with a unique source) — the
	// manager must recognize it as the pending pan and create NO movement.
	visual_animation_managerst floaty;
	pan_previous[1*3+1]=42;
	pan_current[1*3+1]=42;
	run_frame(floaty,pan_input,4990);
	pan_input.pan_x=1;                   // frame A: window scrolled, buffers unchanged
	run_frame(floaty,pan_input,5000);
	assert(!floaty.get_movement(viewport,viewport_visual_layer::center,1,1).active);
	pan_previous[1*3+1]=42;              // frame B: buffers apply the shift
	pan_current.fill(0);
	pan_current[0*3+1]=42;
	run_frame(floaty,pan_input,5010);
	assert(!floaty.get_movement(viewport,viewport_visual_layer::center,0,1).active);

	// FOLLOW: an in-flight movement survives the announce frame untouched and is translated on the
	// frame the buffers shift, so the sprite tracks the scrolled world.
	visual_animation_managerst panner;
	pan_current.fill(0);
	pan_previous.fill(0);
	pan_input.pan_x=0;
	run_frame(panner,pan_input,5990);
	pan_previous[0*3+1]=42;              // creature steps (0,1) -> (1,1)
	pan_current[1*3+1]=42;
	run_frame(panner,pan_input,6000);
	auto moved=panner.get_movement(viewport,viewport_visual_layer::center,1,1);
	assert(moved.active&&moved.source_x==0&&moved.source_y==1);

	pan_input.pan_x=1;                   // frame A: pan announced, buffers unchanged
	pan_previous[0*3+1]=0;
	pan_previous[1*3+1]=42;              // previous now matches current (stationary at (1,1))
	run_frame(panner,pan_input,6010);
	moved=panner.get_movement(viewport,viewport_visual_layer::center,1,1);
	assert(moved.active&&moved.source_x==0);   // untouched: still anchored to the old frame

	pan_current.fill(0);                 // frame B: buffers shift east by one
	pan_current[0*3+1]=42;
	run_frame(panner,pan_input,6020);
	assert(!panner.get_movement(viewport,viewport_visual_layer::center,1,1).active);
	auto followed=panner.get_movement(viewport,viewport_visual_layer::center,0,1);
	assert(followed.active&&followed.source_x==-1&&followed.source_y==1);

	// SAME-FRAME: pan announced and buffers shifted in the same call — translated immediately.
	visual_animation_managerst same_frame;
	pan_current.fill(0);
	pan_previous.fill(0);
	pan_input.pan_x=0;
	run_frame(same_frame,pan_input,6990);
	pan_previous[0*3+1]=42;
	pan_current[1*3+1]=42;
	run_frame(same_frame,pan_input,7000);
	assert(same_frame.get_movement(viewport,viewport_visual_layer::center,1,1).active);
	pan_input.pan_x=1;
	pan_previous=pan_current;
	pan_current.fill(0);
	pan_current[0*3+1]=42;
	run_frame(same_frame,pan_input,7010);
	followed=same_frame.get_movement(viewport,viewport_visual_layer::center,0,1);
	assert(followed.active&&followed.source_x==-1);

	// A change that is NOT a pure pan (context revision bump) still resets, even with in-flight work.
	visual_animation_managerst reset_on_zoom;
	pan_current.fill(0);
	pan_previous.fill(0);
	pan_input.pan_x=0;
	pan_input.context_revision=1;
	run_frame(reset_on_zoom,pan_input,8000);
	pan_previous[0*3+1]=42;
	pan_current[1*3+1]=42;
	run_frame(reset_on_zoom,pan_input,8010);
	assert(reset_on_zoom.get_movement(viewport,viewport_visual_layer::center,1,1).active);
	pan_input.context_revision=2;       // e.g. zoom / z-level / resize
	run_frame(reset_on_zoom,pan_input,8020);
	assert(!reset_on_zoom.get_movement(viewport,viewport_visual_layer::center,1,1).active);

	// Status fragments inherit nearby center motion even while their texture flashes.
	std::array<int32_t,9> status_current{};
	std::array<int32_t,9> status_previous{};
	current.fill(0);
	previous.fill(0);
	input.context_revision=1;
	input.current.fill(empty.data());
	input.previous.fill(empty.data());
	set_layer(input,viewport_visual_layer::center,current.data(),previous.data());
	set_layer(input,viewport_visual_layer::item,status_current.data(),status_previous.data());
	set_layer(
		input,
		viewport_visual_layer::designation,
		status_current.data(),
		status_previous.data());
	visual_animation_managerst companion;
	run_frame(companion,input,8990);
	previous[0*3+1]=42;
	current[1*3+1]=42;
	status_previous[0*3+0]=90;
	status_current[1*3+0]=91;
	assert(visual_moved_between_tiles(
		viewport_visual_layer::designation,
		status_current.data(),status_previous.data(),0*3+0,1*3+0));
	status_previous[0*3+0]=0;
	assert(visual_moved_between_tiles(
		viewport_visual_layer::designation,
		status_current.data(),status_previous.data(),0*3+0,1*3+0));
	assert(!visual_moved_between_tiles(
		viewport_visual_layer::item,
		status_current.data(),status_previous.data(),0*3+0,1*3+0));
	status_previous[1*3+0]=80;
	assert(!visual_moved_between_tiles(
		viewport_visual_layer::designation,
		status_current.data(),status_previous.data(),0*3+0,1*3+0));
	status_previous[0*3+0]=90;
	status_previous[1*3+0]=0;
	run_frame(companion,input,9000);
	auto status=companion.get_movement(viewport,viewport_visual_layer::designation,1,0);
	assert(status.active&&!status.inherited&&status.source_x==0&&status.source_y==0);
	const auto carried_item=companion.get_movement(
		viewport,viewport_visual_layer::item,1,0);
	assert(carried_item.active&&carried_item.inherited);
	previous=current;
	status_current[1*3+0]=92;
	run_frame(companion,input,9075);
	status=companion.get_movement(viewport,viewport_visual_layer::designation,1,0);
	assert(status.active&&travelled(status,1,0)==0.5f);

	// Divergent nearby creature movements make companion ownership ambiguous, so the overlay snaps.
	current.fill(0);
	previous.fill(0);
	status_current.fill(0);
	status_previous.fill(0);
	visual_animation_managerst crowd;
	run_frame(crowd,input,9990);
	previous[0*3+0]=41;
	current[0*3+1]=41;
	previous[2*3+2]=42;
	current[2*3+1]=42;
	status_current[1*3+1]=99;
	run_frame(crowd,input,10000);
	assert(!crowd.get_movement(
		viewport,viewport_visual_layer::designation,1,1).active);
	status_previous[1*3+0]=90;
	status_current[1*3+1]=91;
	run_frame(crowd,input,10010);
	status=crowd.get_movement(viewport,viewport_visual_layer::designation,1,1);
	assert(status.active);
	assert(!status.inherited);
	assert(status.source_x==1&&status.source_y==0);

	// Wheelbarrows use the item layer and the same independent adjacent-movement detection.
	current.fill(0);
	previous.fill(0);
	input.current.fill(empty.data());
	input.previous.fill(empty.data());
	set_layer(input,viewport_visual_layer::item,current.data(),previous.data());
	visual_animation_managerst item;
	run_frame(item,input,10990);
	previous[0*3+1]=77;
	current[1*3+1]=77;
	run_frame(item,input,11000);
	const auto item_move=item.get_movement(
		viewport,viewport_visual_layer::item,1,1);
	assert(item_move.active&&item_move.source_x==0&&item_move.source_y==1);

	// Minecart graphics can change texpos while moving; vehicle identity is tile occupancy.
	current.fill(0);
	previous.fill(0);
	input.current.fill(empty.data());
	input.previous.fill(empty.data());
	set_layer(input,viewport_visual_layer::vehicle,current.data(),previous.data());
	visual_animation_managerst vehicle;
	run_frame(vehicle,input,11990);
	previous[0*3+1]=77;
	current[1*3+1]=78;
	run_frame(vehicle,input,12000);
	assert(vehicle.get_movement(
		viewport,viewport_visual_layer::vehicle,1,1).active);
	previous=current;
	current[1*3+1]=79;
	run_frame(vehicle,input,12075);
	const auto cart=vehicle.get_movement(
		viewport,viewport_visual_layer::vehicle,1,1);
	assert(cart.active&&travelled(cart,1,1)==0.5f);
	previous=current;
	current.fill(0);
	current[2*3+1]=80;
	run_frame(vehicle,input,12060);
	const auto chained=vehicle.get_movement(
		viewport,viewport_visual_layer::vehicle,2,1);
	assert(chained.active&&chained.source_x>0.0f&&chained.source_x<1.0f&&
		travelled(chained,2,1)==0.0f);

	// Every cardinal and diagonal follow step gets a stable inverse visual anchor.
	for(int32_t dx=-1;dx<=1;++dx)
		for(int32_t dy=-1;dy<=1;++dy)
			{
			if(dx==0&&dy==0)continue;
			constexpr int32_t dim=7;
			constexpr size_t tiles=size_t(dim)*size_t(dim);
			const int token=dx*3+dy;
			std::array<int32_t,tiles> empty{};
			std::array<int32_t,tiles> creature{};
			std::array<int32_t,tiles> creature_old{};
			std::array<int32_t,tiles> background{};
			std::array<int32_t,tiles> background_old{};
			fill_background(background_old,2000);
			background=background_old;
			const int32_t center=dim/2;
			creature[size_t(center*dim+center)]=42;
			creature_old=creature;
			auto follow_input=make_input(&token,dim,empty.data());
			set_layer(follow_input,viewport_visual_layer::center,
				creature.data(),creature_old.data());
			follow_input.current_background=background.data();
			follow_input.previous_background=background_old.data();
			visual_animation_managerst follow_manager;
			run_frame(follow_manager,follow_input,20000);
			follow_input.pan_x=dx;
			follow_input.pan_y=dy;
			run_frame(follow_manager,follow_input,20010);
			shift_background(background,background_old,dim,dx,dy,3000);
			run_frame(follow_manager,follow_input,20020);
			const auto scroll=follow_manager.get_scroll(&token);
			assert(scroll.landed&&scroll.landed_x==dx&&scroll.landed_y==dy&&
				!scroll.pending&&scroll.follow_candidate!=no_visual_movement);
			const auto movement=follow_manager.get_movement(
				&token,viewport_visual_layer::center,center,center);
			const auto follow=follow_manager.get_follow(&token,scroll.follow_candidate);
			assert(movement.active&&follow.active&&
				movement.movement_id==scroll.follow_candidate);
			assert(std::abs((movement.source_x-center)+movement.offset_x+
				follow.offset_x)<0.000001f);
			assert(std::abs((movement.source_y-center)+movement.offset_y+
				follow.offset_y)<0.000001f);
			run_frame(follow_manager,follow_input,20095);
			const auto fractional=follow_manager.get_follow(&token,scroll.follow_candidate);
			assert(fractional.active&&std::abs(fractional.offset_x)<1.0f&&
				std::abs(fractional.offset_y)<1.0f);
			}

	// A multi-tile announcement may land partially, then retire the remaining debt.
	{
	constexpr int32_t dim=6;
	constexpr size_t tiles=size_t(dim)*size_t(dim);
	const int token=0;
	std::array<int32_t,tiles> empty{};
	std::array<int32_t,tiles> background{};
	std::array<int32_t,tiles> background_old{};
	fill_background(background_old,4000);
	background=background_old;
	auto input=make_input(&token,dim,empty.data());
	input.current_background=background.data();
	input.previous_background=background_old.data();
	visual_animation_managerst partial;
	run_frame(partial,input,21000);
	input.pan_x=3;
	run_frame(partial,input,21010);
	shift_background(background,background_old,dim,1,0,5000);
	run_frame(partial,input,21020);
	auto scroll=partial.get_scroll(&token);
	assert(scroll.landed&&scroll.landed_x==1&&scroll.pending&&scroll.pending_x==2);
	background_old=background;
	shift_background(background,background_old,dim,2,0,6000);
	run_frame(partial,input,21030);
	scroll=partial.get_scroll(&token);
	assert(scroll.landed&&scroll.landed_x==2&&!scroll.pending);
	}

	// Uniform terrain is visually safe to retire; absent data abandons without an offset.
	{
	constexpr int32_t dim=4;
	constexpr size_t tiles=size_t(dim)*size_t(dim);
	const int uniform_token=0,empty_token=1;
	std::array<int32_t,tiles> empty{};
	std::array<int32_t,tiles> uniform{};
	uniform.fill(77);
	auto input=make_input(&uniform_token,dim,empty.data());
	input.current_background=uniform.data();
	input.previous_background=uniform.data();
	visual_animation_managerst manager;
	run_frame(manager,input,22000);
	input.pan_x=1;
	run_frame(manager,input,22010);
	assert(manager.get_scroll(&uniform_token).landed);
	auto empty_input=make_input(&empty_token,dim,empty.data());
	empty_input.current_background=empty.data();
	empty_input.previous_background=empty.data();
	visual_animation_managerst empty_manager;
	run_frame(empty_manager,empty_input,22100);
	empty_input.pan_x=1;
	run_frame(empty_manager,empty_input,22110);
	const auto abandoned=empty_manager.get_scroll(&empty_token);
	assert(abandoned.abandoned&&!abandoned.pending);
	}

	// The array signature is hashed in lanes of eight entries with the remainder in a tail
	// loop. A 3x3 viewport has nine entries, so tile (2,2) is the only tail entry: a change
	// there alone must still read as a redrawn viewport, or its movement goes undetected.
	{
	constexpr int32_t dim=3;
	int32_t empty[dim*dim]={};
	int32_t source[dim*dim]={};
	int32_t target[dim*dim]={};
	const int tail_token=0;
	const void *viewport=&tail_token;
	source[2*dim+1]=77;
	target[2*dim+2]=77;   // moved from (2,1) to (2,2), the last entry
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,empty,source);
	run_frame(manager,input,30000);
	run_frame(manager,input,30016);
	set_layer(input,viewport_visual_layer::center,target,source);
	run_frame(manager,input,30032);
	assert(manager.get_movement(
		viewport,viewport_visual_layer::center,2,2).active);
	}

	// The render code walks only the tiles the tracker lists for a viewport, so the list must
	// hold every tile get_movement reports active on: the moved creature's own tile and, one
	// tile around it, the tiles its other layers may inherit the movement on. A viewport with
	// no movement lists nothing, and a finished movement drops out of the list.
	{
	constexpr int32_t dim=5;
	int32_t empty[dim*dim]={};
	int32_t source[dim*dim]={};
	int32_t target[dim*dim]={};
	const int tiles_token=0;
	const void *viewport=&tiles_token;
	source[1*dim+2]=91;
	target[2*dim+2]=91;   // moved from (1,2) to (2,2)
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,source,empty);
	run_frame(manager,input,40000);
	std::vector<std::array<int32_t,2>> tiles;
	manager.collect_movement_tiles(viewport,dim,dim,tiles);
	assert(tiles.empty());
	set_layer(input,viewport_visual_layer::center,target,source);
	run_frame(manager,input,40016);
	manager.collect_movement_tiles(viewport,dim,dim,tiles);
	assert(tiles.size()==9);
	for(size_t i=1;i<tiles.size();++i)
		assert(tiles[i-1][1]<tiles[i][1]||
			(tiles[i-1][1]==tiles[i][1]&&tiles[i-1][0]<tiles[i][0]));
	for(int32_t y=0;y<dim;++y)
		for(int32_t x=0;x<dim;++x)
			for(size_t layer=0;layer<size_t(viewport_visual_layer::count);++layer)
				{
				const auto movement=manager.get_movement(
					viewport,static_cast<viewport_visual_layer>(layer),x,y);
				const bool listed=std::find(tiles.begin(),tiles.end(),
					std::array<int32_t,2>{x,y})!=tiles.end();
				assert(!movement.active||listed);
				}
	run_frame(manager,input,40400);   // past the movement's duration
	manager.collect_movement_tiles(viewport,dim,dim,tiles);
	assert(tiles.empty());
	}

	// A creature at the corner of the viewport reaches only the three tiles beside it, and two
	// creatures side by side share tiles that are listed once: the render code indexes the
	// per-tile arrays with every listed tile, so one outside the viewport reads past an array
	// and one listed twice draws twice.
	{
	constexpr int32_t dim=5;
	int32_t empty[dim*dim]={};
	int32_t source[dim*dim]={};
	int32_t target[dim*dim]={};
	const int edge_token=0;
	const void *viewport=&edge_token;
	source[1*dim+0]=92;
	target[0*dim+0]=92;   // moved from (1,0) to the corner (0,0)
	source[4*dim+4]=93;
	target[3*dim+4]=93;   // moved from (4,4) to (3,4)
	source[2*dim+4]=94;
	target[3*dim+3]=94;   // moved from (2,4) to (3,3), next to the previous one
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim,empty);
	set_layer(input,viewport_visual_layer::center,source,empty);
	run_frame(manager,input,41000);
	set_layer(input,viewport_visual_layer::center,target,source);
	run_frame(manager,input,41016);
	std::vector<std::array<int32_t,2>> tiles;
	manager.collect_movement_tiles(viewport,dim,dim,tiles);
	size_t corner=0;
	for(const auto &tile:tiles)
		{
		assert(tile[0]>=0&&tile[0]<dim&&tile[1]>=0&&tile[1]<dim);
		if(tile[0]<=1&&tile[1]<=1)++corner;
		}
	assert(corner==4);
	// (3,4) reaches x 2..4, y 3..4 once y 5 is clipped, and (3,3) reaches x 2..4, y 2..4,
	// which covers those: 9 distinct tiles, and with the corner's 4 that is 13 in all.
	assert(tiles.size()==13);
	for(size_t i=1;i<tiles.size();++i)
		assert(tiles[i-1]!=tiles[i]);
	}

	// The sweep for resting mirrored creatures visits only the tiles the tracker lists, so
	// the list must name exactly the tiles whose facing is not the native one, column by
	// column and top to bottom within a column, the order the sweep walked them in. The
	// viewport is wider than it is tall so that a list built with the two dimensions
	// confused, in the tile index or in the walk's bounds, names the wrong tiles.
	{
	constexpr int32_t dim_x=5;
	constexpr int32_t dim_y=3;
	int32_t empty[dim_x*dim_y]={};
	int32_t before[dim_x*dim_y]={};
	int32_t after[dim_x*dim_y]={};
	const int mirrored_token=0;
	const void *viewport=&mirrored_token;
	before[3*dim_y+1]=81;
	after[4*dim_y+1]=81;   // moved east from (3,1) to (4,1)
	before[0*dim_y+2]=82;
	after[1*dim_y+2]=82;   // moved east from (0,2) to (1,2)
	visual_animation_managerst manager;
	auto input=make_input(viewport,dim_x,empty);
	input.dim_y=dim_y;
	set_layer(input,viewport_visual_layer::center,before,empty);
	run_frame(manager,input,42000);
	std::vector<std::array<int32_t,2>> tiles;
	manager.collect_mirrored_tiles(viewport,dim_x,dim_y,tiles);
	assert(tiles.empty());
	set_layer(input,viewport_visual_layer::center,after,before);
	run_frame(manager,input,42016);
	manager.collect_mirrored_tiles(viewport,dim_x,dim_y,tiles);
	assert(tiles.size()==2);
	assert(tiles[0][0]==1&&tiles[0][1]==2);
	assert(tiles[1][0]==4&&tiles[1][1]==1);
	for(int32_t x=0;x<dim_x;++x)
		for(int32_t y=0;y<dim_y;++y)
			{
			const bool listed=std::find(tiles.begin(),tiles.end(),
				std::array<int32_t,2>{x,y})!=tiles.end();
			assert(listed==(manager.get_facing(viewport,x,y)!=native_sprite_facing));
			}
	set_layer(input,viewport_visual_layer::center,empty,after);
	run_frame(manager,input,42032);
	manager.collect_mirrored_tiles(viewport,dim_x,dim_y,tiles);
	assert(tiles.empty());
	manager.collect_mirrored_tiles(nullptr,dim_x,dim_y,tiles);
	assert(tiles.empty());
	}

	// A paused game: the per-tile arrays keep changing (units sharing a tile are shown in
	// turn, markers blink) at a standing simulation tick. Whatever they show, nothing
	// stepped, so nothing may move or turn: the hop below reads as a step at an advancing
	// tick and as a repaint otherwise.
	{
	constexpr int32_t dim=4;
	int32_t empty[dim*dim]={};
	int32_t west[dim*dim]={};
	int32_t east[dim*dim]={};
	const int token=0;
	const void *viewport=&token;
	west[1*dim+2]=50;
	east[2*dim+2]=50;
	visual_animation_managerst manager;
	manager.set_step_duration_ms(100);
	auto input=make_input(viewport,dim,empty);
	input.simulation_tick=400;
	set_layer(input,viewport_visual_layer::center,west,empty);
	run_frame(manager,input,1000);
	set_layer(input,viewport_visual_layer::center,east,west);
	run_frame(manager,input,1016);
	assert(!manager.requires_full_redraw());
	assert(!manager.get_movement(viewport,viewport_visual_layer::center,2,2).active);
	assert(manager.get_facing(viewport,2,2)==native_sprite_facing);
	set_layer(input,viewport_visual_layer::center,west,east);
	run_frame(manager,input,1032);
	assert(!manager.requires_full_redraw());
	assert(manager.get_facing(viewport,1,2)==native_sprite_facing);
	// The same change on a new tick is a step.
	input.simulation_tick=401;
	set_layer(input,viewport_visual_layer::center,east,west);
	run_frame(manager,input,1048);
	assert(manager.requires_full_redraw());
	assert(manager.get_movement(viewport,viewport_visual_layer::center,2,2).active);
	assert(manager.get_facing(viewport,2,2)==visual_facingst::east);
	run_frame(manager,input,1150); // the step ends: its last full redraw
	// Only the first redraw after a tick counts; a later repaint at that tick is
	// presentation, and the sprite that just turned east stays east rather than flipping
	// back.
	set_layer(input,viewport_visual_layer::center,west,east);
	run_frame(manager,input,1200);
	assert(!manager.requires_full_redraw());
	assert(manager.get_facing(viewport,1,2)==native_sprite_facing);
	set_layer(input,viewport_visual_layer::center,east,west);
	run_frame(manager,input,1216);
	assert(!manager.requires_full_redraw());
	assert(manager.get_facing(viewport,2,2)==native_sprite_facing);
	// A tick that advances before the arrays catch up: the redraw showing the step comes
	// a frame later, at the same tick, and still counts, because no filled arrays claimed
	// it yet.
	input.simulation_tick=402;
	run_frame(manager,input,1232); // arrays unchanged
	assert(!manager.requires_full_redraw());
	set_layer(input,viewport_visual_layer::center,west,east);
	run_frame(manager,input,1248);
	assert(manager.requires_full_redraw());
	assert(manager.get_movement(viewport,viewport_visual_layer::center,1,2).active);
	assert(manager.get_facing(viewport,1,2)==visual_facingst::west);
	// An unknown tick (no simulation to ask) keeps every array change a candidate step.
	visual_animation_managerst blind;
	blind.set_step_duration_ms(100);
	auto blind_input=make_input(viewport,dim,empty);
	set_layer(blind_input,viewport_visual_layer::center,west,empty);
	run_frame(blind,blind_input,1000);
	set_layer(blind_input,viewport_visual_layer::center,east,west);
	run_frame(blind,blind_input,1016);
	assert(blind.requires_full_redraw());
	assert(blind.get_facing(viewport,2,2)==visual_facingst::east);
	// A scroll while the tick stands still is a presentation change too, but the landing
	// still carries the step in flight and its facing to the new screen tile.
	int32_t east_shifted[dim*dim]={};
	east_shifted[1*dim+2]=50;
	visual_animation_managerst scrolled;
	scrolled.set_step_duration_ms(100);
	auto scroll_input=make_input(viewport,dim,empty);
	scroll_input.simulation_tick=401;
	set_layer(scroll_input,viewport_visual_layer::center,west,empty);
	run_frame(scrolled,scroll_input,1000);
	scroll_input.simulation_tick=402;
	set_layer(scroll_input,viewport_visual_layer::center,east,west);
	run_frame(scrolled,scroll_input,1016);
	assert(scrolled.get_movement(viewport,viewport_visual_layer::center,2,2).active);
	scroll_input.pan_x=1;
	set_layer(scroll_input,viewport_visual_layer::center,east,east);
	run_frame(scrolled,scroll_input,1032);
	set_layer(scroll_input,viewport_visual_layer::center,east_shifted,east);
	run_frame(scrolled,scroll_input,1048);
	assert(scrolled.get_movement(viewport,viewport_visual_layer::center,1,2).active);
	assert(!scrolled.get_movement(viewport,viewport_visual_layer::center,2,2).active);
	assert(scrolled.get_facing(viewport,1,2)==visual_facingst::east);
	assert(scrolled.get_facing(viewport,2,2)==native_sprite_facing);
	}

	// The bob movement hops once or twice per step above the straight path, returns to the
	// grid at both ends, and its hop is the amount times the direction's multiplier high.
	{
	auto near=[](float a,float b){return std::fabs(a-b)<1e-5f;};
	// The hop is the negative y of the offset beyond the path: y minus the path's share.
	auto hop=[](const movementst &m,float progress,tile_stepst step)
		{
		const sprite_offsetst at=m.position(progress,step);
		return step.y*m.travelled(progress)-at.y;
		};
	const tile_stepst east{1,0},north{0,-1},north_east{1,-1};
	// A plain bob: every multiplier 1, so the hop is the bare hop shape, `high` tall.
	const float high=0.5f;
	std::unique_ptr<movementst> unit=movements.find("bob")->clone();
	assert(apply_movement_settings(*unit,{{"amount",high},{"horizontal",1.0f},
		{"diagonal",1.0f},{"vertical",1.0f}})=="");
	for(int hops:{1,2})
		{
		assert(unit->set("hops",float(hops))=="");
		assert(near(hop(*unit,0.0f,east),0.0f)&&unit->position(0.0f,east).x==0.0f);
		assert(near(hop(*unit,1.0f,east),0.0f)&&unit->position(1.0f,east).x==1.0f);
		float highest=0.0f;
		for(int i=0;i<=1000;++i)
			{
			const float progress=float(i)/1000.0f;
			const sprite_offsetst at=unit->position(progress,east);
			assert(hop(*unit,progress,east)>=0.0f&&at.x>=0.0f&&at.x<=1.0f);
			// The bob follows the default's path, whatever the direction.
			assert(at.x==default_movement().position(progress,east).x);
			assert(unit->position(progress,north).x==0.0f&&
				near(hop(*unit,progress,north),hop(*unit,progress,east)));
			highest=std::max(highest,hop(*unit,progress,east));
			}
		assert(near(highest,high));
		}
	// The hop peaks halfway along the line (the time progress 0.5 is halfway through
	// smoothstep too); with two hops the foot lands there and the peaks are at a quarter
	// and three quarters of the line, which the soft start reaches later than in time.
	assert(unit->set("hops",1.0f)=="");
	assert(near(hop(*unit,0.5f,east),high));
	assert(unit->set("hops",2.0f)=="");
	assert(near(hop(*unit,0.5f,east),0.0f));
	float peak_along=0.0f,peak_hop=0.0f;
	for(int i=0;i<=1000;++i)
		{
		const float progress=float(i)/1000.0f;
		const float along=unit->position(progress,east).x,h=hop(*unit,progress,east);
		if(along<0.5f&&h>peak_hop){peak_hop=h;peak_along=along;}
		}
	assert(std::fabs(peak_along-0.25f)<0.002f&&near(peak_hop,high));
	assert(hop(*unit,0.25f,east)<high);
	// The reach is the amount times the largest multiplier, above the path only, and a list
	// of settings is refused as a whole when its reach passes what the render code repaints:
	// the movement is then left as it was.
	auto reach_of=[&](float amount,float h,float d,float v)
		{
		std::unique_ptr<movementst> bob=movements.find("bob")->clone();
		return apply_movement_settings(*bob,{{"amount",amount},{"horizontal",h},
			{"diagonal",d},{"vertical",v}});
		};
	assert(reach_of(0.10f,1.0f,2.4f,2.7f)=="");
	assert(reach_of(0.30f,1.0f,3.0f,2.0f)=="");
	assert(reach_of(0.30f,1.0f,3.0f,3.5f)==
		"bob settings reach 1.05 tile beyond the path; the most the plugin repaints is 0.90");
	assert(reach_of(0.30f,4.0f,1.0f,1.0f)!="");
	assert(reach_of(0.90f,1.0f,1.0f,1.0f)=="");
	assert(reach_of(0.91f,1.0f,1.0f,1.0f)!="");
	{
	std::unique_ptr<movementst> kept=movements.find("bob")->clone();
	assert(apply_movement_settings(*kept,{{"amount",0.3f},{"vertical",3.5f}})!="");
	assert(kept->settings()==movements.find("bob")->settings());
	}
	// Each setting checks its own range, NaN included; an unknown name is refused, and the
	// movements without settings refuse every name.
	{
	std::unique_ptr<movementst> bob=movements.find("bob")->clone();
	assert(bob->set("amount",0.0f)=="bob amount must be within 0..0.90 tile");
	assert(bob->set("amount",0.91f)!=""&&bob->set("amount",std::nanf(""))!="");
	assert(bob->set("amount",0.9f)==""&&bob->set("amount",0.2f)=="");
	assert(bob->set("horizontal",5.1f)=="bob multipliers must be within 0..5");
	assert(bob->set("diagonal",-0.1f)!=""&&bob->set("vertical",std::nanf(""))!="");
	assert(bob->set("vertical",0.0f)==""&&bob->set("vertical",5.0f)=="");
	assert(bob->set("hops",3.0f)=="hops per step must be 1 or 2");
	assert(bob->set("hops",1.5f)!=""&&bob->set("hops",std::nanf(""))!="");
	assert(bob->set("stride",1.0f)=="bob has no setting named stride");
	assert(linear.settings().empty()&&default_movement().settings().empty());
	assert(std::unique_ptr<movementst>(linear.clone())->set("amount",0.1f)==
		"linear has no setting named amount");
	assert(check_movement_settings("bounce",{})=="unknown movement bounce");
	assert(check_movement_settings("bob",{{"amount",0.4f}})!="");
	assert(check_movement_settings("bob",{{"amount",0.3f},{"hops",1.0f}})=="");
	assert(check_movement_settings("linear",{})=="");
	}
	// The default settings fit, and scale the hop by the step's direction: a one-hop step
	// peaks at the amount times the multiplier.
	{
	std::unique_ptr<movementst> defaults=movements.find("bob")->clone();
	const std::vector<movement_settingst> expected={{"amount",0.10f},{"horizontal",1.0f},
		{"diagonal",2.4f},{"vertical",2.7f},{"hops",2.0f}};
	assert(defaults->settings()==expected);
	assert(near(defaults->reach().above,0.27f)&&defaults->reach().below==0.0f);
	assert(defaults->set("hops",1.0f)=="");
	assert(near(hop(*defaults,0.5f,east),0.10f));
	assert(near(hop(*defaults,0.5f,north_east),0.24f));
	assert(near(hop(*defaults,0.5f,north),0.27f));
	assert(hop(*defaults,0.0f,north)==0.0f);
	// A retargeted step starts from a fractional source: a delta under half a tile on an
	// axis counts as no move on it.
	assert(near(hop(*defaults,0.5f,{0.4f,-1.0f}),0.27f));
	assert(near(hop(*defaults,0.5f,{1.0f,0.4f}),0.10f));
	assert(near(hop(*defaults,0.5f,{0.6f,-0.6f}),0.24f));
	// At exactly half a tile the source rounds up: a delta of +0.5 is no move on that axis,
	// -0.5 is one.
	assert(near(hop(*defaults,0.5f,{1.0f,0.5f}),0.10f));
	assert(near(hop(*defaults,0.5f,{1.0f,-0.5f}),0.24f));
	assert(near(hop(*defaults,0.5f,{0.5f,-1.0f}),0.27f));
	assert(near(hop(*defaults,0.5f,{-0.5f,-1.0f}),0.24f));
	// The other movements stay on the path whatever the step.
	assert(hop(default_movement(),0.5f,north)==0.0f);
	assert(hop(linear,0.5f,north_east)==0.0f&&linear.position(0.5f,north_east).x==0.5f);
	}
	// The manager hands the offset out with the movement, from the movement it was given,
	// and the default movement's offset beside it as the fallback.
	{
	visual_animation_managerst lifted;
	std::unique_ptr<movementst> bob=movements.find("bob")->clone();
	assert(apply_movement_settings(*bob,{{"hops",1.0f},{"amount",0.2f}})=="");
	lifted.set_movement(*bob);
	int32_t lift_empty[9]={},lift_before[9]={},lift_after[9]={};
	lift_before[0]=7;lift_after[3]=7; // x 0 -> 1 on row 0: a horizontal step
	auto input=make_input(viewport,3,lift_empty);
	set_layer(input,viewport_visual_layer::center,lift_before,lift_empty);
	run_frame(lifted,input,1000);
	set_layer(input,viewport_visual_layer::center,lift_after,lift_before);
	run_frame(lifted,input,1075);
	set_layer(input,viewport_visual_layer::center,lift_after,lift_after);
	run_frame(lifted,input,1150);
	const visual_movement_renderst mid=lifted.get_movement(viewport,viewport_visual_layer::center,1,0);
	assert(mid.active&&mid.offset_x==0.5f&&near(mid.offset_y,-0.2f*1.0f));
	assert(mid.fallback_x==0.5f&&mid.fallback_y==0.0f);
	lifted.set_movement(default_movement());
	const visual_movement_renderst plain=lifted.get_movement(viewport,viewport_visual_layer::center,1,0);
	assert(plain.offset_y==0.0f&&plain.fallback_x==plain.offset_x&&plain.fallback_y==0.0f);
	}
	// The step the manager hands a movement is the movement's own: a vertical and a diagonal
	// step hop by their multipliers, not the horizontal one.
	for(const auto &[target_x,target_y,mult]:
		{std::tuple{0,1,2.7f},std::tuple{1,1,2.4f}})
	{
	visual_animation_managerst lifted;
	std::unique_ptr<movementst> bob=movements.find("bob")->clone();
	assert(apply_movement_settings(*bob,{{"hops",1.0f},{"amount",0.2f}})=="");
	lifted.set_movement(*bob);
	int32_t lift_empty[9]={},lift_before[9]={},lift_after[9]={};
	lift_before[0]=7;lift_after[target_x*3+target_y]=7;
	auto input=make_input(viewport,3,lift_empty);
	set_layer(input,viewport_visual_layer::center,lift_before,lift_empty);
	run_frame(lifted,input,1000);
	set_layer(input,viewport_visual_layer::center,lift_after,lift_before);
	run_frame(lifted,input,1075);
	set_layer(input,viewport_visual_layer::center,lift_after,lift_after);
	run_frame(lifted,input,1150);
	const visual_movement_renderst mid=
		lifted.get_movement(viewport,viewport_visual_layer::center,target_x,target_y);
	assert(mid.active&&mid.offset_x==0.5f*target_x&&
		near(mid.offset_y,0.5f*target_y-0.2f*mult));
	assert(mid.fallback_x==0.5f*target_x&&mid.fallback_y==0.5f*target_y);
	}
	}

	// The creature-level fallback decision: any rider that must fall back takes its whole
	// creature with it, riders copy their root, chains of any depth resolve, and independent
	// groups do not affect each other.
	{
	// 0: centre A; 1: fragment of A; 2: icon riding on fragment 1 (depth 2); 3: centre B;
	// 4: icon on B; 5: a lone item with no creature.
	const std::vector<int32_t> anchors={-1,0,1,-1,3,-1};
	std::vector<bool> fell={false,false,true,false,false,true};
	resolve_creature_fallback(anchors,fell);
	assert(fell[0]&&fell[1]&&fell[2]); // the grandchild's blocked row stops all of A
	assert(!fell[3]&&!fell[4]);        // B is untouched
	assert(fell[5]);
	std::vector<bool> none={false,false,false,false,false,true};
	resolve_creature_fallback(anchors,none);
	assert(!none[0]&&!none[1]&&!none[2]&&!none[3]&&!none[4]&&none[5]);
	std::vector<bool> root_off={true,false,false,false,false,false};
	resolve_creature_fallback(anchors,root_off);
	assert(root_off[0]&&root_off[1]&&root_off[2]&&!root_off[3]&&!root_off[4]);
	std::vector<bool> empty;
	resolve_creature_fallback({},empty);
	}
	printf("visual animation manager tests: OK\n");
}
