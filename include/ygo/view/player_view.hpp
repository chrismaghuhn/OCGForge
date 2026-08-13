#pragma once

#include <cstdint>
#include <vector>

namespace ygo::view {

struct VisibleCard {
    std::uint8_t controller = 0;
    std::uint8_t owner = 0;
    std::uint32_t location = 0;
    std::uint32_t sequence = 0;
    std::uint8_t position = 0;
    std::uint32_t passcode = 0;
    bool public_identity = false;
};

struct PlayerViewCard {
    std::uint8_t controller = 0;
    std::uint32_t location = 0;
    std::uint32_t sequence = 0;
    std::uint8_t position = 0;
    std::uint32_t passcode = 0;
    bool identity_visible = false;
};

struct PlayerView {
    std::uint8_t perspective = 0;
    std::vector<PlayerViewCard> cards;
    std::uint32_t own_deck_count = 0;
    std::uint32_t own_hand_count = 0;
    std::uint32_t opponent_deck_count = 0;
    std::uint32_t opponent_hand_count = 0;
};

PlayerView project_player_view(std::uint8_t perspective, const std::vector<VisibleCard>& cards,
                              std::uint32_t own_deck_count, std::uint32_t own_hand_count,
                              std::uint32_t opponent_deck_count, std::uint32_t opponent_hand_count);

}  // namespace ygo::view
