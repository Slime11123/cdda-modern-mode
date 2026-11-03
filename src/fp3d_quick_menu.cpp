#include "fp3d_quick_menu.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
#include <vector>
#include "cata_imgui.h"
#include "cata_scope_helpers.h"
#include "fp3d_compositor.h"
#include "fp3d_input.h"
#include "fp3d_modern_ui.h"
#include "game.h"
#include "imgui/imgui.h"
#include "input_context.h"
#include "sdltiles.h"
#include "ui_manager.h"

namespace fp3d_quick_menu
{
namespace
{
constexpr float pi = 3.14159265358979323846f;
const std::array<entry, 8> default_options = {{
        { "投掷", "选择物品与目标，按原版规则投掷。", ACTION_THROW, nullptr },
        { "拖拽", "选择相邻家具或车辆，按原版规则拖拽。", ACTION_GRAB, nullptr },
        { "变异能力", "打开原版变异能力与特殊行动。", ACTION_MUTATIONS, nullptr },
        { "仿生装置", "打开原版仿生装置控制。", ACTION_BIONICS, nullptr },
        { "装填", "装填手持武器，沿用原版弹药与耗时。", ACTION_RELOAD_WIELDED, nullptr },
        { "附近物品", "查看当前可见且可触及的物品与容器。", ACTION_NULL, "FP3D_PICKUP_LONG" },
        { "生存面板", "装备、背包、健康、制作与地图。", ACTION_NULL, "FP3D_PANEL" },
        { "更多行动", "打开完整原版行动列表。", ACTION_ACTIONMENU, nullptr }
    }
};

class wheel : public cataimgui::window
{
        SDL_Texture_Ptr background;
        float scale;
        std::vector<entry> options;
        std::string heading;
        int selected = -1;
        std::optional<int> chosen;

