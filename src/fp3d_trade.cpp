#include "fp3d_trade.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <utility>
#include "catacharset.h"
#include "cata_utility.h"
#include "fp3d_compositor.h"
#include "fp3d_input.h"
#include "fp3d_materials.h"
#include "fp3d_snapshot.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "item.h"
#include "itype.h"
#include "item_category.h"
#include "npc.h"
#include "npctrade.h"
#include "options.h"
#include "output.h"
#include "scoped_rng.h"
#include "units.h"

namespace fp3d_trade
{
namespace
{
const ImVec4 gold( 1, .86f, .38f, 1 );

void set_font_scale( float scale )
{
    // This ImGui version multiplies a window's scale by its immediate parent.
    // Compensate at every nesting level so grids/popups use the chosen UI size.
    const ImGuiWindow *parent = ImGui::GetCurrentWindow()->ParentWindow;
    ImGui::SetWindowFontScale( scale / ( parent ? parent->FontWindowScale : 1.0f ) );
}
}

window::window( trade_ui &trade, int scale_percent ) :
    cataimgui::window( "NPC trade", ImGuiWindowFlags_NoTitleBar |
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse ),
    trade( trade ), scale( scale_percent / 100.0f ),
    restore_no_mouse( ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_NoMouse ),
    background( fp3d_compositor::copy_view() )
{
    force_to_back = true;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    fp3d_input::world_gestures().cancel();
    fp3d_compositor::set_input_context( "NPC_TRADE_MOUSE" );
    refresh();
}

window::~window()
{
    if( restore_no_mouse ) { ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouse; }
    fp3d_input::world_gestures().cancel();
}

void window::refresh()
{
    scoped_rng_restore restore_rng;
    for( size_t side = 0; side < rows.size(); ++side ) {
        rows[side].clear();
        for( const inventory_entry *entry : trade._panes[side]->trade_entries() ) {
            const auto loc = entry->any_item();
            if( !loc ) { continue; }
            const item &object = *loc;
            const auto key = object.typeId().str() + ( object.has_itype_variant() ? object.itype_variant().id : "" );
            const int type = object.is_gun() || object.is_melee() ? 1 : object.is_ammo() ? 2 :
                             object.is_medication() ? 4 : object.is_food() ? 3 : object.is_container() ? 6 :
                             object.is_armor() ? 5 : object.is_tool() ? 7 : 8;
            rows[side].push_back( { loc, remove_color_tags( object.tname() ),
                                   remove_color_tags( entry->get_category_ptr()->name_header() ),
                                   remove_color_tags( entry->denial.value_or( "" ) ), key,
                                   static_cast<int>( entry->get_available_count() ), static_cast<int>( entry->chosen_count ),
                                   npc_trading::trading_price( *trade._parties[1 - side], *trade._parties[side], { loc, 1 } ), type } );
        }
        std::stable_sort( rows[side].begin(), rows[side].end(), []( const row &a, const row &b ) {
            return a.group != b.group ? a.group < b.group : a.name < b.name;
        } );
        filter_dirty[side] = true;
    }
}

void window::filter( size_t side )
{
    if( !filter_dirty[side] ) { return; }
    filtered[side].clear();
    for( size_t i = 0; i < rows[side].size(); ++i ) {
        const row &entry = rows[side][i];
        if( ( category[side] && entry.category != category[side] ) ||
            ( !search[side].empty() && !lcmatch( entry.name, search[side] ) ) ||
            ( selected_only[side] && !entry.chosen ) ) { continue; }
        if( filtered[side].empty() || filtered[side].back().name != entry.group ) {
            filtered[side].push_back( { entry.group, {} } );
        }
        filtered[side].back().entries.push_back( i );
    }
    filter_dirty[side] = false;
}

void window::handle_action( const std::string &action_name )
{
    if( action_name == "QUIT" && message.empty() && !ImGui::GetIO().WantTextInput &&
        !ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId ) ) { pending = action::cancel; }
}

