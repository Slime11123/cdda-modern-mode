#include "fp3d_save_preview.h"
#if defined(TILES) && defined(ENABLE_FP3D)
#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <SDL3_image/SDL_image.h>
#include "avatar.h"
#include "calendar.h"
#include "cata_imgui.h"
#include "cata_path.h"
#include "cata_scope_helpers.h"
#include "cata_utility.h"
#include "catacharset.h"
#include "color.h"
#include "debug.h"
#include "filesystem.h"
#include "fp3d_compositor.h"
#include "imgui/imgui.h"
#include "imgui/imgui_stdlib.h"
#include "input_context.h"
#include "json.h"
#include "json_loader.h"
#include "mapsharing.h"
#include "mod_manager.h"
#include "path_info.h"
#include "sdltiles.h"
#include "string_formatter.h"
#include "ui_manager.h"
#include "zzip.h"

extern std::unique_ptr<cataimgui::client> imclient;

namespace fp3d_save_preview
{
namespace
{
const std::vector<std::pair<std::string, std::string>> difficulty_options = {
    { "MONSTER_SPEED", "怪物速度" }, { "MONSTER_RESILIENCE", "怪物韧性" },
    { "SPAWN_DENSITY", "怪物生成密度" }, { "ITEM_SPAWNRATE", "物品生成倍率" }
};

bool screenshot( const cata_path &path )
{
    if( !imclient || is_native_menu_demo_worker() ) { return false; }
    auto *renderer = imclient->sdl_renderer.get();
    const bool three_d = fp3d_compositor::view_3d();
    auto image = three_d ? fp3d_compositor::copy_view() : copy_2d_display_buffer();
    int width = 0, height = 0;
    if( !image || QueryTexturePixels( image.get(), nullptr, nullptr, &width, &height ) != 0 ||
        width <= 0 || height <= 0 ) { return false; }
    const float scale = std::min( 1.f, 640.f / width );
    width = std::max( 1, static_cast<int>( width * scale ) );
    height = std::max( 1, static_cast<int>( height * scale ) );
    SDL_Texture_Ptr target( SDL_CreateTexture( renderer, SDL_PIXELFORMAT_RGBA32,
                           SDL_TEXTUREACCESS_TARGET, width, height ) );
    SDL_Surface_Ptr surface( CreateSurfaceWithFormat( 0, width, height, 32,
                             SDL_PIXELFORMAT_RGBA32 ) );
    if( !target || !surface ) { return false; }
    auto *previous = SDL_GetRenderTarget( renderer );
    SDL_Rect viewport, clip;
    SDL_GetRenderViewport( renderer, &viewport );
    SDL_GetRenderClipRect( renderer, &clip );
    const bool clipped = SDL_RenderClipEnabled( renderer );
    float sx = 1, sy = 1;
    SDL_GetRenderScale( renderer, &sx, &sy );
    on_out_of_scope restore( [&]() {
        SDL_SetRenderTarget( renderer, previous );
        SDL_SetRenderScale( renderer, sx, sy );
        SDL_SetRenderViewport( renderer, &viewport );
        SDL_SetRenderClipRect( renderer, clipped ? &clip : nullptr );
    } );
    if( !SDL_SetRenderTarget( renderer, target.get() ) ) { return false; }
    SDL_SetRenderScale( renderer, 1, 1 );
    SDL_SetRenderViewport( renderer, nullptr );
    SDL_SetRenderClipRect( renderer, nullptr );
    if( RenderTextureRotatedPixels( renderer, image.get(), nullptr, nullptr, 0, nullptr,
                         three_d ? SDL_FLIP_VERTICAL : SDL_FLIP_NONE ) != 0 ||
        ReadRenderPixels( renderer, nullptr, surface->format,
                              surface->pixels, surface->pitch ) != 0 ) { return false; }
    const cata_path temporary = path + ".tmp";
    return IMG_SavePNG( surface.get(), temporary.generic_u8string().c_str() ) &&
           rename_file( temporary, path );
}

std::time_t modified( const cata_path &path )
{
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time( path.get_unrelative_path(), error );
    if( error ) { return 0; }
    return std::chrono::system_clock::to_time_t(
               std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                   stamp - decltype( stamp )::clock::now() + std::chrono::system_clock::now() ) );
}

std::string time_text( std::time_t stamp )
{
    if( !stamp ) { return "未知"; }
    const auto *local = std::localtime( &stamp );
    char buffer[64] = {};
    if( !local || !std::strftime( buffer, sizeof( buffer ), "%Y-%m-%d %H:%M", local ) ) {
        return "未知";
    }
    return buffer;
}

struct entry {
    WORLD *world;
    save_t save;
    std::time_t last_played;
};

class browser : public cataimgui::window
{
    public:
        std::vector<entry> entries;
        std::vector<int> filtered;
        int selected = -1;
        bool accepted = false;

