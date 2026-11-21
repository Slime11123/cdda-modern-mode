#include "fp3d_input.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include "fp3d_camera.h"
#include "fp3d_compositor.h"
#include "fp3d_hud.h"
#include "fp3d_shaderpacks.h"
#include "fp3d_simple_ui.h"
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
        enum result_code { cancel = -1, apply = 1, edit_classic, edit_modern_2d, edit_modern_3d,
                           graphics_options };
        static constexpr std::array<const char *, 3> mode_options = {
            "FP3D_VIEW", "FP3D_CONTROLS", "FP3D_UI_STYLE"
        };
        int page = 0;
        int result = cancel;
        bool camera_overhead = fp3d_compositor::preferred_overhead();
        bool focus_window = true;
        std::array<bool, 3> use_defaults{};
        std::vector<fp3d_shaderpacks::entry> shader_entries;
        std::string scan_message;

        cataimgui::bounds get_bounds() override {
            return { -1, -1, .85f, .85f };
        }

        void finish( result_code value ) {
            result = value;
            is_open = false;
        }

        void scan_shaders() {
            shader_entries = fp3d_shaderpacks::scan( &scan_message );
            if( scan_message.empty() ) {
                scan_message = "光影包列表已刷新";
            }
        }

        void draw_shader_page() {
            if( shader_entries.empty() ) {
                scan_shaders();
                if( scan_message == "光影包列表已刷新" ) {
                    scan_message.clear();
                }
            }
            ImGui::TextWrapped( "光影只作用于 3D 世界。更改自动保存。" );
            ImGui::Spacing();
            ImGui::TextUnformatted( "光影包" );
            const std::string active_id = fp3d_shaderpacks::selected();
            const auto active_pack = std::find_if( shader_entries.begin(), shader_entries.end(),
            [&]( const fp3d_shaderpacks::entry &pack ) {
                return pack.id == active_id;
            } );
            const float refresh_size = ImGui::GetFrameHeight();
            ImGui::SetNextItemWidth( ImGui::GetContentRegionAvail().x - refresh_size -
                                     ImGui::GetStyle().ItemSpacing.x );
            if( ImGui::BeginCombo( "##shaderpack", active_pack == shader_entries.end() ?
                                   active_id.c_str() : active_pack->title.c_str() ) ) {
                for( const auto &pack : shader_entries ) {
                    ImGui::PushID( pack.id.c_str() );
                    const bool selected = pack.id == active_id;
                    if( ImGui::Selectable( pack.title.c_str(), selected,
                                           pack.compatible ? ImGuiSelectableFlags_None : ImGuiSelectableFlags_Disabled ) ) {
                        fp3d_shaderpacks::choose( pack.id );
                    }
                    ImGui::SetItemTooltip( "%s", pack.note.c_str() );
                    if( selected ) {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            const bool refresh = ImGui::Button( "##refresh_shader_list", ImVec2( refresh_size, refresh_size ) );
            ImGui::SetItemTooltip( "刷新光影包列表" );
            const ImVec2 corner = ImGui::GetItemRectMin();
            const ImVec2 center( corner.x + refresh_size / 2, corner.y + refresh_size / 2 );
            const float radius = refresh_size * .23f;
            ImDrawList *draw = ImGui::GetWindowDrawList();
            const ImU32 gold = ImGui::GetColorU32( ImGuiCol_CheckMark );
            draw->PathArcTo( center, radius, .15f, 5.25f, 22 );
            draw->PathStroke( gold, ImDrawFlags_None, std::max( 1.f, refresh_size * .065f ) );
            const ImVec2 tip( center.x + radius * .68f, center.y - radius * .7f );
            draw->AddTriangleFilled( tip, ImVec2( tip.x - radius * .82f, tip.y ),
                                     ImVec2( tip.x, tip.y - radius * .82f ), gold );
            if( active_pack != shader_entries.end() ) {
                ImGui::TextWrapped( "%s", active_pack->note.c_str() );
            } else {
                ImGui::TextWrapped( "找不到当前光影包。刷新列表或选择其他光影包。" );
            }
            if( refresh ) {
                scan_shaders();
            }
            if( !scan_message.empty() ) {
                ImGui::TextWrapped( "%s", scan_message.c_str() );
            }
            ImGui::Spacing();
            ImGui::TextUnformatted( "光影质量" );
            const char *qualities[] = { "低", "中", "高", "极高" };
            int quality = fp3d_shaderpacks::quality();
            ImGui::SetNextItemWidth( -1 );
            if( ImGui::Combo( "##shader_quality", &quality, qualities, 4 ) ) {
                fp3d_shaderpacks::set_quality( quality );
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
            ImGui::TextWrapped( "内置光影及 MC 适配包支持近场太阳阴影；如投影异常可单独关闭。" );
            bool ssr = fp3d_shaderpacks::wet_ssr_enabled();
            if( ImGui::Checkbox( "水面与雨天湿地 SSR 反射", &ssr ) ) {
                fp3d_shaderpacks::set_wet_ssr_enabled( ssr );
            }
            ImGui::TextWrapped( "Photon 水面在中档及以上读取本地包内水波与水色参数，替换原版水纹；极高档再启用场景倒影。关闭 SSR 仍保留水波与水色。水面低于岸边约 18 厘米。" );
            bool volume = fp3d_shaderpacks::shadow_volume_enabled();
            if( ImGui::Checkbox( "阴影遮挡体积太阳光", &volume ) ) {
                fp3d_shaderpacks::set_shadow_volume_enabled( volume );
            }
            ImGui::TextWrapped( "上述两项为 MC 视觉桥的极高档实验特性。SSR 仅渲染原版已可见的水面与下雨的道路；水面倒影较清晰，粗糙湿路较柔和，屏幕边缘渐隐。屏幕外信息缺失时保留湿润光泽。不影响 2D、原生光影和直接 final.fsh。" );
            bool experimental_pbr = fp3d_shaderpacks::pbr_enabled();
            if( ImGui::Checkbox( "实验性 PBR 法线与材质高光（Ultra）", &experimental_pbr ) ) {
                fp3d_shaderpacks::set_pbr_enabled( experimental_pbr );
            }
            int pbr_strength = fp3d_shaderpacks::pbr_strength_percent();
            if( ImGui::SliderInt( "PBR 细节强度 (%)", &pbr_strength, 0, 200 ) ) {
                fp3d_shaderpacks::set_pbr_strength_percent( pbr_strength );
            }
            ImGui::TextWrapped( "PBR 默认关闭，仅在极高画质及受支持的内置/MC 视觉桥中启用。\n当前只覆盖普通墙面、地面；有额外显存与 GPU 负载。若画面或帧数异常可直接关闭。" );
            bool local_lights = fp3d_shaderpacks::local_lights_enabled();
            if( ImGui::Checkbox( "动态灯光（电网 / 手电筒 / 手机）", &local_lights ) ) {
                fp3d_shaderpacks::set_local_lights_enabled( local_lights );
            }
            ImGui::TextWrapped( "灯具必须在原版中确实通电、开启；便携设备必须正在发光。近距离色彩与光晕在 Photon/内置光影生效，最多 8 个邻近光源。" );
            bool local_shadows = fp3d_shaderpacks::local_shadows_enabled();
            if( ImGui::Checkbox( "局部光源阴影（手持光束 / 重点灯具）", &local_shadows ) ) {
                fp3d_shaderpacks::set_local_shadows_enabled( local_shadows );
            }
            ImGui::TextWrapped( "最多同时为一个手持光束和一盏重要固定灯生成阴影，使用已知墙体、门、楼板及车辆遮挡。其余灯保持原有照明；关闭此项可保留灯光和太阳阴影。" );
            ImGui::TextWrapped( "Minecraft 光影包需要 SDL3 GPU 适配。Photon 使用本游戏的原生光影实现。" );
            ImGui::Separator();
            if( ImGui::Button( "打开光影包文件夹" ) ) {
                fp3d_shaderpacks::open_folder();
            }
            ImGui::SameLine();
            if( ImGui::Button( "复制路径" ) ) {
                ImGui::SetClipboardText( fp3d_shaderpacks::directory().c_str() );
            }
            ImGui::SameLine();
            if( ImGui::Button( "重载当前光影" ) ) {
                fp3d_shaderpacks::request_reload();
            }
            const auto status = fp3d_shaderpacks::status();
            if( !status.empty() && status != scan_message ) {
                ImGui::TextWrapped( "%s", status.c_str() );
            }
        }

        void draw_hud_page() {
            ImGui::TextUnformatted( "现代 UI 大小" );
            ImGui::TextWrapped( "%s", "各面板独立缩放（含文字和图标），每档 25%，范围 25%～200%。修改自动保存，重新打开游戏后仍然生效。" );
            ImGui::Separator();
            for( const auto &panel : fp3d_hud::panel_settings ) {
                ImGui::PushID( panel.option );
                int step = get_option<int>( panel.option ) / 25;
                const std::string format = std::to_string( step * 25 ) + "%%";
                ImGui::TextUnformatted( panel.label );
                ImGui::SetNextItemWidth( -1 );
                if( ImGui::SliderInt( "##hud_size", &step, 1, 8, format.c_str(),
                                     ImGuiSliderFlags_NoInput | ImGuiSliderFlags_AlwaysClamp ) ) {
                    get_options().get_option( panel.option ).setValue( std::to_string( step * 25 ) );
                    get_options().save();
                }
                ImGui::PopID();
            }
            if( ImGui::Button( "恢复默认大小" ) ) {
                for( const auto &panel : fp3d_hud::panel_settings ) {
                    auto &option = get_options().get_option( panel.option );
                    option.setValue( option.getDefaultValue() );
                }
                get_options().save();
            }
        }

        bool selection_button( const char *label, bool selected, const ImVec2 &size ) {
            if( selected ) {
                const ImVec4 amber = ImGui::GetStyleColorVec4( ImGuiCol_CheckMark );
                ImGui::PushStyleColor( ImGuiCol_Border, amber );
                ImGui::PushStyleColor( ImGuiCol_Text, amber );
                ImGui::PushStyleColor( ImGuiCol_Button, ImGui::GetStyleColorVec4( ImGuiCol_Header ) );
            }
            const bool pressed = ImGui::Button( label, size );
            if( selected ) {
                ImGui::PopStyleColor( 3 );
            }
            return pressed;
        }

        void draw_mode_choice( int field, const char *title, const char *classic,
                               const char *modern, int &value ) {
            ImGui::PushID( field );
            ImGui::TextUnformatted( title );
            const float width = ( ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x ) / 2;
            const ImVec2 size( width, ImGui::GetFrameHeight() * 1.2f );
            if( selection_button( classic, value == 0, size ) ) {
                value = 0;
                use_defaults[field] = false;
            }
            ImGui::SameLine();
            if( selection_button( modern, value == 1, size ) ) {
                value = 1;
                use_defaults[field] = false;
            }
            ImGui::Spacing();
            ImGui::PopID();
        }

        void draw_view_page() {
            draw_mode_choice( 0, "渲染模式", "2D", "3D", view );
            if( use_defaults[1] && get_options().get_option( mode_options[1] ).getDefaultValue() == "auto" ) {
                keys = fp3d_compositor::default_modern_controls( view != 0 );
            }
            if( use_defaults[2] && get_options().get_option( mode_options[2] ).getDefaultValue() == "auto" ) {
                style = view;
            }
            draw_mode_choice( 1, "键位模式", "经典键位", "现代键位", keys );
            draw_mode_choice( 2, "UI 模式", "经典 UI", "现代 UI", style );
            if( view ) {
                ImGui::Separator();
                fp3d_simple_ui::accent_text( "3D 专属设置" );
                ImGui::TextUnformatted( "视角" );
                int camera = camera_overhead ? 1 : 0;
                ImGui::SetNextItemWidth( -1 );
                if( ImGui::Combo( "##camera", &camera, "第一人称\0上帝视角\0" ) ) {
                    camera_overhead = camera != 0;
                }
                ImGui::TextWrapped( "游戏内按 F6 切换第一人称与上帝视角。" );
            }
        }

        void draw_controls() override {
            constexpr const char *pages[] = { "画面模式", "操作键位", "界面风格", "光影设置", "性能设置" };
            const ImVec4 gold = ImGui::GetStyleColorVec4( ImGuiCol_CheckMark );
            ImGui::PushStyleVar( ImGuiStyleVar_FrameBorderSize, 1.f );
            fp3d_simple_ui::accent_text( "模式设置" );
            ImGui::Separator();
            const float footer = 2 * ImGui::GetFrameHeightWithSpacing() +
                                 ImGui::GetStyle().ItemSpacing.y;
            const float nav_width = ImGui::GetContentRegionAvail().x * .24f;
            const float icon_size = ImGui::GetFontSize() * .65f;
            if( ImGui::BeginChild( "##mode_navigation", ImVec2( nav_width, -footer ),
                                   ImGuiChildFlags_AlwaysUseWindowPadding | ImGuiChildFlags_NavFlattened,
                                   ImGuiWindowFlags_NoScrollbar ) ) {
                ImGui::PushStyleVar( ImGuiStyleVar_ButtonTextAlign, ImVec2( 0, .5f ) );
                for( int i = 0; i < 5; ++i ) {
                    const bool selected = page == i;
                    const std::string label = "     " + std::string( pages[i] );
                    if( selection_button( label.c_str(), selected,
                                          ImVec2( -1, ImGui::GetFrameHeight() * 1.35f ) ) ) {
                        page = i;
                    }
                    if( selected ) {
                        ImGui::SetItemDefaultFocus();
                    }
                    const ImVec2 min = ImGui::GetItemRectMin();
                    const ImVec2 max = ImGui::GetItemRectMax();
                    const ImVec2 center( min.x + ImGui::GetStyle().FramePadding.x + icon_size * .65f,
                                         ( min.y + max.y ) / 2 );
                    ImGui::GetWindowDrawList()->AddNgon( center, icon_size * .6f,
                            ImGui::GetColorU32( selected ? ImGuiCol_CheckMark : ImGuiCol_TextDisabled ), 4 + i );
                }
                ImGui::PopStyleVar();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::PushID( page );
            ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( .095f, .092f, .080f, .97f ) );
            if( ImGui::BeginChild( "##mode_content", ImVec2( 0, -footer ),
                                   ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding |
                                   ImGuiChildFlags_NavFlattened ) ) {
                fp3d_simple_ui::accent_text( pages[page] );
                ImGui::Spacing();
                switch( page ) {
                    case 0:
                        draw_view_page();
                        break;
                    case 1:
                        ImGui::TextWrapped( "当前选择：%s", keys ? "现代键位" : "经典键位" );
                        ImGui::TextWrapped( "可独立编辑三套绑定，修改按键时由原有编辑器保存。" );
                        if( ImGui::Button( "编辑经典原版键位", ImVec2( -1, 0 ) ) ) {
                            finish( edit_classic );
                        }
                        if( ImGui::Button( "编辑 2D 现代键位", ImVec2( -1, 0 ) ) ) {
                            finish( edit_modern_2d );
                        }
                        if( ImGui::Button( "编辑 3D 现代键位", ImVec2( -1, 0 ) ) ) {
                            finish( edit_modern_3d );
                        }
                        break;
                    case 2:
                        ImGui::TextWrapped( "当前选择：%s", style ? "现代 UI" : "经典 UI" );
                        draw_hud_page();
                        break;
                    case 3:
                        draw_shader_page();
                        break;
                    case 4:
                        ImGui::TextWrapped( "原版图形设置可调整缩放、窗口和图块。当前 SDL3 GPU 后端暂未提供独立的帧率限制、垂直同步或渲染距离选项。" );
                        if( ImGui::Button( "打开原版设置" ) ) {
                            finish( graphics_options );
                        }
                        break;
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopID();
            ImGui::Separator();
            ImGui::TextUnformatted( "基础模式与视角需应用。键位绑定、HUD 大小与光影设置自动保存。" );
            if( ImGui::Button( "恢复基础模式默认" ) ) {
                use_defaults.fill( true );
                const auto value = []( int index ) {
                    return get_options().get_option( mode_options[index] ).getDefaultValue();
                };
                const auto default_view = value( 0 );
                view = default_view == "auto" ? fp3d_compositor::default_view_3d() : default_view == "3d";
                const auto default_keys = value( 1 );
                keys = default_keys == "auto" ? fp3d_compositor::default_modern_controls( view != 0 ) :
                       default_keys == "modern";
                const auto default_style = value( 2 );
                style = default_style == "auto" ? view : default_style == "hud";
            }
            const float actions_width = ImGui::CalcTextSize( "取消" ).x +
                                        ImGui::CalcTextSize( "应用设置" ).x +
                                        4 * ImGui::GetStyle().FramePadding.x + ImGui::GetStyle().ItemSpacing.x;
            ImGui::SameLine( ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - actions_width );
            if( ImGui::Button( "取消" ) ) {
                finish( cancel );
            }
            ImGui::SameLine();
            ImGui::PushStyleColor( ImGuiCol_Button, gold );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( .98f, .77f, .43f, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( .82f, .60f, .28f, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( .10f, .075f, .035f, 1 ) );
            if( ImGui::Button( "应用设置" ) ) {
                finish( apply );
            }
            ImGui::PopStyleColor( 4 );
            ImGui::PopStyleVar();
        }

    public:
        mode_settings( int &v, int &k, int &s ) : cataimgui::window( "模式设置",
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar ),
            view( v ), keys( k ), style( s ) {
            // Let ImGui raise the settings window on focus and keep its combo popups above it.
            force_to_back = true;
            window_flags &= ~ImGuiWindowFlags_NoBringToFrontOnFocus;
        }

        void draw() override {
            if( is_open ) {
                ImGui::SetNextWindowBgAlpha( .95f );
                if( focus_window ) {
                    ImGui::SetNextWindowFocus();
                    focus_window = false;
                }
                cataimgui::window::draw();
            }
        }

        void save() {
            const char *values[] = { view ? "3d" : "2d", keys ? "modern" : "classic",
                                     style ? "hud" : "classic" };
            for( size_t i = 0; i < mode_options.size(); ++i ) {
                auto &option = get_options().get_option( mode_options[i] );
                option.setValue( use_defaults[i] ? option.getDefaultValue() : values[i] );
            }
            get_options().save();
            fp3d_compositor::apply_modes( view != 0, keys != 0 );
            if( view && fp3d_compositor::preferred_overhead() != camera_overhead ) {
                fp3d_compositor::toggle_camera();
            }
        }

        void run( input_context &original ) {
            input_context input( "FP3D_MODE_SETTINGS", keyboard_mode::keycode );
            input.register_action( "QUIT" );
            input.register_action( "CONFIRM" );
            do {
                is_open = true;
                focus_window = true;
                result = cancel;
                fp3d_compositor::set_input_context( "FP3D_MODE_SETTINGS" );
                while( is_open ) {
                    ui_manager::redraw();
                    if( !is_open ) {
                        break;
                    }
                    const bool combo_open = ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId );
                    const auto action = input.handle_input( 16 );
                    if( action == "QUIT" && !combo_open ) {
                        finish( cancel );
                    } else if( action == "CONFIRM" && !combo_open && !ImGui::GetIO().NavActive ) {
                        finish( apply );
                    }
                }
                if( result == apply ) {
                    save();
                    return;
                }
                if( result == cancel ) {
                    return;
                }
                if( result == graphics_options ) {
                    get_options().show( true );
                } else if( result == edit_classic ) {
                    original.display_menu();
                } else {
                    restore_on_out_of_scope restore_editor( editing_modifiers );
                    editing_modifiers = true;
                    auto profile = result == edit_modern_3d ? context() : modern_2d_context();
                    profile.display_menu();
                }
            } while( true );
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
    mode_settings settings( view, keys, style );
    settings.run( original );
    return ACTION_NULL;
}
}
#endif
