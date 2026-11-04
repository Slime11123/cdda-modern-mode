#include "fp3d_modern_ui.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <variant>
#include "avatar.h"
#include "bionics.h"
#include "cata_imgui.h"
#include "character_martial_arts.h"
#include "fp3d_compositor.h"
#include "fp3d_crafting.h"
#include "fp3d_abilities.h"
#include "fp3d_construction.h"
#include "fp3d_journal.h"
#include "fp3d_input.h"
#include "fp3d_materials.h"
#include "fp3d_map.h"
#include "fp3d_hud.h"
#include "catacharset.h"
#include "cata_utility.h"
#include "cursesport.h"
#include "sdltiles.h"
#include "fp3d_snapshot.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "item.h"
#include "itype.h"
#include "output.h"
#include "overmap_ui.h"
#include "bodypart.h"
#include "display.h"
#include "effect.h"
#include "skill.h"
#include "map.h"
#include "martialarts.h"
#include "messages.h"
#include "mutation.h"
#include "ui_manager.h"
#include "units.h"
namespace fp3d_modern_ui
{
namespace
{
bool resume_after_activity = false;
int remembered_page = 0;
int scale_choice = 0;
constexpr int mutations_page = 9;
constexpr int bionics_page = 10;
constexpr int styles_page = 11;

using fp3d_abilities::ability_request;
using fp3d_abilities::bionic_control;
using fp3d_abilities::bionic_request;

class panel : public cataimgui::window
{
        avatar &you;
        map &here;
        fp3d_inventory::model view;
        std::optional<tripoint_abs_ms> pickup_target;
        std::optional<tripoint_abs_ms> crafting_position;
        std::map<std::string, SDL_Texture_Ptr> icons;
        SDL_Texture_Ptr background;
        std::set<size_t> selected;
        std::string mine_search;
        std::string near_search;
        std::string skill_search;
        std::vector<std::pair<std::string, std::string>> needs;
        std::vector<std::pair<std::string, std::string>> effects;
        struct skill_row {
            std::string name;
            std::string description;
            int practical;
            int theory;
            int exercise;
        };
        std::vector<skill_row> skills;
        bool recipes_loaded = false;
        std::vector<fp3d_crafting::entry> recipes;
        std::string recipe_search;
        std::string recipe_skill;
        std::string recipe_details;
        size_t selected_recipe = 0;
        int crafting_batch = 1;
        bool favorite_recipes_only = false;
        std::optional<crafting_category_id> recipe_category;
        std::optional<std::pair<recipe_id, bool>> pending_recipe_favorite;
        bool journal_loaded = false;
        fp3d_journal::model journal;
        int mission_state = 0;
        int selected_mission = -1;
        int selected_person = -1;
        std::string people_search;
        std::string mission_search;
        bool projects_loaded = false;
        std::vector<fp3d_construction::project> projects;
        std::string project_search;
        std::string project_category;
        std::string project_details;
        size_t selected_project = 0;
        int mine_category = 0;
        int near_category = 0;
        bool show_empty = false;
        int near_source = -1;
        int sort_order = 0;
        int small_tab = 1;
        int side_page = 0;
        std::string gear_part;
        bool map_loaded = false;
        fp3d_map::model map_view;
        tripoint_abs_omt map_center;
        std::optional<tripoint_abs_omt> map_selection;
        int map_radius = 12;
        bool show_map_route = true;
        std::optional<tripoint_abs_omt> pending_travel;
        std::optional<tripoint_abs_omt> note_edit_position;
        std::string note_draft;
        std::string note_status;
        bool confirm_note_delete = false;
        std::optional<std::pair<tripoint_abs_omt, std::string>> pending_note;
        int more_section = 0;
        std::optional<ability_request> pending_ability;
        bool ability_in_progress = false;
        bool messages_loaded = false;
        std::vector<std::pair<std::string, std::string>> messages;
        std::vector<std::string> message_previews;
        std::string message_search;
        std::string action_message;
        bool direct_action_completed = false;
        bool activity_pending = false;
        bool native_prompt_open = false;
        size_t selected_message = 0;
        int popup_quantity = 1;
        bool details_open = false;
        bool open_use = false;
        item_location use_item;
        std::vector<fp3d_inventory::use_option> item_uses;
        bool open_details = false;
        std::string details_text;
        float scale = 1;
        std::optional<fp3d_inventory::operation> result;
        void submit( fp3d_inventory::operation_kind kind, size_t index, item_location destination = {},
                     int quantity = 0, std::optional<tripoint_abs_ms> tile = std::nullopt, bool vehicle = false,
                     int pocket_index = -1 ) {
            if( activity_pending ) {
                action_message = "正在转移物品，请稍候。";
                return;
            }
            fp3d_inventory::operation action;
            action.kind = kind;
            action.destination = destination;
            action.destination_tile = tile;
            action.to_vehicle = vehicle;
            action.destination_pocket = pocket_index;
            if( selected.count( index ) && ( kind == fp3d_inventory::operation_kind::pickup ||
                                             kind == fp3d_inventory::operation_kind::drop || kind == fp3d_inventory::operation_kind::insert ||
                                             kind == fp3d_inventory::operation_kind::place ) ) {
                for( size_t id : selected ) {
                    if( ( kind == fp3d_inventory::operation_kind::pickup && !view.entries[id].nearby ) ||
                        ( kind == fp3d_inventory::operation_kind::drop && view.entries[id].nearby ) ) {
                        continue;
                    }
                    auto loc = view.entries[id].location;
                    if( loc ) {
                        action.items.emplace_back( loc, loc->count() );
                    }
                }
            } else {
                auto loc = view.entries[index].location;
                if( loc ) {
                    action.items.emplace_back( loc,
                                               quantity > 0 ? quantity : ImGui::GetIO().KeyShift ? loc->count() : 1 );
                }
            }
            if( kind == fp3d_inventory::operation_kind::insert ) {
                std::string error;
                if( !fp3d_inventory::can_insert( action, you, here, error ) ) {
                    action_message = std::move( error );
                    return;
                }
            }
            const bool held_items = !action.items.empty() &&
                                    std::all_of( action.items.begin(), action.items.end(), [&]( const auto &part ) {
                return part.first.held_by( you );
            } );
            if( kind == fp3d_inventory::operation_kind::drop && !held_items ) {
                action_message = "附近物品不能直接放下，请使用移动到附近来源。";
                return;
            }
            const bool immediate = ( kind == fp3d_inventory::operation_kind::drop && held_items ) ||
                                   kind == fp3d_inventory::operation_kind::insert ||
                                   kind == fp3d_inventory::operation_kind::place ||
                                   kind == fp3d_inventory::operation_kind::pickup ||
                                   kind == fp3d_inventory::operation_kind::wield ||
                                   kind == fp3d_inventory::operation_kind::wear ||
                                   kind == fp3d_inventory::operation_kind::takeoff;
            result = std::move( action );
            if( !immediate ) {
                is_open = false;
            }
        }
        void target( fp3d_inventory::operation_kind kind, item_location destination = {},
                     std::optional<tripoint_abs_ms> tile = std::nullopt, bool vehicle = false,
                     int pocket_index = -1 ) {
            if( ImGui::BeginDragDropTarget() ) {
                if( const auto *payload = ImGui::AcceptDragDropPayload( "FP3D_ITEM" ) ) {
                    const size_t index = *static_cast<const size_t *>( payload->Data );
                    if( index < view.entries.size() ) {
                        submit( kind, index, destination, 0, tile, vehicle, pocket_index );
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }
        size_t container_pocket_count( const item_location &owner ) const {
            return std::count_if( view.groups.begin(), view.groups.end(), [&]( const auto &group ) {
                return group.container == owner && group.pocket_index >= 0;
            } );
        }
        void item_card( size_t index, int depth = 0, bool show_contents = false, bool grid = false ) {
            const auto &entry = view.entries[index];
            auto loc = entry.location;
            if( !loc ) {
                return;
            }
            ImGui::PushID( static_cast<const void *>( &*loc ) );
            const bool expandable_gear = !grid && show_contents && container_pocket_count( loc ) > 0;
            bool gear_expanded = true;
            bool clicked = false;
            if( expandable_gear ) {
                ImGui::PushStyleVar( ImGuiStyleVar_FramePadding, ImVec2( 4 * scale, ( 52 * scale - ImGui::GetFontSize() ) / 2 ) );
                gear_expanded = ImGui::TreeNodeEx( "##item", ImGuiTreeNodeFlags_Framed |
                                                  ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_OpenOnArrow |
                                                  ImGuiTreeNodeFlags_DefaultOpen |
                                                  ( selected.count( index ) ? ImGuiTreeNodeFlags_Selected : 0 ) );
                ImGui::PopStyleVar();
                clicked = ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen();
            } else if( grid ) {
                clicked = ImGui::InvisibleButton( "##item", ImVec2( 80 * scale, 76 * scale ) );
            } else {
                clicked = ImGui::Selectable( "##item", selected.count( index ), ImGuiSelectableFlags_AllowDoubleClick,
                                             ImVec2( 0, 52 * scale ) );
            }
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            auto *draw = ImGui::GetWindowDrawList();
            if( grid ) {
                draw->AddRectFilled( min, max, selected.count( index ) ? IM_COL32( 110, 79, 36, 190 ) :
                                     ImGui::IsItemHovered() ? IM_COL32( 70, 60, 43, 180 ) : IM_COL32( 20, 19, 17, 120 ) );
                draw->AddRect( min, max, IM_COL32( 105, 95, 75, 100 ) );
            }
            const auto key = loc->typeId().str() + ( loc->has_itype_variant() ? loc->itype_variant().id : "" );
            const auto icon = icons.find( key );
            const ImVec2 image_min( min.x + ( grid ? 18 : expandable_gear ? 25 : 4 ) * scale, min.y + 4 * scale );
            if( icon != icons.end() && icon->second ) {
                draw->AddImage( ( ImTextureID )( intptr_t )icon->second.get(), image_min,
                                ImVec2( image_min.x + 44 * scale, image_min.y + 44 * scale ) );
            } else {
                draw->AddText( image_min, ImGui::GetColorU32( ImGuiCol_Text ), "?" );
            }
            const ImVec2 text_min( grid ? min.x + 3 * scale : image_min.x + 52 * scale,
                                   grid ? min.y + 52 * scale : min.y + ( 52 * scale - ImGui::GetFontSize() ) / 2 );
            const int text_width = std::max( 1, static_cast<int>( ( max.x - text_min.x - 4 * scale ) /
                                          ( ImGui::GetFontSize() * .5f ) ) );
            const auto label = utf8_truncate( entry.name, text_width );
            const ImVec4 clip( min.x, min.y, max.x, max.y );
            draw->AddText( ImGui::GetFont(), ImGui::GetFontSize(), text_min,
                           ImGui::GetColorU32( ImGuiCol_Text ), label.c_str(), nullptr, 0, &clip );
            if( ImGui::IsItemClicked( ImGuiMouseButton_Right ) ) {
                ImGui::OpenPopup( "操作" );
            }
            if( clicked ) {
                if( !ImGui::GetIO().KeyShift ) {
                    selected.clear();
                }
                if( !selected.erase( index ) ) {
                    selected.insert( index );
                }
                if( ImGui::IsMouseDoubleClicked( ImGuiMouseButton_Left ) ) {
                    details_text = remove_color_tags( loc->info( true ) );
                    open_details = true;
                }
            }
            if( selected.count( index ) ) {
                ImGui::GetWindowDrawList()->AddRect( min, max, IM_COL32( 220, 170, 85, 255 ), 2, 0, 2 );
            }
            if( loc->count() > 1 ) {
                const auto count = std::to_string( loc->count() );
                ImGui::GetWindowDrawList()->AddText( ImVec2( min.x + 3, min.y + 3 ), IM_COL32_WHITE,
                                                     count.c_str() );
            }
            const float durability = std::clamp( 1 - loc->damage_level() / 4.0f, 0.0f, 1.0f );
            ImGui::GetWindowDrawList()->AddRectFilled( ImVec2( min.x + 2, max.y - 4 ),
                    ImVec2( min.x + 2 + ( max.x - min.x - 4 ) * durability, max.y - 2 ), IM_COL32( 105, 190, 125,
                            230 ) );
            if( ImGui::IsItemHovered() ) {
                ImGui::BeginTooltip();
                ImGui::TextWrapped( "%s", entry.name.c_str() );
                ImGui::Text( "%.2f kg / %.2f L", entry.weight_kg, entry.volume_l );
                ImGui::TextWrapped( "位置：%s", entry.group.c_str() );
                ImGui::Text( "数量 %d / 损坏等级 %d", loc->count(), loc->damage_level() );
                ImGui::TextUnformatted( "右键操作；拖到容器或装备区域；Shift 多选" );
                ImGui::EndTooltip();
            }
            if( ImGui::BeginDragDropSource() ) {
                ImGui::SetDragDropPayload( "FP3D_ITEM", &index, sizeof( index ) );
                ImGui::TextWrapped( "%s", entry.name.c_str() );
                ImGui::EndDragDropSource();
            }
            if( ImGui::BeginPopup( "操作" ) ) {
                using kind = fp3d_inventory::operation_kind;
                ImGui::TextWrapped( "%s", entry.name.c_str() );
                ImGui::Separator();
                if( ImGui::MenuItem( "查看完整详情" ) ) {
                    details_text = remove_color_tags( loc->info( true ) );
                    open_details = true;
                }
                if( ImGui::IsWindowAppearing() ) {
                    popup_quantity = 1;
                }
                if( loc->count() > 1 ) {
                    ImGui::SetNextItemWidth( 120 * scale );
                    ImGui::InputInt( "数量", &popup_quantity );
                    popup_quantity = std::clamp( popup_quantity, 1, loc->count() );
                }
                if( entry.nearby && ImGui::MenuItem( "拿取" ) ) {
                    submit( kind::pickup, index, {}, popup_quantity );
                }
                if( loc.held_by( you ) && ImGui::MenuItem( "放到脚下" ) ) {
                    submit( kind::drop, index, {}, popup_quantity );
                }
                if( you.can_wield( *loc ).success() && ImGui::MenuItem( "持握" ) ) {
                    submit( kind::wield, index );
                }
                if( loc->is_armor() && !you.is_worn( *loc ) && you.can_wear( *loc ).success() &&
                    ImGui::MenuItem( "穿戴" ) ) {
                    submit( kind::wear, index );
                }
                if( you.is_worn( *loc ) && you.can_takeoff( *loc ).success() && ImGui::MenuItem( "脱下" ) ) {
                    submit( kind::takeoff, index );
                }
                if( loc.held_by( you ) && ImGui::BeginMenu( "加入快捷栏（数字键切换手持）" ) ) {
                    for( int slot = 0; slot < 8; ++slot ) {
                        if( ImGui::MenuItem( std::to_string( slot + 1 ).c_str() ) ) {
                            fp3d_inventory::assign_slot( slot, loc );
                        }
                    }
                    ImGui::EndMenu();
                }
                if( ImGui::BeginMenu( "移动到附近来源" ) ) {
                    for( size_t i = 0; i < view.groups.size(); ++i ) {
                        const auto &group = view.groups[i];
                        if( !group.tile ) {
                            continue;
                        }
                        ImGui::PushID( static_cast<int>( i ) );
                        if( ImGui::MenuItem( group.name.c_str() ) ) {
                            submit( kind::place, index, {}, popup_quantity, group.tile, group.vehicle );
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndMenu();
                }
                if( ImGui::MenuItem( "使用…" ) ) {
                    use_item = loc;
                    item_uses = fp3d_inventory::use_options( loc, you, here );
                    open_use = true;
                }
                ImGui::EndPopup();
            }
            if( show_contents && depth < 16 && gear_expanded ) {
                gear_pockets( loc, depth );
            }
            if( expandable_gear && gear_expanded ) {
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        void gear_pockets( const item_location &owner, int depth ) {
            if( !owner || depth >= 16 ) {
                return;
            }
            std::vector<size_t> pockets;
            for( size_t i = 0; i < view.groups.size(); ++i ) {
                if( view.groups[i].container == owner && view.groups[i].pocket_index >= 0 ) {
                    pockets.push_back( i );
                }
            }
            if( pockets.empty() ) {
                return;
            }
            const bool multiple_pockets = pockets.size() > 1;
            for( const size_t group_index : pockets ) {
                const auto &group = view.groups[group_index];
                ImGui::PushID( group.pocket_index );
                const ImVec2 pocket_min = ImGui::GetCursorScreenPos();
                const float pocket_width = ImGui::GetContentRegionAvail().x;
                ImGui::BeginGroup();
                if( !multiple_pockets || ImGui::TreeNodeEx( group.name.c_str(), ImGuiTreeNodeFlags_DefaultOpen ) ) {
                    if( group.capacity_l > 0 ) {
                        ImGui::Text( "%.2f / %.2f L", group.used_l, group.capacity_l );
                    }
                    const bool simple = std::none_of( group.entries.begin(), group.entries.end(), [&]( size_t index ) {
                        return container_pocket_count( view.entries[index].location ) > 0;
                    } );
                    if( simple ) {
                        const int columns = std::max( 1, static_cast<int>( ImGui::GetContentRegionAvail().x / ( 90 * scale ) ) );
                        const int rows = ( group.entries.size() + columns - 1 ) / columns;
                        ImGuiListClipper clipper;
                        clipper.Begin( rows, 82 * scale );
                        while( clipper.Step() ) {
                            for( int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row ) {
                                const float y = ImGui::GetCursorPosY();
                                for( int column = 0; column < columns; ++column ) {
                                    const size_t entry = row * columns + column;
                                    if( entry >= group.entries.size() ) { break; }
                                    if( column ) { ImGui::SameLine(); }
                                    ImGui::BeginGroup();
                                    const auto index = group.entries[entry];
                                    item_card( index, depth + 1, true, true );
                                    ImGui::EndGroup();
                                }
                                ImGui::SetCursorPosY( y + 82 * scale );
                            }
                        }
                    } else {
                        for( size_t index : group.entries ) {
                            item_card( index, depth + 1, true );
                        }
                    }
                    if( group.entries.empty() ) {
                        ImGui::TextDisabled( "空" );
                    }
                    if( multiple_pockets ) { ImGui::TreePop(); }
                }
                ImGui::EndGroup();
                const ImVec2 pocket_max( pocket_min.x + pocket_width, ImGui::GetItemRectMax().y );
                if( ImGui::BeginDragDropTargetCustom( ImRect( pocket_min, pocket_max ), ImGui::GetID( "pocket_drop" ) ) ) {
                    if( const auto *payload = ImGui::AcceptDragDropPayload( "FP3D_ITEM" ) ) {
                        const size_t index = *static_cast<const size_t *>( payload->Data );
                        if( index < view.entries.size() ) {
                            submit( fp3d_inventory::operation_kind::insert, index, group.container, 0,
                                    std::nullopt, false, group.pocket_index );
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
        }
        void groups( bool nearby ) {
            auto &search = nearby ? near_search : mine_search;
            auto &category = nearby ? near_category : mine_category;
            ImGui::PushID( nearby ? "near" : "mine" );
            ImGui::InputText( "搜索", &search );
            ImGui::Combo( "分类", &category,
                          "全部\0武器\0弹药\0食品\0医疗\0衣物\0背包/容器\0工具\0其它\0" );
            if( nearby && !pickup_target ) {
                ImGui::Checkbox( "显示空来源（可拖入）", &show_empty );
                const char *source_name = near_source < 0 ? "全部来源" : view.groups[near_source].name.c_str();
                if( ImGui::BeginCombo( "来源", source_name ) ) {
                    if( ImGui::Selectable( "全部来源", near_source < 0 ) ) {
                        near_source = -1;
                    }
                    for( size_t i = 0; i < view.groups.size(); ++i ) {
                        if( !view.groups[i].nearby ) {
                            continue;
                        }
                        ImGui::PushID( static_cast<int>( i ) );
                        if( ImGui::Selectable( view.groups[i].name.c_str(), near_source == static_cast<int>( i ) ) ) {
                            near_source = i;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndCombo();
                }
            }
            for( size_t group_index = 0; group_index < view.groups.size(); ++group_index ) {
                const auto &group = view.groups[group_index];
                if( group.nearby != nearby || group.pocket_index >= 0 ||
                    ( nearby && pickup_target && group.tile != pickup_target ) ||
                    ( nearby && near_source >= 0 && near_source != static_cast<int>( group_index ) ) ||
                    ( nearby && !pickup_target && !show_empty && group.entries.empty() ) ) {
                    continue;
                }
                ImGui::PushID( static_cast<int>( group_index ) );
                const ImVec2 group_min = ImGui::GetCursorScreenPos();
                const float group_width = ImGui::GetContentRegionAvail().x;
                const bool expanded = ImGui::CollapsingHeader( nearby ? group.name.c_str() : "随身容器",
                                      ImGuiTreeNodeFlags_DefaultOpen );
                if( group.container ) {
                    target( fp3d_inventory::operation_kind::insert, group.container, std::nullopt, false,
                            group.pocket_index );
                }
                if( group.container && ImGui::IsItemHovered() ) {
                    ImGui::SetTooltip( "%s\n拖入此分组时只使用这个口袋；空间不足不会改放其他口袋。",
                                       group.name.c_str() );
                }
                if( group.tile ) {
                    target( fp3d_inventory::operation_kind::place, {}, group.tile, group.vehicle );
                }
                if( !expanded ) {
                    ImGui::PopID();
                    continue;
                }
                if( group.capacity_l > 0 && !group.container ) {
                    ImGui::Text( "%.2f / %.2f L", group.used_l, group.capacity_l );
                }
                if( group.container ) {
                    if( group.capacity_l > 0 ) {
                        ImGui::ProgressBar( std::clamp( group.used_l / group.capacity_l, 0.0f, 1.0f ), ImVec2( -1,
                                            5 * scale ), "" );
                        ImGui::Text( "%.2f / %.2f L", group.used_l, group.capacity_l );
                    }
                }
                if( group.container ) {
                    ImGui::Text( "重量 %.2f / %.2f kg", group.used_kg, group.capacity_kg );
                    if( group.max_length_cm > 0 ) {
                        ImGui::Text( "最长 %.0f cm", group.max_length_cm );
                    }
                    if( group.sealed ) {
                        ImGui::TextDisabled( "已密封" );
                    }
                }
                std::vector<size_t> indices;
                for( size_t index : group.entries ) {
                    const auto &entry = view.entries[index];
                    if( !nearby && entry.equipped && container_pocket_count( entry.location ) == 0 ) {
                        continue;
                    }
                    if( ( category == 0 || entry.category == category ) &&
                        lcmatch( entry.name, search ) ) {
                        indices.push_back( index );
                    }
                }
                std::stable_sort( indices.begin(), indices.end(), [&]( size_t a, size_t b ) {
                    if( sort_order == 1 ) {
                        return view.entries[a].weight_kg > view.entries[b].weight_kg;
                    }
                    if( sort_order == 2 ) {
                        return view.entries[a].volume_l > view.entries[b].volume_l;
                    }
                    return view.entries[a].name < view.entries[b].name;
                } );
                const int columns = std::max( 1,
                                              static_cast<int>( ImGui::GetContentRegionAvail().x / ( 90 * scale ) ) );
                int column = 0;
                for( const size_t index : indices ) {
                    const bool has_pockets = container_pocket_count( view.entries[index].location ) > 0;
                    if( has_pockets ) {
                        if( column != 0 ) {
                            ImGui::NewLine();
                            column = 0;
                        }
                        item_card( index, 0, true );
                        ImGui::NewLine();
                        continue;
                    }
                    if( column != 0 ) {
                        ImGui::SameLine();
                    }
                    ImGui::BeginGroup();
                    item_card( index, 0, false, true );
                    ImGui::EndGroup();
                    column = ( column + 1 ) % columns;
                }
                if( nearby && !indices.empty() && ImGui::SmallButton( "拿取本组全部物品" ) ) {
                    fp3d_inventory::operation action;
                    action.kind = fp3d_inventory::operation_kind::pickup;
                    for( size_t id : indices ) {
                        const auto loc = view.entries[id].location;
                        if( loc ) {
                            action.items.emplace_back( loc, loc->count() );
                        }
                    }
                    result = std::move( action );
                }
                if( indices.empty() ) {
                    ImGui::TextDisabled( "没有匹配物品" );
                }
                // Include the header, item grid and remaining space in its last row.
                const ImVec2 group_max( group_min.x + group_width,
                                        std::max( ImGui::GetCursorScreenPos().y, ImGui::GetItemRectMax().y ) );
                if( group.tile && ImGui::BeginDragDropTargetCustom( ImRect( group_min, group_max ),
                        ImGui::GetID( "source_drop" ) ) ) {
                    if( const auto *payload = ImGui::AcceptDragDropPayload( "FP3D_ITEM" ) ) {
                        const size_t index = *static_cast<const size_t *>( payload->Data );
                        if( index < view.entries.size() ) {
                            submit( fp3d_inventory::operation_kind::place, index, {}, 0, group.tile, group.vehicle );
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        void body_equipment() {
            const float width = ( ImGui::GetContentRegionAvail().x - 16 * scale ) / 3;
            if( ImGui::BeginTable( "装备部位示意", 3, ImGuiTableFlags_SizingStretchSame ) ) {
                const std::pair<const char *, const char *> parts[] = {
                    { "", "" }, { "head", "头部" }, { "", "" },
                    { "arm_l", "左臂" }, { "torso", "躯干" }, { "arm_r", "右臂" },
                    { "leg_l", "左腿" }, { "", "" }, { "leg_r", "右腿" }
                };
                for( const auto &part : parts ) {
                    ImGui::TableNextColumn();
                    if( !*part.first || !you.get_body().count( bodypart_str_id( part.first ) ) ) {
                        ImGui::Dummy( ImVec2( width, 39 * scale ) );
                        continue;
                    }
                    ImGui::PushID( part.first );
                    if( gear_part == part.first ) {
                        ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( .22f, .4f, .45f, 1 ) );
                    }
                    const bool selected = gear_part == part.first;
                    if( ImGui::Button( part.second, ImVec2( width, 30 * scale ) ) ) {
                        gear_part = part.first;
                    }
                    if( selected ) {
                        ImGui::PopStyleColor();
                    }
                    target( fp3d_inventory::operation_kind::wear );
                    if( ImGui::IsItemHovered() ) {
                        ImGui::SetTooltip( "点击筛选覆盖此部位的衣物；拖入装备时仍由原版决定实际穿戴部位与层次。" );
                    }
                    const auto id = bodypart_str_id( part.first ).id();
                    const int maximum = you.get_part_hp_max( id );
                    ImGui::ProgressBar( maximum > 0 ? you.get_part_hp_cur( id ) / static_cast<float>( maximum ) : 0,
                                        ImVec2( width, 5 * scale ), "" );
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            const auto selected = gear_part.empty() ? "全身" : body_part_name( bodypart_str_id(
                                      gear_part ).id() );
            ImGui::TextUnformatted( "覆盖部位" );
            ImGui::SetNextItemWidth( -1 );
            if( ImGui::BeginCombo( "##coverage", selected.c_str() ) ) {
                if( ImGui::Selectable( "全身", gear_part.empty() ) ) {
                    gear_part.clear();
                }
                for( const auto &part : you.get_all_body_parts() ) {
                    if( ImGui::Selectable( body_part_name( part ).c_str(), gear_part == part.id().str() ) ) {
                        gear_part = part.id().str();
                    }
                }
                ImGui::EndCombo();
            }
        }
        void gear() {
            if( side_page != 1 ) {
                ImGui::TextUnformatted( "主手 / 双手" );
                ImGui::Button( "拖到这里持握", ImVec2( -1, 38 * scale ) );
                target( fp3d_inventory::operation_kind::wield );
                for( size_t i = 0; i < view.entries.size(); ++i ) {
                    const auto &entry = view.entries[i];
                    if( entry.equipped && entry.location && !you.is_worn( *entry.location ) ) {
                        item_card( i );
                    }
                }
            }
            ImGui::TextUnformatted( "衣物与穿戴层" );
            body_equipment();
            ImGui::Button( "拖到这里穿戴", ImVec2( -1, 38 * scale ) );
            target( fp3d_inventory::operation_kind::wear );
            bool any = false;
            for( size_t i = 0; i < view.entries.size(); ++i ) {
                const auto &entry = view.entries[i];
                if( !entry.equipped || !entry.location || !you.is_worn( *entry.location ) ||
                    ( !gear_part.empty() && !entry.location->covers( bodypart_str_id( gear_part ).id() ) ) ) {
                    continue;
                }
                any = true;
                item_card( i );
            }
            if( !any ) {
                ImGui::TextDisabled( "此部位没有穿戴物品" );
            }
            ImGui::Separator();
            ImGui::Text( "携带 %.1f / %.1f kg", units::to_gram( you.weight_carried() ) / 1000.0,
                         units::to_gram( you.weight_capacity() ) / 1000.0 );
            ImGui::Text( "耐力 %s", fp3d_hud::stamina_text( you ).c_str() );
        }
        void map_details() {
            ImGui::TextUnformatted( "选中位置" );
            const auto selected = std::find_if( map_view.cells.begin(), map_view.cells.end(),
            [&]( const auto & row ) {
                return map_selection && row.position == *map_selection;
            } );
            if( selected != map_view.cells.end() ) {
                ImGui::TextWrapped( "%s", selected->name.c_str() );
                ImGui::TextWrapped( "%d, %d / 层 %d", selected->position.x(), selected->position.y(),
                                    selected->position.z() );
                if( !selected->note.empty() ) {
                    ImGui::TextWrapped( "标记：%s", selected->note.c_str() );
                }
                if( selected->route ) {
                    ImGui::TextUnformatted( "原版旅行路线经过此处" );
                }
                if( note_edit_position != map_selection ) {
                    note_edit_position = map_selection;
                    note_draft = selected->note_text;
                    note_status.clear();
                    confirm_note_delete = false;
                }
                ImGui::Separator();
                ImGui::TextUnformatted( "地点标记" );
                ImGui::BeginDisabled( !selected->editable );
                ImGui::InputTextMultiline( "##标记内容", &note_draft,
                                           ImVec2( -1, 90 * scale ) );
                ImGui::BeginDisabled( note_draft.empty() );
                if( ImGui::Button( "保存标记" ) ) {
                    pending_note = std::make_pair( selected->position, note_draft );
                    confirm_note_delete = false;
                }
                ImGui::EndDisabled();
                ImGui::BeginDisabled( selected->note_text.empty() );
                if( ImGui::Button( "删除标记" ) ) {
                    confirm_note_delete = true;
                }
                ImGui::EndDisabled();
                if( confirm_note_delete ) {
                    ImGui::TextWrapped( "删除此位置的标记？" );
                    if( ImGui::Button( "确认删除" ) ) {
                        pending_note = std::make_pair( selected->position, std::string() );
                        confirm_note_delete = false;
                    }
                    if( ImGui::Button( "保留标记" ) ) { confirm_note_delete = false; }
                }
                ImGui::EndDisabled();
                if( !selected->editable ) {
                    ImGui::TextWrapped( "此区域尚未载入，暂不能添加标记。" );
                }
                if( !note_status.empty() ) { ImGui::TextWrapped( "%s", note_status.c_str() ); }
                ImGui::TextWrapped( "编辑后点击保存；切换位置会放弃未保存的修改。" );
                ImGui::Separator();
                const bool can_travel = selected->known && selected->position.z() == you.posz() &&
                                        selected->position != map_view.player;
                ImGui::BeginDisabled( !can_travel );
                if( ImGui::Button( fp3d_input::vehicle_controls() ? "驾车前往此处…" : "步行前往此处…" ) ) {
                    pending_travel = selected->position;
                }
                ImGui::EndDisabled();
                ImGui::TextWrapped( "选择同层已知地点后，按原版确认并开始旅行。" );
            }
            ImGui::Separator();
            ImGui::TextUnformatted( "当前目的地" );
            if( map_view.objective ) {
                const auto &p = *map_view.objective;
                ImGui::TextWrapped( "%d, %d / 层 %d", p.x(), p.y(), p.z() );
                if( ImGui::Button( "定位目的地" ) ) {
                    map_center = p;
                    map_selection = p;
                    map_loaded = false;
                }
            } else {
                ImGui::TextDisabled( "无旅行终点或任务目标" );
            }
            bool navigation = fp3d_hud::navigation_enabled();
            if( ImGui::Checkbox( "显示目的地方向", &navigation ) ) {
                fp3d_hud::set_navigation_enabled( navigation );
            }
            ImGui::TextWrapped( "只展示原版已知地形与标记；黄色为标记，蓝色为路线，橙框为目的地，青框为玩家。" );
            compatible( ACTION_MAP, "旅行 / 完整地图（原版）" );
        }
        void map_page() {
            if( !map_loaded ) {
                if( !map_selection ) {
                    map_center = you.pos_abs_omt();
                    map_selection = map_center;
                }
                map_view = fp3d_map::capture( you, map_center, map_radius );
                map_center = map_view.center;
                map_loaded = true;
            }
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = std::max( 100.0f, ImGui::GetContentRegionAvail().y - 85 * scale );
            const bool compact = width < 1100 * scale;
            const float side = width * ( compact ? .27f : .21f );
            ImGui::BeginChild( "地图标记", ImVec2( side, height ), true );
            ImGui::TextUnformatted( "地图范围" );
            ImGui::SetNextItemWidth( -1 );
            const char *ranges[] = { "附近 17×17", "周边 25×25", "远处 37×37" };
            const int radii[] = { 8, 12, 18 };
            int zoom = map_radius == 8 ? 0 : map_radius == 18 ? 2 : 1;
            if( ImGui::Combo( "##地图范围", &zoom, ranges, 3 ) ) {
                map_radius = radii[zoom];
                map_loaded = false;
            }
            if( ImGui::Button( "回到玩家" ) ) {
                map_center = you.pos_abs_omt();
                map_selection = map_center;
                map_loaded = false;
            }
            const auto pan = [&]( int x, int y, int z ) {
                map_center += tripoint_rel_omt( x, y, z );
                map_selection = map_center;
                map_loaded = false;
            };
            if( ImGui::SmallButton( "北" ) ) {
                pan( 0, -map_radius, 0 );
            }
            if( ImGui::SmallButton( "西" ) ) {
                pan( -map_radius, 0, 0 );
            }
            ImGui::SameLine();
            if( ImGui::SmallButton( "东" ) ) {
                pan( map_radius, 0, 0 );
            }
            if( ImGui::SmallButton( "南" ) ) {
                pan( 0, map_radius, 0 );
            }
            if( ImGui::SmallButton( "上层" ) ) {
                pan( 0, 0, 1 );
            }
            ImGui::SameLine();
            if( ImGui::SmallButton( "下层" ) ) {
                pan( 0, 0, -1 );
            }
            ImGui::Checkbox( "显示路线", &show_map_route );
            ImGui::Separator();
            ImGui::TextUnformatted( "此范围的标记" );
            int notes = 0;
            for( size_t i = 0; i < map_view.cells.size(); ++i ) {
                const auto &row = map_view.cells[i];
                if( row.note.empty() ) {
                    continue;
                }
                ++notes;
                ImGui::PushID( static_cast<int>( i ) );
                if( ImGui::Selectable( "##note", map_selection && row.position == *map_selection ) ) {
                    map_selection = row.position;
                }
                const ImVec2 note_min = ImGui::GetItemRectMin();
                const ImVec2 note_max = ImGui::GetItemRectMax();
                const ImVec4 note_clip( note_min.x, note_min.y, note_max.x, note_max.y );
                ImGui::GetWindowDrawList()->AddText( ImGui::GetFont(), ImGui::GetFontSize(), note_min,
                                                    ImGui::GetColorU32( ImGuiCol_Text ), row.note.c_str(), nullptr, 0, &note_clip );
                if( ImGui::IsItemHovered() ) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos( 300 * scale );
                    ImGui::TextUnformatted( row.note.c_str() );
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::PopID();
            }
            if( notes == 0 ) {
                ImGui::TextDisabled( "没有标记" );
            }
            if( compact ) {
                ImGui::Separator();
                map_details();
            }
            ImGui::EndChild();
            ImGui::SameLine();
            const float canvas_width = compact ? 0 : ImGui::GetContentRegionAvail().x - side - 8;
            ImGui::BeginChild( "地图画布", ImVec2( canvas_width, height ), true,
                               ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse );
            ImGui::Text( "北在上方 / 层 %d", map_view.center.z() );
            const auto available = ImGui::GetContentRegionAvail();
            const int diameter = map_view.radius * 2 + 1;
            const float tile = std::max( 1.0f, std::min( available.x, available.y ) / diameter );
            const float extent = tile * diameter;
            ImVec2 origin = ImGui::GetCursorScreenPos();
            origin.x += std::max( 0.0f, ( available.x - extent ) * .5f );
            ImGui::SetCursorScreenPos( origin );
            ImGui::InvisibleButton( "##map_cells", ImVec2( extent, extent ) );
            if( ImGui::IsItemHovered() ) {
                const auto mouse = ImGui::GetIO().MousePos;
                const int x = std::clamp( static_cast<int>( ( mouse.x - origin.x ) / tile ), 0, diameter - 1 );
                const int y = std::clamp( static_cast<int>( ( mouse.y - origin.y ) / tile ), 0, diameter - 1 );
                const auto &row = map_view.cells[y * diameter + x];
                if( ImGui::IsItemClicked() ) {
                    map_selection = row.position;
                }
                ImGui::BeginTooltip();
                ImGui::PushTextWrapPos( 300 * scale );
                ImGui::TextUnformatted( row.name.c_str() );
                ImGui::Text( "%d, %d / 层 %d", row.position.x(), row.position.y(), row.position.z() );
                if( !row.note.empty() ) {
                    ImGui::TextWrapped( "%s", row.note.c_str() );
                }
                ImGui::PopTextWrapPos();
                ImGui::EndTooltip();
            }
            auto *draw = ImGui::GetWindowDrawList();
            for( size_t i = 0; i < map_view.cells.size(); ++i ) {
                const auto &row = map_view.cells[i];
                const ImVec2 start( origin.x + ( i % diameter ) * tile, origin.y + ( i / diameter ) * tile );
                const ImVec2 end( start.x + tile - 1, start.y + tile - 1 );
                const ImVec2 middle( start.x + tile * .5f, start.y + tile * .5f );
                const auto &pair = cata_cursesport::colorpairs[row.color.to_color_pair_index()];
                const auto &rgb = windowsPalette[static_cast<int>( pair.FG ) + ( row.color.is_bold() ? 8 : 0 )];
                const ImVec4 foreground( rgb.r / 255.0f, rgb.g / 255.0f, rgb.b / 255.0f, 1 );
                draw->AddRectFilled( start, end, row.known ? ImGui::ColorConvertFloat4ToU32(
                                         ImVec4( foreground.x * .3f, foreground.y * .3f, foreground.z * .3f, 1 ) ) :
                                     IM_COL32( 15, 20, 26, 255 ) );
                if( !row.symbol.empty() && tile >= ImGui::GetFontSize() * .8f &&
                    ImGui::GetFont()->FindGlyphNoFallback( static_cast<ImWchar>( UTF8_getch( row.symbol ) ) ) ) {
                    const auto size = ImGui::CalcTextSize( row.symbol.c_str() );
                    draw->AddText( ImVec2( middle.x - size.x * .5f, middle.y - size.y * .5f ),
                                   ImGui::ColorConvertFloat4ToU32( foreground ), row.symbol.c_str() );
                }
                if( show_map_route && row.route ) {
                    draw->AddRectFilled( ImVec2( middle.x - 2, middle.y - 2 ),
                                         ImVec2( middle.x + 2, middle.y + 2 ), IM_COL32( 70, 150, 255, 255 ) );
                }
                if( !row.note.empty() ) {
                    draw->AddRectFilled( start, ImVec2( start.x + 4, start.y + 4 ), IM_COL32( 255, 220, 50, 255 ) );
                }
                if( row.objective || row.position == map_view.player ||
                    ( map_selection && row.position == *map_selection ) ) {
                    draw->AddRect( start, end, row.objective ? IM_COL32( 255, 160, 50, 255 ) :
                                   row.position == map_view.player ? IM_COL32( 70, 210, 255, 255 ) :
                                   IM_COL32( 255, 255, 255, 255 ), 0, 0, 2 );
                }
            }
            ImGui::EndChild();
            if( !compact ) {
                ImGui::SameLine();
                ImGui::BeginChild( "地图目标", ImVec2( 0, height ), true );
                map_details();
                ImGui::EndChild();
            }
        }
        void construction() {
            if( !projects_loaded ) {
                const auto previous = selected_project < projects.size() ? projects[selected_project].id :
                                      construction_group_str_id();
                projects = fp3d_construction::catalogue();
                const auto found = std::find_if( projects.begin(), projects.end(), [&]( const auto &entry ) {
                    return entry.id == previous;
                } );
                selected_project = found != projects.end() ? static_cast<size_t>( found - projects.begin() ) : 0;
                project_details = projects.empty() ? std::string() :
                                  fp3d_construction::details( projects[selected_project].id, &you );
                projects_loaded = true;
            }
            const float height = std::max( 100.0f, ImGui::GetContentRegionAvail().y - 85 * scale );
            const float width = ImGui::GetContentRegionAvail().x;
            const bool category_sidebar = width >= 1100 * scale;
            const float category_width = category_sidebar ? std::clamp( width * .16f, 130 * scale,
                                         190 * scale ) : 0;
            const float remaining = width - ( category_sidebar ? category_width +
                                             ImGui::GetStyle().ItemSpacing.x : 0 );
            const auto category_options = [&]() {
                if( ImGui::Selectable( "全部", project_category.empty() ) ) {
                    project_category.clear();
                }
                std::set<std::string> categories;
                for( const auto &entry : projects ) {
                    categories.insert( entry.category );
                }
                for( const auto &category : categories ) {
                    if( ImGui::Selectable( category.c_str(), project_category == category ) ) {
                        project_category = category;
                    }
                    if( ImGui::IsItemHovered() ) {
                        ImGui::SetTooltip( "%s", category.c_str() );
                    }
                }
            };
            if( category_sidebar ) {
                ImGui::BeginChild( "建造分类", ImVec2( category_width, height ), true );
                ImGui::TextUnformatted( "建造分类" );
                ImGui::Separator();
                category_options();
                ImGui::EndChild();
                ImGui::SameLine();
            }
            ImGui::BeginChild( "建造项目", ImVec2( remaining * .46f, height ), true );
            ImGui::SetNextItemWidth( -1 );
            ImGui::InputTextWithHint( "##建造搜索", "搜索建造项目", &project_search );
            if( !category_sidebar ) {
                ImGui::SetNextItemWidth( -1 );
                if( ImGui::BeginCombo( "##建造分类",
                                      project_category.empty() ? "全部" : project_category.c_str() ) ) {
                    category_options();
                    ImGui::EndCombo();
                }
                if( ImGui::IsItemHovered() ) {
                    ImGui::SetTooltip( "建造分类" );
                }
            }
            bool selected_visible = false;
            std::optional<size_t> first;
            for( size_t i = 0; i < projects.size(); ++i ) {
                const auto &entry = projects[i];
                if( !lcmatch( entry.name, project_search ) ||
                    ( !project_category.empty() && entry.category != project_category ) ) {
                    continue;
                }
                if( !first ) {
                    first = i;
                }
                ImGui::PushID( static_cast<int>( i ) );
                if( ImGui::Selectable( entry.name.c_str(), selected_project == i ) ) {
                    selected_project = i;
                    project_details = fp3d_construction::details( entry.id, &you );
                }
                selected_visible = selected_visible || selected_project == i;
                if( ImGui::IsItemHovered() ) {
                    ImGui::SetTooltip( "%s", entry.name.c_str() );
                }
                ImGui::PopID();
            }
            if( !selected_visible && first ) {
                selected_project = *first;
                project_details = fp3d_construction::details( projects[*first].id, &you );
            }
            if( !first ) {
                ImGui::TextUnformatted( "没有匹配的建造项目。" );
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild( "建造说明", ImVec2( 0, height ), true );
            if( first ) {
                const auto &entry = projects[selected_project];
                ImGui::TextWrapped( "%s / %s", entry.name.c_str(), entry.category.c_str() );
                ImGui::TextWrapped( "位置、材料、技能、光照及所属势力沿用原版检查。" );
                if( ImGui::Button( "选择位置并预览" ) ) {
                    fp3d_inventory::operation request;
                    request.kind = fp3d_inventory::operation_kind::native;
                    request.native_action = ACTION_CONSTRUCT;
                    request.construction_group = entry.id;
                    result = std::move( request );
                    is_open = false;
                }
                ImGui::Separator();
                ImGui::TextWrapped( "%s", project_details.c_str() );
            }
            ImGui::EndChild();
            compatible( ACTION_CONSTRUCT, "完整建造菜单（原版兼容）" );
        }
        void journal_page( bool people ) {
            if( !journal_loaded ) {
                journal = fp3d_journal::capture( you, get_map() );
                journal_loaded = true;
            }
            const float height = ImGui::GetContentRegionAvail().y - 85 * scale;
            if( people ) {
                const float width = ImGui::GetContentRegionAvail().x;
                const bool compact = width < 1250 * scale;
                ImGui::BeginChild( "可见人物", ImVec2( width * .29f, height ), true );
                ImGui::SetNextItemWidth( -1 );
                ImGui::InputTextWithHint( "##人物搜索", "搜索可见人物", &people_search );
                if( ImGui::Button( "刷新人物列表" ) ) { journal_loaded = false; }
                std::vector<const fp3d_journal::person_entry *> filtered;
                const fp3d_journal::person_entry *person = nullptr;
                for( const auto &entry : journal.people ) {
                    if( !lcmatch( entry.name, people_search ) ) {
                        continue;
                    }
                    filtered.push_back( &entry );
                    if( entry.id == selected_person ) {
                        person = &entry;
                    }
                }
                if( !person && !filtered.empty() ) {
                    person = filtered.front();
                    selected_person = person->id;
                }
                for( const auto *entry : filtered ) {
                    ImGui::PushID( entry->id );
                    if( ImGui::Selectable( entry->name.c_str(), selected_person == entry->id ) ) {
                        selected_person = entry->id;
                        person = entry;
                    }
                    if( ImGui::IsItemHovered() ) {
                        ImGui::SetTooltip( "%s", entry->name.c_str() );
                    }
                    ImGui::TextDisabled( "%d 格 / %s", entry->distance,
                                         entry->following ? "正在跟随" : entry->ally ? "队友" : "尚未入队" );
                    ImGui::PopID();
                }
                if( filtered.empty() ) {
                    ImGui::TextWrapped( "没有匹配的当前同层可见人物。" );
                }
                ImGui::EndChild();
                const auto actions = [&]() {
                    ImGui::TextUnformatted( "人物操作" );
                    if( !person ) {
                        ImGui::TextDisabled( "先从左侧选择人物" );
                        return;
                    }
                    ImGui::BeginDisabled( person->distance > 1 );
                    if( ImGui::Button( "与此人物交谈…" ) ) {
                        if( fp3d_journal::dialogue_target( you, here, person->id, person->position ) ) {
                            fp3d_inventory::operation request;
                            request.kind = fp3d_inventory::operation_kind::native;
                            request.native_action = ACTION_CHAT;
                            request.talking_npc_id = person->id;
                            request.destination_tile = person->position;
                            result = std::move( request );
                            is_open = false;
                        } else {
                            action_message = "此人物已移动或当前无法交谈，请刷新队友页。";
                            journal_loaded = false;
                        }
                    }
                    ImGui::EndDisabled();
                    if( person->distance > 1 ) {
                        ImGui::TextWrapped( "靠近到相邻一格后可交谈。" );
                    }
                    for( const auto order : {
                             npc_orders::kind::guard, npc_orders::kind::follow
                         } ) {
                        ImGui::BeginDisabled( order == npc_orders::kind::guard ? !person->can_guard : !person->can_follow );
                        if( ImGui::Button( order == npc_orders::kind::guard ? "原地守卫" : "继续跟随" ) ) {
                            fp3d_inventory::operation request;
                            request.kind = fp3d_inventory::operation_kind::follower_order;
                            request.talking_npc_id = person->id;
                            request.destination_tile = person->position;
                            request.follower_command = order;
                            result = std::move( request );
                        }
                        ImGui::EndDisabled();
                    }
                    ImGui::TextWrapped( "守卫/跟随沿用原版听力、喊话与行动消耗。交易和招募仍由原版对话确认。" );
                };
                ImGui::SameLine();
                ImGui::BeginChild( "人物详情", ImVec2( compact ? 0 : width * .43f, height ), true );
                if( person ) {
                    ImGui::TextWrapped( "%s", person->name.c_str() );
                    ImGui::Separator();
                    ImGui::TextWrapped( "态度：%s", person->attitude.c_str() );
                    ImGui::TextWrapped( "距离：%d 格 / 楼层：%d", person->distance, person->position.z() );
                    ImGui::TextWrapped( "关系：%s", person->ally ? "队友" : "尚未入队" );
                    ImGui::TextWrapped( "跟随状态：%s", person->following ? "正在跟随" : "当前没有跟随" );
                } else {
                    ImGui::TextWrapped( "这里只列出角色当前可以看见的同层人物。" );
                }
                if( compact ) {
                    ImGui::Separator();
                    actions();
                }
                ImGui::EndChild();
                if( !compact ) {
                    ImGui::SameLine();
                    ImGui::BeginChild( "队友操作", ImVec2( 0, height ), true );
                    actions();
                    ImGui::EndChild();
                }
                compatible( ACTION_CHAT, "对话与队友指令（原版确认）" );
                return;
            }
            const float width = ImGui::GetContentRegionAvail().x;
            const bool compact = width < 1250 * scale;
            ImGui::BeginChild( "任务列表", ImVec2( width * .29f, height ), true );
            ImGui::TextUnformatted( "任务状态" );
            ImGui::SetNextItemWidth( -1 );
            ImGui::Combo( "##任务状态", &mission_state, "进行中\0已完成\0失败\0" );
            ImGui::SetNextItemWidth( -1 );
            ImGui::InputTextWithHint( "##任务搜索", "搜索任务", &mission_search );
            if( ImGui::Button( "刷新任务列表" ) ) { journal_loaded = false; }
            std::vector<const fp3d_journal::mission_entry *> filtered;
            const fp3d_journal::mission_entry *mission = nullptr;
            for( const auto &entry : journal.missions ) {
                if( entry.state != mission_state || !lcmatch( entry.name, mission_search ) ) {
                    continue;
                }
                filtered.push_back( &entry );
                if( entry.id == selected_mission ) {
                    mission = &entry;
                }
            }
            if( !mission && !filtered.empty() ) {
                mission = filtered.front();
                selected_mission = mission->id;
            }
            for( const auto *entry : filtered ) {
                ImGui::PushID( entry->id );
                if( ImGui::Selectable( ( ( entry->tracked ? "[追踪] " : "" ) + entry->name ).c_str(),
                                       selected_mission == entry->id ) ) {
                    selected_mission = entry->id;
                    mission = entry;
                }
                if( ImGui::IsItemHovered() ) {
                    ImGui::SetTooltip( "%s", entry->name.c_str() );
                }
                ImGui::PopID();
            }
            if( filtered.empty() ) {
                ImGui::TextUnformatted( "没有匹配的任务。" );
            }
            ImGui::EndChild();
            const auto actions = [&]() {
                ImGui::TextUnformatted( "任务操作" );
                if( !mission ) {
                    ImGui::TextWrapped( "先从左侧选择任务。" );
                    return;
                }
                if( mission->tracked ) {
                    ImGui::TextWrapped( "正在追踪此任务。" );
                } else if( mission->state != 0 ) {
                    ImGui::TextWrapped( "仅进行中的任务可以设为追踪目标。" );
                } else if( ImGui::Button( "追踪此任务" ) ) {
                    fp3d_inventory::operation request;
                    request.kind = fp3d_inventory::operation_kind::native;
                    request.native_action = ACTION_MISSIONS;
                    request.tracking_mission = mission->id;
                    result = std::move( request );
                    is_open = false;
                }
            };
            ImGui::SameLine();
            ImGui::BeginChild( "任务详情", ImVec2( compact ? 0 : width * .43f, height ), true );
            if( mission ) {
                ImGui::TextWrapped( "%s", mission->name.c_str() );
                ImGui::TextWrapped( "%s", mission->destination.c_str() );
                if( !mission->deadline.empty() ) {
                    ImGui::TextWrapped( "%s", mission->deadline.c_str() );
                }
                ImGui::Separator();
                ImGui::TextWrapped( "%s", mission->description.c_str() );
            } else {
                ImGui::TextWrapped( "从左侧选择任务后查看目标、期限与说明。" );
            }
            if( compact ) {
                ImGui::Separator();
                actions();
            }
            ImGui::EndChild();
            if( !compact ) {
                ImGui::SameLine();
                ImGui::BeginChild( "任务操作", ImVec2( 0, height ), true );
                actions();
                ImGui::EndChild();
            }
            compatible( ACTION_MISSIONS, "完整任务管理（原版兼容）" );
        }
        void crafting() {
            if( crafting_position ) {
                ImGui::TextWrapped( "简易制作 · %s", here.name( here.get_bub( *crafting_position ) ).c_str() );
            }
            if( !recipes_loaded ) {
                const recipe_id previous = selected_recipe < recipes.size() ? recipes[selected_recipe].id : recipe_id();
                recipes = fp3d_crafting::capture( you );
                const auto found = std::find_if( recipes.begin(), recipes.end(), [&]( const auto &entry ) {
                    return entry.id == previous;
                } );
                selected_recipe = found != recipes.end() ? static_cast<size_t>( found - recipes.begin() ) : 0;
                recipe_details = recipes.empty() ? std::string() :
                                 fp3d_crafting::details( recipes[selected_recipe].id, you, crafting_batch );
                recipes_loaded = true;
            }
            const float height = std::max( 100.0f, ImGui::GetContentRegionAvail().y - 85 * scale );
            const float width = ImGui::GetContentRegionAvail().x;
            const bool category_sidebar = width >= 1100 * scale;
            const float category_width = category_sidebar ? std::clamp( width * .16f, 130 * scale,
                                         190 * scale ) : 0;
            const float remaining = width - ( category_sidebar ? category_width +
                                             ImGui::GetStyle().ItemSpacing.x : 0 );
            std::map<crafting_category_id, std::string> categories;
            for( const auto &entry : recipes ) {
                categories.emplace( entry.category, entry.category_name );
            }
            const auto category_options = [&]() {
                if( ImGui::Selectable( "全部", !recipe_category ) ) {
                    recipe_category.reset();
                }
                for( const auto &[id, name] : categories ) {
                    ImGui::PushID( id.str().c_str() );
                    if( ImGui::Selectable( name.c_str(), recipe_category && *recipe_category == id ) ) {
                        recipe_category = id;
                    }
                    if( ImGui::IsItemHovered() ) {
                        ImGui::SetTooltip( "%s", name.c_str() );
                    }
                    ImGui::PopID();
                }
            };
            if( category_sidebar ) {
                ImGui::BeginChild( "配方分类", ImVec2( category_width, height ), true );
                ImGui::TextUnformatted( "配方分类" );
                ImGui::Separator();
                category_options();
                ImGui::EndChild();
                ImGui::SameLine();
            }
            ImGui::BeginChild( "配方", ImVec2( remaining * .53f, height ), true );
            if( !category_sidebar ) {
                const auto category = recipe_category ? categories.find( *recipe_category ) : categories.end();
                const char *name = !recipe_category ? "全部" :
                                   category != categories.end() ? category->second.c_str() : "无匹配分类";
                ImGui::SetNextItemWidth( -1 );
                if( ImGui::BeginCombo( "##配方分类", name ) ) {
                    category_options();
                    ImGui::EndCombo();
                }
                if( ImGui::IsItemHovered() ) {
                    ImGui::SetTooltip( "配方分类" );
                }
            }
            ImGui::TextWrapped( "已知、书籍与队友可用配方：%zu", recipes.size() );
            ImGui::SetNextItemWidth( -1 );
            ImGui::InputTextWithHint( "##配方搜索", "搜索配方", &recipe_search );
            ImGui::Checkbox( "仅显示收藏", &favorite_recipes_only );
            if( ImGui::BeginCombo( "主技能", recipe_skill.empty() ? "全部" : recipe_skill.c_str() ) ) {
                if( ImGui::Selectable( "全部", recipe_skill.empty() ) ) {
                    recipe_skill.clear();
                }
                std::set<std::string> choices;
                for( const auto &row : recipes ) {
                    choices.insert( row.skill );
                }
                for( const auto &choice : choices ) {
                    if( ImGui::Selectable( choice.c_str(), recipe_skill == choice ) ) {
                        recipe_skill = choice;
                    }
                }
                ImGui::EndCombo();
            }
            std::vector<size_t> filtered;
            for( size_t i = 0; i < recipes.size(); ++i ) {
                if( lcmatch( recipes[i].name, recipe_search ) &&
                    ( recipe_skill.empty() || recipes[i].skill == recipe_skill ) &&
                    ( !favorite_recipes_only || fp3d_crafting::is_favorite( recipes[i].id ) ) &&
                    ( !recipe_category || recipes[i].category == *recipe_category ) ) {
                    filtered.push_back( i );
                }
            }
            if( !filtered.empty() && std::find( filtered.begin(), filtered.end(), selected_recipe ) == filtered.end() ) {
                selected_recipe = filtered.front();
                recipe_details = fp3d_crafting::details( recipes[selected_recipe].id, you, crafting_batch );
            }
            if( filtered.empty() ) {
                ImGui::TextWrapped( "没有匹配的配方，请修改分类、搜索、主技能或收藏筛选。" );
            }
            const float area = ImGui::GetContentRegionAvail().x;
            const int columns = std::max( 1, static_cast<int>( area / ( 175 * scale ) ) );
            const float cell = ( area - ( columns - 1 ) * ImGui::GetStyle().ItemSpacing.x ) / columns;
            const float card_height = 100 * scale;
            ImGuiListClipper clipper;
            clipper.Begin( ( filtered.size() + columns - 1 ) / columns,
                           card_height + ImGui::GetStyle().ItemSpacing.y );
            while( clipper.Step() ) {
                for( int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row ) {
                    for( int column = 0; column < columns; ++column ) {
                        const size_t offset = row * columns + column;
                        if( offset >= filtered.size() ) {
                            break;
                        }
                        if( column ) {
                            ImGui::SameLine();
                        }
                        const size_t index = filtered[offset];
                        const auto &entry = recipes[index];
                        ImGui::PushID( static_cast<int>( index ) );
                        if( ImGui::Selectable( "##配方", selected_recipe == index, 0, ImVec2( cell, card_height ) ) ) {
                            selected_recipe = index;
                            recipe_details = fp3d_crafting::details( entry.id, you, crafting_batch );
                        }
                        const auto start = ImGui::GetItemRectMin();
                        const auto end = ImGui::GetItemRectMax();
                        auto *draw = ImGui::GetWindowDrawList();
                        draw->PushClipRect( start, end, true );
                        const std::string key = "recipe:" + entry.id.str();
                        if( !entry.result.is_null() && !icons.count( key ) ) {
                            fp3d_snapshot::sprite sprite;
                            sprite.kind = fp3d_snapshot::sprite_kind::held_item;
                            sprite.id = entry.result.str();
                            sprite.variant = entry.variant;
                            icons.emplace( key, fp3d_materials::item_icon( sprite ) );
                        }
                        const auto icon = icons.find( key );
                        if( icon != icons.end() && icon->second ) {
                            draw->AddImage( ( ImTextureID )( intptr_t )icon->second.get(),
                                            ImVec2( start.x + 4 * scale, start.y + 4 * scale ),
                                            ImVec2( start.x + 52 * scale, start.y + 52 * scale ) );
                        }
                        const std::string name = ( fp3d_crafting::is_favorite( entry.id ) ? "★ " : "" ) + entry.name;
                        draw->AddText( nullptr, ImGui::GetFontSize(), ImVec2( start.x + 58 * scale, start.y + 4 * scale ),
                                       IM_COL32( 235, 235, 235, 255 ), name.c_str(), nullptr, cell - 64 * scale );
                        draw->PopClipRect();
                        if( ImGui::IsItemHovered() ) {
                            ImGui::SetTooltip( "%s", entry.name.c_str() );
                        }
                        ImGui::PopID();
                    }
                }
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild( "配方详情", ImVec2( 0, height ), true );
            if( !filtered.empty() ) {
                const auto &entry = recipes[selected_recipe];
                ImGui::TextWrapped( "%s", entry.name.c_str() );
                const bool favorite = fp3d_crafting::is_favorite( entry.id );
                if( ImGui::Button( favorite ? "取消配方收藏" : "收藏配方" ) ) {
                    pending_recipe_favorite = std::pair{ entry.id, !favorite };
                }
                ImGui::TextWrapped( "主技能：%s / 难度 %d", entry.skill.c_str(), entry.difficulty );
                ImGui::TextWrapped( "由玩家制作；材料、工具、技能、容器、光照和士气按原版检查。" );
                ImGui::SetNextItemWidth( 100 * scale );
                if( ImGui::InputInt( "批量（1-50）", &crafting_batch ) ) {
                    crafting_batch = std::clamp( crafting_batch, 1, 50 );
                    recipe_details = fp3d_crafting::details( entry.id, you, crafting_batch );
                }
                if( ImGui::Button( "制作所选批量" ) ) {
                    fp3d_inventory::operation request;
                    request.kind = fp3d_inventory::operation_kind::craft;
                    request.crafting_recipe = entry.id;
                    request.crafting_batch = crafting_batch;
                    request.crafting_position = crafting_position;
                    result = std::move( request );
                }
                if( !crafting_position ) {
                    ImGui::TextWrapped( "材料替代与特殊确认仍由原版处理；选择其他制作人请用下方完整菜单。" );
                    if( ImGui::Button( "在原版确认制作" ) ) {
                        fp3d_inventory::operation request;
                        request.kind = fp3d_inventory::operation_kind::native;
                        request.native_action = ACTION_CRAFT;
                        request.crafting_recipe = entry.id;
                        result = std::move( request );
                        is_open = false;
                    }
                }
                ImGui::Separator();
                ImGui::TextWrapped( "%s", recipe_details.c_str() );
            } else {
                ImGui::TextWrapped( recipes.empty() ? "当前没有可用配方。" : "没有匹配的配方。" );
            }
            ImGui::EndChild();
            if( !crafting_position ) {
                compatible( ACTION_CRAFT, "完整制作菜单（原版兼容）" );
            }
        }
        void health() {
            const float height = ImGui::GetContentRegionAvail().y - 85 * scale;
            const float width = ImGui::GetContentRegionAvail().x;
            ImGui::BeginChild( "身体与需求", ImVec2( width * .49f, height ), true );
            ImGui::TextWrapped( "%s", you.get_name().c_str() );
            ImGui::Text( "力量 %d  敏捷 %d", you.get_str(), you.get_dex() );
            ImGui::Text( "智力 %d  感知 %d", you.get_int(), you.get_per() );
            ImGui::Text( "耐力 %s", fp3d_hud::stamina_text( you ).c_str() );
            for( const auto &need : needs ) {
                ImGui::TextWrapped( "%s：%s", need.first.c_str(), need.second.c_str() );
            }
            ImGui::Separator();
            ImGui::TextUnformatted( "身体 / 原版生命与累赘" );
            for( const auto &part : you.get_all_body_parts() ) {
                const int maximum = you.get_part_hp_max( part );
                if( maximum <= 0 ) {
                    continue;
                }
                ImGui::TextWrapped( "%s %s  累赘 %d", body_part_name( part ).c_str(),
                                    fp3d_hud::health_text( you, part ).c_str(), you.encumb( part ) );
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild( "状态与技能", ImVec2( 0, height ), true );
            ImGui::TextUnformatted( "当前状态" );
            if( effects.empty() ) {
                ImGui::TextDisabled( "没有可显示的状态" );
            }
            for( size_t i = 0; i < effects.size(); ++i ) {
                ImGui::PushID( static_cast<int>( i ) );
                const bool expanded = ImGui::CollapsingHeader( effects[i].first.c_str() );
                if( ImGui::IsItemHovered() ) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos( 320 * scale );
                    ImGui::TextUnformatted( effects[i].first.c_str() );
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                if( expanded ) {
                    ImGui::TextWrapped( "%s", effects[i].second.c_str() );
                }
                ImGui::PopID();
            }
            ImGui::Separator();
            ImGui::TextUnformatted( "技能 / 实践与理论" );
            ImGui::SetNextItemWidth( -1 );
            ImGui::InputTextWithHint( "##技能搜索", "搜索技能", &skill_search );
            for( const auto &row : skills ) {
                if( !lcmatch( row.name, skill_search ) ) {
                    continue;
                }
                ImGui::TextWrapped( "%s  %d / %d", row.name.c_str(), row.practical, row.theory );
                if( ImGui::IsItemHovered() ) {
                    ImGui::BeginTooltip();
                    ImGui::PushTextWrapPos( 320 * scale );
                    ImGui::TextUnformatted( row.description.c_str() );
                    ImGui::PopTextWrapPos();
                    ImGui::EndTooltip();
                }
                ImGui::ProgressBar( row.exercise / 100.0f, ImVec2( -1, 6 * scale ), "" );
            }
            ImGui::EndChild();
            compatible( ACTION_PL_INFO, "完整人物信息（原版兼容）" );
        }
        void ability_pages() {
            const float height = std::max( 80.f, ImGui::GetContentRegionAvail().y - 55 * scale );
            ImGui::PushID( remembered_page );
            ImGui::BeginChild( "能力操作", ImVec2( 0, height ), true );
            if( remembered_page == mutations_page ) {
                const auto traits = you.get_functioning_mutations( false, true );
                if( traits.empty() ) {
                    ImGui::TextDisabled( "没有可显示的变异。" );
                }
                for( const trait_id &id : traits ) {
                    ImGui::PushID( id.str().c_str() );
                    const bool active = you.has_active_mutation( id );
                    ImGui::TextWrapped( "%s · %s", you.mutation_name( id ).c_str(),
                                        active ? "已启用" : id->activated ? "未启用" : "被动" );
                    if( id->activated ) {
                        const bool ready = fp3d_abilities::mutation_ready( you, id );
                        ImGui::BeginDisabled( !ready );
                        if( ImGui::Button( active ? "停用" : "启用" ) ) {
                            pending_ability = id;
                        }
                        ImGui::EndDisabled();
                        if( !ready ) {
                            ImGui::TextDisabled( "当前资源或状态不足以启用。" );
                        }
                    }
                    if( ImGui::TreeNode( "变异说明" ) ) {
                        cataimgui::draw_colored_text( you.mutation_desc( id ), c_white,
                                                     ImGui::GetContentRegionAvail().x );
                        ImGui::TreePop();
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            } else if( remembered_page == bionics_page ) {
                ImGui::Text( "电量 %s / %s", units::display( you.get_power_level() ).c_str(),
                             units::display( you.get_max_power_level() ).c_str() );
                if( you.num_bionics() == 0 ) {
                    ImGui::TextDisabled( "未安装仿生插件。" );
                }
                for( int i = 0; i < you.num_bionics(); ++i ) {
                    const bionic &bio = you.bionic_at_index( i );
                    const bionic_data &info = bio.info();
                    ImGui::PushID( static_cast<int>( bio.get_uid() ) );
                    ImGui::TextWrapped( "%s · %s", info.name.translated().c_str(),
                                        bio.powered ? "已启用" : info.activated ? "未启用" : "被动" );
                    if( info.activated ) {
                        ImGui::Text( "启用消耗 %s", units::display( info.power_activate ).c_str() );
                        const char *blocked = nullptr;
                        if( !bio.powered ) {
                            if( bio.can_install_weapon() && !bio.has_weapon() ) {
                                blocked = "请先安装仿生武器。";
                            } else if( !you.enough_power_for( bio.id ) ) {
                                blocked = "电量不足。";
                            }
                        }
                        ImGui::BeginDisabled( blocked != nullptr );
                        if( ImGui::Button( bio.powered ? "停用" : "启用" ) ) {
                            pending_ability = bionic_request{ bio.get_uid(), bionic_control::power };
                        }
                        ImGui::EndDisabled();
                        if( blocked ) {
                            ImGui::SameLine();
                            ImGui::TextDisabled( "%s", blocked );
                        }
                    }
                    if( bio.can_install_weapon() && !bio.powered &&
                        ImGui::Button( bio.has_weapon() ? "卸下武器" : "安装武器" ) ) {
                        pending_ability = bionic_request{ bio.get_uid(), bionic_control::weapon };
                    }
                    if( !info.fuel_opts.empty() || info.is_remote_fueled ) {
                        if( bio.is_safe_fuel_on() ) {
                            ImGui::Text( "节能模式：%.0f%%", bio.get_safe_fuel_thresh() * 100 );
                        } else {
                            ImGui::TextUnformatted( "节能模式：关闭" );
                        }
                        if( ImGui::Button( "切换节能模式" ) ) {
                            pending_ability = bionic_request{ bio.get_uid(), bionic_control::fuel };
                        }
                    }
                    if( ImGui::TreeNode( "插件说明" ) ) {
                        cataimgui::draw_colored_text( info.description.translated(), c_white,
                                                     ImGui::GetContentRegionAvail().x );
                        ImGui::TreePop();
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            } else {
                const auto &martial = *you.martial_arts_data;
                ImGui::TextWrapped( "当前战斗方式：%s", martial.selected_style()->name.translated().c_str() );
                bool hands_free = martial.keep_hands_free;
                if( ImGui::Checkbox( "保持双手空闲", &hands_free ) ) {
                    pending_ability = hands_free;
                }
                ImGui::Separator();
                for( const matype_id &id : fp3d_abilities::available_styles( you ) ) {
                    ImGui::PushID( id.str().c_str() );
                    ImGui::TextWrapped( "%s", id->name.translated().c_str() );
                    const bool current = id == martial.selected_style();
                    ImGui::BeginDisabled( current );
                    if( ImGui::Button( current ? "当前使用" : "切换到此武术" ) ) {
                        pending_ability = id;
                    }
                    ImGui::EndDisabled();
                    if( ImGui::TreeNode( "武术说明" ) ) {
                        cataimgui::draw_colored_text( id->description.translated(), c_white,
                                                     ImGui::GetContentRegionAvail().x );
                        ImGui::TreePop();
                    }
                    ImGui::Separator();
                    ImGui::PopID();
                }
            }
            ImGui::EndChild();
            ImGui::PopID();
        }

        void execute_ability( const ability_request &request ) {
            const int moves = you.get_moves();
            const tripoint_abs_ms position = you.pos_abs();
            const auto turn = calendar::turn;
            bool close = false;
            ability_in_progress = true;
            const bool success = fp3d_abilities::execute( you, request, close );
            ability_in_progress = false;
            fp3d_compositor::set_input_context( "FP3D_PANEL" );
            const bool acted_in_world = you.get_moves() != moves || you.pos_abs() != position ||
                                        calendar::turn != turn || static_cast<bool>( you.activity );
            direct_action_completed |= acted_in_world;
            if( close || acted_in_world || you.is_dead_state() ) {
                is_open = false;
            }
            action_message = success ? "" : "操作未执行，请查看插件或能力条件及游戏消息。";
            if( is_open ) {
                refresh_character();
                refresh_items();
            }
        }

        void more() {
            const float width = ImGui::GetContentRegionAvail().x;
            const float height = std::max( 100.0f, ImGui::GetContentRegionAvail().y - 85 * scale );
            ImGui::BeginChild( "更多分类", ImVec2( width * .23f, height ), true );
            const char *sections[] = { "消息日志", "区域与阵营", "车辆与活动", "设置与规则", "侧栏信息" };
            for( int i = 0; i < 5; ++i ) {
                if( ImGui::Selectable( sections[i], more_section == i ) ) {
                    more_section = i;
                }
            }
            ImGui::Separator();
            if( ImGui::Button( "人物与技能" ) ) {
                remembered_page = 1;
            }
            if( ImGui::Button( "任务与追踪" ) ) {
                remembered_page = 5;
            }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild( "更多内容", ImVec2( 0, height ), true );
            if( more_section == 0 ) {
                if( !messages_loaded ) {
                    messages = Messages::recent_messages( 1000 );
                    std::reverse( messages.begin(), messages.end() );
                    for( auto &row : messages ) {
                        row.second = remove_color_tags( row.second );
                        auto preview = row.first + "  " + row.second;
                        std::replace( preview.begin(), preview.end(), '\n', ' ' );
                        std::replace( preview.begin(), preview.end(), '\r', ' ' );
                        message_previews.push_back( utf8_truncate( preview, 180 ) );
                    }
                    messages_loaded = true;
                }
                ImGui::Text( "最近 %d 条 / 最新在前", static_cast<int>( messages.size() ) );
                ImGui::SetNextItemWidth( -1 );
                ImGui::InputTextWithHint( "##消息搜索", "搜索消息", &message_search );
                std::vector<size_t> filtered;
                for( size_t i = 0; i < messages.size(); ++i ) {
                    if( lcmatch( messages[i].second, message_search ) ) {
                        filtered.push_back( i );
                    }
                }
                if( std::find( filtered.begin(), filtered.end(), selected_message ) == filtered.end() ) {
                    selected_message = filtered.empty() ? messages.size() : filtered.front();
                }
                ImGui::BeginChild( "消息列表", ImVec2( 0, height * .4f ), true );
                ImGui::SetWindowFontScale( scale );
                ImGuiListClipper clipper;
                clipper.Begin( static_cast<int>( filtered.size() ), ImGui::GetTextLineHeightWithSpacing() );
                while( clipper.Step() ) {
                    for( int n = clipper.DisplayStart; n < clipper.DisplayEnd; ++n ) {
                        const size_t index = filtered[n];
                        ImGui::PushID( static_cast<int>( index ) );
                        const auto position = ImGui::GetCursorScreenPos();
                        if( ImGui::Selectable( "##message", selected_message == index, 0,
                                               ImVec2( 0, ImGui::GetTextLineHeight() ) ) ) {
                            selected_message = index;
                        }
                        ImGui::GetWindowDrawList()->AddText( position, ImGui::GetColorU32( ImGuiCol_Text ),
                                                             message_previews[index].c_str() );
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                ImGui::BeginChild( "消息详情", ImVec2( 0, -45 * scale ), true );
                ImGui::SetWindowFontScale( scale );
                if( selected_message < messages.size() ) {
                    ImGui::TextUnformatted( messages[selected_message].first.c_str() );
                    ImGui::Separator();
                    ImGui::TextWrapped( "%s", messages[selected_message].second.c_str() );
                } else {
                    ImGui::TextDisabled( "没有匹配的消息" );
                }
                ImGui::EndChild();
                compatible( ACTION_MESSAGES, "完整消息记录（原版兼容）" );
            } else if( more_section == 4 ) {
                const auto hud = fp3d_compositor::hud_data();
                ImGui::TextUnformatted( "原版右侧栏信息" );
                ImGui::Separator();
                ImGui::Text( "状态：%s", hud.status.c_str() );
                ImGui::Text( "时间：%s", hud.day_time.c_str() );
                ImGui::Text( "温度：%s", hud.temperature.c_str() );
                ImGui::Text( "任务：%s", hud.mission.c_str() );
                ImGui::Separator();
                ImGui::TextUnformatted( "快捷栏" );
                for( size_t i = 0; i < hud.slots.size(); ++i ) {
                    ImGui::Text( "%d  %s", static_cast<int>( i + 1 ), hud.slots[i].c_str() );
                }
                ImGui::Separator();
                ImGui::TextUnformatted( "最近消息" );
                for( const auto &message : hud.messages ) {
                    ImGui::TextWrapped( "%s", message.text.c_str() );
                }
            } else {
                ImGui::TextUnformatted( "本组功能使用原版兼容界面" );
                ImGui::Separator();
                if( more_section == 1 ) {
                    compatible( ACTION_MORALE, "士气详情" );
                    compatible( ACTION_ZONES, "区域管理" );
                    compatible( ACTION_FACTIONS, "阵营信息" );
                } else if( more_section == 2 ) {
                    compatible( ACTION_CONTROL_VEHICLE, "车辆控制" );
                    compatible( ACTION_WAIT, "等待" );
                    compatible( ACTION_SLEEP, "睡眠" );
                    compatible( ACTION_READ, "阅读" );
                    compatible( ACTION_DIARY, "日记" );
                    compatible( ACTION_ACTIONMENU, "完整行动列表" );
                } else {
                    compatible( ACTION_KEYBINDINGS, "2D / 3D 按键设置" );
                    ImGui::Separator();
                    ImGui::TextUnformatted( "3D互动 / 拾取长按时间" );
                    auto &gestures = fp3d_input::world_gestures();
                    int hold_delay = static_cast<int>( gestures.hold_delay() );
                    ImGui::SetNextItemWidth( -1 );
                    if( ImGui::SliderInt( "##3D长按时间", &hold_delay, 100, 1000, "%d 毫秒" ) ) {
                        gestures.set_hold_delay( hold_delay );
                    }
                    if( ImGui::Button( "恢复250毫秒" ) ) {
                        gestures.set_hold_delay( 250 );
                    }
                    ImGui::TextWrapped( "本次启动期间有效；只调整互动和拾取的长按识别。" );
                    ImGui::Separator();
                    compatible( ACTION_OPTIONS, "游戏设置" );
                    compatible( ACTION_AUTOPICKUP, "自动拾取规则" );
                    compatible( ACTION_AUTONOTES, "自动标记规则" );
                    compatible( ACTION_SAFEMODE, "安全模式规则" );
                    compatible( ACTION_COLOR, "颜色设置" );
                    compatible( ACTION_WORLD_MODS, "当前世界模组" );
                    compatible( ACTION_HELP, "帮助" );
                }
            }
            ImGui::EndChild();
        }
        void sidebar_page() {
            const auto hud = fp3d_compositor::hud_data();
            ImGui::BeginChild( "侧栏信息内容", ImVec2( 0, 0 ), true );
            ImGui::TextUnformatted( "原版右侧栏信息" );
            ImGui::Separator();
            ImGui::TextWrapped( "%s", hud.status.c_str() );
            ImGui::Text( "%s  %s", hud.day_time.c_str(), hud.temperature.c_str() );
            ImGui::Text( "任务目标：%s", hud.mission.c_str() );
            ImGui::Separator();
            ImGui::TextUnformatted( "快捷栏" );
            for( size_t i = 0; i < hud.slots.size(); ++i ) {
                ImGui::Text( "%d  %s", static_cast<int>( i + 1 ), hud.slots[i].c_str() );
            }
            ImGui::Separator();
            ImGui::TextUnformatted( "最近消息" );
            for( const auto &message : hud.messages ) {
                ImGui::TextWrapped( "%s", message.text.c_str() );
            }
            ImGui::EndChild();
        }
        void compatible( action_id action, const char *label ) {
            if( ImGui::Button( label ) ) {
                if( direct_action_completed ) {
                    action_message = "请先关闭面板并结算已完成的放置操作。";
                    return;
                }
                fp3d_inventory::operation request;
                request.kind = fp3d_inventory::operation_kind::native;
                request.native_action = action;
                result = std::move( request );
                is_open = false;
            }
        }
    protected:
        cataimgui::bounds get_bounds() override {
            const auto size = ImGui::GetIO().DisplaySize;
            return { size.x * .025f, size.y * .035f, size.x * .95f, size.y * .93f };
        }
        void draw_controls() override {
            const auto size = ImGui::GetContentRegionAvail();
            scale = 1 + scale_choice * .25f;
            ImGui::SetWindowFontScale( scale );
            if( !action_message.empty() ) {
                ImGui::TextColored( ImVec4( 1, .86f, .18f, 1 ), "%s", action_message.c_str() );
            }
            const char *pages[] = { "背包", "人物/健康", "制作", "建造", "地图", "任务", "队友", "更多", "侧栏", "变异", "仿生插件", "武术" };
            if( pickup_target ) {
                ImGui::TextUnformatted( "拾取物品" );
            }
            if( crafting_position ) {
                ImGui::TextUnformatted( "简易制作" );
            }
            for( int i = 0; !pickup_target && !crafting_position && i <= styles_page; ++i ) {
                const float tab_width = ( i == 1 ? 110 : i == bionics_page ? 95 : 65 ) * scale;
                if( i && ImGui::GetItemRectMax().x + tab_width + 50 * scale <
                    ImGui::GetWindowPos().x + ImGui::GetWindowWidth() ) {
                    ImGui::SameLine();
                }
                if( ImGui::Selectable( pages[i], remembered_page == i, 0, ImVec2( tab_width, 0 ) ) ) {
                    remembered_page = i;
                }
            }
            ImGui::SameLine( ImGui::GetWindowWidth() - 42 * scale );
            if( ImGui::Button( "X", ImVec2( 26 * scale, 0 ) ) ) {
                is_open = false;
            }
            ImGui::Separator();
            ImGui::SetNextItemWidth( 125 * scale );
            ImGui::Combo( "界面缩放", &scale_choice, "100%\0" "125%\0" "150%\0" );
            if( pickup_target || ( !crafting_position && remembered_page == 0 ) ) {
                ImGui::SameLine();
                ImGui::SetNextItemWidth( 125 * scale );
                ImGui::Combo( "左右排序", &sort_order, "名称\0重量\0体积\0" );
            }
            const bool narrow = size.x < 1250 * scale;
            if( pickup_target ) {
                const float available_height = ImGui::GetContentRegionAvail().y - 70 * scale;
                const float gap = ImGui::GetStyle().ItemSpacing.x;
                ImGui::BeginChild( "拾取背包", ImVec2( ( ImGui::GetContentRegionAvail().x - gap ) * .5f,
                                                       available_height ), true );
                ImGui::TextUnformatted( "我的物品 / 背包" );
                groups( false );
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild( "拾取目标", ImVec2( 0, available_height ), true );
                ImGui::TextUnformatted( "目标容器 / 地面物品" );
                groups( true );
                ImGui::EndChild();
            } else if( crafting_position ) {
                crafting();
            } else if( remembered_page == 0 ) {
                const float available_height = ImGui::GetContentRegionAvail().y - 70 * scale;
                ImGui::BeginChild( "分类", ImVec2( 58 * scale, available_height ), true );
                for( int i = 0; i < 4; ++i ) {
                    if( ImGui::Selectable( ( i == 0 ? "装备" : i == 1 ? "衣物" : i == 2 ? "身体" : "更多" ),
                                           side_page == i ) ) {
                        side_page = i;
                    }
                }
                ImGui::EndChild();
                ImGui::SameLine();
                const float body_width = ImGui::GetContentRegionAvail().x;
                ImGui::BeginChild( "装备", ImVec2( body_width * ( narrow ? .34f : .28f ), available_height ),
                                   true );
                if( side_page == 2 ) {
                    for( const auto &part : you.get_all_body_parts() ) {
                        const int maximum = you.get_part_hp_max( part );
                        if( maximum <= 0 ) {
                            continue;
                        }
                        ImGui::Text( "%s %s", body_part_name( part ).c_str(),
                                     fp3d_hud::health_text( you, part ).c_str() );
                    }
                } else if( side_page == 3 ) {
                    compatible( ACTION_ADVANCEDINV, "原版高级物品管理" );
                    compatible( ACTION_INVENTORY, "原版背包" );
                    compatible( ACTION_KEYBINDINGS, "操作设置" );
                } else {
                    gear();
                }
                ImGui::EndChild();
                ImGui::SameLine();
                const float column_width = narrow ? ImGui::GetContentRegionAvail().x :
                                           ImGui::GetContentRegionAvail().x * .5f - 4;
                ImGui::BeginChild( "我的物品", ImVec2( column_width, available_height ), true );
                if( narrow ) {
                    ImGui::RadioButton( "我的物品", &small_tab, 1 );
                    ImGui::SameLine();
                    ImGui::RadioButton( "附近物品", &small_tab, 2 );
                    groups( small_tab == 2 );
                } else {
                    ImGui::TextUnformatted( "我的物品 / 真实口袋" );
                    groups( false );
                }
                ImGui::EndChild();
                if( !narrow ) {
                    ImGui::SameLine();
                    ImGui::BeginChild( "附近物品", ImVec2( 0, available_height ), true );
                    ImGui::TextUnformatted( "附近 / 同层可见且可触及" );
                    groups( true );
                    ImGui::EndChild();
                }
                ImGui::Button( "拖到这里放到脚下", ImVec2( 0, 0 ) );
                target( fp3d_inventory::operation_kind::drop );
            } else if( remembered_page == 1 ) {
                health();
            } else if( remembered_page == 3 ) {
                construction();
            } else if( remembered_page == 5 || remembered_page == 6 ) {
                journal_page( remembered_page == 6 );
            } else if( remembered_page == 2 ) {
                crafting();
            } else if( remembered_page == 4 ) {
                map_page();
            } else if( remembered_page == 7 ) {
                more();
            } else if( remembered_page >= mutations_page ) {
                ability_pages();
            } else {
                sidebar_page();
            }
            if( open_use ) {
                ImGui::OpenPopup( "使用物品" );
                open_use = false;
            }
            ImGui::SetNextWindowSizeConstraints( ImVec2( 0, 0 ), ImVec2( size.x * .85f, size.y * .85f ) );
            ImGui::SetNextWindowSize( ImVec2( std::min( 620 * scale, size.x * .85f ), 0 ), ImGuiCond_Appearing );
            if( ImGui::BeginPopupModal( "使用物品", nullptr, ImGuiWindowFlags_AlwaysAutoResize ) ) {
                if( use_item ) {
                    ImGui::TextWrapped( "%s", remove_color_tags( use_item->tname() ).c_str() );
                    ImGui::Separator();
                    if( item_uses.empty() ) { ImGui::TextWrapped( "此物品没有可直接调用的用途。" ); }
                    for( size_t i = 0; i < item_uses.size(); ++i ) {
                        const auto &option = item_uses[i];
                        ImGui::PushID( static_cast<int>( i ) );
                        ImGui::BeginDisabled( !option.enabled );
                        if( ImGui::Button( option.name.c_str(), ImVec2( -1, 38 * scale ) ) ) {
                            fp3d_inventory::operation action;
                            action.kind = fp3d_inventory::operation_kind::use;
                            action.items.emplace_back( use_item, 1 );
                            action.use_method = option.method;
                            action.activate_relic = option.relic;
                            result = std::move( action );
                            is_open = false;
                            ImGui::CloseCurrentPopup();
                        }
                        ImGui::EndDisabled();
                        if( !option.description.empty() ) { ImGui::TextWrapped( "%s", remove_color_tags( option.description ).c_str() ); }
                        ImGui::PopID();
                    }
                } else { ImGui::TextWrapped( "物品已失效或已移走。" ); }
                ImGui::Separator();
                if( ImGui::Button( "返回" ) || ImGui::IsKeyPressed( ImGuiKey_Escape ) ) { ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            if( open_details ) {
                ImGui::OpenPopup( "物品详情" );
                details_open = true;
                open_details = false;
            }
            ImGui::SetNextWindowSize( ImVec2( std::min( 700 * scale, size.x * .85f ), size.y * .75f ),
                                      ImGuiCond_Appearing );
            if( ImGui::BeginPopupModal( "物品详情", &details_open ) ) {
                ImGui::BeginChild( "属性", ImVec2( 0, -40 * scale ) );
                ImGui::TextWrapped( "%s", details_text.c_str() );
                ImGui::EndChild();
                if( ImGui::Button( "返回" ) || !details_open ) {
                    details_open = false;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
            ImGui::PushTextWrapPos( 0 );
            ImGui::TextDisabled( "%s", crafting_position ?
                                 "选择配方，查看材料与工具，设置批量后点击制作；Esc 或 Tab 返回。" :
                                 pickup_target || remembered_page == 0 ?
                                 "左键选择 / 双击详情 / Shift 多选 / 右键操作 / 拖拽 / Esc 或 Tab 返回；转移按原版耗时处理，完成后刷新。" :
                                 "滚动查看 / Esc 或 Tab 返回；查看不消耗回合，实际操作按原版规则结算。" );
            ImGui::PopTextWrapPos();
        }
    public:
        void draw() override {
            if( ability_in_progress ) {
                return;
            }
            ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            if( background && fp3d_compositor::controls() == fp3d_input::mode::first_person_3d ) {
                ImGui::GetBackgroundDrawList()->AddImage( ( ImTextureID )( intptr_t )background.get(),
                        ImVec2( 0, 0 ), ImGui::GetIO().DisplaySize, ImVec2( 0, 1 ), ImVec2( 1, 0 ) );
            }
            // The native 2D buffer is retained between UI frames. An opaque panel
            // prevents old page contents accumulating under translucent children.
            const float opacity = fp3d_compositor::controls() == fp3d_input::mode::modern_2d ? 1.0f : .72f;
            ImGui::PushStyleColor( ImGuiCol_WindowBg, ImVec4( .035f, .033f, .03f, opacity ) );
            ImGui::PushStyleColor( ImGuiCol_ChildBg, ImVec4( .045f, .043f, .038f, .48f ) );
            ImGui::PushStyleColor( ImGuiCol_Button, ImVec4( .12f, .115f, .10f, .74f ) );
            ImGui::PushStyleColor( ImGuiCol_Header, ImVec4( .43f, .31f, .15f, .70f ) );
            ImGui::PushStyleColor( ImGuiCol_ButtonHovered, ImVec4( .43f, .32f, .17f, .85f ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderHovered, ImVec4( .50f, .36f, .16f, .8f ) );
            ImGui::PushStyleColor( ImGuiCol_HeaderActive, ImVec4( .65f, .46f, .20f, .9f ) );
            ImGui::PushStyleColor( ImGuiCol_Border, ImVec4( .43f, .40f, .33f, .5f ) );
            ImGui::PushStyleColor( ImGuiCol_PopupBg, ImVec4( .045f, .04f, .03f, .97f ) );
            ImGui::PushStyleColor( ImGuiCol_TitleBg, ImVec4( .16f, .13f, .085f, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_TitleBgActive, ImVec4( .30f, .22f, .11f, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_Text, ImVec4( 1, 1, 1, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_TextDisabled, ImVec4( 1, .86f, .18f, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBg, ImVec4( .12f, .115f, .10f, .65f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgHovered, ImVec4( .30f, .25f, .17f, .8f ) );
            ImGui::PushStyleColor( ImGuiCol_FrameBgActive, ImVec4( .38f, .29f, .16f, .9f ) );
            ImGui::PushStyleColor( ImGuiCol_CheckMark, ImVec4( .88f, .66f, .32f, 1 ) );
            ImGui::PushStyleColor( ImGuiCol_PlotHistogram, ImVec4( .48f, .68f, .40f, 1 ) );
            ImGui::BeginDisabled( activity_pending || native_prompt_open );
            cataimgui::window::draw();
            ImGui::EndDisabled();
            ImGui::PopStyleColor( 18 );
        }
        void refresh_character() {
            needs.clear();
            effects.clear();
            skills.clear();
            const auto add_need = [&]( const char *label, const std::pair<std::string, nc_color> &value ) {
                needs.emplace_back( label, value.first.empty() ? "—" : remove_color_tags( value.first ) );
            };
            add_need( "饥饿", display::hunger_text_color( you ) );
            add_need( "口渴", display::thirst_text_color( you ) );
            add_need( "困倦", display::sleepiness_text_color( you ) );
            add_need( "疼痛", display::pain_text_color( you ) );
            for( const auto &reference : you.get_effects() ) {
                const effect &status = reference.get();
                auto name = remove_color_tags( status.disp_name() );
                if( name.empty() ) {
                    continue;
                }
                effects.emplace_back( std::move( name ), remove_color_tags( status.disp_desc() ) );
            }
            const auto levels = you.get_all_skills();
            for( const Skill &skill : Skill::skills ) {
                if( skill.is_contextual_skill() ) {
                    continue;
                }
                const auto &level = levels.get_skill_level_object( skill.ident() );
                skills.push_back( { skill.name(), skill.description(), level.level(), level.knowledgeLevel(), level.exercise() } );
            }
            std::sort( skills.begin(), skills.end(), []( const auto & a, const auto & b ) {
                return a.name < b.name;
            } );
        }
        void refresh_items() {
            refresh_character();
            journal_loaded = false;
            map_loaded = false;
            you.invalidate_crafting_inventory();
            recipes_loaded = false;
            projects_loaded = false;
            view = fp3d_inventory::capture( you, here, pickup_target );
            for( const auto &entry : view.entries ) {
                if( !entry.location ) {
                    continue;
                }
                fp3d_snapshot::sprite image;
                image.kind = fp3d_snapshot::sprite_kind::held_item;
                image.id = entry.location->typeId().str();
                if( entry.location->has_itype_variant() ) {
                    image.variant = entry.location->itype_variant().id;
                }
                const auto key = image.id + image.variant;
                if( !icons.count( key ) ) {
                    icons.emplace( key, fp3d_materials::item_icon( image ) );
                }
            }
        }
        panel( avatar &player, map &current_map, bool nearby,
               int initial_page, item_location initial_use = {},
               std::optional<tripoint_abs_ms> pickup_position = std::nullopt ) : cataimgui::window( "生存面板",
                           ImGuiWindowFlags_NoTitleBar ),
            you( player ), here( current_map ), view( fp3d_inventory::capture( player, current_map, pickup_position ) ),
            pickup_target( pickup_position ),
            background( fp3d_compositor::copy_view() ) {
            // Keep ImGui popup windows above their parent panel.
            force_to_back = true;
            if( initial_use ) {
                use_item = initial_use;
                item_uses = fp3d_inventory::use_options( use_item, you, here );
                open_use = true;
            }
            if( initial_page >= 0 ) {
                remembered_page = std::clamp( initial_page, 0, styles_page );
            }
            refresh_character();
            if( nearby ) {
                remembered_page = 0;
                small_tab = 2;
            }
            for( const auto &entry : view.entries ) {
                if( !entry.location ) {
                    continue;
                }
                fp3d_snapshot::sprite image;
                image.kind = fp3d_snapshot::sprite_kind::held_item;
                image.id = entry.location->typeId().str();
                if( entry.location->has_itype_variant() ) {
                    image.variant = entry.location->itype_variant().id;
                }
                const auto key = image.id + image.variant;
                if( !icons.count( key ) ) {
                    icons.emplace( key, fp3d_materials::item_icon( image ) );
                }
            }
        }
        bool waiting() const { return activity_pending; }
        void set_crafting_position( std::optional<tripoint_abs_ms> position ) {
            crafting_position = position;
        }
        void resume() {
            activity_pending = false;
            direct_action_completed = false;
            result.reset();
            selected.clear();
            refresh_items();
        }
        std::optional<fp3d_inventory::operation> run() {
            input_context context( "FP3D_PANEL", keyboard_mode::keycode );
            context.register_action( "QUIT" );
            context.register_action( "ANY_INPUT" );
            context.register_action( "SELECT" );
            context.register_action( "MOUSE_MOVE" );
            context.register_action( "HELP_KEYBINDINGS" );
            const auto profile = fp3d_compositor::controls() == fp3d_input::mode::modern_2d ?
                                 fp3d_input::modern_2d_context() : fp3d_input::context();
            while( is_open ) {
                ui_manager::redraw();
                if( pending_ability ) {
                    const ability_request request = std::move( *pending_ability );
                    pending_ability.reset();
                    execute_ability( request );
                    continue;
                }
                if( pending_recipe_favorite ) {
                    const auto request = *pending_recipe_favorite;
                    pending_recipe_favorite.reset();
                    action_message = fp3d_crafting::set_favorite( you, request.first, request.second ) ?
                                     std::string() : "配方已不在可用列表中，请刷新制作页。";
                    recipes_loaded = false;
                    continue;
                }
                if( pending_travel ) {
                    const auto target = *pending_travel;
                    pending_travel.reset();
                    native_prompt_open = true;
                    const bool started = overmap_ui::try_travel_to_destination( you, target, target,
                                         fp3d_input::vehicle_controls() );
                    native_prompt_open = false;
                    if( started ) {
                        direct_action_completed = true;
                        is_open = false;
                        break;
                    }
                    // Cancelled/failed native confirmation leaves this same map panel open.
                    map_loaded = false;
                    continue;
                }
                if( pending_note ) {
                    std::string error;
                    if( fp3d_map::write_note( pending_note->first, pending_note->second, error ) ) {
                        note_status = pending_note->second.empty() ? "标记已删除。" : "标记已保存。";
                        note_draft = pending_note->second;
                        map_loaded = false;
                    } else {
                        note_status = std::move( error );
                    }
                    pending_note.reset();
                    continue;
                }
                if( result && ( result->kind == fp3d_inventory::operation_kind::place ||
                                result->kind == fp3d_inventory::operation_kind::drop ||
                                result->kind == fp3d_inventory::operation_kind::insert ||
                                result->kind == fp3d_inventory::operation_kind::pickup ||
                                result->kind == fp3d_inventory::operation_kind::wield ||
                                result->kind == fp3d_inventory::operation_kind::wear ||
                                result->kind == fp3d_inventory::operation_kind::takeoff ||
                                result->kind == fp3d_inventory::operation_kind::follower_order ||
                                result->kind == fp3d_inventory::operation_kind::craft ) ) {
                    std::string error;
                    if( fp3d_inventory::execute( *result, you, here, &error ) ) {
                        direct_action_completed = true;
                        if( result->kind == fp3d_inventory::operation_kind::craft ) {
                            recipes_loaded = false;
                        }
                        if( result->kind == fp3d_inventory::operation_kind::follower_order ) {
                            journal_loaded = false;
                        }
                        action_message.clear();
                        selected.clear();
                        activity_pending = static_cast<bool>( you.activity ) || you.get_moves() <= 0;
                        if( activity_pending ) {
                            resume_after_activity = true;
                        }
                        refresh_items();
                    } else {
                        action_message = std::move( error );
                        if( result->kind == fp3d_inventory::operation_kind::craft ) {
                            recipes_loaded = false;
                        }
                        if( result->kind == fp3d_inventory::operation_kind::follower_order ) {
                            journal_loaded = false;
                        }
                    }
                    result.reset();
                    if( activity_pending ) { break; }
                    continue;
                }
                const auto &action = context.handle_input( 16 );
                if( action == "QUIT" || ( !ImGui::GetIO().WantTextInput &&
                                          profile.input_to_action( context.get_raw_input() ) == "FP3D_PANEL" ) ) {
                    if( details_open ) {
                        details_open = false;
                    } else if( !ImGui::IsPopupOpen( nullptr, ImGuiPopupFlags_AnyPopupId ) ) {
                        is_open = false;
                    }
                }
            }
            if( direct_action_completed ) {
                fp3d_inventory::operation completed;
                completed.kind = fp3d_inventory::operation_kind::completed;
                return completed;
            }
            return std::move( result );
        }
};
std::unique_ptr<panel> retained_panel;
}
void clear_resume()
{
    resume_after_activity = false;
    retained_panel.reset();
}
int scale_percent()
{
    return 100 + scale_choice * 25;
}
bool transfer_pending()
{
    return retained_panel != nullptr;
}
bool take_resume()
{
    const bool resume = resume_after_activity;
    resume_after_activity = false;
    return resume;
}
std::unique_ptr<cataimgui::window> create( avatar &you, map &here, int scale_percent, bool nearby,
        int initial_page, item_location initial_use, std::optional<tripoint_abs_ms> pickup_target,
        std::optional<tripoint_abs_ms> crafting_position )
{
    if( scale_percent > 0 ) {
        scale_choice = std::clamp( ( scale_percent - 100 ) / 25, 0, 2 );
    }
    auto menu = std::make_unique<panel>( you, here, nearby, initial_page, initial_use, pickup_target );
    menu->set_crafting_position( crafting_position );
    return menu;
}
std::optional<fp3d_inventory::operation> show( avatar &you, map &here, bool nearby,
        int initial_page, std::optional<tripoint_abs_ms> pickup_target,
        std::optional<tripoint_abs_ms> crafting_position )
{
    fp3d_input::world_gestures().cancel();
    fp3d_compositor::set_input_context( "FP3D_PANEL" );
    ImGuiIO &imgui_io = ImGui::GetIO();
    const bool no_mouse = imgui_io.ConfigFlags & ImGuiConfigFlags_NoMouse;
    imgui_io.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
    auto menu = retained_panel ? std::move( retained_panel ) : std::make_unique<panel>( you, here, nearby, initial_page,
                item_location(), pickup_target );
    if( crafting_position ) {
        menu->set_crafting_position( crafting_position );
    }
    menu->resume();
    auto result = menu->run();
    const bool waiting = menu->waiting();
    if( waiting ) { retained_panel = std::move( menu ); }
    if( no_mouse ) {
        imgui_io.ConfigFlags |= ImGuiConfigFlags_NoMouse;
    }
    if( !waiting ) { fp3d_compositor::set_input_context( "DEFAULTMODE" ); }
    return result;
}
}
#endif
