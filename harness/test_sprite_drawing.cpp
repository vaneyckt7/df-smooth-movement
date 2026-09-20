// SPDX-License-Identifier: MIT
// Checks sprite_drawing.h: the rectangle the plugin asks SDL to copy a gliding sprite into,
// the smaller rectangle a hauled item's icon goes in, and which of SDL's two copy calls each
// sprite goes through. Nothing here draws. The plugin reaches SDL through a table of function
// pointers it binds at load (sdl_apist in plugin_state.h), so this suite fills that table
// with functions that record what was asked for, which is the whole of what the drawing code
// does: everything else about a frame is decided before it is called.

#include "df/renderer_2d_base.h"

#include "sprite_drawing.h"
#include "sprite_proxies.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace {

int failures=0;

// One copy the drawing code asked for. `extended` says it went through SDL_RenderCopyExF,
// which is the only call that can flip a sprite; the plain SDL_RenderCopyF cannot.
struct copy_callst
{
	bool extended;
	SDL_Renderer *renderer;
	SDL_Texture *texture;
	bool whole_texture;
	SDL_FRect destination;
	double angle;
	bool about_the_centre;
	SDL_RendererFlip flip;
};

std::vector<copy_callst> calls;

// The source rectangle is null in both calls, which is SDL for "the whole texture": the
// plugin copies a tile's sprite entire and never a part of one.
int record_copy(
	SDL_Renderer *renderer,
	SDL_Texture *texture,
	const SDL_Rect *source,
	const SDL_FRect *destination)
{
	calls.push_back(
		{false,renderer,texture,source==nullptr,*destination,0.0,true,SDL_FLIP_NONE});
	return 0;
}

int record_copy_ex(
	SDL_Renderer *renderer,
	SDL_Texture *texture,
	const SDL_Rect *source,
	const SDL_FRect *destination,
	double angle,
	const SDL_FPoint *about,
	SDL_RendererFlip flip)
{
	calls.push_back(
		{true,renderer,texture,source==nullptr,*destination,angle,about==nullptr,flip});
	return 0;
}

// A table with both calls bound, which is what load_sdl leaves behind on every supported
// build. Passing false drops the flipping call, the one case the drawing code guards against.
sdl_apist recording_sdl(bool with_flip=true)
{
	calls.clear();
	sdl_apist sdl;
	sdl.render_copy_f=record_copy;
	sdl.render_copy_ex_f=with_flip?record_copy_ex:nullptr;
	return sdl;
}

// Stand-ins for the two pointers the drawing code only passes along. SDL_Renderer and
// SDL_Texture are opaque to the plugin as well, so their addresses are all a test can check.
char renderer_storage=0;
char texture_storage=0;
SDL_Renderer *const sdl_renderer=reinterpret_cast<SDL_Renderer *>(&renderer_storage);
SDL_Texture *const sprite_texture=reinterpret_cast<SDL_Texture *>(&texture_storage);

df::renderer_2d_base make_renderer(int32_t zoom,int32_t origin_x,int32_t origin_y)
{
	df::renderer_2d_base renderer;
	renderer.sdl_renderer=&renderer_storage;
	renderer.viewport_zoom_factor=zoom;
	renderer.origin_x=origin_x;
	renderer.origin_y=origin_y;
	return renderer;
}

render_proxyst make_proxy(
	float source_x_tiles,
	float source_y_tiles,
	int32_t target_x,
	int32_t target_y,
	float offset_x_tiles,
	float offset_y_tiles,
	bool mirrored=false,
	int32_t mirror_shift=0)
{
	render_proxyst proxy={};
	proxy.source_x_tiles=source_x_tiles;
	proxy.source_y_tiles=source_y_tiles;
	proxy.target_x=target_x;
	proxy.target_y=target_y;
	proxy.offset_x_tiles=offset_x_tiles;
	proxy.offset_y_tiles=offset_y_tiles;
	proxy.texture=sprite_texture;
	proxy.mirrored=mirrored;
	proxy.mirror_shift=mirror_shift;
	return proxy;
}

