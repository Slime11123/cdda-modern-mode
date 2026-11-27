#include "fp3d_materials.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
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
    CHECK_FALSE( fp3d_materials::lookup_gpu( "fp3d_unknown_terrain" ) );
    for( const auto &id : { "t_floor", "t_concrete", "t_vitrified_floor", "t_stairs_up" } ) {
        set.create_tile_type( id, tile_type{} );
    }
    CHECK( fp3d_materials::resolve_terrain( set, "t_stairs_up", SPRING ) ==
           set.find_tile_type( "t_concrete" ) );
    CHECK( fp3d_materials::resolve_terrain( set, "t_stairs_down", SPRING ) ==
           set.find_tile_type( "t_concrete" ) );
    CHECK( fp3d_materials::resolve_terrain( set, "t_wood_stairs_up", SPRING ) ==
           set.find_tile_type( "t_floor" ) );
    CHECK( fp3d_materials::resolve_terrain( set, "t_vitrified_stairs_down", SPRING ) ==
           set.find_tile_type( "t_vitrified_floor" ) );
}

TEST_CASE( "fp3d_ground_field_materials_follow_native_connections", "[fp3d][materials][ground_fields]" )
{
    tileset set;
    tile_type base;
    base.multitile = true;
    base.available_subtiles = { "unconnected", "end_piece", "corner", "edge", "t_connection", "center" };
    set.create_tile_type( "fd_blood", tile_type( base ) );
    for( const auto &name : base.available_subtiles ) {
        tile_type child;
        child.rotates = true;
        set.create_tile_type( "fd_blood_" + name, std::move( child ) );
    }
    const std::array<std::string, 16> expected{
        "unconnected", "end_piece", "end_piece", "corner",
        "end_piece", "corner", "edge", "t_connection",
        "end_piece", "edge", "corner", "t_connection",
        "corner", "t_connection", "t_connection", "center" };
    const std::array<int, 16> angles{ 0, 0, 1, 0, 3, 3, 1, 0, 2, 0, 1, 1, 2, 3, 2, 0 };
    for( int mask = 0; mask < 16; ++mask ) {
        CAPTURE( mask );
        int rotation = -1;
        CHECK( fp3d_materials::resolve_ground_field( set, { "fd_blood", 3, mask }, SPRING, rotation ) ==
               set.find_tile_type( "fd_blood_" + expected[mask] ) );
        CHECK( rotation == angles[mask] );
    }
    int rotation = -1;
    set.create_tile_type( "fd_blood_int2", tile_type{} );
    CHECK( fp3d_materials::resolve_ground_field( set, { "fd_blood", 2, 15 }, SPRING, rotation ) ==
           set.find_tile_type( "fd_blood_int2" ) );
    CHECK( rotation == 0 );
    CHECK( fp3d_materials::ground_field_key( { "fd_blood", 3, 1 } ) !=
           fp3d_materials::ground_field_key( { "fd_blood", 3, 8 } ) );
    CHECK_FALSE( fp3d_materials::resolve_ground_field( set, { "missing", 1 }, SPRING, rotation ) );
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

TEST_CASE( "fp3d_materials_atlas_preserves_renderer_without_native_gpu_interop", "[fp3d][materials]" )
{
    // Offscreen CPU renderer: no desktop window or GPU readback is involved.
    SDL_Surface_Ptr surface( CreateSurfaceWithFormat( 0, 128, 128, 32,
                             SDL_PIXELFORMAT_RGBA8888 ) );
    REQUIRE( surface );
    SDL_Renderer_Ptr renderer( SDL_CreateSoftwareRenderer( surface.get() ) );
    REQUIRE( renderer );
    SDL_Texture_Ptr target( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                            SDL_TEXTUREACCESS_TARGET, 256, 256 ) );
    REQUIRE( target );
    REQUIRE( SDL_SetRenderTarget( renderer.get(), target.get() ) );
    SetLogicalRenderSize( renderer.get(), 200, 200 );
    const SDL_Rect viewport{ 4, 6, 180, 170 };
    const SDL_Rect clip{ 2, 3, 50, 40 };
    SDL_SetRenderViewport( renderer.get(), &viewport );
    SDL_SetRenderClipRect( renderer.get(), &clip );
    SDL_SetRenderDrawColor( renderer.get(), 17, 33, 51, 99 );
    SDL_SetRenderDrawBlendMode( renderer.get(), SDL_BLENDMODE_ADD );
    float old_scale_x = 0;
    float old_scale_y = 0;
    SDL_GetRenderScale( renderer.get(), &old_scale_x, &old_scale_y );
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
    SDL_GetRenderViewport( renderer.get(), &restored );
    CHECK( SDL_RectsEqual( &restored, &viewport ) == true );
    SDL_GetRenderClipRect( renderer.get(), &restored );
    CHECK( SDL_RectsEqual( &restored, &clip ) == true );
    CHECK( SDL_RenderClipEnabled( renderer.get() ) == true );
    float scale_x = 0;
    float scale_y = 0;
    SDL_GetRenderScale( renderer.get(), &scale_x, &scale_y );
    CHECK( scale_x == old_scale_x );
    CHECK( scale_y == old_scale_y );
    int width = 0;
    int height = 0;
    GetLogicalRenderSize( renderer.get(), &width, &height );
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
    // Atlas coordinates remain available on the software renderer, but
    // the texture cannot be borrowed as a native GPU sampler.
    const auto terrain_uv = fp3d_materials::lookup_gpu( unknown.terrain );
    const auto sprite_uv = fp3d_materials::lookup_gpu( fp3d_materials::sprite_key( absent_sprite ) );
    REQUIRE( terrain_uv );
    REQUIRE( sprite_uv );
    CHECK( fp3d_materials::gpu_rect_is_opaque( *terrain_uv ) );
    CHECK_FALSE( fp3d_materials::gpu_rect_is_opaque( *sprite_uv ) );
    CHECK_FALSE( fp3d_materials::gpu_rect_is_opaque( { 0.f, 0.f, .5f, .5f } ) );
    CHECK( fp3d_materials::gpu_colour_atlas( renderer.get() ) == nullptr );
    fp3d_materials::set_tileset( std::make_shared<tileset>(), renderer );
    CHECK_FALSE( fp3d_materials::ready() );
    fp3d_materials::prepare( scene );
    CHECK( fp3d_materials::ready() );

    const auto sprite_key = fp3d_materials::sprite_key( absent_sprite );
    const auto *sprite_maps = fp3d_materials::auxiliary( sprite_key );
    const auto *terrain_maps = fp3d_materials::auxiliary( unknown.terrain );
    REQUIRE( sprite_maps );
    REQUIRE( terrain_maps );
    REQUIRE( fp3d_materials::auxiliary_tiles().size() == 2 );
    CHECK( fp3d_materials::auxiliary_tiles()[0].data == terrain_maps );
    CHECK( fp3d_materials::auxiliary_tiles()[1].data == sprite_maps );
    CHECK( fp3d_materials::auxiliary( unknown.terrain ) == terrain_maps );
    CHECK( fp3d_materials::auxiliary_tiles().size() == 2 );

    for( int i = 0; i < 1024; ++i ) {
        auto &cell = scene.cells.emplace_back();
        cell.terrain = "fp3d_fill_atlas_" + std::to_string( i );
    }
    fp3d_materials::prepare( scene );
    REQUIRE( fp3d_materials::atlas_slot_count() == 1024 );
    const unsigned full_revision = fp3d_materials::revision();

    fp3d_snapshot::scene_snapshot replacement;
    replacement.cells.push_back( unknown );
    replacement.held_item = absent_sprite;
    replacement.held_item->id = "fp3d_new_priority_item";
    fp3d_materials::prepare( replacement );
    CHECK( fp3d_materials::revision() != full_revision );
    CHECK( fp3d_materials::atlas_slot_count() == 2 );
    CHECK( fp3d_materials::auxiliary_tiles().empty() );
    std::array<Uint8, 64 * 64 * 4> pixels{};
    pixels.fill( 255 );
    fp3d_materials::cache_auxiliary_readback( full_revision, 1, pixels.data() );
    CHECK( fp3d_materials::auxiliary_tiles().empty() );
    const auto current_revision = fp3d_materials::revision();
    fp3d_materials::cache_auxiliary_readback( current_revision, 1, pixels.data() );
    fp3d_materials::cache_auxiliary_readback( current_revision, 1, pixels.data() );
    REQUIRE( fp3d_materials::auxiliary_tiles().size() == 1 );
    CHECK( fp3d_materials::auxiliary_tiles().front().slot == 1 );
    const auto expected = fp3d_material_aux::generate( unknown.terrain, pixels.data(), 64, 64, 256 );
    CHECK( fp3d_materials::auxiliary_tiles().front().data->normal_rgba == expected.normal_rgba );
    CHECK( fp3d_materials::auxiliary_tiles().front().data->orm_rgba == expected.orm_rgba );

    replacement.cells.emplace_back().terrain = "t_pavement";
    replacement.cells.back().known = fp3d_snapshot::knowledge::visible;
    replacement.cells.emplace_back().terrain = "t_wall";
    replacement.cells.back().known = fp3d_snapshot::knowledge::visible;
    replacement.cells.back().kind = fp3d_snapshot::terrain_kind::wall;
    fp3d_materials::prepare( replacement );
    const auto requested = fp3d_materials::visible_auxiliary_slots( replacement );
    REQUIRE( requested.size() == 2 );
    CHECK( requested[0] == 3 );
    CHECK( requested[1] == 2 );
    fp3d_materials::cache_auxiliary_readback( current_revision, requested[0], pixels.data() );
    const auto remaining = fp3d_materials::visible_auxiliary_slots( replacement );
    REQUIRE( remaining.size() == 1 );
    CHECK( remaining.front() == requested[1] );
}

