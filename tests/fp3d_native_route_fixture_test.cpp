#include "fp3d_regression.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include "cata_catch.h"
#include "flexbuffer_json.h"
#include "json.h"
#include "json_loader.h"
#include "zzip.h"

// Only imports an independently checked native-route payload into a private QA
// world. Use the game's own archive writer, including its live-entry footer.
TEST_CASE( "fp3d_import_native_route_into_private_qa_archive", "[.fp3d_native_route_fixture]" )
{
    const char *archive_env = std::getenv( "CDDA_FP3D_ROUTE_ARCHIVE" );
    const char *payload_env = std::getenv( "CDDA_FP3D_ROUTE_PAYLOAD" );
    REQUIRE( archive_env );
    REQUIRE( payload_env );
    const auto archive_path = std::filesystem::u8path( archive_env );
    const auto payload_path = std::filesystem::u8path( payload_env );
    REQUIRE( archive_path.lexically_normal().generic_u8string().find( "/qa/0i1/" ) !=
             std::string::npos );
    REQUIRE( payload_path.lexically_normal().generic_u8string().find( "/qa/0i1/" ) !=
             std::string::npos );
    REQUIRE( std::filesystem::is_regular_file( archive_path ) );
    REQUIRE( std::filesystem::is_regular_file( payload_path ) );
    std::ifstream input( payload_path, std::ios::binary );
    const std::string payload( ( std::istreambuf_iterator<char>( input ) ), {} );
    REQUIRE_FALSE( payload.empty() );
    const JsonValue new_json = json_loader::from_string( payload );
    const JsonObject next = new_json.get_object();
    next.allow_omitted_members();
    const JsonObject next_player = next.get_object( "player" );
    next_player.allow_omitted_members();
    REQUIRE( next_player.get_array( "automoveroute" ).size() == 60001 );
    REQUIRE( next_player.get_object( "activity" ).get_string( "type" ) == "ACT_NULL" );
    REQUIRE( next_player.get_object( "destination_activity" ).get_string( "type" ) == "ACT_NULL" );
    auto archive = zzip::load( archive_path );
    REQUIRE( archive );
    const auto entries = archive->get_entries();
    REQUIRE( entries.size() == 1 );
    const auto bytes = archive->get_file( entries.front() );
    REQUIRE_FALSE( bytes.empty() );
    const std::string previous( reinterpret_cast<const char *>( bytes.data() ), bytes.size() );
    const JsonValue old_json = json_loader::from_string( previous );
    const JsonObject old = old_json.get_object();
    old.allow_omitted_members();
    const JsonObject old_player = old.get_object( "player" );
    old_player.allow_omitted_members();
    const auto previous_steps = old_player.get_array( "automoveroute" ).size();
    CAPTURE( previous_steps );
    CHECK( ( previous_steps == 0 || previous_steps == 60001 ) );
    CHECK( old.get_int( "savegame_loading_version" ) == next.get_int( "savegame_loading_version" ) );
    CHECK( old.get_int( "turn" ) == next.get_int( "turn" ) );
    CHECK( old_player.get_int( "moves" ) == next_player.get_int( "moves" ) );
    for( int axis = 0; axis < 3; ++axis ) {
        CHECK( old_player.get_array( "location" ).get_int( axis ) ==
               next_player.get_array( "location" ).get_int( axis ) );
    }
    REQUIRE( archive->add_file( entries.front(), payload ) );
    archive.reset();
    archive = zzip::load( archive_path );
    REQUIRE( archive );
    const auto roundtrip = archive->get_file( entries.front() );
    const std::string restored( reinterpret_cast<const char *>( roundtrip.data() ), roundtrip.size() );
    CHECK( restored == payload );
    std::ofstream report( payload_path.parent_path() / "native-import.json" );
    REQUIRE( report.good() );
    JsonOut json( report );
    json.start_object();
    json.member( "previous_queued_steps", previous_steps );
    json.member( "imported_queued_steps", 60001 );
    json.member( "turn", next.get_int( "turn" ) );
    json.member( "moves", next_player.get_int( "moves" ) );
    json.member( "payload_bytes", payload.size() );
    json.end_object();
}
#endif
