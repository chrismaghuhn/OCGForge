#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "ocgapi_constants.h"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/m3/canonical_rules.hpp"

#ifndef YGO_M3_DECK_A
#error "YGO_M3_DECK_A must be supplied by CMake"
#endif
#ifndef YGO_M3_DECK_B
#error "YGO_M3_DECK_B must be supplied by CMake"
#endif
#ifndef YGO_M3_CARDSCRIPTS
#error "YGO_M3_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::core::CoreHostConfig make_config(const std::vector<std::uint32_t>& required_scripts) {
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(ygo::m3::canonical_rules().rules_bundle_id);
    config.duel_flags = ygo::m3::canonical_rules().duel_flags;
    config.required_script_codes = required_scripts;
    config.starting_draw_count = 0;
    config.draw_count_per_turn = 0;
    config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                         0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    return config;
}

}  // namespace

int main() {
    try {
        const auto deck_a = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
        const auto deck_b = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
        const auto unique_codes = ygo::core::canonical_required_script_codes(deck_a, deck_b);
        require(unique_codes.size() == 50, "unexpected unique fixed-deck code count");

        auto config = make_config(unique_codes);
        ygo::core::CoreHost host(config);
        host.load_deck(0, deck_a);
        host.load_deck(1, deck_b);
        host.start_duel();
        // Register one additional public fixture instance for every unique
        // selected code. OCG_DuelNewCard exercises the pinned card-reader and
        // script-reader callbacks; the test is intentionally fail-closed on
        // any required effect-script or metadata error.
        std::uint32_t fixture_sequence = 0;
        for (const auto code : unique_codes) {
            host.load_fixture_card(0, code, LOCATION_HAND, fixture_sequence++, POS_FACEDOWN_DEFENSE);
        }
        (void)host.process();
        for (const auto code : unique_codes) {
            require(host.static_card_data(code).has_value(), "unique card lacks callback metadata");
        }
        std::cout << "unique_cards_instantiated=" << unique_codes.size() << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
