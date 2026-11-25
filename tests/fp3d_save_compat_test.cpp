#include "fp3d_inventory.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <chrono>
#include <cmath>
#include <vector>
#include <filesystem>
#include <fstream>
#include <sstream>
#include "avatar.h"
#include "calendar.h"
#include "cata_catch.h"
#include "cata_scope_helpers.h"
#include "cata_tiles.h"
#include "sdl_geometry.h"
#include "sdltiles.h"
#include "fp3d_compositor.h"
#include "fp3d_camera.h"
#include "fp3d_snapshot.h"
#include "fp3d_interaction.h"
#include "fp3d_map.h"
#include "game.h"
#include "item.h"
#include "item_location.h"
#include "item_pocket.h"
#include "json.h"
#include "json_loader.h"
#include "map.h"
#include "map_helpers.h"
#include "monster.h"
#include "output.h"
#include "fp3d_materials.h"
#include "fp3d_geometry_curved.h"
#include "overmapbuffer.h"
#include "player_activity.h"
#include "player_helpers.h"
#include "worldfactory.h"
#include "rng.h"

TEST_CASE( "fp3d_character_save_restores_its_view_mode_after_renderer_restart",
           "[.fp3d_saved_view_gl]" )
{
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "Hidden saved camera regression", 128, 128,
                           SDL_WINDOW_HIDDEN ) );
    REQUIRE( window );
    SDL_SetHintWithPriority( SDL_HINT_GPU_DRIVER, "direct3d12", SDL_HINT_OVERRIDE );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "gpu" ) );
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

