#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

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
#ifndef YGO_M0_PLAYER_A
#error "YGO_M0_PLAYER_A must be supplied by CMake"
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

}  // namespace

int main() {
    try {
        const auto deck_a = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
        const auto deck_b = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
        const auto legacy = ygo::core::load_fixture_deck(YGO_M0_PLAYER_A);
        require(deck_a.main_deck.size() == 40, "Deck A Main count mismatch");
        require(deck_a.extra_deck.size() == 15, "Deck A Extra count mismatch");
        require(deck_b.main_deck.size() == 40, "Deck B Main count mismatch");
        require(deck_b.extra_deck.size() == 15, "Deck B Extra count mismatch");
        require(!deck_a.sha256.empty() && !deck_b.sha256.empty(), "YDK hashes are empty");
        require(legacy.main_deck.size() == 40, "legacy Main count mismatch");
        require(legacy.extra_deck.empty(), "legacy .deck unexpectedly has Extra cards");

        ygo::core::CoreHostConfig config;
        config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
        config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
        config.rules.bundle_id = std::string(ygo::m3::canonical_rules().rules_bundle_id);
        config.duel_flags = ygo::m3::canonical_rules().duel_flags;
        config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                             0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
        ygo::core::CoreHost host(config);
        host.load_deck(0, deck_a);
        host.load_deck(1, deck_b);
        host.start_duel();
        require(host.query_count(0, LOCATION_EXTRA) == 15, "Deck A Extra cards were not registered");
        require(host.query_count(1, LOCATION_EXTRA) == 15, "Deck B Extra cards were not registered");
        std::cout << "deck_loader=ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
