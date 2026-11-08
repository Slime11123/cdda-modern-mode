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
        if( appearance == "f_coffee_table" ) {
            return model{ id, kind::coffee_table };
        }
        if( appearance == "f_table" ) {
            return model{ id, kind::table };
        }
        if( appearance == "f_chair" ) {
            return model{ id, kind::chair };
        }
        if( appearance == "f_stool" ) {
            return model{ id, kind::stool };
        }
        if( appearance == "f_glass_fridge" ) {
            return model{ id, kind::chest_freezer };
        }
        if( appearance == "f_bed" || appearance == "f_bed_down" ) {
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
        if( appearance == "f_washer" || appearance == "f_dryer" ) {
            return model{ id, kind::washer };
        }
        if( appearance == "f_dishwasher" ) {
            return model{ id, kind::dishwasher };
        }
        if( appearance == "f_woodstove" ) {
            return model{ id, kind::wood_stove };
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
        // These sprites use several 2D pseudo-3D views; the geometry below
        // reconstructs real top/front/side surfaces rather than extruding them.
        if( appearance == "f_displaycase" || appearance == "f_displaycase_b" ) {
            return model{ id, kind::display_case };
        }
        if( appearance == "f_filing_cabinet" ) {
            return model{ id, kind::file_cabinet };
        }
        if( appearance == "f_vending_c" || appearance == "f_vending_o" ||
            appearance == "f_vending_reinforced" ) {
            return model{ id, kind::vending_machine };
        }
        if( appearance == "f_workbench" ) {
            return model{ id, kind::workbench };
        }
        if( appearance == "f_piano" ) {
            return model{ id, kind::piano };
        }
        if( appearance == "f_pool_table" ) {
            return model{ id, kind::pool_table };
        }
        if( appearance == "f_anvil" ) {
            return model{ id, kind::anvil };
        }
        if( appearance == "f_kiln_empty" || appearance == "f_kiln_full" ||
            appearance == "f_clay_kiln" ) {
            return model{ id, kind::kiln };
        }
        if( appearance == "f_kiln_metal_empty" || appearance == "f_kiln_metal_full" ||
            appearance == "f_kiln_portable_empty" || appearance == "f_kiln_portable_full" ) {
            return model{ id, kind::metal_kiln };
        }
        if( appearance == "f_tablesaw" ) {
            return model{ id, kind::table_saw };
        }
        // Outdoor/indoor fixtures are real, tall 3D objects.  The Undead
        // People images for street lights, power poles and mailboxes have
        // built-in 2D cast shadows; never use the complete sprite as geometry.
        if( appearance == "f_street_light" ) {
            return model{ id, kind::street_light };
        }
        if( appearance == "f_traffic_light" ) {
            return model{ id, kind::traffic_light };
        }
        if( appearance == "f_utility_pole" ) {
            return model{ id, kind::utility_pole };
        }
        if( appearance == "f_mailbox" || appearance == "f_vitrified_mailbox" ) {
            return model{ id, kind::mailbox };
        }
        if( appearance == "f_floor_lamp" ) {
            return model{ id, kind::floor_lamp };
        }
        if( appearance == "f_planter" || appearance == "f_planter_harvest" ||
            appearance == "f_planter_mature" || appearance == "f_planter_seed" ||
            appearance == "f_planter_seedling" ) {
            return model{ id, kind::planter };
        }
        appearance = definition->looks_like;
    }
    return std::nullopt;
}

