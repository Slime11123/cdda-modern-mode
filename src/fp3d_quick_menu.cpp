#include "fp3d_quick_menu.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cmath>
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
const std::array<entry, 8> options = {{
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
        int selected = -1;
        std::optional<int> chosen;

        void choose( int index ) {
            chosen = index;
            button_action = options[index].command ? options[index].command :
                            action_ident( options[index].action );
            is_open = false;
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
            const int hovered = sector_at( mouse.x - center.x, mouse.y - center.y, inner, outer );
            const auto delta = ImGui::GetIO().MouseDelta;
            if( delta.x != 0 || delta.y != 0 || selected < 0 ) {
                selected = hovered;
            }
            auto *draw = ImGui::GetWindowDrawList();
            const auto text = [&]( const char *value, float x, float y, ImU32 color ) {
                const auto extent = ImGui::CalcTextSize( value );
                draw->AddText( ImVec2( x - extent.x / 2, y - extent.y / 2 ), color, value );
            };
            const ImU32 white = IM_COL32( 235, 232, 224, 255 );
            const ImU32 gold = IM_COL32( 232, 180, 94, 255 );
            for( int index = 0; index < 8; ++index ) {
                const float angle = -pi / 2 + index * pi / 4;
                const float first = angle - pi / 8 + .014f;
                const float last = angle + pi / 8 - .014f;
                const auto path = [&]() {
                    draw->PathArcTo( center, outer, first, last, 16 );
                    draw->PathArcTo( center, inner, last, first, 8 );
                };
                path();
                draw->PathFillConcave( index == selected ? IM_COL32( 113, 80, 35, 225 ) :
                                       IM_COL32( 22, 21, 19, 205 ) );
                path();
                draw->PathStroke( index == selected ? gold : IM_COL32( 133, 120, 96, 160 ),
                                  ImDrawFlags_Closed, index == selected ? 2 : 1 );
                const float radius = ( inner + outer ) / 2;
                text( options[index].label, center.x + std::cos( angle ) * radius,
                      center.y + std::sin( angle ) * radius, index == selected ? gold : white );
            }
            draw->AddCircleFilled( center, inner - 3, IM_COL32( 16, 15, 14, 205 ), 48 );
            text( "快捷行动", center.x, center.y - font * .6f, white );
            text( "Esc 返回", center.x, center.y + font * .9f, IM_COL32( 190, 188, 180, 255 ) );
            if( selected >= 0 ) {
                text( options[selected].description, center.x, center.y + outer + font, white );
            }
            text( "点击执行 / 方向键选择 / Enter 确认", center.x, center.y + outer + font * 2.3f,
                  IM_COL32( 200, 198, 190, 255 ) );
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
        explicit wheel( int percent ) : cataimgui::window( "快捷行动轮盘",
                    ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground |
                    ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ),
            background( fp3d_compositor::copy_view() ), scale( std::clamp( percent, 100, 150 ) / 100.0f ) {}
        void draw() override {
            if( !is_open ) {
                return;
            }
            auto *draw = ImGui::GetBackgroundDrawList();
            const auto size = ImGui::GetIO().DisplaySize;
            const auto viewport = fp3d_compositor::viewport_pixels();
            if( background && viewport[2] > 0 && viewport[3] > 0 ) {
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
            while( is_open && fp3d_compositor::turn_based() ) {
                ui_manager::redraw();
                if( !is_open ) {
                    break;
                }
                const auto &action = context.handle_input( 16 );
                if( action == "QUIT" ) {
                    is_open = false;
                } else if( action == "DOWN" || action == "RIGHT" ) {
                    selected = ( selected + 1 ) % 8;
                } else if( action == "UP" || action == "LEFT" ) {
                    selected = selected < 0 ? 7 : ( selected + 7 ) % 8;
                } else if( action == "CONFIRM" && selected >= 0 ) {
                    choose( selected );
                }
            }
            return chosen ? std::optional<entry>( options[*chosen] ) : std::nullopt;
        }
};
}

const std::array<entry, 8> &entries()
{
    return options;
}
int sector_at( float x, float y, float inner_radius, float outer_radius )
{
    if( !std::isfinite( x ) || !std::isfinite( y ) || inner_radius < 0 ||
        outer_radius <= inner_radius ) {
        return -1;
    }
    const float radius = std::hypot( x, y );
    if( radius <= inner_radius || radius > outer_radius ) {
        return -1;
    }
    float angle = std::atan2( y, x ) + pi / 2 + pi / 8;
    if( angle < 0 ) {
        angle += 2 * pi;
    }
    return static_cast<int>( angle / ( pi / 4 ) ) % 8;
}
std::unique_ptr<cataimgui::window> create( int scale_percent )
{
    return std::make_unique<wheel>( scale_percent );
}
std::optional<entry> show()
{
    if( !fp3d_compositor::turn_based() ) {
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
    wheel menu( fp3d_modern_ui::scale_percent() );
    return menu.run();
}
}
#endif