        explicit browser( const std::string &world ) : cataimgui::window( "读取世界" ) {
            for( const auto &[name, w] : world_generator->get_all_worlds() ) {
                if( !world.empty() && world != name ) { continue; }
                for( const auto &s : w->world_saves ) {
                    if( MAP_SHARING::isSharing() && s.decoded_name() != MAP_SHARING::getUsername() ) {
                        continue;
                    }
                    const auto base = w->folder_path() / s.base_path();
                    auto stamp = modified( base + ".sav" );
                    if( !stamp ) { stamp = modified( base + ".sav.zzip" ); }
                    entries.push_back( { w.get(), s, stamp } );
                }
            }
            std::stable_sort( entries.begin(), entries.end(), []( const entry &a, const entry &b ) {
                return a.last_played > b.last_played;
            } );
            refilter();
        }

        void move( int delta ) {
            if( filtered.empty() ) { return; }
            auto it = std::find( filtered.begin(), filtered.end(), selected );
            int index = it == filtered.end() ? 0 : std::distance( filtered.begin(), it );
            index = std::clamp( index + delta, 0, static_cast<int>( filtered.size() ) - 1 );
            select( filtered[index] );
            follow_selection = true;
        }

        void draw() override {
            // Use the player's original CDDA palette, including its blue selection highlight.
            const std::vector<std::pair<ImGuiCol, nc_color>> colors = {
                { ImGuiCol_WindowBg, c_black }, { ImGuiCol_ChildBg, c_black },
                { ImGuiCol_TitleBg, c_black }, { ImGuiCol_TitleBgActive, c_black },
                { ImGuiCol_Text, c_light_gray }, { ImGuiCol_Border, c_dark_gray },
                { ImGuiCol_Separator, c_dark_gray }, { ImGuiCol_Header, c_blue },
                { ImGuiCol_HeaderHovered, c_blue }, { ImGuiCol_HeaderActive, c_blue },
                { ImGuiCol_Button, c_blue }, { ImGuiCol_ButtonHovered, c_light_blue },
                { ImGuiCol_ButtonActive, c_blue }, { ImGuiCol_FrameBg, c_dark_gray }
            };
            for( auto [id, color] : colors ) {
                ImGui::PushStyleColor( id, cataimgui::imvec4_from_color( color ) );
            }
            cataimgui::window::draw();
            ImGui::PopStyleColor( colors.size() );
        }

    private:
        std::string search;
        SDL_Texture_Ptr preview;
        int preview_width = 0, preview_height = 0;
        int days = -1;
        std::vector<std::string> mods;
        std::vector<std::string> difficulty;
        bool follow_selection = false;

        cataimgui::bounds get_bounds() override {
            const auto size = ImGui::GetMainViewport()->Size;
            return { -1, -1, std::min( 1100.f, size.x * .90f ),
                     std::min( 760.f, size.y * .88f ) };
        }

        void refilter() {
            filtered.clear();
            for( size_t i = 0; i < entries.size(); ++i ) {
                const auto &e = entries[i];
                if( search.empty() || lcmatch( e.save.decoded_name() + " " + e.world->world_name, search ) ) {
                    filtered.push_back( i );
                }
            }
            if( std::find( filtered.begin(), filtered.end(), selected ) == filtered.end() ) {
                select( filtered.empty() ? -1 : filtered.front() );
            }
        }

