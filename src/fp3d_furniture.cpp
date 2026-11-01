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
        { 0, .46f, .90f, .08f, 0, .68f },
        { 0, -.46f, .90f, .08f, 0, .32f },
        { -.40f, 0, .06f, .90f, .12f, .28f },
        { .40f, 0, .06f, .90f, .12f, .28f }
    };
    switch( shape ) {
        case kind::table:
            return table;
        case kind::chair:
            return chair;
        case kind::bed:
            return bed;
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
    }
    return 0;
}
}
#endif
