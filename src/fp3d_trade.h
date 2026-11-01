#pragma once
#ifndef CATA_SRC_FP3D_TRADE_H
#define CATA_SRC_FP3D_TRADE_H
#if defined(TILES) && defined(ENABLE_FP3D)
#include <array>
#include <map>
#include <optional>
#include <string>
#include <vector>
#include "cata_imgui.h"
#include "trade_ui.h"

namespace fp3d_trade
{
// The renderer queues choices; process_action applies them to the native trade
// selector outside UI redraw.  Item transfer remains in npc_trading::trade.
class window : public cataimgui::window
{
    public:
        window( trade_ui &trade, int scale_percent );
        ~window() override;
        void draw() override;
        void handle_action( const std::string &action );
        std::optional<trade_ui::trade_result_t> process_action();

    protected:
        cataimgui::bounds get_bounds() override;
        void draw_controls() override;

    private:
        struct row {
            item_location location;
            std::string name;
            std::string group;
            std::string denial;
            std::string icon;
            int available;
            int chosen;
            int price;
            int category;
        };
        struct group {
            std::string name;
            std::vector<size_t> entries;
        };
        enum class action { none, quantity, balance, clear, bank, confirm, accept, cancel };
        trade_ui &trade;
        float scale;
        bool restore_no_mouse;
        bool reset_scroll = true;
        std::array<std::vector<row>, 2> rows;
        std::array<std::vector<group>, 2> filtered;
        std::array<bool, 2> filter_dirty{{ true, true }};
        std::array<std::string, 2> search;
        std::array<int, 2> category{};
        std::array<item_location, 2> focused;
        std::array<bool, 2> selected_only{};
        std::map<std::string, SDL_Texture_Ptr> icons;
        SDL_Texture_Ptr background;
        action pending = action::none;
        size_t pending_side = 0;
        item_location pending_item;
        int pending_quantity = 0;
        std::string message;
        bool offer_valid = false;
        bool open_confirmation = false;
        void refresh();
        void filter( size_t side );
        void card( size_t side, const row &entry, bool compact );
        void pane( size_t side, float width, float height );
        void quantity( size_t side, const row &entry, int count );
};
}
#endif
#endif