        void select( int index ) {
            selected = index;
            preview.reset();
            days = -1;
            mods.clear();
            difficulty.clear();
            if( index < 0 ) { return; }
            const auto &e = entries[index];
            const auto base = e.world->folder_path() / e.save.base_path();
            for( const auto &id : e.world->active_mod_order ) { mods.push_back( id.str() ); }
            for( const auto &[key, label] : difficulty_options ) {
                auto option = e.world->WORLD_OPTIONS.find( key );
                if( option != e.world->WORLD_OPTIONS.end() ) {
                    difficulty.push_back( label + "：" + option->second.getValueName() );
                }
            }
            try {
                if( auto value = json_loader::from_path_opt( base + ".preview.json" ) ) {
                    JsonObject data = *value;
                    data.allow_omitted_members();
                    days = data.get_int( "days", -1 );
                    data.read( "mods", mods );
                    data.read( "difficulty", difficulty );
                } else {
                    // Inspect old saves without loading their world or changing the avatar.
                    std::string text;
                    if( file_exist( base + ".sav" ) ) {
                        std::ifstream stream( ( base + ".sav" ).get_unrelative_path() );
                        if( stream.peek() == '#' ) { std::getline( stream, text ); }
                        text.assign( std::istreambuf_iterator<char>( stream ), {} );
                    } else if( file_exist( base + ".sav.zzip" ) ) {
                        auto archive = zzip::load( ( base + ".sav.zzip" ).get_unrelative_path() );
                        if( archive ) {
                            auto bytes = archive->get_file( std::filesystem::u8path( e.save.base_path() + ".sav" ) );
                            text.assign( reinterpret_cast<const char *>( bytes.data() ), bytes.size() );
                        }
                    }
                    if( !text.empty() ) {
                        JsonObject data = json_loader::from_string( std::move( text ) );
                        data.allow_omitted_members();
                        days = std::max( 0, ( data.get_int( "turn", 0 ) -
                                             data.get_int( "game_start", data.get_int( "calendar_start", 0 ) ) ) / 86400 ) + 1;
                    }
                }
            } catch( const std::exception &error ) {
                DebugLog( D_WARNING, D_MAIN ) << "save preview unavailable: " << error.what();
            }
            SDL_Surface_Ptr surface( IMG_Load( ( base + ".preview.png" ).generic_u8string().c_str() ) );
            if( surface && imclient ) {
                preview.reset( SDL_CreateTextureFromSurface( imclient->sdl_renderer.get(), surface.get() ) );
                preview_width = surface->w;
                preview_height = surface->h;
            }
        }

        void draw_controls() override {
            ImGui::SetNextItemWidth( -1 );
            if( ImGui::InputTextWithHint( "##save-search", "搜索存档名 / 世界名", &search ) ) { refilter(); }
            ImGui::Separator();
            const float footer = ImGui::GetFrameHeightWithSpacing() + 8;
            const float height = std::max( 1.f, ImGui::GetContentRegionAvail().y - footer );
            const float left = ImGui::GetContentRegionAvail().x * .40f;
            ImGui::BeginChild( "save-list", ImVec2( left, height ), ImGuiChildFlags_Borders );
            for( int index : filtered ) {
                const auto &e = entries[index];
                ImGui::PushID( index );
                const std::string label = e.save.decoded_name() + "\n" + e.world->world_name;
                if( ImGui::Selectable( label.c_str(), index == selected, ImGuiSelectableFlags_AllowDoubleClick,
                                      ImVec2( 0, ImGui::GetTextLineHeightWithSpacing() * 2 + 8 ) ) ) {
                    select( index );
                    if( ImGui::IsMouseDoubleClicked( 0 ) ) { accepted = true; }
                }
                if( follow_selection && index == selected ) { ImGui::SetScrollHereY(); }
                ImGui::Separator();
                ImGui::PopID();
            }
            follow_selection = false;
            if( filtered.empty() ) { ImGui::TextWrapped( "%s", entries.empty() ? "暂无可读取的存档" : "没有匹配的存档" ); }
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild( "save-details", ImVec2( 0, height ), ImGuiChildFlags_Borders );
            if( selected >= 0 ) {
                const auto &e = entries[selected];
                const float width = ImGui::GetContentRegionAvail().x;
                if( preview && preview_width > 0 && preview_height > 0 ) {
                    const float scale = std::min( width / preview_width, height * .43f / preview_height );
                    ImGui::SetCursorPosX( ImGui::GetCursorPosX() + ( width - preview_width * scale ) * .5f );
                    ImGui::Image( reinterpret_cast<ImTextureID>( preview.get() ),
                                  ImVec2( preview_width * scale, preview_height * scale ) );
                } else {
                    ImGui::BeginChild( "no-preview",
                                       ImVec2( width, std::min( width * 9.f / 16.f, height * .43f ) ),
                                       ImGuiChildFlags_Borders );
                    ImGui::TextWrapped( "暂无存档截图\n下次保存时自动生成。" );
                    ImGui::EndChild();
                }
                cataimgui::draw_colored_text( e.save.decoded_name(), c_yellow );
                ImGui::Separator();
                ImGui::TextWrapped( "世界名称：%s", e.world->world_name.c_str() );
                ImGui::TextWrapped( "角色名称：%s", e.save.decoded_name().c_str() );
                ImGui::TextWrapped( "最后游玩：%s", time_text( e.last_played ).c_str() );
                if( days >= 0 ) { ImGui::Text( "存档天数：第 %d 天", days ); }
                else { ImGui::TextUnformatted( "存档天数：未知" ); }
                ImGui::Separator();
                cataimgui::draw_colored_text( "存档难度（世界设置）", c_light_cyan );
                for( const auto &line : difficulty ) { ImGui::TextWrapped( "%s", line.c_str() ); }
                ImGui::Separator();
                cataimgui::draw_colored_text( string_format( "加载的模组（%d）", mods.size() ), c_light_cyan );
                for( const auto &id : mods ) {
                    const mod_id mod( id );
                    const auto label = mod.is_valid() ? mod->name() : id + "（缺失）";
                    cataimgui::draw_colored_text( label, mod.is_valid() ? c_light_gray : c_light_red, width );
                }
            }
            ImGui::EndChild();
            ImGui::Separator();
            if( ImGui::Button( "返回 [Esc]" ) ) { is_open = false; }
            ImGui::SameLine();
            ImGui::Text( "%d 个存档", static_cast<int>( filtered.size() ) );
            ImGui::SameLine();
            const float button_width = ImGui::CalcTextSize( "读取存档 [Enter]" ).x +
                                       ImGui::GetStyle().FramePadding.x * 2;
            ImGui::SetCursorPosX( std::max( ImGui::GetCursorPosX(),
                                           ImGui::GetWindowContentRegionMax().x - button_width ) );
            ImGui::BeginDisabled( selected < 0 );
            if( ImGui::Button( "读取存档 [Enter]" ) ) { accepted = true; }
            ImGui::EndDisabled();
        }
};
}

