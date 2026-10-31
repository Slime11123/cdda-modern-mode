#include "fp3d_materials.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <utility>
#include <vector>

#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "fp3d_snapshot.h"
#include "mapdata.h"
#include "rng.h"
#include "type_id.h"

TEST_CASE( "fp3d_materials_resolve_season_and_native_looks_like", "[fp3d][materials]" )
{
    tileset set;
    set.create_tile_type( "t_pavement_y", tile_type{} );
    set.create_tile_type( "t_pavement_y_season_winter", tile_type{} );
    const tile_type *base = set.find_tile_type( "t_pavement_y" );
    const tile_type *winter = set.find_tile_type( "t_pavement_y_season_winter" );
    REQUIRE( base );
    REQUIRE( winter );
    CHECK( fp3d_materials::resolve_terrain( set, "t_pavement_y", SPRING ) == base );
    CHECK( fp3d_materials::resolve_terrain( set, "t_pavement_y", WINTER ) == winter );
    REQUIRE( ter_str_id( "t_thconc_y" ).is_valid() );
    REQUIRE( ter_str_id( "t_thconc_y" )->looks_like == "t_pavement_y" );
    CHECK( fp3d_materials::resolve_terrain( set, "t_thconc_y", WINTER ) == winter );
    CHECK_FALSE( fp3d_materials::resolve_terrain( set, "fp3d_unknown_terrain", SPRING ) );
    CHECK_FALSE( fp3d_materials::resolve_terrain( set, "", SPRING ) );
    CHECK_FALSE( fp3d_materials::lookup( "fp3d_unknown_terrain" ) );
}

TEST_CASE( "fp3d_materials_choose_stable_sprites_without_gameplay_rng", "[fp3d][materials]" )
{
    tile_type tile;
    tile.bg.add( std::vector<int> { 3 }, 1 );
    tile.fg.add( std::vector<int> { 5, 7, 8, 9 }, 1 );
    tile.fg.add( std::vector<int> { 6 }, 3 );
    const auto before = rng_get_engine();
    const auto chosen = fp3d_materials::choose_sprites( tile, "t_floor" );
    CHECK( chosen.background == 3 );
    CHECK( ( chosen.foreground == 5 || chosen.foreground == 6 ) );
    for( int i = 0; i < 20; ++i ) {
        const auto refreshed = fp3d_materials::choose_sprites( tile, "t_floor" );
        CHECK( refreshed.background == chosen.background );
        CHECK( refreshed.foreground == chosen.foreground );
    }
    CHECK( rng_get_engine() == before );
    const auto absent = fp3d_materials::choose_sprites( tile_type{}, "missing" );
    CHECK( absent.foreground == -1 );
    CHECK( absent.background == -1 );
}

TEST_CASE( "fp3d_materials_resolve_sprite_variants_and_worn_layers", "[fp3d][materials]" )
{
    tileset set;
    for( const std::string &id : {
             "mon_zombie", "mon_zombie_var_special", "corpse",
             "overlay_male_worn_shirt", "overlay_worn_shirt",
             "overlay_worn_shirt_var_red", "overlay_male_mutation_active_test",
             "overlay_mutation_test_var_red"
         } ) {
        set.create_tile_type( id, tile_type{} );
    }
    fp3d_snapshot::sprite image;
    image.id = "mon_zombie";
    image.variant = "special";
    CHECK( fp3d_materials::resolve_sprite( set, image, SPRING ) ==
           set.find_tile_type( "mon_zombie_var_special" ) );
    image.variant.clear();
    CHECK( fp3d_materials::resolve_sprite( set, image, SPRING ) == set.find_tile_type( "mon_zombie" ) );
    image.id = "corpse_mon_zombie";
    image.kind = fp3d_snapshot::sprite_kind::item;
    CHECK( fp3d_materials::resolve_sprite( set, image, SPRING ) == set.find_tile_type( "corpse" ) );
    image.id = "fp3d_missing_sprite";
    CHECK_FALSE( fp3d_materials::resolve_sprite( set, image, SPRING ) );
    CHECK( fp3d_materials::resolve_overlay( set, true, "worn_shirt", "" ) ==
           set.find_tile_type( "overlay_male_worn_shirt" ) );
    CHECK( fp3d_materials::resolve_overlay( set, false, "worn_shirt", "" ) ==
           set.find_tile_type( "overlay_worn_shirt" ) );
    CHECK( fp3d_materials::resolve_overlay( set, true, "worn_shirt", "red" ) ==
           set.find_tile_type( "overlay_worn_shirt_var_red" ) );
    CHECK( fp3d_materials::resolve_overlay( set, true, "mutation_active_test", "red" ) ==
           set.find_tile_type( "overlay_mutation_test_var_red" ) );
    CHECK_FALSE( fp3d_materials::resolve_overlay( set, true, "fp3d_missing_overlay", "" ) );
}

