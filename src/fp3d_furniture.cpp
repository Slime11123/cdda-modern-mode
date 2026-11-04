#include "fp3d_furniture.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <utility>
#include "mapdata.h"
namespace fp3d_furniture
{
std::optional<model> describe( const std::string &id )
{
    std::string appearance = id;
    for( int jumps = 0; jumps < 10 && !appearance.empty(); ++jumps ) {
        const furn_str_id definition( appearance );
        if( !definition.is_valid() ) {
            break;
        }
        if( appearance == "f_counter_gate_c" ) {
            break; // The swinging panel shares the counter art, but not its body.
        }
        if( appearance == "f_table" ) {
            return model{ id, kind::table };
        }
        if( appearance == "f_chair" ) {
            return model{ id, kind::chair };
        }
        if( appearance == "f_bed" ) {
            return model{ id, kind::bed };
        }
        if( appearance == "f_bed_frame" ) {
            return model{ id, kind::bed_frame };
        }
        if( appearance == "f_crate_c" ) {
            return model{ id, kind::crate };
        }
        if( appearance == "f_crate_o" ) {
            return model{ id, kind::open_crate };
        }
        if( appearance == "f_cupboard" ) {
            return model{ id, kind::cupboard };
        }
        if( appearance == "f_locker" ) {
            return model{ id, kind::locker };
        }
        if( appearance == "f_bookcase" ) {
            return model{ id, kind::cabinet };
        }
        if( appearance == "f_rack" ) {
            return model{ id, kind::rack };
        }
        if( appearance == "f_counter" ) {
            return model{ id, kind::counter };
        }
        if( appearance == "f_desk" ) {
            return model{ id, kind::desk };
        }
        if( appearance == "f_sofa" ) {
            return model{ id, kind::sofa };
        }
        if( appearance == "f_bench" ) {
            return model{ id, kind::bench };
        }
        if( appearance == "f_bench_park" ) {
            return model{ id, kind::park_bench };
        }
        if( appearance == "f_armchair" ) {
            return model{ id, kind::armchair };
        }
        if( appearance == "f_fireplace" ) {
            return model{ id, kind::fireplace };
        }
        if( appearance == "f_sink" ) {
            return model{ id, kind::sink };
        }
        if( appearance == "f_bathtub" ) {
            return model{ id, kind::bathtub };
        }
        if( appearance == "f_toilet" ) {
            return model{ id, kind::toilet };
        }
        if( appearance == "f_oven" ) {
            return model{ id, kind::oven };
        }
        if( appearance == "f_minifridge" ) {
            return model{ id, kind::mini_fridge };
        }
        if( appearance == "f_fridge" ) {
            return model{ id, kind::fridge };
        }
        if( appearance == "f_dresser" ) {
            return model{ id, kind::dresser };
        }
        if( appearance == "f_wardrobe" ) {
            return model{ id, kind::locker };
        }
        appearance = definition->looks_like;
    }
    return std::nullopt;
}

const std::vector<part> &parts( kind shape )
{
    static const std::vector<part> table{
        { 0, 0, .94f, .94f, .70f, .78f },
        { -.37f, -.37f, .09f, .09f, 0, .70f },
        { .37f, -.37f, .09f, .09f, 0, .70f },
        { -.37f, .37f, .09f, .09f, 0, .70f },
        { .37f, .37f, .09f, .09f, 0, .70f }
    };
    static const std::vector<part> chair{
        { 0, 0, .58f, .58f, .40f, .48f },
        { 0, -.25f, .58f, .08f, .48f, 1.02f },
        { -.23f, -.23f, .07f, .07f, 0, .40f },
        { .23f, -.23f, .07f, .07f, 0, .40f },
        { -.23f, .23f, .07f, .07f, 0, .40f },
        { .23f, .23f, .07f, .07f, 0, .40f }
    };
    static const std::vector<part> bed{
        { 0, 0, .86f, .98f, .20f, .42f },
        { 0, -.46f, .90f, .08f, 0, .68f },
        { 0, .46f, .90f, .08f, 0, .32f },
        { -.40f, 0, .06f, .90f, .12f, .28f },
        { .40f, 0, .06f, .90f, .12f, .28f }
    };
    static const std::vector<part> bed_frame{
        { 0, -.46f, .90f, .08f, 0, .68f },
        { 0, .46f, .90f, .08f, 0, .32f },
        { -.40f, 0, .06f, .90f, .12f, .28f },
        { .40f, 0, .06f, .90f, .12f, .28f },
        { 0, -.28f, .76f, .10f, .20f, .25f },
        { 0, 0, .76f, .10f, .20f, .25f },
        { 0, .28f, .76f, .10f, .20f, .25f }
    };
    static const std::vector<part> crate{
        { 0, 0, .88f, .88f, 0, .72f }
    };
    static const std::vector<part> open_crate{
        { 0, 0, .88f, .88f, 0, .10f },
        { -.40f, 0, .08f, .88f, .10f, .72f },
        { .40f, 0, .08f, .88f, .10f, .72f },
        { 0, -.40f, .72f, .08f, .10f, .72f },
        { 0, .40f, .72f, .08f, .10f, .72f }
    };
    static const std::vector<part> cupboard{
        { 0, 0, .94f, .80f, 0, .86f }
    };
    static const std::vector<part> locker{
        { 0, 0, .94f, .84f, 0, 1.85f }
    };
    static const std::vector<part> cabinet{
        { -.43f, 0, .08f, .84f, 0, 1.85f },
        { .43f, 0, .08f, .84f, 0, 1.85f },
        { 0, -.38f, .78f, .08f, 0, 1.85f },
        { 0, 0, .78f, .76f, .04f, .12f },
        { 0, 0, .78f, .76f, .92f, 1.00f },
        { 0, 0, .94f, .84f, 1.77f, 1.85f }
    };
    static const std::vector<part> rack{
        { -.43f, -.38f, .07f, .07f, 0, 1.70f },
        { .43f, -.38f, .07f, .07f, 0, 1.70f },
        { -.43f, .38f, .07f, .07f, 0, 1.70f },
        { .43f, .38f, .07f, .07f, 0, 1.70f },
        { 0, 0, .94f, .84f, .08f, .14f },
        { 0, 0, .94f, .84f, .78f, .84f },
        { 0, 0, .94f, .84f, 1.48f, 1.54f }
    };
    static const std::vector<part> counter{
        { 0, 0, 1.0f, .82f, .80f, .89f },
        { 0, 0, .92f, .72f, 0, .80f }
    };
    static const std::vector<part> desk{
        { 0, 0, .96f, .80f, .70f, .78f },
        { -.32f, 0, .25f, .70f, 0, .70f },
        { .41f, 0, .08f, .70f, 0, .70f },
        { .06f, -.30f, .62f, .06f, .35f, .70f }
    };
    static const std::vector<part> sofa{
        { 0, 0, .98f, .84f, 0, .35f },
        { 0, -.31f, .98f, .22f, .35f, .90f },
        { 0, .10f, .72f, .60f, .35f, .46f },
        { -.425f, .10f, .13f, .60f, .35f, .66f },
        { .425f, .10f, .13f, .60f, .35f, .66f }
    };
    static const std::vector<part> bench{
        { 0, 0, .96f, .44f, .39f, .47f },
        { -.36f, 0, .10f, .36f, 0, .39f },
        { .36f, 0, .10f, .36f, 0, .39f }
    };
    static const std::vector<part> park_bench{
        { 0, 0, .98f, .48f, .39f, .47f },
        { -.37f, 0, .09f, .39f, 0, .39f },
        { .37f, 0, .09f, .39f, 0, .39f },
        { 0, -.20f, .98f, .08f, .55f, .88f },
        { -.37f, -.20f, .06f, .06f, .47f, .55f },
        { .37f, -.20f, .06f, .06f, .47f, .55f }
    };
    static const std::vector<part> armchair{
        { 0, 0, .72f, .65f, 0, .32f },
        { 0, -.25f, .76f, .19f, .32f, .98f },
        { 0, .07f, .51f, .50f, .32f, .46f },
        { -.32f, .07f, .13f, .50f, .32f, .65f },
        { .32f, .07f, .13f, .50f, .32f, .65f }
    };
    static const std::vector<part> fireplace{
        { 0, 0, .98f, .86f, 0, .14f },
        { -.40f, 0, .18f, .80f, .14f, .78f },
        { .40f, 0, .18f, .80f, .14f, .78f },
        { 0, -.32f, .62f, .16f, .14f, .78f },
        { 0, 0, .98f, .86f, .78f, .90f },
        { 0, -.13f, .78f, .54f, .90f, 1.40f },
        { 0, -.18f, .56f, .44f, 1.40f, 2.20f }
    };
    static const std::vector<part> sink{
        { 0, 0, .90f, .72f, 0, .74f },
        { 0, -.28f, .90f, .16f, .74f, .88f },
        { 0, .31f, .90f, .10f, .74f, .88f },
        { -.395f, .03f, .11f, .52f, .74f, .88f },
        { .395f, .03f, .11f, .52f, .74f, .88f },
        { 0, -.28f, .07f, .07f, .88f, 1.04f },
        { 0, -.21f, .07f, .16f, 1.00f, 1.04f }
    };
    static const std::vector<part> bathtub{
        { 0, 0, .78f, .96f, 0, .16f },
        { -.335f, 0, .11f, .96f, .16f, .55f },
        { .335f, 0, .11f, .96f, .16f, .55f },
        { 0, -.415f, .56f, .13f, .16f, .55f },
        { 0, .425f, .56f, .11f, .16f, .55f },
        { 0, -.415f, .06f, .06f, .55f, .67f },
        { 0, -.36f, .06f, .14f, .63f, .67f }
    };
    static const std::vector<part> toilet{
        { 0, -.29f, .58f, .25f, .39f, .92f },
        { 0, .03f, .30f, .48f, 0, .34f },
        { 0, .095f, .48f, .59f, .25f, .39f },
        { 0, -.16f, .48f, .08f, .39f, .48f },
        { 0, .335f, .40f, .11f, .39f, .48f },
        { -.19f, .07f, .10f, .38f, .39f, .48f },
        { .19f, .07f, .10f, .38f, .39f, .48f },
        { -.17f, .285f, .10f, .09f, .39f, .48f },
        { .17f, .285f, .10f, .09f, .39f, .48f }
    };
    static const std::vector<part> oven{
        { 0, 0, .88f, .78f, 0, .89f },
        { 0, -.35f, .88f, .08f, .89f, 1.03f }
    };
    static const std::vector<part> fridge{
        { 0, 0, .83f, .80f, 0, 1.82f }
    };
    static const std::vector<part> mini_fridge{
        { 0, 0, .60f, .64f, 0, .85f }
    };
    static const std::vector<part> dresser{
        { 0, 0, .93f, .72f, 0, .90f }
    };
    switch( shape ) {
        case kind::table:
            return table;
        case kind::chair:
            return chair;
        case kind::bed:
            return bed;
        case kind::bed_frame:
            return bed_frame;
        case kind::crate:
            return crate;
        case kind::open_crate:
            return open_crate;
        case kind::cupboard:
            return cupboard;
        case kind::cabinet:
            return cabinet;
        case kind::locker:
            return locker;
        case kind::rack:
            return rack;
        case kind::counter:
            return counter;
        case kind::desk:
            return desk;
        case kind::sofa:
            return sofa;
        case kind::bench:
            return bench;
        case kind::park_bench:
            return park_bench;
        case kind::armchair:
            return armchair;
        case kind::fireplace:
            return fireplace;
        case kind::sink:
            return sink;
        case kind::bathtub:
            return bathtub;
        case kind::toilet:
            return toilet;
        case kind::oven:
            return oven;
        case kind::fridge:
            return fridge;
        case kind::mini_fridge:
            return mini_fridge;
        case kind::dresser:
            return dresser;
    }
    return table;
}

float item_surface( kind shape )
{
    switch( shape ) {
        case kind::table:
            return .78f;
        case kind::chair:
            return .48f;
        case kind::bed:
            return .42f;
        case kind::bed_frame:
            return .25f;
        case kind::crate:
            return .72f;
        case kind::open_crate:
            return .10f;
        case kind::cupboard:
            return .15f;
        case kind::cabinet:
        case kind::locker:
            return 1.00f;
        case kind::rack:
            return .84f;
        case kind::counter:
            return .89f;
        case kind::desk:
            return .78f;
        case kind::sofa:
        case kind::armchair:
            return .46f;
        case kind::bench:
        case kind::park_bench:
            return .47f;
        case kind::fireplace:
            return .14f;
        case kind::sink:
            return .74f;
        case kind::bathtub:
            return .16f;
        case kind::toilet:
            return .39f;
        case kind::oven:
            return .89f;
        case kind::fridge:
            return .74f;
        case kind::mini_fridge:
            return .19f;
        case kind::dresser:
            return .25f;
    }
    return 0;
}

bool shows_contents( kind shape )
{
    return shape == kind::cupboard || shape == kind::locker || shape == kind::dresser ||
           shape == kind::fridge || shape == kind::mini_fridge;
}

bool chair_table( const std::optional<model> &furniture )
{
    return furniture && ( furniture->shape == kind::table || furniture->shape == kind::desk );
}

unsigned facing( kind shape, const std::array<bool, 4> &walls,
                 const std::array<bool, 4> &tables )
{
    if( shape == kind::chair ) {
        for( unsigned front = 0; front < tables.size(); ++front ) {
            if( tables[front] ) {
                return ( front + 2 ) % 4;
            }
        }
        return 2;
    }
    if( !shows_contents( shape ) && shape != kind::cabinet && shape != kind::counter &&
        shape != kind::desk && shape != kind::sofa && shape != kind::sink && shape != kind::oven &&
        shape != kind::bathtub && shape != kind::toilet && shape != kind::armchair &&
        shape != kind::fireplace && shape != kind::park_bench ) {
        return 0;
    }
    // Preserve the established orientation in free space. Prefer a wall behind
    // the cabinet and an open front; corners and corridors resolve consistently.
    for( unsigned back = 0; back < walls.size(); ++back ) {
        if( walls[back] && !walls[( back + 2 ) % 4] ) {
            return back;
        }
    }
    for( unsigned back = 0; back < walls.size(); ++back ) {
        if( !walls[( back + 2 ) % 4] ) {
            return back;
        }
    }
    return 0;
}

part rotated( part value, unsigned quarter_turns )
{
    const float x = value.x;
    const float z = value.z;
    switch( quarter_turns % 4 ) {
        case 1:
            value.x = -z;
            value.z = x;
            std::swap( value.width, value.depth );
            break;
        case 2:
            value.x = -x;
            value.z = -z;
            break;
        case 3:
            value.x = z;
            value.z = -x;
            std::swap( value.width, value.depth );
            break;
    }
    return value;
}

}
#endif
