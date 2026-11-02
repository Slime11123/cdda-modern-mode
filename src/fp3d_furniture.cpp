#include "fp3d_furniture.h"
#if defined(TILES) && defined(ENABLE_FP3D)
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
        if( appearance == "f_locker" || appearance == "f_bookcase" ) {
            return model{ id, kind::cabinet };
        }
        if( appearance == "f_rack" ) {
            return model{ id, kind::rack };
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
        { 0, .25f, .58f, .08f, .48f, 1.02f },
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
        case kind::rack:
            return rack;
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
            return .86f;
        case kind::cabinet:
            return 1.00f;
        case kind::rack:
            return .84f;
    }
    return 0;
}
}
#endif
