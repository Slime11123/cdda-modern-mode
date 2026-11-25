#pragma once

#if defined(TILES) && defined(ENABLE_FP3D)
#include "imgui/imgui.h"

// Modern Mode yellow/black palette, shared by every modern (ImGui) panel.
// Reference values are the agreed tactical HUD colours: deep neutral grounds,
// one amber accent, neutral body text, teal for valid state and a warm red for
// danger. Keep this table the single source of truth for modern UI colours.
namespace fp3d_survival_style
{
inline constexpr ImU32 background = IM_COL32( 0x11, 0x14, 0x19, 255 ); // 主背景 #111419
inline constexpr ImU32 panel = IM_COL32( 0x24, 0x2A, 0x32, 255 ); // 次级背景 #242A32
inline constexpr ImU32 item = IM_COL32( 0x2B, 0x32, 0x3B, 255 );
inline constexpr ImU32 container = IM_COL32( 0x33, 0x3A, 0x45, 255 );
inline constexpr ImU32 hover = IM_COL32( 0x4A, 0x41, 0x24, 255 );
inline constexpr ImU32 selected = IM_COL32( 0x5C, 0x4B, 0x20, 255 );
inline constexpr ImU32 accent = IM_COL32( 0xE5, 0xB9, 0x40, 255 ); // 主要强调色 #E5B940
inline constexpr ImU32 heading = IM_COL32( 0xF0, 0xD4, 0x7A, 255 );
inline constexpr ImU32 text = IM_COL32( 0xDC, 0xE0, 0xE4, 255 ); // 普通文字 #DCE0E4
inline constexpr ImU32 muted = IM_COL32( 0x9F, 0xA6, 0xAD, 255 ); // 次要文字 #9FA6AD
inline constexpr ImU32 normal = IM_COL32( 0x69, 0xB9, 0xAB, 255 ); // 有效状态 #69B9AB
inline constexpr ImU32 warning = IM_COL32( 0xE0, 0xA2, 0x4A, 255 );
inline constexpr ImU32 danger = IM_COL32( 0xE9, 0x6A, 0x62, 255 ); // 危险提示 #E96A62

inline ImVec4 color( ImU32 value )
{
    return ImGui::ColorConvertU32ToFloat4( value );
}

class scoped_theme
{
    public:
        explicit scoped_theme( float opacity ) {
            ImVec4 window = color( background );
            window.w = opacity;
            ImGui::PushStyleColor( ImGuiCol_WindowBg, window );
            for( const auto &entry : colors ) {
                ImGui::PushStyleColor( entry.slot, color( entry.value ) );
            }
        }

        ~scoped_theme() {
            ImGui::PopStyleColor( 1 + sizeof( colors ) / sizeof( colors[0] ) );
        }

        scoped_theme( const scoped_theme & ) = delete;
        scoped_theme &operator=( const scoped_theme & ) = delete;

    private:
        struct entry {
            ImGuiCol slot;
            ImU32 value;
        };
        static constexpr entry colors[] = {
            { ImGuiCol_ChildBg, panel }, { ImGuiCol_PopupBg, panel },
            { ImGuiCol_Text, text }, { ImGuiCol_TextDisabled, muted },
            { ImGuiCol_Border, item }, { ImGuiCol_Separator, item },
            { ImGuiCol_Button, item }, { ImGuiCol_ButtonHovered, hover },
            { ImGuiCol_ButtonActive, selected }, { ImGuiCol_Header, item },
            { ImGuiCol_HeaderHovered, hover }, { ImGuiCol_HeaderActive, selected },
            { ImGuiCol_FrameBg, item }, { ImGuiCol_FrameBgHovered, hover },
            { ImGuiCol_FrameBgActive, selected }, { ImGuiCol_TitleBg, panel },
            { ImGuiCol_TitleBgActive, container }, { ImGuiCol_CheckMark, accent },
            { ImGuiCol_PlotHistogram, accent }, { ImGuiCol_ResizeGrip, container },
            { ImGuiCol_SliderGrab, accent }, { ImGuiCol_SliderGrabActive, heading },
            { ImGuiCol_ScrollbarBg, background }, { ImGuiCol_ScrollbarGrab, item },
            { ImGuiCol_ScrollbarGrabHovered, hover }, { ImGuiCol_ScrollbarGrabActive, selected },
            { ImGuiCol_NavCursor, accent }, { ImGuiCol_TextSelectedBg, selected },
            { ImGuiCol_Tab, panel }, { ImGuiCol_TabHovered, hover }, { ImGuiCol_TabSelected, selected }
        };
};

inline void title( const char *label )
{
    ImGui::PushStyleColor( ImGuiCol_Text, color( heading ) );
    ImGui::TextWrapped( "%s", label );
    ImGui::PopStyleColor();
    ImGui::Separator();
}
}
#endif
