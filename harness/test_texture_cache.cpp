// SPDX-License-Identifier: MIT
// Checks texture_cache.h: which texture the plugin gets back for a texture position, which
// item a creature counts as hauling, and which texture position that item's material names.
// Only the first of those three reaches the recordings, which was measured rather than
// assumed. Looking a texture up runs on every frame that draws a creature, and a build of it
// that finds nothing changes all four digests. The other two are never entered: no unit in
// any of the four recordings was hauling anything, and in fortress-hauled-400 -- the one
// recorded with the icons switched on -- the hauled-item lookup is asked 5998 times and
// finds an item on none of them. Every mutant of those two that was replayed left all four
// digests and repaint counts exactly as they were, a build that draws no icon at all
// included. So for them this suite is not the better guard, it is the only one.

#include "texture_cache.h"

#include <cstdint>
#include <cstdio>
#include <unordered_map>
#include <vector>

namespace {

int failures=0;

constexpr int32_t dim_x=2,dim_y=3,tile_count=dim_x*dim_y;

// The game's key for a texture: the texture position, the colour the plugin always asks for
// -- a white foreground on a black background, which is the sprite drawn as it is -- and the
// flag for a sprite whose background is left unpainted. Written out here rather than taken
// from the header, so that a key the header builds differently is a miss.
df::texture_fullid key(int32_t texpos,bool transparent_background=true)
{
	df::texture_fullid id;
	id.texpos=texpos;
	id.r=id.g=id.b=1.0f;
	id.br=id.bg=id.bb=0.0f;
	id.flag=transparent_background?
		uint32_t(df::texture_fullid_flag::mask_transparent_background):
		uint32_t(0);
	return id;
}

// A texture is compared and handed on, never read, so any distinct address is one.
SDL_Texture *fake_texture(int32_t texpos)
{
	static char textures[256];
	return reinterpret_cast<SDL_Texture *>(&textures[texpos&0xff]);
}

struct repaint_callst
{
	int32_t x;
	int32_t y;
};

// texture_cache.h is templated over the renderer, so a stand-in needs the two things it uses:
// the cache the game fills as it draws, and the repaint. This one fills the cache the way the
// game does, with a texture for whatever the tile says at the moment of the repaint, so that
// staging the wrong thing, or nothing, comes back as no texture.
struct cache_rendererst
{
	struct { std::unordered_map<df::texture_fullid,void *> tile_cache; } tile_cache;
	std::vector<repaint_callst> calls;
	bool fills_the_cache=true;
	int32_t staged_seen=-1;

	void update_viewport_tile(df::graphic_viewportst *vp,int32_t x,int32_t y)
		{
		calls.push_back({x,y});
		if(!fills_the_cache)return;
		staged_seen=vp->screentexpos_background_two[x*vp->dim_y+y];
		tile_cache.tile_cache[key(staged_seen)]=fake_texture(staged_seen);
		}
};

// The one per-tile array the header stages on, and the layout it works the tile out from.
// Nothing here reads the rest of the viewport. The cases below stage on tile (1,0): its index
// is 3, which the wrong way round reads as (0,1), so a tile worked out by dividing and taking
// the remainder the other way round is a different tile.
struct viewportst
{
	df::graphic_viewportst vp{};
	std::vector<int32_t> background_two;

	viewportst():background_two(tile_count,0)
		{
		vp.dim_x=dim_x;vp.dim_y=dim_y;
		vp.screentexpos_background_two=background_two.data();
		}