std::optional<trade_ui::trade_result_t> window::process_action()
{
    scoped_rng_restore restore_rng;
    switch( std::exchange( pending, action::none ) ) {
        case action::none:
            break;
        case action::quantity:
            trade._cpane = pending_side;
            trade._panes[pending_side]->set_trade_count( pending_item, std::max( 0, pending_quantity ) );
            refresh();
            break;
        case action::balance:
            trade._cpane = pending_side;
            for( inventory_entry *entry : trade._panes[pending_side]->trade_entries() ) {
                if( entry->any_item() == pending_item ) {
                    trade.autobalance( *entry );
                    break;
                }
            }
            refresh();
            break;
        case action::clear:
            for( size_t side = 0; side < rows.size(); ++side ) {
                trade._cpane = side;
                for( const auto &entry : rows[side] ) {
                    if( entry.chosen ) { trade._panes[side]->set_trade_count( entry.location, 0 ); }
                }
            }
            refresh();
            break;
        case action::bank:
            trade.bank_balance();
            break;
        case action::confirm: {
            const auto check = trade._trade_confirmation();
            offer_valid = check.first;
            message = remove_color_tags( check.second );
            open_confirmation = true;
            break;
        }
        case action::accept: {
            // Recheck native credit/capacity rules at the final confirmation.
            const auto check = trade._trade_confirmation();
            if( check.first ) { return trade._result( true ); }
            offer_valid = false;
            message = remove_color_tags( check.second );
            open_confirmation = true;
            break;
        }
        case action::cancel:
            return trade._result( false );
    }
    return std::nullopt;
}

cataimgui::bounds window::get_bounds()
{
    const ImVec2 size = ImGui::GetIO().DisplaySize;
    return { size.x * .025f, size.y * .035f, size.x * .95f, size.y * .93f };
}

void window::draw()
{
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    if( background ) {
        ImGui::GetBackgroundDrawList()->AddImage( ( ImTextureID )( intptr_t )background.get(),
                ImVec2( 0, 0 ), ImGui::GetIO().DisplaySize, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
    }
    ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( .035f, .033f, .03f, .72f ) );
    ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( .045f, .043f, .038f, .48f ) );
    ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1, 1, 1, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_TextDisabled, ImVec4( .70f, .64f, .48f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( .43f, .40f, .33f, .5f ) );
    ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( .12f, .115f, .10f, .74f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( .43f, .32f, .17f, .85f ) );
    ImGui::PushStyleColor( ImGuiCol_ButtonActive, ImVec4( .65f, .46f, .20f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( .43f, .31f, .15f, .70f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( .50f, .36f, .16f, .8f ) );
    ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( .65f, .46f, .20f, .9f ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( .12f, .115f, .10f, .65f ) );
    ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( .30f, .25f, .17f, .8f ) );
    ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4( .045f, .04f, .03f, .97f ) );
    ImGui::PushStyleColor( ImGuiCol_CheckMark, gold );
    ImGui::PushStyleColor( ImGuiCol_SliderGrab, ImVec4( .72f, .53f, .24f, 1 ) );
    ImGui::PushStyleColor( ImGuiCol_SliderGrabActive, gold );
    ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( .38f, .29f, .16f, .9f ) );
    ImGui::PushStyleVar( ImGuiStyleVar_WindowPadding, ImVec2( 16 * scale, 12 * scale ) );
    ImGui::PushStyleVar( ImGuiStyleVar_ItemSpacing, ImVec2( 8 * scale, 6 * scale ) );
    ImGui::PushStyleVar( ImGuiStyleVar_FrameRounding, 4 * scale );
    cataimgui::window::draw();
    ImGui::PopStyleVar( 3 );
    ImGui::PopStyleColor( 18 );
}

void window::quantity( size_t side, const row &entry, int count )
{
    pending = action::quantity;
    pending_side = side;
    pending_item = entry.location;
    pending_quantity = std::clamp( count, 0, entry.available );
}