// The rectangles are sums of tile sizes and fractions of one, and the icon's insets are
// tenths and twentieths, which are not exact in binary. An eighth of a pixel covers that and
// is far below any wrong answer this code can produce, all of which are off by a whole tile,
// a whole inset, or the difference between a tile and a shifted one.
void expect_rect(const char *name,const SDL_FRect &got,float x,float y,float w,float h)
{
	if(std::fabs(got.x-x)>0.125f||std::fabs(got.y-y)>0.125f||
		std::fabs(got.w-w)>0.125f||std::fabs(got.h-h)>0.125f)
		printf("%s: %.3f %.3f %.3f x %.3f, expected %.3f %.3f %.3f x %.3f\n",
			name,double(got.x),double(got.y),double(got.w),double(got.h),
			double(x),double(y),double(w),double(h)),++failures;
}

// Every case draws one sprite, so every case expects exactly one recorded call. A case that
// drew nothing, or twice, would otherwise read its answer off a neighbour's call.
const copy_callst *only_call(const char *name)
{
	if(calls.size()!=1)
		{
		printf("%s: %zu calls, expected 1\n",name,calls.size()),++failures;
		return nullptr;
		}
	return &calls.front();
}

void expect_passthrough(const char *name,const copy_callst &call)
{
	if(call.renderer!=sdl_renderer)printf("%s: wrong renderer\n",name),++failures;
	if(call.texture!=sprite_texture)printf("%s: wrong texture\n",name),++failures;
	if(!call.whole_texture)printf("%s: copied part of the texture\n",name),++failures;
}

void test_plain_sprite()
{
	const df::renderer_2d_base renderer=make_renderer(128,10,20);
	{
	// A sprite that has not moved: the destination is its tile, a tile square, and it goes
	// through the plain copy because nothing is flipped.
	const sdl_apist sdl=recording_sdl();
	draw_proxy(sdl,&renderer,make_proxy(4.0f,3.0f,4,3,0.0f,0.0f));
	if(const copy_callst *call=only_call("resting sprite"))
		{
		if(call->extended)printf("resting sprite: flipped call\n"),++failures;
		expect_passthrough("resting sprite",*call);
		expect_rect("resting sprite",call->destination,138.0f,116.0f,32.0f,32.0f);
		}
	}
	{
	// Halfway through a step east. The destination is the placement's corner and stays a
	// tile square: a gliding sprite is not stretched, only moved.
	const sdl_apist sdl=recording_sdl();
	draw_proxy(sdl,&renderer,make_proxy(3.0f,3.0f,4,3,0.5f,0.0f));
	if(const copy_callst *call=only_call("gliding sprite"))
		expect_rect("gliding sprite",call->destination,122.0f,116.0f,32.0f,32.0f);
	}
}