	static int32_t index_of(int32_t x,int32_t y){return x*dim_y+y;}
};

template<typename T>
void expect_pointer(const char *name,const T *got,const T *expected)
{
	if(got!=expected)
		printf("%s: %p, expected %p\n",name,(const void *)got,(const void *)expected),++failures;
}

void expect_value(const char *name,long long got,long long expected)
{
	if(got!=expected)printf("%s: %lld, expected %lld\n",name,got,expected),++failures;
}

void expect_calls(const char *name,const cache_rendererst &renderer,size_t expected)
{
	if(renderer.calls.size()!=expected)
		printf("%s: %zu repaints, expected %zu\n",name,renderer.calls.size(),expected),
			++failures;
}

void expect_tile(const char *name,const repaint_callst &call,int32_t x,int32_t y)
{
	if(call.x!=x||call.y!=y)
		printf("%s: repainted (%d,%d), expected (%d,%d)\n",name,call.x,call.y,x,y),++failures;
}

void test_texture_comes_from_the_cache()
{
	cache_rendererst renderer;
	renderer.tile_cache.tile_cache[key(7)]=fake_texture(7);
	renderer.tile_cache.tile_cache[key(8,false)]=fake_texture(8);
	// Texture position zero is the game's word for nothing there, and nothing is what it
	// gets even with an entry sitting under that key: a hit on it would draw an empty tile
	// over the one underneath.
	renderer.tile_cache.tile_cache[key(0)]=fake_texture(1);
	expect_pointer("texture position zero",cached_texture(&renderer,0),(SDL_Texture *)nullptr);
	expect_pointer("cached",cached_texture(&renderer,7),fake_texture(7));
	expect_pointer("never drawn",cached_texture(&renderer,9),(SDL_Texture *)nullptr);
	// The flag is part of the key. The same sprite drawn with its background painted and
	// without is two textures to the game, and the plugin wants the one it can put on top of
	// what is already there.
	expect_pointer("transparent asked of an opaque entry",
		cached_texture(&renderer,8),(SDL_Texture *)nullptr);
	expect_pointer("opaque asked of a transparent entry",
		cached_texture(&renderer,7,false),(SDL_Texture *)nullptr);
	expect_pointer("opaque",cached_texture(&renderer,8,false),fake_texture(8));
}

void test_texture_key_carries_the_colour()
{
	// The same texture position and flag with a different colour is a separate texture to the
	// game -- a sprite the game shaded for a wall behind it, say -- and the plugin must come
	// away with the plain one rather than whichever entry it meets first.
	cache_rendererst renderer;
	df::texture_fullid shaded=key(7);
	shaded.br=0.5f;
	renderer.tile_cache.tile_cache[shaded]=fake_texture(1);
	renderer.tile_cache.tile_cache[key(7)]=fake_texture(7);
	expect_pointer("plain sprite among shaded ones",
		cached_texture(&renderer,7),fake_texture(7));
}

void test_hauled_item_is_the_first_one_carried()
{
	expect_pointer("no unit",hauled_item(nullptr),(df::item *)nullptr);
	df::unit nothing_carried;
	expect_pointer("empty inventory",hauled_item(&nothing_carried),(df::item *)nullptr);

	df::item worn,first,second;
	df::unit_inventory_item worn_entry{&worn,df::inv_item_role_type::Worn};
	df::unit_inventory_item itemless_entry{nullptr,df::inv_item_role_type::Hauled};
	df::unit_inventory_item first_entry{&first,df::inv_item_role_type::Hauled};
	df::unit_inventory_item second_entry{&second,df::inv_item_role_type::Hauled};
	// A null entry and an entry carrying no item are both things a unit's inventory holds;
	// worn things are not hauled, and a creature hauling two gives up the second, because one
	// icon is all that fits on a tile.
	df::unit unit;
	unit.inventory={nullptr,&worn_entry,&itemless_entry,&first_entry,&second_entry};
	expect_pointer("hauled",hauled_item(&unit),&first);

	df::unit only_worn;
	only_worn.inventory={&worn_entry};
	expect_pointer("nothing hauled",hauled_item(&only_worn),(df::item *)nullptr);
}

void test_item_texpos_reads_the_material()
{
	df::material material;
	material.boulder_texpos1=11;material.boulder_texpos2=12;
	material.bar_texpos=13;material.wood_texpos=14;
	expect_value("no item",item_texpos(nullptr),0);
	df::item boulder{df::item_type::BOULDER,&material};
	expect_value("boulder",item_texpos(&boulder),11);
	// Some materials carry only the second boulder sprite.
	material.boulder_texpos1=0;
	expect_value("boulder with only the second sprite",item_texpos(&boulder),12);
	material.boulder_texpos1=11;
	df::item bar{df::item_type::BAR,&material};
	expect_value("bar",item_texpos(&bar),13);
	df::item wood{df::item_type::WOOD,&material};
	expect_value("wood",item_texpos(&wood),14);
	// Everything else is drawn as an icon the material says nothing about, so there is no
	// sprite to carry along with the creature and the plugin leaves it where the game put it.
	df::item other{df::item_type::OTHER,&material};
	expect_value("another kind of item",item_texpos(&other),0);
	// An item the game cannot name a material for reaches here too, and reading the sprites
	// off it would be reading through nothing.
	df::item nameless{df::item_type::BOULDER,nullptr};
	nameless.harness_no_material=true;
	expect_value("item with no material",item_texpos(&nameless),0);
}

void test_viewport_texture_taken_from_the_cache()
{
	viewportst viewport;
	plugin_statest state;
	cache_rendererst renderer;
	const int32_t index=viewportst::index_of(1,0);
	// What the tile is showing this frame, which the game is about to draw.
	viewport.background_two[index]=5;
	expect_pointer("no texture position",
		cached_viewport_texture(state,&renderer,&viewport.vp,index,0),(SDL_Texture *)nullptr);
	expect_calls("no texture position",renderer,0);

	// Already drawn this session, so the game has it and the tile is left alone: a repaint
	// here is a whole tile's work for a texture that is already to hand.
	renderer.tile_cache.tile_cache[key(7)]=fake_texture(7);
	expect_pointer("already cached",
		cached_viewport_texture(state,&renderer,&viewport.vp,index,7),fake_texture(7));
	expect_calls("already cached",renderer,0);
	expect_value("already cached, stats repaints",(long long)state.stats.repaints.load(),0);
	expect_value("already cached, the tile",viewport.background_two[index],5);
}

void test_viewport_texture_stages_a_tile_to_make_one()
{
	viewportst viewport;
	plugin_statest state;
	cache_rendererst renderer;
	const int32_t index=viewportst::index_of(1,0);
	viewport.background_two[index]=5;
	expect_pointer("staged",
		cached_viewport_texture(state,&renderer,&viewport.vp,index,7),fake_texture(7));
	expect_calls("staged",renderer,1);
	if(renderer.calls.size()==1)expect_tile("staged",renderer.calls[0],1,0);
	// The repaint has to find the item on the tile, or it draws the floor again and the cache
	// comes away without the item's texture in it.
	expect_value("what the repaint saw on the tile",renderer.staged_seen,7);
	// And the tile has to be put back, because the frame it was borrowed from is still going
	// to be drawn: left staged, the floor there is an item for the rest of the frame.
	expect_value("the tile afterwards",viewport.background_two[index],5);
	expect_value("staged, stats repaints",(long long)state.stats.repaints.load(),1);
	expect_value("staged, hook repaints",state.render.hook_repaints,1);
}

void test_viewport_texture_gives_up_without_a_texture()
{
	{
	// A viewport whose background layer has been freed: there is nowhere to stage the item,
	// and staging it anyway writes through a null pointer.
	viewportst viewport;
	plugin_statest state;
	cache_rendererst renderer;
	viewport.vp.screentexpos_background_two=nullptr;
	expect_pointer("no layer to stage on",
		cached_viewport_texture(state,&renderer,&viewport.vp,viewportst::index_of(1,0),7),
		(SDL_Texture *)nullptr);
	expect_calls("no layer to stage on",renderer,0);
	}
	{
	// A repaint the game makes no texture out of leaves the caller with nothing to draw --
	// and the tile still has to come back, repaint or no repaint.
	viewportst viewport;
	plugin_statest state;
	cache_rendererst renderer;
	renderer.fills_the_cache=false;
	const int32_t index=viewportst::index_of(1,0);
	viewport.background_two[index]=5;
	expect_pointer("repaint made no texture",
		cached_viewport_texture(state,&renderer,&viewport.vp,index,7),(SDL_Texture *)nullptr);
	expect_calls("repaint made no texture",renderer,1);
	expect_value("the tile afterwards",viewport.background_two[index],5);
	}
}

} // namespace

int main()
{
	test_texture_comes_from_the_cache();
	test_texture_key_carries_the_colour();
	test_hauled_item_is_the_first_one_carried();
	test_item_texpos_reads_the_material();
	test_viewport_texture_taken_from_the_cache();
	test_viewport_texture_stages_a_tile_to_make_one();
	test_viewport_texture_gives_up_without_a_texture();
	if(failures!=0){printf("texture cache tests: %d failures\n",failures);return 1;}
	printf("texture cache tests: OK\n");
	return 0;
}
