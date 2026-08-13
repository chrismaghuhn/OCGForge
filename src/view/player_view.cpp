#include "ygo/view/player_view.hpp"

#include "ocgapi_constants.h"

namespace ygo::view {

PlayerView project_player_view(std::uint8_t perspective, const std::vector<VisibleCard>& cards,
                               std::uint32_t own_deck_count, std::uint32_t own_hand_count,
                               std::uint32_t opponent_deck_count, std::uint32_t opponent_hand_count) {
    PlayerView view;
    view.perspective = perspective;
    view.own_deck_count = own_deck_count;
    view.own_hand_count = own_hand_count;
    view.opponent_deck_count = opponent_deck_count;
    view.opponent_hand_count = opponent_hand_count;
    for (const VisibleCard& card : cards) {
        PlayerViewCard projected;
        projected.controller = card.controller;
        projected.location = card.location;
        projected.sequence = card.sequence;
        projected.position = card.position;
        const bool own_identity = card.owner == perspective || card.controller == perspective;
        // A player sees their own hand and controlled cards. Opponent facedown
        // cards remain redacted unless the engine has marked the identity
        // public; hidden deck entries are handled below.
        projected.identity_visible = card.public_identity || own_identity;
        if (card.location == LOCATION_DECK ||
            (!projected.identity_visible && card.location == LOCATION_HAND)) {
            // Hidden deck order and opponent hand identities are represented by
            // zone counts, not by records that could preserve hidden ordering.
            continue;
        }
        projected.passcode = projected.identity_visible ? card.passcode : 0;
        view.cards.push_back(projected);
    }
    return view;
}

}  // namespace ygo::view
