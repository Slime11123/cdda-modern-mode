#pragma once
#ifndef CATA_SRC_FP3D_BESTIARY_H
#define CATA_SRC_FP3D_BESTIARY_H

#if defined(TILES) && defined(ENABLE_FP3D)

#include <algorithm>
#include <string>
#include <vector>

#include "monstergenerator.h"
#include "mtype.h"

namespace fp3d_bestiary
{
struct record {
    const mtype *type = nullptr;
    std::string name;
    std::string id;
    int category = 11;
};

// This is a presentation taxonomy, not a replacement for CDDA monster species.
// Species overlap (e.g. zombie humans), so the first applicable group wins.
inline const char *category_name( int category )
{
    static constexpr const char *names[] = {
        "全部", "丧尸", "动物", "昆虫与蛛类", "真菌", "植物",
        "异界与恐怖", "机器人与改造体", "变异生物", "黏液与异常体",
        "人类与狂暴者", "其他"
    };
    return category >= 0 && category < 12 ? names[category] : names[11];
}

inline int classify( const mtype &mon )
{
    const auto has = [&]( const char *id ) {
        return mon.species.count( species_id( id ) ) != 0;
    };
    if( has( "ZOMBIE" ) ) {
        return 1;
    }
    if( has( "FUNGUS" ) ) {
        return 4;
    }
    if( has( "PLANT" ) || has( "LEECH_PLANT" ) ) {
        return 5;
    }
    if( has( "ROBOT" ) || has( "CYBORG" ) || has( "ROBOT_FLYING" ) ) {
        return 7;
    }
    if( has( "NETHER" ) || has( "NETHER_EMANATION" ) || has( "MIGO" ) ||
        has( "HORROR" ) || has( "ABERRATION" ) ) {
        return 6;
    }
    if( has( "MUTANT" ) ) {
        return 8;
    }
    if( has( "BLOB" ) || has( "SLIME" ) || has( "HALLUCINATION" ) ) {
        return 9;
    }
    if( has( "INSECT" ) || has( "INSECT_FLYING" ) || has( "CENTIPEDE" ) ||
        has( "SPIDER" ) || has( "WORM" ) ) {
        return 3;
    }
    if( has( "MAMMAL" ) || has( "BIRD" ) || has( "FISH" ) || has( "REPTILE" ) ||
        has( "AMPHIBIAN" ) || has( "MOLLUSK" ) ) {
        return 2;
    }
    if( has( "HUMAN" ) || has( "FERAL" ) ) {
        return 10;
    }
    return 11;
}

inline std::vector<record> load_catalog()
{
    std::vector<record> records;
    const auto &types = MonsterGenerator::generator().get_all_mtypes();
    records.reserve( types.size() );
    for( const mtype &type : types ) {
        if( type.id == mtype_id::NULL_ID() || type.id.str().empty() ) {
            continue;
        }
        records.push_back( { &type, type.nname(), type.id.str(), classify( type ) } );
    }
    std::sort( records.begin(), records.end(), []( const record &a, const record &b ) {
        return a.name == b.name ? a.id < b.id : a.name < b.name;
    } );
    return records;
}
} // namespace fp3d_bestiary

#endif
#endif
