#include <cstdint>
#include <iostream>
#include <vector>
#include <algorithm>

#include "ocgapi_constants.h"
#include "ygo/view/player_view.hpp"

int main() {
    const std::vector<ygo::view::VisibleCard> cards = {
        {0, 0, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, 1001, false},
        {0, 0, LOCATION_DECK, 17, POS_FACEDOWN_DEFENSE, 8008, false},
        {1, 1, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE, 2002, false},
        {1, 1, LOCATION_MZONE, 0, POS_FACEUP_ATTACK, 3003, true},
        {1, 1, LOCATION_SZONE, 0, POS_FACEDOWN_DEFENSE, 4004, false},
        {1, 1, LOCATION_DECK, 17, POS_FACEDOWN_DEFENSE, 7007, false},
        {0, 0, LOCATION_GRAVE, 0, POS_FACEUP_ATTACK, 5005, false},
        {1, 0, LOCATION_MZONE, 1, POS_FACEUP_ATTACK, 6006, true},
    };
    const auto view = ygo::view::project_player_view(0, cards, 34, 5, 32, 4);
    const auto has_passcode = [&view](std::uint32_t passcode) {
        return std::any_of(view.cards.begin(), view.cards.end(), [passcode](const auto& card) {
            return card.passcode == passcode;
        });
    };
    const auto has_opponent_location = [&view](std::uint32_t location) {
        return std::any_of(view.cards.begin(), view.cards.end(), [location](const auto& card) {
            return card.controller == 1 && card.location == location;
        });
    };
    if (view.cards.size() != 5 || !has_passcode(1001) || !has_passcode(3003) || !has_passcode(5005) ||
        !has_passcode(6006) || has_passcode(2002) || has_passcode(7007) || has_passcode(8008) ||
        has_opponent_location(LOCATION_DECK) ||
        has_opponent_location(LOCATION_HAND) || view.opponent_hand_count != 4 || view.opponent_deck_count != 32) {
        std::cerr << "player-view redaction or count projection failed\n";
        return 1;
    }
    const auto facedown_opponent = std::find_if(view.cards.begin(), view.cards.end(), [](const auto& card) {
        return card.controller == 1 && card.location == LOCATION_SZONE;
    });
    if (facedown_opponent == view.cards.end() || facedown_opponent->passcode != 0 ||
        facedown_opponent->identity_visible) {
        std::cerr << "face-down opponent field card was not redacted\n";
        return 1;
    }
    return 0;
}