// Explicit opt-in: briefly opens a dedicated SDL3 window, without a player save.
TEST_CASE( "fp3d_right_drag_live_gpu_switches_rooms_without_a_native_redraw",
           "[.fp3d_cutaway_live_gpu]" )
{
    const bool modern = GENERATE( false, true );
    CAPTURE( modern );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "SDL3 room cutaway regression (temporary)", 800, 600, 0 ) );
    REQUIRE( window );
    SDL_SetHintWithPriority( SDL_HINT_GPU_DRIVER, "direct3d12", SDL_HINT_OVERRIDE );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "gpu" ) );
    REQUIRE( renderer );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    tileset_cache cache;
    restore_on_out_of_scope restore_tiles( tilecontext );
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry, cache );
    tilecontext->load_tileset( "UltimateCataclysm", false, false, false );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    clear_avatar();
    clear_map();
    set_time_to_day();
    auto &you = get_avatar();
    auto &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto player = you.pos_bub( here );
    for( int dy = -5; dy <= -1; ++dy ) {
        for( int dx = -4; dx <= 4; ++dx ) {
            const auto p = player + tripoint_rel_ms( dx, dy, 0 );
            const bool wall = dx == -4 || dx == 4 || dx == 0 || dy == -5 || dy == -1;
            here.ter_set( p, ter_str_id( wall ? "t_wall" : "t_floor" ) );
            here.ter_set( p + tripoint_rel_ms::above, ter_str_id( "t_flat_roof" ) );
        }
    }
    here.furn_set( player + tripoint_rel_ms( -2, -3, 0 ), furn_str_id( "f_bookcase" ) );
    here.furn_set( player + tripoint_rel_ms( 2, -3, 0 ), furn_str_id( "f_table" ) );
    fp3d_snapshot::prepare_view( you, here );
    fp3d_compositor::apply_modes( true, modern );
    fp3d_compositor::restore_view_mode( "overhead" );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::set_viewport( 0, 0, 800, 600, 800, 600 );
    // Publish exactly once: all later frames must use camera-only updates.
    fp3d_compositor::publish_camera( you, here, true );
    REQUIRE( SDL_RaiseWindow( window.get() ) );
    // Windows delivers activation asynchronously, especially after another
    // temporary GPU test window has just closed. Still require real focus.
    for( int attempt = 0; attempt < 40 && SDL_GetKeyboardFocus() != window.get(); ++attempt ) {
        SDL_Delay( 50 );
        SDL_PumpEvents();
    }
    REQUIRE( SDL_GetKeyboardFocus() == window.get() );
    const int moves = you.get_moves();
    const auto turn = calendar::turn;
    const auto frame = [&]( const char *name ) {
        for( int n = 0; n < 4; ++n ) {
            fp3d_compositor::tick();
            fp3d_compositor::compose();
            fp3d_compositor::gpu_world_qa_after_present( SDL_RenderPresent( renderer.get() ) );
            REQUIRE( SDL_WaitForGPUIdle( SDL_GetGPURendererDevice( renderer.get() ) ) );
            SDL_Delay( 20 );
        }
        auto image = fp3d_compositor::copy_view();
        REQUIRE( image );
        REQUIRE( SDL_SetRenderTarget( renderer.get(), image.get() ) );
        SDL_Surface_Ptr raw( SDL_RenderReadPixels( renderer.get(), nullptr ) );
        REQUIRE( raw );
        SDL_Surface_Ptr pixels( SDL_ConvertSurface( raw.get(), SDL_PIXELFORMAT_RGBA32 ) );
        REQUIRE( pixels );
        REQUIRE( SDL_SetRenderTarget( renderer.get(), nullptr ) );
        if( const char *directory = std::getenv( "CDDA_FP3D_CUTAWAY_IMAGES" ) ) {
            const auto path = std::filesystem::u8path( directory ) / name;
            REQUIRE( SDL_SaveBMP( pixels.get(), path.string().c_str() ) );
        }
        std::vector<Uint32> result;
        for( int y = 0; y < pixels->h; ++y ) {
            const auto *row = reinterpret_cast<const Uint32 *>(
                                  static_cast<const Uint8 *>( pixels->pixels ) + y * pixels->pitch );
            result.insert( result.end(), row, row + pixels->w );
        }
        return result;
    };
    const auto normal = frame( "normal.bmp" );
    const auto view = fp3d_camera::orbit_pose( { fp3d_compositor::overhead_azimuth(),
                      fp3d_camera::orbit_elevation_degrees, fp3d_compositor::overhead_distance() }, 0, 0 );
    const auto cursor = [&]( int x ) {
        const auto projected = fp3d_camera::project( view, 800.f / 600.f, x, 0, -3 );
        REQUIRE( projected );
        return point( std::lround( ( ( *projected )[0] + 1 ) * 400 ),
                      std::lround( ( 1 - ( *projected )[1] ) * 300 ) );
    };
    SDL_Event button{};
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    button.button.windowID = SDL_GetWindowID( window.get() );
    button.button.button = SDL_BUTTON_RIGHT;
    button.button.x = 400;
    button.button.y = 300;
    REQUIRE( fp3d_compositor::process_event( button ) );
    const auto drag = [&]( int x ) {
        const auto mouse = cursor( x );
        SDL_Event event{};
        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.windowID = button.button.windowID;
        event.motion.state = SDL_BUTTON_RMASK;
        event.motion.x = mouse.x;
        event.motion.y = mouse.y;
        REQUIRE( fp3d_compositor::process_event( event ) );
        SDL_Delay( 200 );
        fp3d_compositor::tick();
        REQUIRE( fp3d_compositor::right_camera_dragging() );
    };
    drag( -2 );
    const auto a = frame( "room-a.bmp" );
    drag( 2 );
    const auto b = frame( "room-b.bmp" );
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    const auto end = cursor( 2 );
    button.button.x = end.x;
    button.button.y = end.y;
    REQUIRE( fp3d_compositor::process_event( button ) );
    CHECK_FALSE( fp3d_compositor::right_camera_dragging() );
    // Neutralise ordinary hover outlines before comparing the restored roof.
    const auto leave = [&]() {
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = button.button.windowID;
        motion.motion.x = -1;
        motion.motion.y = -1;
        fp3d_compositor::process_event( motion );
    };
    leave();
    const auto released = frame( "released.bmp" );
    const auto different = [&]( const std::vector<Uint32> &left, const std::vector<Uint32> &right ) {
        REQUIRE( left.size() == right.size() );
        REQUIRE( left.size() == 800 * 600 );
        size_t count = 0;
        // Building crop; exclude the animated sky. copy_view is vertically flipped.
        for( int y = 250; y < 400; ++y ) {
            for( int x = 300; x < 500; ++x ) { count += left[y * 800 + x] != right[y * 800 + x]; }
        }
        return count;
    };
    CHECK( different( normal, a ) > 100 );
    CHECK( different( normal, b ) > 100 );
    CHECK( different( a, b ) > 100 );
    CHECK( different( normal, released ) < 10 );
    // Menu and focus changes restore the same geometry without another publication.
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    REQUIRE( fp3d_compositor::process_event( button ) );
    drag( -2 );
    fp3d_compositor::set_input_context( "TEST_MODAL" );
    CHECK_FALSE( fp3d_compositor::right_camera_dragging() );
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    leave();
    CHECK( different( normal, frame( "menu-restored.bmp" ) ) < 10 );
    REQUIRE( fp3d_compositor::process_event( button ) );
    drag( -2 );
    SDL_Event focus{};
    focus.type = SDL_EVENT_WINDOW_FOCUS_LOST;
    focus.window.windowID = button.button.windowID;
    fp3d_compositor::process_event( focus );
    CHECK_FALSE( fp3d_compositor::right_camera_dragging() );
    leave();
    CHECK( different( normal, frame( "focus-restored.bmp" ) ) < 10 );
    // Exercise native publications and actual GPU submissions while walking.
    // The original cutaway regression covered only a stationary camera.
    SDL_Event hover{};
    hover.type = SDL_EVENT_MOUSE_MOTION;
    hover.motion.windowID = button.button.windowID;
    hover.motion.x = 430;
    hover.motion.y = 340;
    fp3d_compositor::process_event( hover );
    double moving_capture_ms = 0;
    double moving_draw_ms = 0;
    double worst_step_ms = 0;
    constexpr int moving_steps = 16;
    for( int step = 0; step < moving_steps; ++step ) {
        you.setpos( here, player + tripoint_rel_ms( step % 8, 1, 0 ) );
        const auto begin = std::chrono::steady_clock::now();
        fp3d_snapshot::prepare_view( you, here );
        fp3d_compositor::publish_camera( you, here, true );
        const auto captured = std::chrono::steady_clock::now();
        fp3d_compositor::tick();
        fp3d_compositor::compose();
        fp3d_compositor::gpu_world_qa_after_present( SDL_RenderPresent( renderer.get() ) );
        REQUIRE( SDL_WaitForGPUIdle( SDL_GetGPURendererDevice( renderer.get() ) ) );
        const auto end_step = std::chrono::steady_clock::now();
        moving_capture_ms += std::chrono::duration<double, std::milli>( captured - begin ).count();
        moving_draw_ms += std::chrono::duration<double, std::milli>( end_step - captured ).count();
        worst_step_ms = std::max( worst_step_ms,
                                std::chrono::duration<double, std::milli>( end_step - begin ).count() );
        REQUIRE( fp3d_compositor::copy_view() );
    }
    if( const char *path = std::getenv( "CDDA_FP3D_MOVEMENT_GPU_TIMING" ) ) {
        std::ofstream out( path, std::ios::app );
        out << "modern=" << modern << ", steps=" << moving_steps
            << ", mean_prepare_publish_ms=" << moving_capture_ms / moving_steps
            << ", mean_draw_wait_ms=" << moving_draw_ms / moving_steps
            << ", worst_total_ms=" << worst_step_ms << '\n';
    }
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == turn );
    // E follows the current cursor in both control profiles, including after
    // moving it between two objects without another native publication.
    you.setpos( here, player );
    here.ter_set( player, ter_str_id( "t_stairs_up" ) );
    here.ter_set( player + tripoint_rel_ms::east, ter_str_id( "t_door_c" ) );
    here.ter_set( player + tripoint_rel_ms::west, ter_str_id( "t_door_c" ) );
    here.furn_set( player + tripoint_rel_ms::south, furn_str_id( "f_table" ) );
    fp3d_snapshot::prepare_view( you, here );
    fp3d_compositor::publish_camera( you, here, true );
    SDL_Delay( 250 );
    frame( "e-targets.bmp" );
    const auto interact_at = [&]( int dx, int dy, float height, action_id expected ) {
        const auto projected = fp3d_camera::project( view, 800.f / 600.f, dx, height, dy );
        REQUIRE( projected );
        hover.motion.x = std::lround( ( ( *projected )[0] + 1 ) * 400 );
        hover.motion.y = std::lround( ( 1 - ( *projected )[1] ) * 300 );
        fp3d_compositor::process_event( hover );
        const auto target = fp3d_compositor::interaction_target();
        REQUIRE( target );
        CHECK( target->position == here.get_abs( player + tripoint_rel_ms( dx, dy, 0 ) ) );
        REQUIRE( fp3d_compositor::interaction_available() );
        SDL_Event key{};
        key.type = SDL_EVENT_KEY_DOWN;
        key.key.windowID = button.button.windowID;
        key.key.key = SDLK_E;
        key.key.scancode = SDL_SCANCODE_E;
        REQUIRE( fp3d_compositor::process_event( key ) );
        CHECK( fp3d_input::world_gestures().poll( SDL_GetTicks() ).empty() );
        key.key.repeat = true;
        REQUIRE( fp3d_compositor::process_event( key ) );
        key.key.repeat = false;
        SDL_Event text{};
        text.type = SDL_EVENT_TEXT_INPUT;
        text.text.windowID = button.button.windowID;
        text.text.text = "e";
        REQUIRE( fp3d_compositor::process_event( text ) );
        key.type = SDL_EVENT_KEY_UP;
        REQUIRE( fp3d_compositor::process_event( key ) );
        const auto command = fp3d_input::world_gestures().poll( SDL_GetTicks() );
        REQUIRE( command == "FP3D_INTERACT" );
        const auto intent = fp3d_interaction::resolve( command, target, you, here );
        REQUIRE( intent );
        CHECK( intent->action == expected );
        REQUIRE( intent->position );
        CHECK( *intent->position == player + tripoint_rel_ms( dx, dy, 0 ) );
        CHECK( fp3d_input::world_gestures().poll( SDL_GetTicks() ).empty() );
    };
    interact_at( 1, 0, 1.2f, ACTION_OPEN );
    interact_at( -1, 0, 1.2f, ACTION_OPEN );
    interact_at( 0, 1, .7f, ACTION_EXAMINE_AND_PICKUP );
    REQUIRE( fp3d_interaction::begin_direction_selection( ACTION_GRAB ) );
    const auto confirm = fp3d_interaction::resolve( "FP3D_INTERACT",
                         fp3d_compositor::interaction_target(), you, here );
    REQUIRE( confirm );
    CHECK( confirm->action == ACTION_GRAB );
    CHECK( confirm->position == player + tripoint_rel_ms::south );
    SDL_Event held_key{};
    held_key.type = SDL_EVENT_KEY_DOWN;
    held_key.key.windowID = button.button.windowID;
    held_key.key.key = SDLK_E;
    held_key.key.scancode = SDL_SCANCODE_E;
    REQUIRE( fp3d_compositor::process_event( held_key ) );
    CHECK( fp3d_input::world_gestures().poll( SDL_GetTicks() + 1001 ) == "FP3D_INTERACT_LONG" );
    held_key.type = SDL_EVENT_KEY_UP;
    REQUIRE( fp3d_compositor::process_event( held_key ) );
    CHECK( fp3d_input::world_gestures().poll( SDL_GetTicks() + 1001 ).empty() );
    leave();
    CHECK_FALSE( fp3d_interaction::resolve( "FP3D_INTERACT",
                 fp3d_compositor::interaction_target(), you, here ) );
    // An invalid mouse target must not fall back to the stairs at our feet.
    CHECK( you.get_moves() == moves );
    CHECK( calendar::turn == turn );
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

