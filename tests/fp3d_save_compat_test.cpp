#include "fp3d_inventory.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "fp3d_compositor.h"
#include "fp3d_map.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "worldfactory.h"

TEST_CASE( "fp3d_character_save_restores_its_view_mode_after_renderer_restart",
           "[.fp3d_saved_view_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) == 0 );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden saved camera regression", 0, 0, 128, 128,
                           SDL_WINDOW_HIDDEN | SDL_WINDOW_OPENGL ) );
    REQUIRE( window );
    int driver = -1;
    for( int i = 0; i < SDL_GetNumRenderDrivers(); ++i ) {
        SDL_RendererInfo info{};
        SDL_GetRenderDriverInfo( i, &info );
        if( std::string( info.name ) == "opengl" ) { driver = i; }
    }
    REQUIRE( driver >= 0 );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), driver,
                               SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE ) );
    REQUIRE( renderer );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    clear_avatar();
    avatar &you = get_avatar();
    const auto save = [&]() {
        std::ostringstream output;
        JsonOut json( output );
        you.serialize( json );
        return output.str();
    };
    const auto load = [&]( const std::string &saved ) {
        // Match game::load: deserialize into a fresh avatar, with no old ID.
        you = avatar();
        you.deserialize( json_loader::from_string( saved ).get_object() );
    };
    const auto stored_mode = []( const std::string &saved ) {
        const JsonObject data = json_loader::from_string( saved ).get_object();
        data.allow_omitted_members();
        return data.get_string( "fp3d_view_mode" );
    };
    std::string overhead_save;
    std::string first_person_save;
    std::string two_d_save;
    for( const std::string mode : { "overhead", "first_person", "2d" } ) {
        CAPTURE( mode );
        fp3d_compositor::apply_modes( true, true );
        if( fp3d_compositor::overhead() != ( mode == "overhead" ) ) {
            fp3d_compositor::toggle_camera();
        }
        fp3d_compositor::apply_modes( mode != "2d", true );
        const auto saved = save();
        CHECK( stored_mode( saved ) == mode );
        if( mode == "overhead" ) { overhead_save = saved; }
        if( mode == "first_person" ) { first_person_save = saved; }
        if( mode == "2d" ) { two_d_save = saved; }
        fp3d_compositor::release();
        fp3d_compositor::attach( renderer.get(), window.get() );
        // Start in the opposite rendering mode, as when another character
        // changed the shared F5 preference before this character was loaded.
        fp3d_compositor::apply_modes( mode == "2d", true );
        load( saved );
        CHECK( fp3d_compositor::view_3d() == ( mode != "2d" ) );
        CHECK( fp3d_compositor::overhead() == ( mode == "overhead" ) );
        CHECK( fp3d_compositor::saved_view_mode() == mode );
        CHECK( stored_mode( save() ) == mode );
    }
    // Different character saves in one process keep their own modes.
    load( overhead_save );
    CHECK( fp3d_compositor::overhead() );
    load( two_d_save );
    CHECK_FALSE( fp3d_compositor::view_3d() );
    load( overhead_save );
    CHECK( fp3d_compositor::overhead() );
    load( first_person_save );
    CHECK( fp3d_compositor::view_3d() );
    CHECK_FALSE( fp3d_compositor::overhead() );
    // Saves predating this optional field load normally and cannot inherit
    // the previous character's overhead camera.
    auto legacy = first_person_save;
    const auto field = legacy.find( "fp3d_view_mode" );
    REQUIRE( field != std::string::npos );
    legacy.replace( field, std::string( "fp3d_view_mode" ).size(), "legacy_view_mode" );
    load( overhead_save );
    load( legacy );
    CHECK( fp3d_compositor::view_3d() );
    CHECK_FALSE( fp3d_compositor::overhead() );
}

// Export an actual native world save before the test runner removes its scratch world.
// The caller supplies a fresh QA destination; never overwrite an existing directory.
TEST_CASE( "fp3d_export_pending_activity_for_official_readback", "[.fp3d_save_compat_export]" )
{
    const char *export_dir = std::getenv( "CDDA_FP3D_COMPAT_EXPORT_DIR" );
    REQUIRE( export_dir );
    const auto destination = std::filesystem::u8path( export_dir );
    REQUIRE_FALSE( std::filesystem::exists( destination ) );
    clear_avatar();
    clear_map();
    set_time_to_day();
    avatar &you = get_avatar();
    map &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    you.abort_automove();
    you.cancel_activity();
    you.omt_path.clear();
    you.wear_item( item( itype_id( "backpack" ) ), false );
    REQUIRE_FALSE( you.top_items_loc().empty() );
    auto bag = you.top_items_loc().front();
    REQUIRE( bag->typeId() == itype_id( "backpack" ) );
    auto pockets = bag->get_all_contained_pockets();
    REQUIRE( pockets.size() >= 2 );
    const auto tile = you.pos_bub( here ) + tripoint_rel_ms::north;
    here.add_item_or_charges( tile, item( itype_id( "bottle_plastic" ) ) );
    here.build_map_cache( 0 );
    here.update_visibility_cache( 0 );
    auto objects = here.i_at( tile );
    REQUIRE( objects.size() == 1 );
    item_location bottle( map_cursor( tile ), &*objects.begin() );
    REQUIRE( pockets[1]->can_contain( *bottle ).success() );
    fp3d_inventory::operation transfer;
    transfer.kind = fp3d_inventory::operation_kind::insert;
    transfer.destination = bag;
    transfer.destination_pocket = 1;
    transfer.items.emplace_back( bottle, 1 );
    REQUIRE( fp3d_inventory::execute( transfer, you, here ) );
    REQUIRE( you.has_activity( activity_id( "ACT_INSERT_ITEM" ) ) );
    CHECK( you.activity.values == std::vector<int> { 1 } );
    CHECK( you.activity.str_values.front() == "fp3d_target_pocket" );
    const auto note_position = you.pos_abs_omt();
    const std::string note = "!:Y;新版 3D 存档兼容核验";
    std::string error;
    REQUIRE( fp3d_map::write_note( note_position, note, error ) );
    CHECK( overmap_buffer.note( note_position ) == note );
    you.set_moves( 0 );
    REQUIRE( g->save() );
    CHECK( you.has_activity( activity_id( "ACT_INSERT_ITEM" ) ) );
    REQUIRE( world_generator->active_world );
    const auto source = world_generator->active_world->folder_path().get_unrelative_path();
    std::filesystem::copy( source, destination, std::filesystem::copy_options::recursive );
    REQUIRE( std::filesystem::exists( destination / "mods.json" ) );
    REQUIRE( std::filesystem::exists( destination / "worldoptions.json" ) );
    std::ofstream report( destination.parent_path() / "fixture.json" );
    REQUIRE( report.good() );
    JsonOut json( report );
    json.start_object();
    json.member( "world", world_generator->active_world->world_name );
    json.member( "activity", you.activity.id().str() );
    json.member( "preferred_pocket", 1 );
    json.member( "note", note );
    json.member( "note_x", note_position.x() );
    json.member( "note_y", note_position.y() );
    json.member( "turn", to_turn<int>( calendar::turn ) );
    json.member( "ground_items", objects.size() );
    json.end_object();
}
#endif