        void choose( int index ) {
            if( index < 0 || index >= static_cast<int>( options.size() ) || !options[index].enabled ) {
                return;
            }
            if( options[index].action == ACTION_NULL && !options[index].command ) {
                is_open = false;
                return;
            }
            chosen = index;
            button_action = options[index].command ? options[index].command :
                            action_ident( options[index].action );
            is_open = false;
        }
        void move_selection( int direction ) {
            if( options.empty() ) {
                return;
            }
            const int count = options.size();
            int next = selected < 0 ? ( direction > 0 ? -1 : 0 ) : selected;
            for( int step = 0; step < count; ++step ) {
                next = ( next + direction + count ) % count;
                if( options[next].enabled ) {
                    selected = next;
                    return;
                }
            }
        }
        cataimgui::bounds get_bounds() override {
            const auto size = ImGui::GetIO().DisplaySize;
            return { 0, 0, size.x, size.y };
        }
        void draw_controls() override {
            ImGui::SetWindowFontScale( scale );
            const auto size = ImGui::GetIO().DisplaySize;
            auto viewport = fp3d_compositor::viewport_pixels();
            if( viewport[2] <= 0 || viewport[3] <= 0 ) {
                viewport = { 0, 0, static_cast<int>( size.x ), static_cast<int>( size.y ) };
            }
            const ImVec2 center( viewport[0] + viewport[2] * .5f, viewport[1] + viewport[3] * .5f );
            const float font = ImGui::GetFontSize();
            const float outer = std::min( std::min( viewport[2], viewport[3] ) * .38f, font * 11.5f );
            const float inner = outer * .36f;
            const auto mouse = ImGui::GetMousePos();
            const int hovered = sector_at( mouse.x - center.x, mouse.y - center.y, inner, outer,
                                           options.size() );
            const auto delta = ImGui::GetIO().MouseDelta;
            if( delta.x != 0 || delta.y != 0 || selected < 0 ) {
                selected = hovered;
            }
            auto *draw = ImGui::GetWindowDrawList();
            const auto text = [&]( const char *value, float x, float y, ImU32 color ) {
                const auto extent = ImGui::CalcTextSize( value );
                draw->AddText( ImVec2( x - extent.x / 2, y - extent.y / 2 ), color, value );
            };
            const ImU32 white = IM_COL32( 255, 255, 255, 255 );
            const ImU32 gold = IM_COL32( 255, 220, 80, 255 );
            const float step = options.empty() ? 0 : 2 * pi / options.size();
            for( int index = 0; index < static_cast<int>( options.size() ); ++index ) {
                const float angle = -pi / 2 + index * step;
                const float first = angle - step / 2 + .014f;
                const float last = angle + step / 2 - .014f;
                const auto path = [&]() {
                    draw->PathArcTo( center, outer, first, last, 16 );
                    draw->PathArcTo( center, inner, last, first, 8 );
                };
                const bool active = index == selected && options[index].enabled;
                path();
                draw->PathFillConcave( active ? IM_COL32( 88, 78, 18, 225 ) :
                                       IM_COL32( 22, 21, 19, 205 ) );
                path();
                draw->PathStroke( active ? gold : IM_COL32( 220, 220, 220, 160 ),
                                  ImDrawFlags_Closed, active ? 2 : 1 );
                const float radius = ( inner + outer ) / 2;
                text( options[index].label, center.x + std::cos( angle ) * radius,
                      center.y + std::sin( angle ) * radius, !options[index].enabled ?
                      gold : active ? gold : white );
            }
            draw->AddCircleFilled( center, inner - 3, IM_COL32( 16, 15, 14, 205 ), 48 );
            text( heading.c_str(), center.x, center.y - font * .6f, white );
            text( "Esc 返回", center.x, center.y + font * .9f, white );
            if( selected >= 0 ) {
                text( options[selected].description, center.x, center.y + outer + font, white );
            }
            text( "点击执行 / 方向键选择 / Enter 确认", center.x, center.y + outer + font * 2.3f,
                  white );
            if( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) ) {
                is_open = false;
            } else if( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) ) {
                if( hovered >= 0 ) {
                    choose( hovered );
                } else {
                    is_open = false;
                }
            }
        }
    public:
        wheel( const std::array<entry, 8> &choices, const std::string &caption,
               int percent ) : cataimgui::window( "快捷行动轮盘",
                           ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
                           ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ),
            background( fp3d_compositor::copy_view() ), scale( std::clamp( percent, 100, 150 ) / 100.0f ),
            heading( caption ) {
            for( const auto &choice : choices ) {
                if( choice.visible ) {
                    options.push_back( choice );
                }
            }
        }
        void draw() override {
            if( !is_open ) {
                return;
            }
            auto *draw = ImGui::GetBackgroundDrawList();
            const auto size = ImGui::GetIO().DisplaySize;
            const auto viewport = fp3d_compositor::viewport_pixels();
            if( background && fp3d_compositor::controls() == fp3d_input::mode::modern_2d ) {
                draw->AddImage( ( ImTextureID )( intptr_t )background.get(), ImVec2( 0, 0 ), size );
            } else if( background && viewport[2] > 0 && viewport[3] > 0 ) {
                draw->AddImage( ( ImTextureID )( intptr_t )background.get(),
                                ImVec2( viewport[0], viewport[1] ),
                                ImVec2( viewport[0] + viewport[2], viewport[1] + viewport[3] ),
                                ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
            }
            draw->AddRectFilled( ImVec2( 0, 0 ), size, IM_COL32( 0, 0, 0, 80 ) );
            cataimgui::window::draw();
        }
        std::optional<entry> run() {
            input_context context( "FP3D_WHEEL", keyboard_mode::keycode );
            for( const char *action : {
                     "QUIT", "CONFIRM", "UP", "DOWN", "LEFT", "RIGHT",
                     "SELECT", "MOUSE_MOVE", "ANY_INPUT"
                 } ) {
                context.register_action( action );
            }
            while( is_open && fp3d_compositor::survival_menu_available() ) {
                ui_manager::redraw();
                if( !is_open ) {
                    break;
                }
                const auto &action = context.handle_input( 16 );
                if( action == "QUIT" ) {
                    is_open = false;
                } else if( !fp3d_compositor::turn_based() &&
                           fp3d_input::modern_2d_context().input_to_action( context.get_raw_input() ) == "FP3D_ACTIONS" ) {
                    is_open = false;
                } else if( action == "DOWN" || action == "RIGHT" ) {
                    move_selection( 1 );
                } else if( action == "UP" || action == "LEFT" ) {
                    move_selection( -1 );
                } else if( action == "CONFIRM" && selected >= 0 ) {
                    choose( selected );
                }
            }
            return chosen ? std::optional<entry>( options[*chosen] ) : std::nullopt;
        }
};

class dropdown : public cataimgui::window
{
        std::vector<entry> options;
        point anchor;
        float scale;
        int selected = 0;
        std::optional<int> chosen;
        bool first_frame = true;
        void choose( int index ) {
            if( index < 0 || index >= static_cast<int>( options.size() ) || !options[index].enabled ) {
                return;
            }
            chosen = index;
            button_action = options[index].command ? options[index].command : action_ident( options[index].action );
            is_open = false;
        }
        cataimgui::bounds get_bounds() override {
            const auto size = ImGui::GetIO().DisplaySize;
            const float width = std::min( 260 * scale, size.x );
            const float height = std::min( ( 16 + options.size() * 32 ) * scale, size.y );
            return { std::clamp( static_cast<float>( anchor.x ), 0.0f, size.x - width ),
                     std::clamp( static_cast<float>( anchor.y ), 0.0f, size.y - height ), width, height };
        }
        void draw_controls() override {
            ImGui::SetWindowFontScale( scale );
            for( int index = 0; index < static_cast<int>( options.size() ); ++index ) {
                const auto &option = options[index];
                ImGui::PushID( index );
                ImGui::BeginDisabled( !option.enabled );
                if( ImGui::Selectable( option.label, selected == index, 0, ImVec2( 0, 28 * scale ) ) ) {
                    choose( index );
                }
                if( ImGui::IsItemHovered( ImGuiHoveredFlags_AllowWhenDisabled ) ) {
                    selected = index;
                    ImGui::SetTooltip( "%s", option.enabled ? option.description : "需要靠近目标或满足操作条件。" );
                }
                ImGui::EndDisabled();
                ImGui::PopID();
            }
            // Ignore the right press that opened this list.
            if( !first_frame && ( ImGui::IsMouseClicked( ImGuiMouseButton_Right ) ||
                                  ( ImGui::IsMouseClicked( ImGuiMouseButton_Left ) &&
                                    !ImGui::IsWindowHovered( ImGuiHoveredFlags_ChildWindows ) ) ) ) {
                is_open = false;
            }
            first_frame = false;
        }
    public:
        dropdown( const std::vector<entry> &choices, const point &position, int percent ) :
            cataimgui::window( "地块操作", ImGuiWindowFlags_NoTitleBar ), anchor( position ),
            scale( std::clamp( percent, 100, 150 ) / 100.0f ) {
            for( const auto &option : choices ) {
                if( option.visible ) { options.push_back( option ); }
            }
        }
        void draw() override {
            if( !is_open ) { return; }
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( .045f, .043f, .038f, 1 ) );
            cataimgui::window::draw();
            ImGui::PopStyleColor();
        }
        std::optional<entry> run() {
            input_context context( "MODERN_2D_CONTEXT", keyboard_mode::keycode );
            for( const char *action : { "QUIT", "CONFIRM", "UP", "DOWN", "SELECT", "MOUSE_MOVE", "ANY_INPUT" } ) {
                context.register_action( action );
            }
            while( is_open && fp3d_compositor::survival_menu_available() ) {
                ui_manager::redraw();
                if( !is_open ) { break; }
                const auto &action = context.handle_input( 16 );
                if( action == "QUIT" ) { is_open = false; }
                if( action == "CONFIRM" ) { choose( selected ); }
                if( action == "UP" || action == "DOWN" ) {
                    const int count = options.size();
                    const int step = action == "UP" ? -1 : 1;
                    for( int i = 0; i < count; ++i ) {
                        selected = ( selected + count + step ) % count;
                        if( options[selected].enabled ) { break; }
                    }
                }
            }
            return chosen ? std::optional<entry>( options[*chosen] ) : std::nullopt;
        }
};
}

