#pragma once

#if defined(TILES) && defined(ENABLE_FP3D)
#include "imgui/imgui.h"

namespace fp3d_survival_style
{
inline constexpr ImU32 background = IM_COL32( 0x10, 0x11, 0x13, 255 );
inline constexpr ImU32 panel = IM_COL32( 0x1C, 0x1E, 0x21, 255 );
inline constexpr ImU32 item = IM_COL32( 0x2A, 0x2C, 0x30, 255 );
inline constexpr ImU32 container = IM_COL32( 0x39, 0x33, 0x29, 255 );
inline constexpr ImU32 hover = IM_COL32( 0x51, 0x45, 0x26, 255 );
inline constexpr ImU32 selected = IM_COL32( 0x76, 0x60, 0x2D, 255 );
inline constexpr ImU32 accent = IM_COL32( 0xE5, 0xB9, 0x4E, 255 );
inline constexpr ImU32 heading = IM_COL32( 0xF2, 0xD5, 0x80, 255 );
inline constexpr ImU32 text = IM_COL32( 0xD8, 0xD8, 0xD4, 255 );
inline constexpr ImU32 normal = IM_COL32( 0x81, 0xB4, 0x77, 255 );
inline constexpr ImU32 warning = IM_COL32( 0xD9, 0x8B, 0x42, 255 );
inline constexpr ImU32 danger = IM_COL32( 0xC9, 0x55, 0x50, 255 );

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
            { ImGuiCol_Text, text }, { ImGuiCol_TextDisabled, text },
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
    ImGui::TextUnformatted( label );
    ImGui::PopStyleColor();
    ImGui::Separator();
}
}
#endif