void window::card( size_t side, const row &entry, bool compact )
{
    ImGui::PushID( static_cast<const void *>( entry.location.get_item() ) );
    const bool enabled = entry.denial.empty();
    if( ImGui::InvisibleButton( "##item", ImVec2( 96 * scale, ( compact ? 100 : 110 ) * scale ) ) ) {
        focused[side] = entry.location;
        if( enabled ) { quantity( side, entry, entry.chosen ? 0 : 1 ); }
    }
    if( !ImGui::IsItemVisible() ) {
        ImGui::PopID();
        return;
    }
    const ImVec2 min = ImGui::GetItemRectMin();
    const ImVec2 max = ImGui::GetItemRectMax();
    auto *draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled( min, max, entry.chosen ? IM_COL32( 110, 79, 36, 190 ) :
                         ImGui::IsItemHovered() ? IM_COL32( 70, 60, 43, 180 ) : IM_COL32( 20, 19, 17, 120 ), 3 );
    draw->AddRect( min, max, entry.chosen || focused[side] == entry.location ?
                   IM_COL32( 220, 170, 85, 255 ) : IM_COL32( 105, 95, 75, 100 ), 3 );
    auto icon = icons.find( entry.icon );
    if( icon == icons.end() ) {
        scoped_rng_restore restore_rng;
        fp3d_snapshot::sprite image;
        image.kind = fp3d_snapshot::sprite_kind::held_item;
        image.id = entry.location->typeId().str();
        if( entry.location->has_itype_variant() ) { image.variant = entry.location->itype_variant().id; }
        icon = icons.emplace( entry.icon, fp3d_materials::item_icon( image ) ).first;
    }
    if( icon != icons.end() && icon->second ) {
        draw->AddImage( ( ImTextureID )( intptr_t )icon->second.get(),
                        ImVec2( min.x + 24 * scale, min.y + 4 * scale ),
                        ImVec2( min.x + 72 * scale, min.y + 52 * scale ), ImVec2( 0, 0 ), ImVec2( 1, 1 ),
                        enabled ? IM_COL32_WHITE : IM_COL32( 145, 145, 145, 150 ) );
    }
    const ImU32 color = ImGui::GetColorU32( enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled );
    const ImVec4 clip( min.x + 4 * scale, min.y, max.x - 4 * scale, max.y );
    draw->AddText( ImGui::GetFont(), ImGui::GetFontSize(), ImVec2( min.x + 4 * scale, min.y + ( compact ? 50 : 56 ) * scale ),
                   color, entry.name.c_str(), nullptr, 0, &clip );
    const std::string price = format_money( entry.price );
    draw->AddText( ImGui::GetFont(), ImGui::GetFontSize(), ImVec2( min.x + 4 * scale, min.y + ( compact ? 76 : 82 ) * scale ),
                   ImGui::GetColorU32( enabled ? gold : ImGui::GetStyleColorVec4( ImGuiCol_TextDisabled ) ),
                   price.c_str(), nullptr, 0, &clip );
    const std::string amount = std::to_string( entry.chosen ) + "/" + std::to_string( entry.available );
    draw->AddText( ImVec2( min.x + 3 * scale, min.y + 2 * scale ), color, amount.c_str() );
    if( ImGui::IsItemHovered() ) {
        ImGui::BeginTooltip();
        set_font_scale( scale );
        ImGui::TextWrapped( "%s", entry.name.c_str() );
        ImGui::Text( "单价 %s · 已选 %d / %d", price.c_str(), entry.chosen, entry.available );
        ImGui::Text( "%.2f kg / %.2f L", units::to_gram( entry.location->weight() ) / 1000.0f,
                     units::to_milliliter( entry.location->volume() ) / 1000.0f );
        ImGui::TextWrapped( "位置：%s", entry.location.describe().c_str() );
        if( !enabled ) { ImGui::TextColored( gold, "%s", entry.denial.c_str() ); }
        ImGui::EndTooltip();
    }
    ImGui::PopID();
}