TEST_CASE( "pointer_melee_live_sdl3_sprite_hover_is_white_or_red_in_both_views",
           "[.pointer_melee_live_gpu]" )
{
    const bool overhead = GENERATE( false, true );
    const bool modern = GENERATE( false, true );
    CAPTURE( overhead, modern );
    REQUIRE( SDL_InitSubSystem( SDL_INIT_VIDEO ) );
    on_out_of_scope quit_video( []() { SDL_QuitSubSystem( SDL_INIT_VIDEO ); } );
    SDL_Window_Ptr window( SDL_CreateWindow( "SDL3 pointer melee regression", 800, 600, 0 ) );
    REQUIRE( window );
    SDL_SetHintWithPriority( SDL_HINT_GPU_DRIVER, "direct3d12", SDL_HINT_OVERRIDE );
    SDL_Renderer_Ptr renderer( SDL_CreateRenderer( window.get(), "gpu" ) );
    REQUIRE( renderer );
    GeometryRenderer_Ptr geometry = std::make_unique<DefaultGeometryRenderer>();
    tileset_cache cache;
    restore_on_out_of_scope restore_tiles( tilecontext );
    restore_on_out_of_scope restore_posx( POSX );
    restore_on_out_of_scope restore_posy( POSY );
    POSX = 12;
    POSY = 9;
    tilecontext = std::make_shared<cata_tiles>( renderer, geometry, cache );
    tilecontext->load_tileset( "UltimateCataclysm", false, false, false );
    fp3d_compositor::attach( renderer.get(), window.get() );
    on_out_of_scope cleanup( []() { fp3d_compositor::release(); } );
    clear_avatar();
    clear_map();
    set_time_to_day();
    auto &you = get_avatar();
    auto &here = get_map();
    you.setpos( here, tripoint_bub_ms( 65, 65, 0 ) );
    const auto center = you.pos_bub( here );
    monster &zed = spawn_test_monster( "mon_zombie", center + tripoint_rel_ms( 0, -2, 0 ) );
    fp3d_snapshot::prepare_view( you, here );
    fp3d_compositor::apply_modes( overhead, modern );
    if( overhead && !fp3d_compositor::overhead() ) { fp3d_compositor::toggle_camera(); }
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    fp3d_compositor::set_viewport( 0, 0, 800, 600, 800, 600 );
    if( overhead ) { fp3d_compositor::publish_camera( you, here, true ); }
    REQUIRE( SDL_RaiseWindow( window.get() ) );
    for( int n = 0; n < 600 && SDL_GetKeyboardFocus() != window.get(); ++n ) {
        SDL_Delay( 50 );
        SDL_PumpEvents();
    }
    REQUIRE( SDL_GetKeyboardFocus() == window.get() );
    SDL_Texture_Ptr canvas( SDL_CreateTexture( renderer.get(), SDL_PIXELFORMAT_RGBA8888,
                                            SDL_TEXTUREACCESS_TARGET, 800, 600 ) );
    REQUIRE( canvas );
    const auto mouse_at = [&]( point p ) {
        SDL_WarpMouseInWindow( window.get(), p.x, p.y );
        SDL_PumpEvents();
        SDL_Event motion{};
        motion.type = SDL_EVENT_MOUSE_MOTION;
        motion.motion.windowID = SDL_GetWindowID( window.get() );
        motion.motion.x = p.x;
        motion.motion.y = p.y;
        fp3d_compositor::process_event( motion );
    };
    int render_number = 0;
    const auto render = [&]() {
        SDL_Surface_Ptr raw;
        if( overhead ) {
            for( int n = 0; n < 3; ++n ) {
                fp3d_compositor::tick();
                fp3d_compositor::compose();
                fp3d_compositor::gpu_world_qa_after_present( SDL_RenderPresent( renderer.get() ) );
                REQUIRE( SDL_WaitForGPUIdle( SDL_GetGPURendererDevice( renderer.get() ) ) );
            }
            auto image = fp3d_compositor::copy_view();
            REQUIRE( image );
            REQUIRE( SDL_SetRenderTarget( renderer.get(), image.get() ) );
            raw.reset( SDL_RenderReadPixels( renderer.get(), nullptr ) );
        } else {
            REQUIRE( SDL_SetRenderTarget( renderer.get(), canvas.get() ) );
            std::multimap<point, formatted_text> strings;
            color_block_overlay_container blocks;
            tilecontext->draw( point::zero, center, 800, 600, strings, blocks );
            raw.reset( SDL_RenderReadPixels( renderer.get(), nullptr ) );
        }
        REQUIRE( raw );
        SDL_Surface_Ptr pixels( SDL_ConvertSurface( raw.get(), SDL_PIXELFORMAT_RGBA32 ) );
        REQUIRE( pixels );
        if( const char *directory = std::getenv( "CDDA_POINTER_MELEE_IMAGES" ) ) {
            const auto file = std::filesystem::u8path( directory ) /
                              ( std::to_string( overhead ) + "-" + std::to_string( modern ) + "-" +
                                std::to_string( render_number++ ) + ".bmp" );
            REQUIRE( SDL_SaveBMP( pixels.get(), file.string().c_str() ) );
        }
        REQUIRE( SDL_SetRenderTarget( renderer.get(), nullptr ) );
        int red = 0;
        for( int y = 0; y < pixels->h; ++y ) {
            const Uint8 *row = static_cast<const Uint8 *>( pixels->pixels ) + y * pixels->pitch;
            for( int x = 0; x < pixels->w; ++x ) {
                red += row[x * 4] > 230 && row[x * 4 + 1] < 80 && row[x * 4 + 2] < 80;
            }
        }
        return red;
    };
    mouse_at( point( -1, -1 ) );
    render();
    std::optional<point> hit;
    if( overhead ) {
        const auto scene = fp3d_snapshot::capture( you, here );
        const auto image = std::find_if( scene.sprites.begin(), scene.sprites.end(), [&]( const auto &sprite ) {
            return sprite.kind == fp3d_snapshot::sprite_kind::monster && sprite.position == zed.pos_abs();
        } );
        REQUIRE( image != scene.sprites.end() );
        const auto *alpha = fp3d_materials::sprite_alpha( *image, SDL_GetTicks() );
        REQUIRE( alpha );
        fp3d_camera::orbit orbit;
        orbit.azimuth = fp3d_compositor::overhead_azimuth();
        orbit.distance = fp3d_compositor::overhead_distance();
        const auto view = fp3d_camera::orbit_pose( orbit, 0, 0 );
        const fp3d_camera::frustum camera( view.yaw, view.pitch, 800.f / 600.f );
        const float h = image->height * image->display_scale;
        for( int y = 0; y < alpha->height && !hit; ++y ) {
            for( int x = 0; x < alpha->width && !hit; ++x ) {
                if( !alpha->opaque( x, y ) ) { continue; }
                const auto world = fp3d_geometry::billboard_position( { 0.f, image->elevation, -2.f },
                                   { camera.right_x(), 0.f, camera.right_z() },
                                   { camera.up_x(), camera.up_y(), camera.up_z() },
                                   ( ( x + .5f ) / alpha->width - .5f ) * h,
                                   ( 1.f - ( y + .5f ) / alpha->height ) * ( h - .04f ) );
                const auto projected = fp3d_camera::project( view, 800.f / 600.f, world[0], world[1], world[2] );
                if( !projected ) { continue; }
                const point p( std::lround( ( ( *projected )[0] + 1 ) * 400 ),
                               std::lround( ( 1 - ( *projected )[1] ) * 300 ) );
                mouse_at( p );
                if( fp3d_compositor::pointer_monster( true ) == zed.pos_abs() ) { hit = p; }
            }
        }
    } else {
        for( int y = 0; y < 600 && !hit; y += 2 ) {
            for( int x = 0; x < 800 && !hit; x += 2 ) {
                if( tilecontext->monster_at_pixel( point( x, y ) ) == zed.pos_abs() ) { hit = point( x, y ); }
            }
        }
    }
    REQUIRE( hit );
    mouse_at( *hit );
    CHECK_FALSE( fp3d_interaction::can_quick_melee( you, here, zed.pos_bub( here ) ) );
    const int white_red_pixels = render();
    item polearm( itype_id( "pike" ) );
    you.wield( polearm );
    REQUIRE( fp3d_interaction::can_quick_melee( you, here, zed.pos_bub( here ) ) );
    const auto rng = rng_get_engine();
    const int moves = you.get_moves();
    const int red_pixels = render();
    CHECK( red_pixels > white_red_pixels + 4 );
    CHECK( you.get_moves() == moves );
    CHECK( rng_get_engine() == rng );
    // Neither view sends a left press into the first-person held-attack queue.
    SDL_Event button{};
    button.button.windowID = SDL_GetWindowID( window.get() );
    button.button.button = SDL_BUTTON_LEFT;
    button.button.x = hit->x;
    button.button.y = hit->y;
    button.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    CHECK_FALSE( fp3d_compositor::process_event( button ) );
    button.type = SDL_EVENT_MOUSE_BUTTON_UP;
    CHECK_FALSE( fp3d_compositor::process_event( button ) );
    CHECK( fp3d_input::world_gestures().poll( SDL_GetTicks() + 1000 ).empty() );
    fp3d_compositor::set_input_context( "TEST_MODAL" );
    CHECK_FALSE( tilecontext->monster_at_pixel( *hit ) );
    CHECK_FALSE( fp3d_compositor::pointer_monster( true ) );
    if( !overhead ) { CHECK( render() < red_pixels ); }
    fp3d_compositor::set_input_context( "DEFAULTMODE" );
    mouse_at( point( -1, -1 ) );
    CHECK( render() < red_pixels );
    CHECK_FALSE( tilecontext->monster_at_pixel( point( -1, -1 ) ) );
    CHECK_FALSE( fp3d_compositor::pointer_monster( true ) );
}
#endif
