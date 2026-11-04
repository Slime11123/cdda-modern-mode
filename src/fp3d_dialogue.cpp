#include "fp3d_dialogue.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <array>
#include <cstdint>
#include <utility>
#include "catacharset.h"
#include "fp3d_compositor.h"
#include "fp3d_input.h"
#include "fp3d_materials.h"
#include "fp3d_snapshot.h"
#include "imgui/imgui.h"
#include "mutation.h"
#include "npc.h"

namespace fp3d_dialogue
{
namespace
{
const ImVec4 gold( 1, .86f, .38f, 1 );
}

window::window( const npc &speaker, const std::string &history, int &selection,
                bool can_close, int scale_percent ) :
    cataimgui::window( "NPC dialogue", ImGuiWindowFlags_NoTitleBar |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ),
    name( remove_color_tags( speaker.disp_name() ) ), transcript( remove_color_tags( history ) ),
    selected( selection ), can_close( can_close ), scale( scale_percent / 100.0f ),
    restore_no_mouse( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse ),
    background( fp3d_compositor::copy_view() )
{
    force_to_back = true;
    selected = 0;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    fp3d_input::world_gestures().cancel();
    fp3d_compositor::set_input_context( "DIALOGUE_CHOOSE_RESPONSE" );
    fp3d_snapshot::sprite image;
    image.kind = fp3d_snapshot::sprite_kind::character;
    image.male = speaker.male;
    const auto overrides = speaker.get_functioning_mutations( true, false,
    []( const mutation_branch & mutation ) {
        return mutation.override_look.has_value();
    } );
    image.id = overrides.empty() ? ( speaker.male ? "npc_male" : "npc_female" ) :
               overrides.front()->override_look->id;
    image.overlays = overrides.empty() ? speaker.get_overlay_ids() :
                     speaker.get_overlay_ids_when_override_look();
    portrait = fp3d_materials::item_icon( image );
}

window::~window()
{
    if( restore_no_mouse ) {
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }
    fp3d_input::world_gestures().cancel();
}

void window::set_responses( const std::vector<talk_data> &lines, const std::vector<bool> &enabled )
{
    responses = lines;
    available = enabled;
    selected = std::clamp( selected, 0, std::max( 0, static_cast<int>( responses.size() ) - 1 ) );
}

void window::set_history( const std::string &history )
{
    transcript = remove_color_tags( history );
    scroll_to_history = true;
}

void window::handle_action( std::string &action )
{
    if( !pending_action.empty() ) {
        action = std::exchange( pending_action, {} );
        return;
    }
    const int count = responses.size();
    const int previous = selected;
    if( count > 0 ) {
        if( action == "UP" ) {
            selected = ( selected + count - 1 ) % count;
        }
        if( action == "DOWN" ) {
            selected = ( selected + 1 ) % count;
        }
        if( action == "HOME" ) {
            selected = 0;
        }
        if( action == "END" ) {
            selected = count - 1;
        }
        if( action == "PAGE_UP" ) {
            selected = std::max( 0, selected - 5 );
        }
        if( action == "PAGE_DOWN" ) {
            selected = std::min( count - 1, selected + 5 );
        }
    }
    scroll_to_selection = scroll_to_selection || previous != selected;
    // Mouse selection and scrolling belong to ImGui, never the hidden curses list.
    if( action == "SELECT" || action == "MOUSE_MOVE" || action == "SCROLL_UP" ||
        action == "SCROLL_DOWN" ) {
        action = "NONE";
    }
}

void window::request_confirmation( const std::string &text )
{
    warning = remove_color_tags( text );
    confirmation.reset();
    pending_action.clear();
    open_warning = true;
}

std::optional<bool> window::take_confirmation()
{
    return std::exchange( confirmation, std::nullopt );
}

cataimgui::bounds window::get_bounds()
{
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float width = std::min( 1180 * scale, display.x * .92f );
    const float height = std::min( 820 * scale, display.y * .90f );
    return { ( display.x - width ) / 2, ( display.y - height ) / 2, width, height };
}

void window::draw()
{
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    const bool modern_2d = fp3d_compositor::controls() == fp3d_input::mode::modern_2d;
    if( background ) {
        ImGui::GetBackgroundDrawList()->AddImage( ( ImTextureID )( intptr_t )background.get(),
                ImVec2( 0, 0 ), ImGui::GetIO().DisplaySize,
                ImVec2( 0, modern_2d ? 0 : 1 ), ImVec2( 1, modern_2d ? 1 : 0 ) );
    }
    ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( .035f, .033f, .03f,
                          modern_2d ? 1.0f : .78f ) );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( .045f, .043f, .038f, .35f ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1, 1, 1, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, ImVec4( .70f, .64f, .48f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( .68f, .54f, .28f, .55f ) );
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( .18f, .15f, .10f, .72f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( .43f, .32f, .17f, .85f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( .65f, .46f, .20f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( .43f, .31f, .15f, .70f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( .50f, .36f, .16f, .8f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( .65f, .46f, .20f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4( .045f, .04f, .03f, .97f ) );
    ImGui::PushStyleColor( ImGuiCol_TitleBgActive, ImVec4( .30f, .22f, .11f, 1 ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 18 * scale, 16 * scale ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 10 * scale, 8 * scale ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4 * scale );
    cataimgui::window::draw();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 13 );
}

void window::draw_controls()
{
    ImGui::SetWindowFontScale( scale );
    if( reset_scroll ) {
        ImGui::SetScrollY( 0 );
    }
    ImGui::BeginDisabled( !warning.empty() );
    const float avatar_size = 80 * scale;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddRectFilled( origin,
            ImVec2( origin.x + avatar_size, origin.y + avatar_size ), IM_COL32( 78, 62, 35, 150 ), 5 );
    if( portrait ) {
        // Crop the upper body, preserving the source aspect ratio.
        ImGui::Image( ( ImTextureID )( intptr_t )portrait.get(), ImVec2( avatar_size, avatar_size ),
                      ImVec2( .2f, 0 ), ImVec2( .8f, .6f ) );
    } else {
        ImGui::Dummy( ImVec2( avatar_size, avatar_size ) );
        ImGui::GetWindowDrawList()->AddText( ImVec2( origin.x + 24 * scale, origin.y + 24 * scale ),
                                             IM_COL32( 255, 219, 97, 255 ), "?" );
    }
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::TextColored( gold, "正在交谈" );
    const float close_width = 100 * scale;
    ImGui::PushTextWrapPos( ImGui::GetWindowWidth() - close_width - 40 * scale );
    ImGui::TextWrapped( "%s", name.c_str() );
    ImGui::PopTextWrapPos();
    ImGui::TextDisabled( "点击回复 · 滚轮浏览" );
    ImGui::EndGroup();
    ImGui::SameLine( ImGui::GetWindowWidth() - close_width - 18 * scale );
    ImGui::BeginDisabled( !can_close );
    if( ImGui::Button( "结束对话", ImVec2( close_width, 36 * scale ) ) ) {
        pending_action = "QUIT";
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    const float content_height = ImGui::GetContentRegionAvail().y;
    const bool narrow = ImGui::GetContentRegionAvail().x < 850 * scale;
    const float footer_height = ( narrow ? 84 : 46 ) * scale;
    const float history_height = std::max( 60 * scale, std::min( content_height * .38f,
                                 content_height - footer_height - 180 * scale ) );
    ImGui::BeginChild( "交谈记录", ImVec2( 0, history_height ), true );
    ImGui::TextWrapped( "%s", transcript.c_str() );
    if( reset_scroll || scroll_to_history || ImGui::IsWindowAppearing() ) {
        ImGui::SetScrollHereY( 1 );
    }
    scroll_to_history = false;
    ImGui::EndChild();

    ImGui::TextColored( gold, "你的回复" );
    ImGui::BeginChild( "回复选项", ImVec2( 0, -footer_height ), true );
    if( reset_scroll ) {
        ImGui::SetScrollY( 0 );
    }
    for( size_t i = 0; i < responses.size(); ++i ) {
        ImGui::PushID( static_cast<int>( i ) );
        const std::string text = remove_color_tags( responses[i].text );
        const float width = std::max( 1.0f, ImGui::GetContentRegionAvail().x - 24 * scale );
        const float height = std::max( 38 * scale, ImGui::CalcTextSize( text.c_str(), nullptr,
                                       false, width ).y + 16 * scale );
        const bool enabled = i < available.size() && available[i];
        ImGui::BeginDisabled( !enabled );
        const ImVec2 start = ImGui::GetCursorScreenPos();
        if( ImGui::Selectable( "##response", selected == static_cast<int>( i ), 0, ImVec2( 0, height ) ) ) {
            selected = i;
            pending_action = "CONFIRM";
        }
        ImGui::GetWindowDrawList()->AddText( ImGui::GetFont(), ImGui::GetFontSize(),
                                             ImVec2( start.x + 10 * scale, start.y + 8 * scale ),
                                             ImGui::GetColorU32( enabled && selected == static_cast<int>( i ) ? gold :
                                                     ImGui::GetStyleColorVec4( enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled ) ),
                                             text.c_str(), nullptr, width );
        if( scroll_to_selection && selected == static_cast<int>( i ) ) {
            ImGui::SetScrollHereY( selected == static_cast<int>( responses.size() ) - 1 ? 1 : .5f );
        }
        ImGui::EndDisabled();
        ImGui::PopID();
    }
    scroll_to_selection = false;
    reset_scroll = false;
    ImGui::EndChild();

    const std::array<std::pair<const char *, const char *>, 5> actions = {{
            { "审视对方", "LOOK_AT" }, { "估测状态", "SIZE_UP_STATS" },
            { "评估性格", "ASSESS_PERSONALITY" }, { "叫喊", "YELL" }, { "观察态度", "CHECK_OPINION" }
        }
    };
    const float button_width = ( ImGui::GetContentRegionAvail().x - ( narrow ? 2 : 4 ) *
                                 ImGui::GetStyle().ItemSpacing.x ) / ( narrow ? 3 : 5 );
    for( size_t i = 0; i < actions.size(); ++i ) {
        if( i && ( !narrow || i != 3 ) ) {
            ImGui::SameLine();
        }
        if( ImGui::Button( actions[i].first, ImVec2( button_width, 30 * scale ) ) ) {
            pending_action = actions[i].second;
        }
    }
    ImGui::EndDisabled();

    if( open_warning ) {
        ImGui::OpenPopup( "确认选择" );
        open_warning = false;
    }
    ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing,
                             ImVec2( .5f, .5f ) );
    ImGui::SetNextWindowSize( ImVec2( std::min( 500 * scale, ImGui::GetIO().DisplaySize.x * .85f ), 0 ),
                              ImGuiCond_Appearing );
    if( ImGui::BeginPopupModal( "确认选择", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) ) {
        ImGui::TextWrapped( "%s", warning.c_str() );
        ImGui::Separator();
        if( ImGui::Button( "继续", ImVec2( 120 * scale, 36 * scale ) ) ) {
            confirmation = true;
        }
        ImGui::SameLine();
        if( ImGui::Button( "返回对话", ImVec2( 120 * scale, 36 * scale ) ) ||
            ImGui::IsKeyPressed( ImGuiKey_Escape ) ) {
            confirmation = false;
        }
        if( confirmation ) {
            warning.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
}
#endif
