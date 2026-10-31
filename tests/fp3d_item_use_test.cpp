#include "fp3d_inventory.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "avatar.h"
#include "avatar_action.h"
#include "calendar.h"
#include "cata_catch.h"
#include "flag.h"
#include "item.h"
#include "itype.h"
#include "item_location.h"
#include "map.h"
#include "map_helpers.h"
#include "magic.h"
#include "relic.h"
#include "player_helpers.h"
#include "rng.h"

TEST_CASE( "fp3d_item_use_choices_preserve_native_effects_and_costs", "[fp3d][inventory]" )
{
    clear_avatar();
    clear_map();
    avatar &you = get_avatar();
    map &here = get_map();
    you.set_wielded_item( item( itype_id( "atomic_lamp" ) ) );
    you.set_moves( 1000 );
    auto loc = you.get_wielded_item();
    const auto engine = rng_get_engine();
    const auto clock = calendar::turn;
    const auto options = fp3d_inventory::use_options( loc, you, here );
    REQUIRE( options.size() == 1 );
    REQUIRE( options.front().enabled );
    CHECK_FALSE( options.front().name.empty() );
    CHECK( rng_get_engine() == engine );
    CHECK( you.get_moves() == 1000 );
    CHECK( calendar::turn == clock );
    avatar_action::use_item( you, loc, options.front().method );
    REQUIRE( loc );
    REQUIRE( loc->typeId() == itype_id( "atomic_lamp_off" ) );
    const int native_moves = you.get_moves();
    you.set_wielded_item( item( itype_id( "atomic_lamp" ) ) );
    you.set_moves( 1000 );
    fp3d_inventory::operation action;
    action.kind = fp3d_inventory::operation_kind::use;
    action.items.emplace_back( you.get_wielded_item(), 1 );
    action.use_method = "invalid_method";
    CHECK_FALSE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.get_moves() == 1000 );
    action.use_method = options.front().method;
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.get_wielded_item()->typeId() == itype_id( "atomic_lamp_off" ) );
    CHECK( you.get_moves() == native_moves );
    CHECK( calendar::turn == clock );
    you.set_wielded_item( item( itype_id( "mp3" ) ) );
    const auto empty_mp3 = fp3d_inventory::use_options( you.get_wielded_item(), you, here );
    const auto music = std::find_if( empty_mp3.begin(), empty_mp3.end(), []( const auto & option ) {
        return option.method == "MP3";
    } );
    REQUIRE( music != empty_mp3.end() );
    CHECK_FALSE( music->enabled );
}

TEST_CASE( "fp3d_relic_use_matches_native_history_charges_and_effect", "[fp3d][inventory]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    map &here = get_map();
    relic artifact;
    artifact.add_active_effect( fake_spell( spell_id( "test_spell_montage" ), true ) );
    relic_charge_info charge;
    charge.charges = 2;
    charge.max_charges = 2;
    charge.charges_per_use = 1;
    artifact.overwrite_charge( charge );
    item object( itype_id( "rock" ) );
    object.overwrite_relic( artifact );
    SECTION( "obtain and use" ) {}
    SECTION( "use in place" ) {
        object.set_flag( flag_id( "ALLOWS_REMOTE_USE" ) );
    }
    you.set_wielded_item( object );
    you.set_stamina( 5000 );
    you.set_moves( 1000 );
    you.last_item = itype_id::NULL_ID();
    const auto clock = calendar::turn;
    const auto engine = rng_get_engine();
    auto location = you.get_wielded_item();
    avatar_action::use_item( you, location );
    REQUIRE( location );
    REQUIRE( location->relic_data->charges() == 1 );
    REQUIRE( you.get_stamina() > 5000 );
    REQUIRE( you.last_item == object.typeId() );
    const auto native_engine = rng_get_engine();
    const int native_moves = you.get_moves();
    const int native_stamina = you.get_stamina();
    const auto native_last_item = you.last_item;
    you.set_wielded_item( object );
    you.set_stamina( 5000 );
    you.set_moves( 1000 );
    you.last_item = itype_id::NULL_ID();
    rng_get_engine() = engine;
    location = you.get_wielded_item();
    const auto options = fp3d_inventory::use_options( location, you, here );
    REQUIRE( options.size() == 1 );
    REQUIRE( options.front().relic );
    REQUIRE( options.front().enabled );
    fp3d_inventory::operation action;
    action.kind = fp3d_inventory::operation_kind::use;
    action.items.emplace_back( location, 1 );
    action.activate_relic = true;
    REQUIRE( fp3d_inventory::execute( action, you, here ) );
    REQUIRE( location );
    CHECK( location->relic_data->charges() == 1 );
    CHECK( you.last_item == native_last_item );
    CHECK( you.get_moves() == native_moves );
    CHECK( you.get_stamina() == native_stamina );
    CHECK( calendar::turn == clock );
    CHECK( rng_get_engine() == native_engine );
}

TEST_CASE( "fp3d_relic_does_not_bypass_native_tool_without_use_action", "[fp3d][inventory]" )
{
    clear_avatar();
    avatar &you = get_avatar();
    map &here = get_map();
    relic artifact;
    artifact.add_active_effect( fake_spell( spell_id( "test_spell_montage" ), true ) );
    item object( itype_id( "anvil" ) );
    REQUIRE( object.is_tool() );
    REQUIRE_FALSE( object.type->has_use() );
    object.overwrite_relic( artifact );
    you.set_wielded_item( object );
    you.set_stamina( 5000 );
    you.set_moves( 1000 );
    const auto engine = rng_get_engine();
    const auto options = fp3d_inventory::use_options( you.get_wielded_item(), you, here );
    REQUIRE( options.size() == 1 );
    CHECK_FALSE( options.front().enabled );
    fp3d_inventory::operation action;
    action.kind = fp3d_inventory::operation_kind::use;
    action.items.emplace_back( you.get_wielded_item(), 1 );
    action.activate_relic = true;
    CHECK_FALSE( fp3d_inventory::execute( action, you, here ) );
    CHECK( you.get_moves() == 1000 );
    CHECK( you.get_stamina() == 5000 );
    CHECK( rng_get_engine() == engine );
    // Calling the explicit native bridge directly must preserve the same gate.
    auto location = you.get_wielded_item();
    avatar_action::use_item( you, location );
    REQUIRE( you.get_stamina() == 5000 );
    const auto native_last_item = you.last_item;
    you.last_item = itype_id::NULL_ID();
    avatar_action::use_item( you, location, {}, true );
    CHECK( you.get_stamina() == 5000 );
    CHECK( you.get_moves() == 1000 );
    CHECK( you.last_item == native_last_item );
    CHECK( rng_get_engine() == engine );
}
#endif
