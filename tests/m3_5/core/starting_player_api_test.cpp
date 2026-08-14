#include <cstdint>
#include <exception>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "ocgapi_constants.h"
#include "public_core_harness.hpp"

#ifndef YGO_M35_PLAYER_A
#error "YGO_M35_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M35_PLAYER_B
#error "YGO_M35_PLAYER_B must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct OpeningState {
    std::uint8_t first_turn = 255;
    std::uint32_t player_zero_hand = 0;
    std::uint32_t player_one_hand = 0;
};

OpeningState run(std::optional<std::uint8_t> selected_player) {
    m35::core_test::PublicCoreDuel duel;
    duel.load_deck(0, ygo::core::load_fixture_deck(YGO_M35_PLAYER_A));
    duel.load_deck(1, ygo::core::load_fixture_deck(YGO_M35_PLAYER_B));
    if (selected_player.has_value()) {
        require(duel.set_starting_player(*selected_player) == 1,
                "M3.5 valid starting-player selection was rejected");
    }
    duel.start();
    std::optional<std::uint8_t> first_turn;
    for (std::size_t step = 0; step < 8 && !first_turn.has_value(); ++step) {
        duel.process_once();
        first_turn = duel.first_new_turn_player();
    }
    if (!first_turn.has_value()) {
        std::ostringstream raw;
        raw << std::hex << std::setfill('0');
        for (const auto byte : duel.last_message()) {
            raw << std::setw(2) << static_cast<unsigned>(byte);
        }
        throw std::runtime_error("M3.5 startup did not emit MSG_NEW_TURN; raw=" + raw.str());
    }
    return {*first_turn, duel.query_count(0, LOCATION_HAND), duel.query_count(1, LOCATION_HAND)};
}

}  // namespace

int main() {
    try {
        const auto default_state = run(std::nullopt);
        require(default_state.first_turn == 0, "M3.5 default starting player changed from player 0");

        const auto explicit_zero = run(0);
        require(explicit_zero.first_turn == 0, "M3.5 explicit player 0 did not start player 0");

        const auto explicit_one = run(1);
        require(explicit_one.first_turn == 1, "M3.5 explicit player 1 did not start player 1");
        require(explicit_one.player_zero_hand == 5 && explicit_one.player_one_hand == 5,
                "M3.5 starting-player selection changed opening draw counts");

        m35::core_test::PublicCoreDuel invalid;
        invalid.load_deck(0, ygo::core::load_fixture_deck(YGO_M35_PLAYER_A));
        invalid.load_deck(1, ygo::core::load_fixture_deck(YGO_M35_PLAYER_B));
        require(invalid.set_starting_player(2) == 0, "M3.5 invalid starting player was accepted");
        invalid.start();
        require(invalid.set_starting_player(1) == 0,
                "M3.5 post-start starting-player mutation was accepted");

        std::cout << "m35_starting_player_api=pass\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