void record()
{
    if( !world_generator->active_world || is_native_menu_demo_worker() ) { return; }
    try {
        const auto base = PATH_INFO::player_base_save_path();
        const auto &world = *world_generator->active_world;
        std::vector<std::string> mods, difficulty;
        for( const auto &id : world.active_mod_order ) { mods.push_back( id.str() ); }
        for( const auto &[key, label] : difficulty_options ) {
            auto option = world.WORLD_OPTIONS.find( key );
            if( option != world.WORLD_OPTIONS.end() ) {
                difficulty.push_back( label + "：" + option->second.getValueName() );
            }
        }
        write_to_file( base + ".preview.json", [&]( std::ostream &stream ) {
            JsonOut out( stream );
            out.start_object();
            out.member( "version", 1 );
            out.member( "world", world.world_name );
            out.member( "character", get_avatar().name );
            out.member( "days", std::max( 0, to_days<int>( calendar::turn - calendar::start_of_game ) ) + 1 );
            out.member( "difficulty", difficulty );
            out.member( "mods", mods );
            out.end_object();
        } );
        if( !screenshot( base + ".preview.png" ) ) {
            DebugLog( D_WARNING, D_MAIN ) << "Could not capture save preview";
        }
    } catch( const std::exception &error ) {
        DebugLog( D_WARNING, D_MAIN ) << "Could not write save preview: " << error.what();
    }
}

std::optional<std::pair<std::string, save_t>> browse( const std::string &world )
{
    browser panel( world );
    input_context input( "SAVE_BROWSER", keyboard_mode::keycode );
    for( const auto *action : { "QUIT", "CONFIRM", "UP", "DOWN", "PAGE_UP", "PAGE_DOWN",
                               "SELECT", "MOUSE_MOVE", "ANY_INPUT" } ) { input.register_action( action ); }
    while( panel.get_is_open() && !panel.accepted ) {
        ui_manager::redraw();
        const auto action = input.handle_input( 33 );
        if( action == "QUIT" ) { break; }
        if( !ImGui::GetIO().WantTextInput ) {
            if( action == "CONFIRM" && panel.selected >= 0 ) { panel.accepted = true; }
            if( action == "UP" || action == "DOWN" ) { panel.move( action == "UP" ? -1 : 1 ); }
            if( action == "PAGE_UP" || action == "PAGE_DOWN" ) { panel.move( action == "PAGE_UP" ? -8 : 8 ); }
        }
    }
    if( panel.accepted && panel.selected >= 0 ) {
        const auto &e = panel.entries[panel.selected];
        return std::make_pair( e.world->world_name, e.save );
    }
    return std::nullopt;
}
}
#endif