void test_mirrored_sprite()
{
	const df::renderer_2d_base renderer=make_renderer(128,10,20);
	{
	// A creature facing the mirrored way, on the layer it flips in place on: collect_proxies
	// gives that layer a shift of zero, so the rectangle is the one an unflipped sprite would
	// get and the flip is entirely SDL's doing. The angle is zero and the centre is null,
	// which is SDL for "flip about the destination rectangle itself".
	const sdl_apist sdl=recording_sdl();
	draw_proxy(sdl,&renderer,make_proxy(3.0f,3.0f,4,3,0.5f,0.0f,true,0));
	if(const copy_callst *call=only_call("mirrored sprite"))
		{
		if(!call->extended)printf("mirrored sprite: plain call cannot flip\n"),++failures;
		if(call->flip!=SDL_FLIP_HORIZONTAL)
			printf("mirrored sprite: flip %d\n",int(call->flip)),++failures;
		if(call->angle!=0.0)printf("mirrored sprite: angle %.3f\n",call->angle),++failures;
		if(!call->about_the_centre)printf("mirrored sprite: given a pivot\n"),++failures;
		expect_passthrough("mirrored sprite",*call);
		expect_rect("mirrored sprite",call->destination,122.0f,116.0f,32.0f,32.0f);
		}
	}
	{
	// A fragment of the same creature, one tile from the tile it flips about, lands one tile
	// the other side of it, which is a shift of two tiles. collect_proxies works the shift out
	// as mirrored_tile_x(x,x+center_x)-x, which comes to twice center_x, and center_x is only
	// -1, 0 or +1, so the only shifts the plugin ever sends are -2, 0 and 2. The shift is in
	// tiles and the destination is in pixels, so it is the tile size that converts between
	// them. collect_proxies sets a shift only on a sprite it also marks mirrored, so this is
	// the pairing that reaches the drawing.
	const sdl_apist sdl=recording_sdl();
	draw_proxy(sdl,&renderer,make_proxy(3.0f,3.0f,4,3,0.5f,0.0f,true,2));
	if(const copy_callst *call=only_call("shifted fragment"))
		expect_rect("shifted fragment",call->destination,186.0f,116.0f,32.0f,32.0f);
	}
	{
	// The same shift of two at a zoom whose tile is not 32 pixels: the shift is in tiles, so
	// it scales with the zoom like everything else. At zoom 50 a tile is 12 pixels and tile 4
	// begins at 50, not at 4*12, so the sprite sits at 50-12+6 and the shift adds 2*12 on top.
	const df::renderer_2d_base zoomed=make_renderer(50,0,0);
	const sdl_apist sdl=recording_sdl();
	draw_proxy(sdl,&zoomed,make_proxy(3.0f,3.0f,4,3,0.5f,0.0f,true,2));
	if(const copy_callst *call=only_call("shifted fragment at zoom 50"))
		expect_rect("shifted fragment at zoom 50",call->destination,68.0f,37.0f,12.0f,12.0f);
	}
	{
	// Without the flipping call there is nowhere to send a mirrored sprite but the plain
	// copy, which draws it facing the wrong way rather than not at all. This is a guard, not
	// a supported build: load_sdl refuses to bind a partial SDL and plugin_enable then leaves
	// the render hook off, so a frame never reaches here with the call missing.
	const sdl_apist sdl=recording_sdl(false);
	draw_proxy(sdl,&renderer,make_proxy(3.0f,3.0f,4,3,0.5f,0.0f,true,2));
	if(const copy_callst *call=only_call("mirrored without the flipping call"))
		{
		if(call->extended)
			printf("mirrored without the flipping call: used it anyway\n"),++failures;
		expect_rect(
			"mirrored without the flipping call",call->destination,186.0f,116.0f,32.0f,32.0f);
		}
	}
}

void test_carried_item()
{
	const df::renderer_2d_base renderer=make_renderer(128,10,20);
	// The icon rides the creature's movement: its proxy carries the same source, target and
	// offset as the creature's, and the icon sits inset inside that tile rather than filling
	// it, so the creature underneath stays visible. It is never flipped, so it goes through
	// the plain copy whatever the creature is doing.
	carried_item_proxyst proxy={};
	proxy.source_x_tiles=3.0f;
	proxy.source_y_tiles=3.0f;
	proxy.target_x=4;
	proxy.target_y=3;
	proxy.offset_x_tiles=0.5f;
	proxy.offset_y_tiles=0.0f;
	proxy.texture=sprite_texture;
	const sdl_apist sdl=recording_sdl();
	draw_carried_item_proxy(sdl,&renderer,proxy);
	if(const copy_callst *call=only_call("carried item"))
		{
		if(call->extended)printf("carried item: flipped call\n"),++failures;
		expect_passthrough("carried item",*call);
		// The same corner the creature's own sprite gets, plus the icon's insets: a twentieth
		// of a tile in from the left, a fifth down from the top, and seven tenths of a tile
		// square (carried_item_icon_rect in visual_animation.h).
		expect_rect(
			"carried item",call->destination,
			122.0f+32.0f*0.05f,116.0f+32.0f*0.2f,32.0f*0.7f,32.0f*0.7f);
		}
}

} // namespace

int main()
{
	test_plain_sprite();
	test_mirrored_sprite();
	test_carried_item();
	if(failures!=0){printf("sprite drawing tests: %d failures\n",failures);return 1;}
	printf("sprite drawing tests: OK\n");
	return 0;
}