TEST_CASE( "fp3d_materials_sprite_cache_keys_follow_appearance", "[fp3d][materials]" )
{
    fp3d_snapshot::sprite image;
    image.id = "npc_male";
    image.kind = fp3d_snapshot::sprite_kind::character;
    const auto base = fp3d_materials::sprite_key( image );
    image.mirrored = true;
    CHECK( fp3d_materials::sprite_key( image ) == base );
    image.overlays.emplace_back( "worn_shirt", "red" );
    const auto dressed = fp3d_materials::sprite_key( image );
    CHECK( dressed != base );
    image.overlays.front().second = "blue";
    CHECK( fp3d_materials::sprite_key( image ) != dressed );
    const auto male = fp3d_materials::sprite_key( image );
    image.male = false;
    CHECK( fp3d_materials::sprite_key( image ) != male );
}

TEST_CASE( "fp3d_materials_fallback_preserves_renderer_and_handles_no_gl", "[fp3d][materials]" )
{
    // Offscreen CPU renderer: no desktop window or GPU readback is involved.
    SDL_Surface_Ptr surface( SDL_CreateRGBSurfaceWithFormat( 0, 128, 128, 32,
                             SDL_PIXELFORMAT_RGBA8888 ) );
    REQUIRE( surface );
    SDL_Renderer_Ptr renderer( SDL_CreateSoftwareRenderer( surface.get() ) );
    REQUIRE( renderer );
    SDL_Texture_Ptr target( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                            SDL_TEXTUREACCESS_TARGET, 256, 256 ) );
    REQUIRE( target );
    REQUIRE( SDL_SetRenderTarget( renderer.get(), target.get() ) == 0 );
    SDL_RenderSetLogicalSize( renderer.get(), 200, 200 );
    const SDL_Rect viewport{ 4, 6, 180, 170 };
    const SDL_Rect clip{ 2, 3, 50, 40 };
    SDL_RenderSetViewport( renderer.get(), &viewport );
    SDL_RenderSetClipRect( renderer.get(), &clip );
    SDL_SetRenderDrawColor( renderer.get(), 17, 33, 51, 99 );
    SDL_SetRenderDrawBlendMode( renderer.get(), SDL_BLENDMODE_ADD );
    float old_scale_x = 0;
    float old_scale_y = 0;
    SDL_RenderGetScale( renderer.get(), &old_scale_x, &old_scale_y );
    auto set = std::make_shared<tileset>();
    fp3d_materials::set_tileset( set, renderer );
    on_out_of_scope cleanup( []() {
        fp3d_materials::release();
    } );
    fp3d_snapshot::scene_snapshot scene;
    fp3d_snapshot::cell unknown;
    unknown.terrain = "fp3d_unknown_terrain";
    scene.cells.push_back( unknown );
    fp3d_snapshot::sprite absent_sprite;
    absent_sprite.id = "fp3d_unknown_monster";
    scene.sprites.push_back( absent_sprite );
    fp3d_materials::prepare( scene );
    REQUIRE( fp3d_materials::ready() );
    CHECK( SDL_GetRenderTarget( renderer.get() ) == target.get() );
    SDL_Rect restored;
    SDL_RenderGetViewport( renderer.get(), &restored );
    CHECK( SDL_RectEquals( &restored, &viewport ) == SDL_TRUE );
    SDL_RenderGetClipRect( renderer.get(), &restored );
    CHECK( SDL_RectEquals( &restored, &clip ) == SDL_TRUE );
    CHECK( SDL_RenderIsClipEnabled( renderer.get() ) == SDL_TRUE );
    float scale_x = 0;
    float scale_y = 0;
    SDL_RenderGetScale( renderer.get(), &scale_x, &scale_y );
    CHECK( scale_x == old_scale_x );
    CHECK( scale_y == old_scale_y );
    int width = 0;
    int height = 0;
    SDL_RenderGetLogicalSize( renderer.get(), &width, &height );
    CHECK( width == 200 );
    CHECK( height == 200 );
    Uint8 r, g, b, a;
    SDL_GetRenderDrawColor( renderer.get(), &r, &g, &b, &a );
    CHECK( r == 17 );
    CHECK( g == 33 );
    CHECK( b == 51 );
    CHECK( a == 99 );
    SDL_BlendMode blend;
    SDL_GetRenderDrawBlendMode( renderer.get(), &blend );
    CHECK( blend == SDL_BLENDMODE_ADD );
    // Unsupported interop must safely retain the untextured presentation.
    CHECK_FALSE( fp3d_materials::bind() );
    CHECK_FALSE( fp3d_materials::ready() );
    CHECK_FALSE( fp3d_materials::lookup( unknown.terrain ) );
    fp3d_materials::set_tileset( std::make_shared<tileset>(), renderer );
    CHECK_FALSE( fp3d_materials::ready() );
    fp3d_materials::prepare( scene );
    CHECK( fp3d_materials::ready() );
}
#endif