TEST_CASE( "fp3d_sprite_animation_follows_native_frame_weights", "[fp3d][materials][animation]" )
{
    // The 2D renderer reads a tile's fg list with a tick counter, and each
    // frame's weight is how many 17 ms ticks it stays visible. The 3D atlas
    // must reproduce that mapping exactly instead of freezing one frame.
    tile_type animated;
    animated.animated = true;
    animated.fg.add( { 900 }, 2 );
    animated.fg.add( { 901 }, 3 );
    animated.fg.add( { 902 }, 1 );
    // Six ticks per loop: 0-1 -> 900, 2-4 -> 901, 5 -> 902, then it wraps.
    CHECK( fp3d_materials::choose_sprites_at( animated, 0 ).foreground == 900 );
    CHECK( fp3d_materials::choose_sprites_at( animated, 1 ).foreground == 900 );
    CHECK( fp3d_materials::choose_sprites_at( animated, 2 ).foreground == 901 );
    CHECK( fp3d_materials::choose_sprites_at( animated, 4 ).foreground == 901 );
    CHECK( fp3d_materials::choose_sprites_at( animated, 5 ).foreground == 902 );
    CHECK( fp3d_materials::choose_sprites_at( animated, 6 ).foreground == 900 );
    CHECK( fp3d_materials::choose_sprites_at( animated, 605 ).foreground == 902 );

    // A static tile keeps the stable per-ID pick the 3D view already used, so
    // packs without frame art are unchanged.
    tile_type plain;
    plain.fg.add( { 910 }, 1 );
    plain.fg.add( { 911 }, 1 );
    const fp3d_materials::sprite_choice stable = fp3d_materials::choose_sprites( plain, "mon_example" );
    CHECK( fp3d_materials::choose_sprites( plain, "mon_example" ).foreground == stable.foreground );
    // Ticks beyond the tile's own weight wrap inside the same list.
    CHECK( fp3d_materials::choose_sprites_at( plain, 0 ).foreground ==
           fp3d_materials::choose_sprites_at( plain, 2 ).foreground );
}
TEST_CASE( "fp3d_sprite_animation_keeps_every_native_frame_in_the_atlas",
           "[fp3d][materials][animation]" )
{
    SDL_Surface_Ptr surface( CreateSurfaceWithFormat( 0, 128, 128, 32,
                             SDL_PIXELFORMAT_RGBA8888 ) );
    REQUIRE( surface );
    SDL_Renderer_Ptr renderer( SDL_CreateSoftwareRenderer( surface.get() ) );
    REQUIRE( renderer );
    auto set = std::make_shared<tileset>();
    // A tileset tile that really declares idle animation: two frames of 2 and 3
    // ticks. Frame art cannot exist without loaded images, but the frame slots,
    // their keys and their 17 ms tick order are all real.
    tile_type animated;
    animated.animated = true;
    animated.fg.add( { 0 }, 2 );
    animated.fg.add( { 1 }, 3 );
    set->create_tile_type( "mon_fp3d_animated", std::move( animated ) );
    set->create_tile_type( "mon_fp3d_static", tile_type{} );
    fp3d_materials::set_tileset( set, renderer );
    on_out_of_scope cleanup( []() {
        fp3d_materials::release();
    } );

    fp3d_snapshot::sprite actor;
    actor.kind = fp3d_snapshot::sprite_kind::monster;
    actor.id = "mon_fp3d_animated";
    actor.height = 1.8f;
    fp3d_snapshot::scene_snapshot scene;
    scene.sprites.push_back( actor );
    fp3d_materials::prepare( scene );

    const auto frames = fp3d_materials::sprite_animation_gpu( actor );
    REQUIRE( frames.size() == 2 );
    REQUIRE( frames[0].uv );
    REQUIRE( frames[1].uv );
    CHECK( frames[0].ticks == 2 );
    CHECK( frames[1].ticks == 3 );
    // Distinct atlas slots, so switching frames never re-uploads a texture.
    CHECK( ( frames[0].uv->left != frames[1].uv->left ||
             frames[0].uv->top != frames[1].uv->top ) );

    // 5 ticks per loop, 17 ms each: the visible frame follows the wall clock,
    // never the render frame rate.
    const auto same = []( const fp3d_materials::uv_rect & left,
    const fp3d_materials::uv_rect & right ) {
        return left.left == right.left && left.top == right.top &&
               left.right == right.right && left.bottom == right.bottom;
    };
    const auto at = [&]( std::int64_t ms, unsigned phase = 0 ) {
        return fp3d_materials::sprite_frame_uv( actor, ms, phase );
    };
    REQUIRE( at( 0 ) );
    CHECK( same( *at( 0 ), *frames[0].uv ) );
    CHECK( same( *at( 17 ), *frames[0].uv ) );        // tick 1
    CHECK( same( *at( 34 ), *frames[1].uv ) );        // tick 2
    CHECK( same( *at( 68 ), *frames[1].uv ) );        // tick 4
    CHECK( same( *at( 85 ), *frames[0].uv ) );        // wrapped to tick 0
    // A different phase desynchronises equal actors on other tiles.
    CHECK( same( *at( 0, 2 ), *frames[1].uv ) );

    // A static actor keeps its single, existing slot.
    actor.id = "mon_fp3d_static";
    fp3d_snapshot::scene_snapshot plain;
    plain.sprites.push_back( actor );
    fp3d_materials::prepare( plain );
    const auto single = fp3d_materials::sprite_animation_gpu( actor );
    REQUIRE( single.size() == 1 );
    REQUIRE( single.front().uv );
    CHECK( same( *at( 17000, 5 ), *single.front().uv ) );
}
#endif