void window::pane( size_t side, float width, float height )
{
    const bool compact = height < 530 * scale;
    ImGui::PushID( static_cast<int>( side ) );
    ImGui::BeginChild( "交易背包", ImVec2( width, height ), true,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    set_font_scale( scale );
    if( reset_scroll ) { ImGui::SetScrollY( 0 ); }
    const auto name = side == trade_ui::_you ? "你的物品 / 出售" :
                      remove_color_tags( trade._parties[side]->get_name() ) + " / 购入";
    ImGui::TextColored( gold, "%s", name.c_str() );
    ImGui::SetNextItemWidth( -1 );
    if( ImGui::InputTextWithHint( "##search", "搜索物品…", &search[side] ) ) {
        filter_dirty[side] = true;
        reset_scroll = true;
    }
    ImGui::SetNextItemWidth( std::max( 100 * scale, ImGui::GetContentRegionAvail().x - 140 * scale ) );
    const std::array<const char *, 9> categories = { "全部", "武器", "弹药", "食品", "医疗", "衣物", "背包/容器", "工具", "其它" };
    if( ImGui::BeginCombo( "##category", categories[category[side]] ) ) {
        set_font_scale( scale );
        for( size_t i = 0; i < categories.size(); ++i ) {
            if( ImGui::Selectable( categories[i], category[side] == static_cast<int>( i ) ) ) {
                category[side] = i;
                filter_dirty[side] = true;
                reset_scroll = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if( ImGui::Checkbox( "仅看已选", &selected_only[side] ) ) {
        filter_dirty[side] = true;
        reset_scroll = true;
    }
    // Independent grid scrolling keeps both the filters and quantity controls visible.
    ImGui::BeginChild( "物品网格", ImVec2( 0, -( compact ? 80 : 130 ) * scale ), true );
    set_font_scale( scale );
    if( reset_scroll ) { ImGui::SetScrollY( 0 ); }
    filter( side );
    const int columns = std::max( 1, static_cast<int>( ( ImGui::GetContentRegionAvail().x + 8 * scale ) / ( 104 * scale ) ) );
    for( const group &section : filtered[side] ) {
        if( !compact ) {
            ImGui::TextColored( gold, "%s", section.name.c_str() );
            ImGui::Separator();
        }
        // Clip entire grid rows: off-screen items do no formatting, hit-testing,
        // icon loading or drawing. Explicit row height also handles small groups.
        ImGuiListClipper clipper;
        clipper.Begin( ( section.entries.size() + columns - 1 ) / columns,
                       ( compact ? 100 : 110 ) * scale + ImGui::GetStyle().ItemSpacing.y );
        while( clipper.Step() ) {
            for( int line = clipper.DisplayStart; line < clipper.DisplayEnd; ++line ) {
                for( int column = 0; column < columns; ++column ) {
                    const size_t index = line * columns + column;
                    if( index >= section.entries.size() ) { break; }
                    if( column ) { ImGui::SameLine(); }
                    card( side, rows[side][section.entries[index]], compact );
                }
            }
        }
    }
    if( filtered[side].empty() ) { ImGui::TextDisabled( "没有匹配物品" ); }
    ImGui::EndChild();
    ImGui::BeginChild( "数量操作", ImVec2( 0, 0 ), false,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
    set_font_scale( scale );
    const auto current = std::find_if( rows[side].begin(), rows[side].end(), [&]( const row & entry ) {
        return entry.location == focused[side];
    } );
    if( current != rows[side].end() ) {
        const row &entry = *current;
        if( !compact ) {
            ImGui::BeginChild( "当前物品", ImVec2( 0, 36 * scale ), false );
            set_font_scale( scale );
            ImGui::TextWrapped( "%s", entry.name.c_str() );
            ImGui::EndChild();
        }
        ImGui::BeginDisabled( !entry.denial.empty() );
        int count = entry.chosen;
        const std::string count_format = "已选 %d / " + std::to_string( entry.available );
        ImGui::SetNextItemWidth( -1 );
        if( ImGui::SliderInt( "##quantity", &count, 0, entry.available, count_format.c_str(), ImGuiSliderFlags_AlwaysClamp ) ) {
            quantity( side, entry, count );
        }
        const float button_width = ( ImGui::GetContentRegionAvail().x - 4 * 8 * scale ) / 5;
        if( ImGui::Button( "-##minus", ImVec2( button_width, 30 * scale ) ) ) { quantity( side, entry, entry.chosen - 1 ); }
        ImGui::SameLine();
        if( ImGui::Button( "+##plus", ImVec2( button_width, 30 * scale ) ) ) { quantity( side, entry, entry.chosen + 1 ); }
        ImGui::SameLine();
        if( ImGui::Button( "全部", ImVec2( button_width, 30 * scale ) ) ) { quantity( side, entry, entry.available ); }
        ImGui::SameLine();
        if( ImGui::Button( "配平", ImVec2( button_width, 30 * scale ) ) ) {
            pending = action::balance;
            pending_side = side;
            pending_item = entry.location;
        }
        ImGui::SameLine();
        if( ImGui::Button( "移除", ImVec2( button_width, 30 * scale ) ) ) { quantity( side, entry, 0 ); }
        ImGui::EndDisabled();
        if( !entry.denial.empty() ) {
            ImGui::PushTextWrapPos( 0 );
            ImGui::TextColored( gold, "%s", entry.denial.c_str() );
            ImGui::PopTextWrapPos();
        }
    } else {
        ImGui::TextDisabled( "点击物品加入交易" );
        ImGui::TextDisabled( "在这里增减数量或选择全部" );
    }
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::PopID();
}

void window::draw_controls()
{
    set_font_scale( scale );
    if( reset_scroll ) { ImGui::SetScrollY( 0 ); }
    ImGui::BeginDisabled( !message.empty() );
    ImGui::TextColored( gold, "%s", remove_color_tags( trade._title ).c_str() );
    ImGui::SameLine( ImGui::GetWindowWidth() - 116 * scale );
    if( ImGui::Button( "返回对话", ImVec2( 100 * scale, 30 * scale ) ) ) { pending = action::cancel; }
    if( ImGui::GetIO().DisplaySize.y >= 750 * scale ) {
        ImGui::TextDisabled( "单击加入 / 移除 · 下方调整数量 · 滚轮浏览" );
    }
    ImGui::Separator();
    const float height = std::max( 100.0f, ImGui::GetContentRegionAvail().y - 70 * scale );
    const float width = ( ImGui::GetContentRegionAvail().x - 8 * scale ) * .5f;
    pane( trade_ui::_you, width, height );
    ImGui::SameLine();
    pane( trade_ui::_trader, 0, height );
    reset_scroll = false;
    ImGui::Text( "购入 %s    出售 %s", format_money( trade._trade_values[trade_ui::_trader] ).c_str(),
                 format_money( trade._trade_values[trade_ui::_you] ).c_str() );
    const npc &trader = *trade._parties[trade_ui::_trader]->as_npc();
    const auto balance = trader.will_exchange_items_freely() ? "自由交换" :
                         std::string( trade._balance >= 0 ? "对方欠你 " : "你需补足 " ) + format_money( std::abs( trade._balance ) );
    ImGui::SameLine();
    ImGui::TextColored( npc_trading::npc_will_accept_trade( trader, trade._balance ) ? gold : ImVec4( 1, .5f, .35f, 1 ),
                        "%s", balance.c_str() );
    if( ImGui::Button( "清空选择", ImVec2( 120 * scale, 34 * scale ) ) ) { pending = action::clear; }
    if( get_option<bool>( "CAPITALISM" ) ) {
        ImGui::SameLine();
        if( ImGui::Button( "银行结算", ImVec2( 120 * scale, 34 * scale ) ) ) { pending = action::bank; }
        ImGui::SameLine();
        ImGui::Text( "余额 %s", format_money( trade._bank ).c_str() );
    }
    ImGui::SameLine( ImGui::GetWindowWidth() - 156 * scale );
    if( ImGui::Button( "确认交易", ImVec2( 140 * scale, 34 * scale ) ) ) { pending = action::confirm; }
    ImGui::EndDisabled();
    if( open_confirmation ) {
        ImGui::OpenPopup( "交易确认" );
        open_confirmation = false;
    }
    ImGui::SetNextWindowPos( ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2( .5f, .5f ) );
    ImGui::SetNextWindowSize( ImVec2( std::min( 560 * scale, ImGui::GetIO().DisplaySize.x * .85f ), 0 ), ImGuiCond_Appearing );
    if( ImGui::BeginPopupModal( "交易确认", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) ) {
        set_font_scale( scale );
        ImGui::TextWrapped( "%s", message.c_str() );
        ImGui::Separator();
        if( offer_valid ) {
            if( ImGui::Button( "完成交易", ImVec2( 130 * scale, 36 * scale ) ) ) {
                pending = action::accept;
                message.clear();
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
        }
        if( ImGui::Button( "返回调整", ImVec2( 130 * scale, 36 * scale ) ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) {
            message.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}
}
#endif