const std::array<entry, 8> &entries()
{
    return default_options;
}
int sector_at( float x, float y, float inner_radius, float outer_radius, int count )
{
    if( !std::isfinite( x ) || !std::isfinite( y ) || inner_radius < 0 ||
        outer_radius <= inner_radius || count < 1 || count > 8 ) {
        return -1;
    }
    const float radius = std::hypot( x, y );
    if( radius <= inner_radius || radius > outer_radius ) {
        return -1;
    }
    const float step = 2 * pi / count;
    float angle = std::atan2( y, x ) + pi / 2 + step / 2;
    if( angle < 0 ) {
        angle += 2 * pi;
    }
    return static_cast<int>( angle / step ) % count;
}
std::unique_ptr<cataimgui::window> create( int scale_percent )
{
    return create( entries(), "快捷行动", scale_percent );
}
std::unique_ptr<cataimgui::window> create( const std::array<entry, 8> &choices,
        const std::string &heading, int scale_percent )
{
    return std::make_unique<wheel>( choices, heading, scale_percent );
}
std::optional<entry> show()
{
    return show( entries(), "快捷行动" );
}
std::optional<entry> show( const std::array<entry, 8> &choices, const std::string &heading )
{
    const bool has_action = std::any_of( choices.begin(), choices.end(), []( const entry &choice ) {
        return choice.visible && choice.enabled && ( choice.action != ACTION_NULL || choice.command );
    } );
    if( !fp3d_compositor::survival_menu_available() || !has_action ) {
        return std::nullopt;
    }
    fp3d_input::world_gestures().cancel();
    fp3d_compositor::set_input_context( "FP3D_WHEEL" );
    auto &io = ImGui::GetIO();
    const bool no_mouse = io.ConfigFlags & ImGuiConfigFlags_NoMouse;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    on_out_of_scope restore( [&]() {
        if( no_mouse ) {
            io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
        }
        fp3d_input::world_gestures().cancel();
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
        if( g ) {
            g->invalidate_main_ui_adaptor();
        }
    } );
    wheel menu( choices, heading, fp3d_modern_ui::scale_percent() );
    return menu.run();
}

std::unique_ptr<cataimgui::window> create_dropdown( const std::vector<entry> &choices,
        const point &anchor, int scale_percent )
{
    return std::make_unique<dropdown>( choices, anchor, scale_percent );
}

std::optional<entry> show_dropdown( const std::vector<entry> &choices, const point &anchor )
{
    if( !fp3d_compositor::survival_menu_available() || choices.empty() ) { return std::nullopt; }
    fp3d_compositor::set_input_context( "MODERN_2D_CONTEXT" );
    auto &io = ImGui::GetIO();
    const bool no_mouse = io.ConfigFlags & ImGuiConfigFlags_NoMouse;
    io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    on_out_of_scope restore( [&]() {
        if( no_mouse ) { io.ConfigFlags |= ImGuiConfigFlags_NoMouse; }
        fp3d_compositor::set_input_context( "DEFAULTMODE" );
        if( g ) { g->invalidate_main_ui_adaptor(); }
    } );
    dropdown menu( choices, anchor, fp3d_modern_ui::scale_percent() );
    return menu.run();
}
}
#endif
