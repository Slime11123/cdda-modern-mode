#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "fp3d_camera.h"
#include "fp3d_compositor.h"
#include "cata_scope_helpers.h"
#include "avatar.h"
#include "game.h"
#include "map.h"
#include "vehicle.h"
#include "veh_type.h"
#include "input_context.h"
#include "translations.h"
#include "uilist.h"
#include "options.h"
#include "sdl_wrappers.h"

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
mode next_mode( mode controls )
{
    switch( controls ) {
        case mode::original_2d: return mode::modern_2d;
        case mode::modern_2d: return mode::first_person_3d;
        case mode::first_person_3d: return mode::original_2d;
    }
    return mode::original_2d;
}

input_context modern_2d_context()
{
    input_context result( "MODERN_2D", keyboard_mode::keycode );
    result.register_action( "MODERN_2D_NORTH", to_translation( "2D现代 向北移动" ) );
    result.register_action( "MODERN_2D_SOUTH", to_translation( "2D现代 向南移动" ) );
    result.register_action( "MODERN_2D_WEST", to_translation( "2D现代 向西移动" ) );
    result.register_action( "MODERN_2D_EAST", to_translation( "2D现代 向东移动" ) );
    result.register_action( "FP3D_PANEL", to_translation( "2D现代 综合生存面板" ) );
    result.register_action( "FP3D_ACTIONS", to_translation( "2D现代 快捷行动菜单" ) );
    return result;
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
             std::pair{ "FP3D_ATTACK", "3D 准星近战 / 砸击（可按住）" },
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
    if( controls == mode::modern_2d ) {
        const auto command = modern_2d_context().input_to_action( event );
        if( command == "FP3D_PANEL" || command == "FP3D_ACTIONS" ) {
            return command;
        }
        for( const auto &entry : {
                 std::pair{ "MODERN_2D_NORTH", tripoint_rel_ms::north },
                 std::pair{ "MODERN_2D_SOUTH", tripoint_rel_ms::south },
                 std::pair{ "MODERN_2D_WEST", tripoint_rel_ms::west },
                 std::pair{ "MODERN_2D_EAST", tripoint_rel_ms::east }
             } ) {
            if( command == entry.first ) {
                return action_ident( get_movement_action_from_delta( entry.second,
                                     driving ? iso_rotate::no : iso_rotate::yes ) );
            }
        }
        return {};
    }
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

bool gestures::mouse_button( unsigned button, bool down, unsigned now )
{
    std::pair<MouseInput, MouseInput> keys;
    switch( button ) {
        case SDL_BUTTON_LEFT:
            keys = { MouseInput::LeftButtonPressed, MouseInput::LeftButtonReleased };
            break;
        case SDL_BUTTON_RIGHT:
            keys = { MouseInput::RightButtonPressed, MouseInput::RightButtonReleased };
            break;
        case SDL_BUTTON_X1:
            keys = { MouseInput::X1ButtonPressed, MouseInput::X1ButtonReleased };
            break;
        case SDL_BUTTON_X2:
            keys = { MouseInput::X2ButtonPressed, MouseInput::X2ButtonReleased };
            break;
        default:
            return false;
    }
    const input_event pressed( keys.first, input_event_t::mouse );
    const input_event released( keys.second, input_event_t::mouse );
    if( !down ) {
        return event( pressed, false, now ) || event( released, false, now );
    }
    // Native preferences name both edges. In 3D they identify one physical
    // button; timing belongs to gestures. A pressed binding wins conflicts.
    return context().input_to_action( pressed ) != "ERROR" ?
           event( pressed, true, now ) : event( released, true, now );
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
    // Keyboard commands continue through the native dispatcher. Mouse buttons
    // are consumed by the compositor, so queue their rebound one-shot actions.
    const bool mouse_action = input.type == input_event_t::mouse &&
                              ( command == "FP3D_PANEL" || command == "FP3D_MAP" || command == "FP3D_ACTIONS" ||
                                command == "FP3D_RELOAD" || command == "FP3D_WAIT" || command == "FP3D_BINDINGS" ||
                                command.compare( 0, 10, "FP3D_SLOT_" ) == 0 );
    if( !mouse_action && command != "FP3D_INTERACT" && command != "FP3D_PICKUP" && command != "FP3D_ATTACK" &&
        command != "FP3D_AIM" && command != "FP3D_STATUS" && command != "FP3D_RUN" &&
        command != "FP3D_CROUCH" ) {
        return false;
    }
    if( buttons.count( key ) ) {
        return true;
    }
    const bool immediate = mouse_action || command == "FP3D_ATTACK" || command == "FP3D_AIM" ||
                           command == "FP3D_STATUS" || command == "FP3D_RUN" || command == "FP3D_CROUCH";
    buttons.emplace( key, held{ command, now, immediate } );
    if( command == "FP3D_STATUS" ) {
        expanded = true;
    } else if( immediate && pending.size() < 4 ) {
        pending.push_back( mouse_action || command == "FP3D_RUN" || command == "FP3D_CROUCH" ?
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
        const unsigned delay = button.command == "FP3D_ATTACK" ? 250 : hold_delay_ms;
        if( now - button.started < delay ) {
            continue;
        }
        if( button.command == "FP3D_ATTACK" ) {
            button.started = now;
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

void gestures::set_hold_delay( int milliseconds )
{
    const unsigned next = std::clamp( milliseconds, 100, 1000 );
    if( hold_delay_ms != next ) {
        cancel();
        hold_delay_ms = next;
    }
}

action_id display_bindings( input_context &original )
{
    uilist menu;
    menu.title = "操作与 UI 设置（F5：2D经典 → 2D现代 → 3D）";
    menu.text =
        "2D经典：原版界面与键位。2D现代：框架阶段，WASD 移动，其余暂沿用原版。\n"
        "3D：现有第一人称操作。修改现代键位请使用局部绑定（+），三套配置互不覆盖。";
    menu.addentry( 0, true, '1', "1. 2D经典模式：原版按键" );
    menu.addentry( 1, true, '2', "2. 2D现代模式：独立移动按键（框架）" );
    menu.addentry( 2, true, '3', "3. 3D模式：第一人称按键" );
    menu.addentry( 3, true, '4', "4. UI 栏样式（独立选择）" );
    menu.selected = static_cast<int>( fp3d_compositor::controls() );
    menu.query();
    if( menu.ret == 3 ) {
        uilist styles;
        styles.title = "UI 栏样式";
        styles.text = "只改变信息栏显示，不改变视角和操作键位。选择后自动保存。";
        styles.addentry( 0, true, '1', "跟随模式（2D 原版侧栏 / 3D HUD）" );
        styles.addentry( 1, true, '2', "原版侧栏（所有模式）" );
        styles.addentry( 2, true, '3', "3D HUD（所有模式）" );
        const auto current = get_option<std::string>( "FP3D_UI_STYLE" );
        styles.selected = current == "classic" ? 1 : current == "hud" ? 2 : 0;
        styles.query();
        if( styles.ret >= 0 && styles.ret <= 2 ) {
            get_options().get_option( "FP3D_UI_STYLE" ).setValue(
                std::string( styles.ret == 1 ? "classic" : styles.ret == 2 ? "hud" : "auto" ) );
            get_options().save();
            g->mark_main_ui_adaptor_resize();
        }
        return ACTION_NULL;
    }
    if( menu.ret == 0 ) {
        return original.display_menu( true );
    }
    if( menu.ret == 1 || menu.ret == 2 ) {
        restore_on_out_of_scope restore_editor( editing_modifiers );
        editing_modifiers = true;
        auto profile = menu.ret == 1 ? modern_2d_context() : context();
        profile.display_menu();
    }
    return ACTION_NULL;
}
}
#endif
