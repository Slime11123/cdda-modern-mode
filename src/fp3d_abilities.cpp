#include "fp3d_abilities.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <set>

#include "avatar.h"
#include "bionics.h"
#include "character_martial_arts.h"
#include "item.h"
#include "item_location.h"
#include "magic.h"
#include "martialarts.h"
#include "messages.h"
#include "mutation.h"
#include "string_formatter.h"
#include "translations.h"
#include "uilist.h"

namespace fp3d_abilities
{
std::vector<matype_id> available_styles( avatar &you ) {
    std::set<matype_id> from_bionics;
    for( int i = 0; i < you.num_bionics(); ++i ) {
        const bionic &bio = you.bionic_at_index( i );
        if( bio.powered ) {
            from_bionics.insert( bio.info().ma_styles.begin(), bio.info().ma_styles.end() );
        }
    }
    if( from_bionics.empty() ) {
        return you.martial_arts_data->get_known_styles( false );
    }
    return { from_bionics.begin(), from_bionics.end() };
}

bool mutation_ready( const avatar &you, const trait_id &id ) {
    return you.has_active_mutation( id ) ||
           ( you.can_power_mutation( id ) &&
             ( !id->hunger || you.get_kcal_percent() >= .8f ) &&
             ( !id->thirst || you.get_thirst() <= 400 ) &&
             ( !id->sleepiness || you.get_sleepiness() <= 400 ) &&
             ( !id->mana || you.magic->available_mana() >= id->cost ) );
}

static bool operate_bionic( avatar &you, const bionic_request &request, bool &close ) {
    const auto installed = you.find_bionic_by_uid( request.uid );
    if( !installed ) {
        return false;
    }
    bionic &bio = **installed;
    if( request.control == bionic_control::fuel ) {
        if( bio.info().fuel_opts.empty() && !bio.info().is_remote_fueled ) {
            return false;
        }
        bio.toggle_safe_fuel_mod();
        return true;
    }
    if( request.control == bionic_control::weapon ) {
        if( bio.powered || !bio.can_install_weapon() ) {
            return false;
        }
        if( bio.has_weapon() ) {
            if( std::optional<item> weapon = bio.uninstall_weapon() ) {
                you.i_add_or_drop( *weapon );
                return true;
            }
            return false;
        }
        const std::vector<item *> weapons = you.items_with( [&bio]( const item &candidate ) {
            return bio.can_install_weapon( candidate );
        } );
        if( weapons.empty() ) {
            return false;
        }
        uilist choices;
        choices.title = "安装仿生武器";
        for( size_t i = 0; i < weapons.size(); ++i ) {
            choices.addentry( i, true, MENU_AUTOASSIGN, weapons[i]->tname() );
        }
        choices.query();
        if( choices.ret < 0 || static_cast<size_t>( choices.ret ) >= weapons.size() ) {
            return false;
        }
        item_location weapon( you, weapons[choices.ret] );
        if( weapon && bio.install_weapon( *weapon ) ) {
            weapon.remove_item();
            return true;
        }
        return false;
    }
    if( !bio.info().activated ) {
        return false;
    }
    if( bio.powered ) {
        const bool closes_on_deactivation = bio.info().deactivated_close_ui;
        const bool deactivated = you.deactivate_bionic( bio );
        close |= deactivated && closes_on_deactivation;
        return deactivated;
    }
    if( bio.can_install_weapon() && !bio.has_weapon() ) {
        return false;
    }
    const bool closes_on_activation = bio.info().activated_close_ui;
    const bool activated = you.activate_bionic( bio, false, &close );
    close |= activated && closes_on_activation;
    return activated;
}

bool execute( avatar &you, const ability_request &request, bool &close )
{
    bool success = false;
    if( const auto *id = std::get_if<trait_id>( &request ) ) {
        const auto available = you.get_functioning_mutations( false, true );
        if( std::find( available.begin(), available.end(), *id ) != available.end() &&
            ( *id )->activated && mutation_ready( you, *id ) ) {
            if( you.has_active_mutation( *id ) ) {
                add_msg( m_neutral, _( "You stop using your %s." ), you.mutation_name( *id ) );
                you.deactivate_mutation( *id );
            } else {
                add_msg( m_neutral, string_format( ( *id )->activation_msg, you.mutation_name( *id ) ) );
                you.activate_mutation( *id );
            }
            success = true;
        }
    } else if( const auto *bio = std::get_if<bionic_request>( &request ) ) {
        success = operate_bionic( you, *bio, close );
    } else if( const auto *style = std::get_if<matype_id>( &request ) ) {
        const auto available = available_styles( you );
        if( std::find( available.begin(), available.end(), *style ) != available.end() ) {
            auto &martial = *you.martial_arts_data;
            martial.clear_all_effects( you );
            martial.set_style( *style, true );
            martial.ma_static_effects( you );
            martial.martialart_use_message( you );
            success = true;
        }
    } else {
        you.martial_arts_data->keep_hands_free = std::get<bool>( request );
        success = true;
    }
    return success;
}
}
#endif
