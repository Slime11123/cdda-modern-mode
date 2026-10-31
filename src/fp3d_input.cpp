#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_camera.h"
#include "cata_scope_helpers.h"
#include "avatar.h"
#include "game.h"
#include "map.h"
#include "vehicle.h"
#include "veh_type.h"
#include "input_context.h"
#include "translations.h"
#include "uilist.h"

namespace fp3d_input
{
namespace
{
bool editing_modifiers = false;
}
bool modifier_binding_editor()
{
    return editing_modifiers;
}
input_context context()
{
    input_context result( "FP3D", keyboard_mode::keycode );
    result.register_action( "FP3D_FORWARD", to_translation( "3D 前进（朝视角方向）" ) );
    result.register_action( "FP3D_BACKWARD", to_translation( "3D 后退" ) );
    result.register_action( "FP3D_LEFT", to_translation( "3D 向左移动" ) );
    result.register_action( "FP3D_RIGHT", to_translation( "3D 向右移动" ) );
    result.register_action( "FP3D_BINDINGS", to_translation( "2D / 3D 操作设置" ) );
    for( const auto &entry : {
             std::pair{ "FP3D_INTERACT", "3D 交互 / 长按情境操作" },
             std::pair{ "FP3D_PICKUP", "3D 拾取 / 长按附近物品" },
             std::pair{ "FP3D_ATTACK", "3D 近战攻击（可按住）" },
             std::pair{ "FP3D_AIM", "3D 原版瞄准 / 射击" },
             std::pair{ "FP3D_PANEL", "3D 综合生存面板" },
             std::pair{ "FP3D_RELOAD", "3D 装填手持武器" },
             std::pair{ "FP3D_RUN", "3D 切换奔跑" },
             std::pair{ "FP3D_CROUCH", "3D 切换蹲姿" },
             std::pair{ "FP3D_MAP", "3D 大地图" },
             std::pair{ "FP3D_WAIT", "3D 等待一回合" },
             std::pair{ "FP3D_STATUS", "3D 按住展开状态" },
             std::pair{ "FP3D_ACTIONS", "3D 快捷行动" }
         } ) {
        result.register_action( entry.first, to_translation( entry.second ) );
    }
    for( int slot = 1; slot <= 8; ++slot ) {
        result.register_action( "FP3D_SLOT_" + std::to_string( slot ),
                                to_translation( "3D 快捷栏切换手持" ) );
    }
    result.register_action( "FP3D_SLOT_NEXT", to_translation( "3D 下一个快捷栏" ) );
    result.register_action( "FP3D_SLOT_PREV", to_translation( "3D 上一个快捷栏" ) );
    return result;
}

std::string resolve( mode controls, const input_event &event, float yaw, bool driving )
{
    if( controls != mode::first_person_3d ) {
        return {};
    }
    const auto profile = context();
    std::string command = profile.input_to_action( event );
    if( command == "ERROR" && !event.modifiers.count( keymod_t::alt ) ) {
        auto movement_event = event;
        movement_event.modifiers.erase( keymod_t::shift );
        movement_event.modifiers.erase( keymod_t::ctrl );
        const auto unmodified = profile.input_to_action( movement_event );
        if( unmodified == "FP3D_FORWARD" || unmodified == "FP3D_BACKWARD" ||
            unmodified == "FP3D_LEFT" || unmodified == "FP3D_RIGHT" ) {
            command = unmodified;
        }
    }
    int forward = 0;
    int right = 0;
    if( command == "FP3D_FORWARD" ) {
        forward = 1;
    } else if( command == "FP3D_BACKWARD" ) {
        forward = -1;
    } else if( command == "FP3D_LEFT" ) {
        right = -1;
    } else if( command == "FP3D_RIGHT" ) {
        right = 1;
    } else if( command == "FP3D_BINDINGS" ) {
        return action_ident( ACTION_KEYBINDINGS );
    } else {
        for( const auto &entry : {
                 std::pair{ "FP3D_RELOAD", ACTION_RELOAD_WIELDED },
                 std::pair{ "FP3D_RUN", ACTION_TOGGLE_RUN },
                 std::pair{ "FP3D_CROUCH", ACTION_TOGGLE_CROUCH },
                 std::pair{ "FP3D_WAIT", ACTION_PAUSE }
             } ) {
            if( command == entry.first ) {
                return action_ident( entry.second );
            }
        }
        if( command == "FP3D_PANEL" || command == "FP3D_MAP" || command == "FP3D_ACTIONS" ||
            command.compare( 0, 10, "FP3D_SLOT_" ) == 0 ) {
            return command;
        }
        return {};
    }
    return action_ident( get_movement_action_from_delta(
                             driving ? tripoint_rel_ms( right, -forward, 0 ) : fp3d_camera::movement( yaw, forward, right ),
                             driving ? iso_rotate::no : iso_rotate::yes ) );
}

bool vehicle_controls()
{
    avatar &you = get_avatar();
    if( you.is_dead_state() ) {
        return false;
    }
    map &here = get_map();
    const auto vp = here.veh_at( you.pos_bub( here ) );
    return ( vp && vp->vehicle().player_in_control( here, you ) ) || ( g && g->remoteveh() );
}

void register_world_actions( input_context &world )
{
    for( const char *command : {
             "FP3D_INTERACT", "FP3D_INTERACT_LONG", "FP3D_PICKUP",
             "FP3D_PICKUP_LONG", "FP3D_ATTACK", "FP3D_AIM", "FP3D_PANEL", "FP3D_MAP", "FP3D_ACTIONS"
         } ) {
        world.register_action( command );
    }
    for( int slot = 1; slot <= 8; ++slot ) {
        world.register_action( "FP3D_SLOT_" + std::to_string( slot ) );
    }
    world.register_action( "FP3D_SLOT_NEXT" );
    world.register_action( "FP3D_SLOT_PREV" );
}

gestures &world_gestures()
{
    static gestures value;
    return value;
}

bool gestures::event( const input_event &input, bool down, unsigned now )
{
    if( input.sequence.empty() ) {
        return false;
    }
    const auto key = std::make_pair( input.type, input.get_first_input() );
    if( !down ) {
        const auto it = buttons.find( key );
        if( it == buttons.end() ) {
            return false;
        }
        if( it->second.command == "FP3D_STATUS" ) {
            expanded = false;
        } else if( !it->second.fired && pending.size() < 4 ) {
            pending.push_back( it->second.command );
        }
        buttons.erase( it );
        return true;
    }
    const std::string command = context().input_to_action( input );
    if( command != "FP3D_INTERACT" && command != "FP3D_PICKUP" && command != "FP3D_ATTACK" &&
        command != "FP3D_AIM" && command != "FP3D_STATUS" && command != "FP3D_RUN" &&
        command != "FP3D_CROUCH" ) {
        return false;
    }
    if( buttons.count( key ) ) {
        return true;
    }
    const bool immediate = command == "FP3D_ATTACK" || command == "FP3D_AIM" ||
                           command == "FP3D_STATUS" || command == "FP3D_RUN" || command == "FP3D_CROUCH";
    buttons.emplace( key, held{ command, now, immediate } );
    if( command == "FP3D_STATUS" ) {
        expanded = true;
    } else if( immediate && pending.size() < 4 ) {
        pending.push_back( command == "FP3D_RUN" || command == "FP3D_CROUCH" ?
                           resolve( mode::first_person_3d, input, 0 ) : command );
    }
    return true;
}

std::string gestures::poll( unsigned now )
{
    if( !pending.empty() ) {
        std::string command = std::move( pending.front() );
        pending.pop_front();
        return command;
    }
    for( auto &entry : buttons ) {
        auto &button = entry.second;
        if( now - button.started < 250 ) {
            continue;
        }
        if( button.command == "FP3D_ATTACK" ) {
            return button.command; // At most once per native action request.
        }
        if( !button.fired && ( button.command == "FP3D_INTERACT" || button.command == "FP3D_PICKUP" ) ) {
            button.fired = true;
            return button.command + "_LONG";
        }
    }
    return {};
}

void gestures::cancel()
{
    buttons.clear();
    pending.clear();
    expanded = false;
}

action_id display_bindings( input_context &original )
{
    uilist menu;
    menu.title = "操作方式（F5 切换 2D / 3D）";
    menu.text =
        "2D 使用原版操作；3D 使用独立键鼠配置。背包等菜单使用各自操作。\n"
        "修改 3D 键位时，请使用当前界面的局部绑定（+），两套配置互不覆盖。";
    menu.addentry( 0, true, '1', "1. 2D 原版操作与按键" );
    menu.addentry( 1, true, '2', "2. 3D 第一人称操作与按键" );
    menu.selected = 1;
    menu.query();
    if( menu.ret == 0 ) {
        return original.display_menu( true );
    }
    if( menu.ret == 1 ) {
        restore_on_out_of_scope restore_editor( editing_modifiers );
        editing_modifiers = true;
        auto profile = context();
        profile.display_menu();
    }
    return ACTION_NULL;
}
}
#endif
