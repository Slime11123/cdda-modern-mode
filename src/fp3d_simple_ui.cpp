#include "fp3d_simple_ui.h"

#if defined(TILES) && defined(ENABLE_FP3D)
#include "fp3d_compositor.h"
#include "cata_imgui.h"
#include "imgui/imgui.h"
#include "item.h"
#include "scoped_rng.h"

namespace fp3d_simple_ui
{
namespace
{
class panel : public cataimgui::window
{
        std::function<void( cataimgui::window & )> contents;
        std::function<bool()> visible;
    public:
        panel( const std::string &title,
               const std::function<void( cataimgui::window & )> &draw,
               const std::function<bool()> &visible ) : window( title, ImGuiWindowFlags_NoNavInputs ),
            contents( draw ), visible( visible ) {}
        void draw() override {
            if( !visible || visible() ) {
                cataimgui::window::draw();
            }
        }
    protected:
        cataimgui::bounds get_bounds() override {
            return { -1, -1, .9f, .85f };
        }
        void draw_controls() override {
            const scoped_rng_restore preserve_preview_rng;
            contents( *this );
        }
};
}

std::unique_ptr<cataimgui::window> create_panel( const std::string &title,
        const std::function<void( cataimgui::window & )> &draw,
        const std::function<bool()> &visible )
{
    return std::make_unique<panel>( title, draw, visible );
}

bool active()
{
    return fp3d_compositor::survival_menu_available();
}

std::vector<iteminfo> item_information( const item &object )
{
    const scoped_rng_restore preserve_preview_rng;
    return object.get_info( true );
}

ImVec4 text_color( const nc_color &color )
{
    if( active() && ( color == c_black || color == h_black ) ) {
        return ImVec4( .88f, .66f, .32f, 1 );
    }
    nc_color copy = color;
    return cataimgui::imvec4_from_color( copy );
}

void accent_text( const std::string &text )
{
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( .88f, .66f, .32f, 1 ) );
    ImGui::TextUnformatted( text.c_str() );
    ImGui::PopStyleColor();
}

scoped_theme::scoped_theme() : enabled( active() )
{
    if( !enabled ) {
        return;
    }
    // The retained 2D buffer can contain earlier frames of this window.
    const float opacity = fp3d_compositor::view_3d() ? .72f : 1.f;
    ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( .035f, .033f, .03f, opacity ) );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( .045f, .043f, .038f, .48f ) );
    ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4( .045f, .04f, .03f, .97f ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1, 1, 1, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, ImVec4( .65f, .63f, .58f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( .43f, .40f, .33f, .5f ) );
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( .12f, .115f, .10f, .74f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( .43f, .32f, .17f, .85f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( .65f, .46f, .20f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( .43f, .31f, .15f, .70f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( .50f, .36f, .16f, .8f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( .65f, .46f, .20f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( .12f, .115f, .10f, .65f ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( .30f, .25f, .17f, .8f ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( .38f, .29f, .16f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_TitleBg, ImVec4( .16f, .13f, .085f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_TitleBgActive, ImVec4( .30f, .22f, .11f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_CheckMark, ImVec4( .88f, .66f, .32f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_SliderGrab, ImVec4( .72f, .53f, .24f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, ImVec4( .88f, .66f, .32f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_Tab, ImVec4( .12f, .115f, .10f, .74f ) );
    ImGui::PushStyleColor( ImGuiCol_TabHovered, ImVec4( .43f, .32f, .17f, .85f ) );
    ImGui::PushStyleColor( ImGuiCol_TabActive, ImVec4( .43f, .31f, .15f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_Separator, ImVec4( .43f, .40f, .33f, .5f ) );
    ImGui::PushStyleColor( ImGuiCol_ScrollbarBg, ImVec4( .045f, .043f, .038f, .48f ) );
    ImGui::PushStyleColor( ImGuiCol_ScrollbarGrab, ImVec4( .43f, .40f, .33f, .8f ) );
    ImGui::PushStyleColor( ImGuiCol_NavCursor, ImVec4( .88f, .66f, .32f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_TextSelectedBg, ImVec4( .43f, .31f, .15f, .70f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 14, 12 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 7, 5 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8, 6 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowRounding, 4.f );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 2.f );
}

scoped_theme::~scoped_theme()
{
    if( enabled ) {
        ImGui::PopStyleVar( 5 );
        ImGui::PopStyleColor( 28 );
    }
}
}
#endif
