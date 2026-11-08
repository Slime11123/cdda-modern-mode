#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include "fp3d_camera.h"
#include "fp3d_compositor.h"
#include "fp3d_shaderpacks.h"
#include "cata_scope_helpers.h"
#include "avatar.h"
#include "game.h"
#include "map.h"
#include "vehicle.h"
#include "veh_type.h"
#include "input_context.h"
#include "input.h"
#include "translations.h"
#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "ui_manager.h"
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
bool is_mode_settings_key( const input_event &event )
{
    const int key = event.get_first_input();
    return ( event.type == input_event_t::keyboard_code &&
             ( key == static_cast<int>( keycode::f5 ) || key == static_cast<int>( keycode::f6 ) ) ) ||
           ( event.type == input_event_t::keyboard_char && ( key == KEY_F( 5 ) || key == KEY_F( 6 ) ) );
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
    result.register_action( "MODERN_2D_WAIT", to_translation( "2D现代 等待一回合" ) );
    result.register_action( "MODERN_2D_RUN", to_translation( "2D现代 切换奔跑 / 行走" ) );
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
             std::pair{ "FP3D_AIM", "3D 右键准星即时射击" },
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
        if( command == "MODERN_2D_WAIT" ) {
            return action_ident( ACTION_PAUSE );
        }
        if( command == "MODERN_2D_RUN" ) {
            return action_ident( ACTION_TOGGLE_RUN );
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

namespace
{
class mode_settings : public cataimgui::window
{
        int &view;
        int &keys;
        int &style;
        int column = 0;
        int result = -1;
        bool shader_page = false;
        std::vector<fp3d_shaderpacks::entry> shader_entries;
        cataimgui::bounds get_bounds() override {
            const auto size = ImGui::GetIO().DisplaySize;
            const float width = std::min( size.x * .9f, 820.0f );
            const float height = std::min( size.y * .85f, 480.0f );
            return { ( size.x - width ) / 2, ( size.y - height ) / 2, width, height };
        }
        void draw_shader_page() {
            ImGui::TextUnformatted( "光影包" );
            ImGui::SameLine();
            ImGui::TextDisabled( "Minecraft 风格列表 / 只作用于 3D 世界" );
            ImGui::Separator();

            const float total_width = ImGui::GetContentRegionAvail().x;
            const float content_height = std::max( 185.0f, ImGui::GetContentRegionAvail().y - 82.0f );
            ImGui::BeginChild( "fp3d_shaderpack_list", ImVec2( total_width * .54f, content_height ), true );
            ImGui::TextDisabled( "选择光影包" );
            ImGui::Separator();
            for( const auto &pack : shader_entries ) {
                ImGui::PushID( pack.id.c_str() );
                const bool chosen = pack.id == fp3d_shaderpacks::selected();
                if( ImGui::Selectable( pack.title.c_str(), chosen ) ) {
                    fp3d_shaderpacks::choose( pack.id );
                }
                if( !pack.compatible ) {
                    ImGui::SameLine();
                    ImGui::TextDisabled( "不兼容" );
                } else if( chosen ) {
                    ImGui::SameLine();
                    ImGui::TextDisabled( "已选择" );
                }
                ImGui::TextDisabled( "%s", pack.note.c_str() );
                ImGui::Separator();
                ImGui::PopID();
            }
            ImGui::EndChild();

            ImGui::SameLine();
            ImGui::BeginChild( "fp3d_shaderpack_details", ImVec2( 0, content_height ), true );
            ImGui::TextUnformatted( "光影包信息" );
            ImGui::Separator();
            const auto &active_id = fp3d_shaderpacks::selected();
            const auto it = std::find_if( shader_entries.begin(), shader_entries.end(),
            [&]( const fp3d_shaderpacks::entry & pack ) {
                return pack.id == active_id;
            } );
            if( it != shader_entries.end() ) {
                ImGui::TextWrapped( "%s", it->title.c_str() );
                ImGui::TextDisabled( "%s", it->note.c_str() );
            } else {
                ImGui::TextWrapped( "上次选择：%s", active_id.c_str() );
                ImGui::TextDisabled( "文件已移动？请刷新列表并重新选择。" );
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextUnformatted( "光影质量" );
            const char *qualities[] = { "低", "中", "高", "极高" };
            for( int level = 0; level < 4; ++level ) {
                if( level > 0 ) { ImGui::SameLine(); }
                if( ImGui::RadioButton( qualities[level], fp3d_shaderpacks::quality() == level ) ) {
                    fp3d_shaderpacks::set_quality( level );
                }
            }
            ImGui::Spacing();
            int mc_strength = fp3d_shaderpacks::mc_intensity_percent();
            if( ImGui::SliderInt( "MC 视觉增强 (%)", &mc_strength, 50, 200 ) ) {
                fp3d_shaderpacks::set_mc_intensity_percent( mc_strength );
            }
            ImGui::TextWrapped( "仅影响 Photon 等 MC 视觉适配包，不影响原版 2D 或直接执行的 final.fsh。" );
            ImGui::Spacing();
            bool experimental_shadows = fp3d_shaderpacks::shadows_enabled();
            if( ImGui::Checkbox( "实验性近场太阳阴影", &experimental_shadows ) ) {
                fp3d_shaderpacks::set_shadows_enabled( experimental_shadows );
            }
            ImGui::TextWrapped( "内置光影及 MC 适配包在极高画质时可启用近场阴影；如投影异常可单独关闭。" );
            bool ssr = fp3d_shaderpacks::wet_ssr_enabled();
            if( ImGui::Checkbox( "水面与雨天湿地 SSR 反射", &ssr ) ) {
                fp3d_shaderpacks::set_wet_ssr_enabled( ssr );
            }
            bool volume = fp3d_shaderpacks::shadow_volume_enabled();
            if( ImGui::Checkbox( "阴影遮挡体积太阳光", &volume ) ) {
                fp3d_shaderpacks::set_shadow_volume_enabled( volume );
            }
            ImGui::TextWrapped( "上述两项为 MC 视觉桥的极高档实验特性。SSR 仅渲染原版已可见的水面与下雨的道路，具有屏幕外反射缺失的限制。不影响 2D、原生光影和直接 final.fsh。" );
            ImGui::TextWrapped( "Minecraft 光影：简单 GLSL 1.20 final.fsh 可直接运行；Photon 等复杂光影使用 CDDA 视觉适配，暂不支持完整 Iris 多阶段效果。" );
            ImGui::EndChild();

            ImGui::Separator();
            if( ImGui::Button( "打开光影文件夹" ) ) {
                fp3d_shaderpacks::open_folder();
            }
            ImGui::SameLine();
            if( ImGui::Button( "复制路径" ) ) {
                ImGui::SetClipboardText( fp3d_shaderpacks::directory().c_str() );
            }
            ImGui::SameLine();
            if( ImGui::Button( "重新扫描/重载" ) ) {
                shader_entries = fp3d_shaderpacks::scan();
                fp3d_shaderpacks::request_reload();
            }
            ImGui::SameLine();
            if( ImGui::Button( "完成" ) ) {
                shader_page = false;
            }
            const auto &status = fp3d_shaderpacks::status();
            ImGui::TextDisabled( "%s", status.empty() ?
                                 "光影包请放在 EXE 同目录的 shaderpacks 文件夹中。" :
                                 status.c_str() );
        }
        void draw_controls() override {
            if( shader_page ) {
                draw_shader_page();
                return;
            }
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = ImGui::GetContentRegionAvail().y;
            const float font = ImGui::GetFontSize();
            const float top = ImGui::GetCursorPosY();
            const char *titles[] = { "画面", "键位", "UI" };
            const char *choices[][2] = { { "2D", "3D" }, { "经典", "现代" }, { "原版", "现代" } };
            int *values[] = { &view, &keys, &style };
            for( int col = 0; col < 3; ++col ) {
                ImGui::PushID( col );
                const float left = font + width * col / 3;
                ImGui::SetCursorPos( ImVec2( left, top + font ) );
                ImGui::Text( "%s%s", titles[col], column == col ? " <" : "" );
                for( int row = 0; row < 2; ++row ) {
                    ImGui::SetCursorPos( ImVec2( left, top + height * ( row == 0 ? .28f : .6f ) ) );
                    if( ImGui::RadioButton( choices[col][row], values[col], row ) ) {
                        column = col;
                    }
                }
                ImGui::PopID();
            }
            ImGui::SetCursorPos( ImVec2( font, top + height - font * 4.5f ) );
            if( ImGui::Button( "光影包..." ) ) {
                shader_entries = fp3d_shaderpacks::scan();
                shader_page = true;
            }
            ImGui::SameLine();
            // Also accessible without navigating into the shaderpack submenu.
            if( ImGui::Button( "打开光影包文件夹" ) ) {
                fp3d_shaderpacks::open_folder();
            }
            ImGui::SameLine();
            if( ImGui::Button( "编辑按键…" ) ) {
                result = 2;
                is_open = false;
            }
            ImGui::SetCursorPos( ImVec2( width - font * 6, top + height - font * 4.5f ) );
            if( ImGui::Button( "完成", ImVec2( font * 5, font * 2 ) ) ) {
                result = 1;
                is_open = false;
            }
            ImGui::SetCursorPos( ImVec2( font, top + height - font * 1.5f ) );
            ImGui::TextDisabled( "左右选列 / 上下选择 / Enter 完成 / Esc 取消" );
        }
    public:
        mode_settings( int &v, int &k, int &s ) : cataimgui::window( "模式设置",
                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar ),
            view( v ), keys( k ), style( s ) {}
        int run() {
            input_context input( "FP3D_MODE_SETTINGS", keyboard_mode::keycode );
            for( const char *action : { "QUIT", "CONFIRM", "UP", "DOWN", "LEFT", "RIGHT",
                                       "SELECT", "MOUSE_MOVE", "ANY_INPUT" } ) {
                input.register_action( action );
            }
            while( is_open ) {
                ui_manager::redraw();
                if( !is_open ) {
                    break;
                }
                const auto action = input.handle_input( 16 );
                if( action == "QUIT" ) {
                    if( shader_page ) { shader_page = false; }
                    else { is_open = false; }
                } else if( shader_page ) {
                    if( action == "CONFIRM" ) { shader_page = false; }
                } else if( action == "CONFIRM" ) {
                    result = 1;
                    is_open = false;
                } else if( action == "LEFT" || action == "RIGHT" ) {
                    column = ( column + ( action == "LEFT" ? 2 : 1 ) ) % 3;
                } else if( action == "UP" || action == "DOWN" ) {
                    int *values[] = { &view, &keys, &style };
                    *values[column] = action == "UP" ? 0 : 1;
                }
            }
            return result;
        }
};
}

std::unique_ptr<cataimgui::window> create_mode_settings( int &view, int &keys, int &style )
{
    return std::make_unique<mode_settings>( view, keys, style );
}

action_id display_bindings( input_context &original )
{
    int view = fp3d_compositor::view_3d() ? 1 : 0;
    int keys = fp3d_compositor::controls() == mode::original_2d ? 0 : 1;
    int style = fp3d_compositor::uses_hud() ? 1 : 0;
    while( true ) {
        int result;
        {
            mode_settings settings( view, keys, style );
            result = settings.run();
        }
        if( result == 1 ) {
            get_options().get_option( "FP3D_VIEW" ).setValue( std::string( view ? "3d" : "2d" ) );
            get_options().get_option( "FP3D_CONTROLS" ).setValue( std::string( keys ? "modern" : "classic" ) );
            get_options().get_option( "FP3D_UI_STYLE" ).setValue( std::string( style ? "hud" : "classic" ) );
            get_options().save();
            fp3d_compositor::apply_modes( view != 0, keys != 0 );
        }
        if( result != 2 ) {
            return ACTION_NULL;
        }
        if( keys == 0 ) {
            original.display_menu();
        } else {
            restore_on_out_of_scope restore_editor( editing_modifiers );
            editing_modifiers = true;
            auto profile = view ? context() : modern_2d_context();
            profile.display_menu();
        }
    }
}
}
#endif