const std::vector<part> &parts( kind shape )
{
    static const std::vector<part> coffee_table{
        { 0, 0, .94f, .68f, .38f, .45f },
        { -.39f, -.25f, .08f, .08f, 0, .38f },
        { .39f, -.25f, .08f, .08f, 0, .38f },
        { -.39f, .25f, .08f, .08f, 0, .38f },
        { .39f, .25f, .08f, .08f, 0, .38f }
    };
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
    static const std::vector<part> stool{
        { 0, 0, .56f, .56f, .45f, .53f, true },
        { -.17f, -.17f, .07f, .07f, 0, .45f },
        { .17f, -.17f, .07f, .07f, 0, .45f },
        { -.17f, .17f, .07f, .07f, 0, .45f },
        { .17f, .17f, .07f, .07f, 0, .45f }
    };
    static const std::vector<part> chest_freezer{
        { 0, 0, .94f, .84f, 0, .90f },
        { 0, 0, .94f, .84f, .90f, 1.00f }
    };
    // Bed: wooden bedframe, mattress and linen are DIFFERENT surfaces.
    // The Undead People 'f_bed' sprite is a flat woven mattress texture,
    // not a valid wood material for a headboard or upright side rails.
    static const std::vector<part> bed{
        { 0, 0, .82f, .89f, .22f, .29f },         // 0 wooden slat foundation
        { 0, 0, .76f, .85f, .29f, .43f },         // 1 spring mattress / ticking
        { 0, .205f, .75f, .42f, .43f, .451f },  // 2 folded cover (lying flat)
        { 0, -.31f, .43f, .18f, .43f, .474f },  // 3 thin pillow on mattress
        { 0, -.447f, .87f, .065f, .06f, .75f }, // 4 headboard
        { 0, .447f, .87f, .065f, .06f, .38f },  // 5 footboard
        { -.422f, 0, .065f, .94f, .18f, .32f }, // 6 side frame
        { .422f, 0, .065f, .94f, .18f, .32f },  // 7 side frame
        { -.41f, -.43f, .085f, .085f, 0, .75f },// 8 legs
        { .41f, -.43f, .085f, .085f, 0, .75f },
        { -.41f, .43f, .085f, .085f, 0, .38f },
        { .41f, .43f, .085f, .085f, 0, .38f }
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
    static const std::vector<part> washer{
        { 0, 0, .84f, .76f, 0, .88f }
    };
    static const std::vector<part> dishwasher{
        { 0, 0, .84f, .74f, 0, .88f }
    };
    static const std::vector<part> wood_stove{
        { 0, 0, .54f, .50f, .18f, .78f },
        { 0, 0, .58f, .54f, .78f, .84f },
        { 0, -.19f, .10f, .10f, .84f, 2.20f },
        { -.20f, -.18f, .07f, .07f, 0, .18f },
        { .20f, -.18f, .07f, .07f, 0, .18f },
        { -.20f, .18f, .07f, .07f, 0, .18f },
        { .20f, .18f, .07f, .07f, 0, .18f }
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
    // UndeadPeople: build real volume from each tile's silhouette.  The 2D
    // pixel art is only a material source, never an extruded 2D plane.
    // Two-level low display counter, matching the *lower two drawers* of
    // the UndeadPeople tile.  Its painted blue upper area is NOT a glass
    // showcase: never build glass panes or elevated posts for this variant.
    static const std::vector<part> display_case{
        { 0, 0, .95f, .82f, 0, .72f },            // two-drawer carcass
        { 0, .420f, .86f, .027f, .39f, .69f },   // upper real drawer
        { 0, .420f, .86f, .027f, .06f, .35f },   // lower real drawer
        { 0, 0, .99f, .86f, .72f, .79f }        // flat top
    };
    // Four front-facing metal drawers, including the bottom one previously
    // missing. The drawer plates project slightly beyond the cabinet front.
    static const std::vector<part> file_cabinet{
        { 0, 0, .84f, .78f, 0, 1.78f },          // side steel casing
        { 0, 0, .88f, .82f, 1.78f, 1.82f },     // top
        { 0, .405f, .71f, .034f, 1.37f, 1.73f }, // drawer 1
        { 0, .405f, .71f, .034f, .96f, 1.32f },  // drawer 2
        { 0, .405f, .71f, .034f, .55f, .91f },   // drawer 3
        { 0, .405f, .71f, .034f, .14f, .50f }    // drawer 4 (bottom)
    };
    // Vending machine: red chassis, large raised display, separate coin
    // control column, lower collection hatch, and a proper header.
    static const std::vector<part> vending_machine{
        { 0, 0, .94f, .84f, 0, 1.85f },
        { 0, 0, .98f, .88f, 1.85f, 1.98f },
        { -.125f, .436f, .61f, .028f, .79f, 1.73f },
        { .337f, .438f, .19f, .028f, .83f, 1.55f },
        { -.10f, .439f, .43f, .03f, .27f, .60f },
        { 0, .432f, .89f, .023f, .05f, .18f }
    };
    static const std::vector<part> workbench{
        { 0, 0, .96f, .85f, .76f, .89f },
        { -.39f, -.35f, .10f, .10f, 0, .76f },
        { .39f, -.35f, .10f, .10f, 0, .76f },
        { -.39f, .35f, .10f, .10f, 0, .76f },
        { .39f, .35f, .10f, .10f, 0, .76f },
        { 0, -.35f, .88f, .08f, .15f, .29f }
    };
    static const std::vector<part> piano{
        { 0, -.11f, .90f, .69f, .49f, 1.24f },
        { 0, .27f, .92f, .24f, .76f, .90f },
        { 0, -.11f, .94f, .73f, 1.24f, 1.30f },
        { -.35f, .23f, .10f, .10f, 0, .76f },
        { .35f, .23f, .10f, .10f, 0, .76f },
        { -.35f, -.34f, .10f, .10f, 0, .49f },
        { .35f, -.34f, .10f, .10f, 0, .49f }
    };
    static const std::vector<part> pool_table{
        { 0, 0, .98f, .98f, .73f, .81f },
        { 0, 0, .83f, .83f, .81f, .83f },
        { -.39f, -.39f, .12f, .12f, 0, .73f },
        { .39f, -.39f, .12f, .12f, 0, .73f },
        { -.39f, .39f, .12f, .12f, 0, .73f },
        { .39f, .39f, .12f, .12f, 0, .73f }
    };
    // Anvil's unmistakable overhanging horn and hardy face, forged waist and
    // broad foot. The source 2D sprite's dark painted shadow is not a model.
    static const std::vector<part> anvil{
        { 0, 0, .47f, .40f, .06f, .21f },        // cast iron foot
        { 0, 0, .29f, .25f, .21f, .52f },        // narrow waist
        { 0, 0, .57f, .34f, .52f, .63f },        // flared shoulder
        { 0, 0, .65f, .42f, .63f, .76f },        // wide flat working face
        { -.38f, 0, .26f, .27f, .62f, .71f },   // tapered horn root
        { -.54f, 0, .12f, .14f, .65f, .685f }, // horn tip
        { .36f, 0, .21f, .30f, .62f, .70f }    // square heel
    };
    // A primitive, hand-stacked small-stone dome. Rings of staggered,
    // irregular masonry follow a curved profile; the front is deliberately
    // missing stones in a narrowing arch, exposing an actual hollow cavity.
    // No large slabs, vertical walls, closed roof, or 2D pseudo-3D sprite.
    // The entire model is static/deterministic for display-list caching and
    // part-level mouse picking. Parts 0/1 are the sooty interior, 2+ stones.
    static const std::vector<part> kiln = [] {
        std::vector<part> pieces;
        pieces.reserve( 140 );
        pieces.push_back( { 0, .075f, .60f, .58f, .065f, .105f } ); // hearth floor
        pieces.push_back( { 0, -.235f, .46f, .045f, .10f, .53f } ); // interior soot
        constexpr std::array<float, 8> ring_x{
            .427f, .418f, .403f, .375f, .337f, .287f, .224f, .148f
        };
        constexpr std::array<float, 8> ring_z{
            .427f, .415f, .399f, .373f, .332f, .279f, .216f, .144f
        };
        constexpr std::array<int, 8> counts{
            18, 18, 17, 16, 15, 13, 11, 9
        };
        constexpr float two_pi = 6.28318530718f;
        for( int course = 0; course < 8; ++course ) {
            const int count = counts[course];
            const float low = .020f + course * .103f;
            const float high = low + .122f;
            const float level = ( low + high ) * .5f;
            // Broad vertical jambs and a rounded arch. The opening narrows
            // above the shoulder rather than ending with a square lintel.
            const float mouth = level < .48f ? .234f :
                                level < .60f ? .211f :
                                level < .70f ? .167f :
                                level < .78f ? .085f : 0.0f;
            for( int i = 0; i < count; ++i ) {
                const float angle = two_pi * ( i + ( course % 2 ? .5f : 0.0f ) ) / count;
                const float cx = std::cos( angle ) * ring_x[course];
                const float cz = std::sin( angle ) * ring_z[course];
                // The front (+z) of the shell has a genuine empty entrance.
                if( cz > .18f && std::abs( cx ) < mouth ) {
                    continue;
                }
                const float drift = .004f * ( ( course * 3 + i * 7 ) % 5 - 2 );
                const float jitter = .003f * ( ( course + i * 3 ) % 5 - 2 );
                const float width = .139f + .010f * ( ( i + course ) % 4 );
                const float depth = .143f + .008f * ( ( i * 3 + course ) % 4 );
                pieces.push_back( { cx + drift * std::cos( angle ),
                                    cz + drift * std::sin( angle ), width, depth,
                                    low + jitter, high + jitter } );
            }
        }
        // A few loose stones along the hearth lip and a crown stone over
        // the arch; neither fills the entrance or seals the interior floor.
        pieces.push_back( { -.31f, .386f, .145f, .15f, .018f, .126f } );
        pieces.push_back( { .31f, .382f, .155f, .14f, .024f, .133f } );
        pieces.push_back( { -.102f, .405f, .108f, .126f, .014f, .084f } );
        pieces.push_back( { .121f, .400f, .116f, .13f, .012f, .077f } );
        pieces.push_back( { 0, .018f, .183f, .176f, .848f, .934f } );
        return pieces;
    }();
    // Industrial/portable kiln artwork is a *different object*: metal drum,
    // vent pipe, door and control lights. Do not share the clay kiln model.
    static const std::vector<part> metal_kiln{
        { 0, 0, .72f, .68f, .12f, .89f, true },
        { 0, 0, .77f, .74f, .89f, .98f, true },
        { 0, 0, .54f, .51f, .98f, 1.01f, true },
        { .27f, -.18f, .15f, .15f, .88f, 1.60f, true },
        { .27f, -.18f, .20f, .20f, 1.60f, 1.68f, true },
        { 0, .355f, .40f, .032f, .33f, .63f }, // dark access hatch
        { -.25f, .354f, .12f, .027f, .46f, .69f },
        { -.24f, -.25f, .07f, .07f, 0, .13f },
        { .24f, -.25f, .07f, .07f, 0, .13f },
        { -.24f, .25f, .07f, .07f, 0, .13f },
        { .24f, .25f, .07f, .07f, 0, .13f }
    };
    static const std::vector<part> table_saw{
        { 0, 0, .98f, .83f, .71f, .83f },
        { -.38f, -.30f, .09f, .09f, 0, .71f },
        { .38f, -.30f, .09f, .09f, 0, .71f },
        { -.38f, .30f, .09f, .09f, 0, .71f },
        { .38f, .30f, .09f, .09f, 0, .71f },
        // A thin blade rising out of the top; not the 2D blade/shadow image.
        { 0, -.10f, .045f, .45f, .83f, 1.03f }
    };
    static const std::vector<part> street_light{
        { 0, 0, .26f, .26f, 0, .16f, true },   // cast base
        { 0, 0, .115f, .115f, .16f, 3.23f, true },
        { 0, .22f, .09f, .55f, 3.15f, 3.26f }, // horizontal outreach
        { 0, .47f, .54f, .31f, 3.10f, 3.26f }, // metal lamp shell
        { 0, .47f, .46f, .26f, 3.065f, 3.105f } // diffuser below shade
    };
    static const std::vector<part> traffic_light{
        { 0, 0, .25f, .25f, 0, .14f, true },
        { 0, 0, .095f, .095f, .14f, 2.95f, true },
        { 0, .135f, .18f, .28f, 2.09f, 3.09f }, // traffic light box
        { 0, .288f, .125f, .025f, 2.20f, 2.94f } // signal glass
    };
    static const std::vector<part> utility_pole{
        { 0, 0, .24f, .24f, 0, 4.05f, true },  // timber utility pole
        { 0, 0, .93f, .12f, 3.55f, 3.68f },
        { 0, 0, .82f, .10f, 3.09f, 3.19f },
        { -.38f, 0, .075f, .075f, 3.19f, 3.82f, true },
        { .38f, 0, .075f, .075f, 3.19f, 3.82f, true }
    };
    static const std::vector<part> mailbox{
        { 0, 0, .13f, .13f, 0, 1.08f },        // wooden support
        { 0, .06f, .35f, .32f, 1.01f, 1.065f },// shelf
        { 0, .08f, .60f, .41f, 1.065f, 1.37f },// red mail box
        { 0, .295f, .51f, .025f, 1.13f, 1.33f }, // door lip
        { .33f, .10f, .055f, .08f, 1.27f, 1.55f } // raised flag
    };
    static const std::vector<part> floor_lamp{
        { 0, 0, .28f, .28f, 0, .10f, true },
        { 0, 0, .055f, .055f, .10f, 1.37f, true },
        { 0, 0, .48f, .48f, 1.33f, 1.69f, true },// shade
        { 0, 0, .28f, .28f, 1.69f, 1.72f, true }
    };
    static const std::vector<part> planter{
        { 0, 0, .94f, .94f, 0, .36f },
        { 0, 0, .80f, .80f, .36f, .39f },
        { -.45f, 0, .07f, .94f, .36f, .45f },
        { .45f, 0, .07f, .94f, .36f, .45f },
        { 0, -.45f, .94f, .07f, .36f, .45f },
        { 0, .45f, .94f, .07f, .36f, .45f }
    };
    switch( shape ) {
        case kind::coffee_table:
            return coffee_table;
        case kind::table:
            return table;
        case kind::chair:
            return chair;
        case kind::stool:
            return stool;
        case kind::chest_freezer:
            return chest_freezer;
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
        case kind::washer:
            return washer;
        case kind::dishwasher:
            return dishwasher;
        case kind::wood_stove:
            return wood_stove;
        case kind::fridge:
            return fridge;
        case kind::mini_fridge:
            return mini_fridge;
        case kind::dresser:
            return dresser;
        case kind::display_case:
            return display_case;
        case kind::file_cabinet:
            return file_cabinet;
        case kind::vending_machine:
            return vending_machine;
        case kind::workbench:
            return workbench;
        case kind::piano:
            return piano;
        case kind::pool_table:
            return pool_table;
        case kind::anvil:
            return anvil;
        case kind::kiln:
            return kiln;
        case kind::metal_kiln:
            return metal_kiln;
        case kind::table_saw:
            return table_saw;
        case kind::planter:
            return planter;
        case kind::street_light:
            return street_light;
        case kind::traffic_light:
            return traffic_light;
        case kind::utility_pole:
            return utility_pole;
        case kind::mailbox:
            return mailbox;
        case kind::floor_lamp:
            return floor_lamp;
    }
    return table;
}

float item_surface( kind shape )
{
    switch( shape ) {
        case kind::coffee_table:
            return .45f;
        case kind::table:
            return .78f;
        case kind::chair:
            return .48f;
        case kind::stool:
            return .53f;
        case kind::chest_freezer:
            return .40f;
        case kind::bed:
            return .451f;
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
        case kind::washer:
        case kind::dishwasher:
            return .88f;
        case kind::wood_stove:
            return .84f;
        case kind::fridge:
            return .74f;
        case kind::mini_fridge:
            return .19f;
        case kind::dresser:
            return .25f;
        case kind::display_case:
            return .79f;
        case kind::file_cabinet:
            return 1.82f;
        case kind::vending_machine:
            return 1.98f;
        case kind::workbench:
            return .89f;
        case kind::piano:
            return 1.30f;
        case kind::pool_table:
            return .83f;
        case kind::anvil:
            return .77f;
        case kind::kiln:
            return .934f;
        case kind::metal_kiln:
            return 1.01f;
        case kind::table_saw:
            return .83f;
        case kind::planter:
            return .39f;
        case kind::street_light:
        case kind::traffic_light:
        case kind::utility_pole:
        case kind::floor_lamp:
            return 0;
        case kind::mailbox:
            return 1.37f;
    }
    return 0;
}

bool shows_contents( kind shape )
{
    return shape == kind::cupboard || shape == kind::locker || shape == kind::dresser ||
           shape == kind::fridge || shape == kind::mini_fridge || shape == kind::chest_freezer;
}

bool chair_table( const std::optional<model> &furniture )
{
    return furniture && ( furniture->shape == kind::table || furniture->shape == kind::desk ||
                         furniture->shape == kind::coffee_table );
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
        shape != kind::bench && shape != kind::desk && shape != kind::sofa &&
        shape != kind::sink && shape != kind::oven &&
        shape != kind::bathtub && shape != kind::toilet && shape != kind::armchair &&
        shape != kind::fireplace && shape != kind::park_bench && shape != kind::washer &&
        shape != kind::dishwasher && shape != kind::wood_stove &&
        shape != kind::display_case && shape != kind::file_cabinet &&
        shape != kind::vending_machine && shape != kind::workbench &&
        shape != kind::piano && shape != kind::table_saw &&
        shape != kind::kiln && shape != kind::metal_kiln &&
        shape != kind::street_light && shape != kind::mailbox &&
        shape != kind::traffic_light ) {
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
