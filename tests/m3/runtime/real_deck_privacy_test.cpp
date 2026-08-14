#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "common.h"
#include "ygo/core/core_host.hpp"
#include "ygo/m3/canonical_rules.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/serialization.hpp"

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

constexpr std::uint32_t kAsh = 14558127;
constexpr std::uint32_t kVeiler = 97268402;

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::uint32_t> required_scripts(const ygo::core::FixtureDeck& a,
                                            const ygo::core::FixtureDeck& b) {
    std::vector<std::uint32_t> codes = a.main_deck;
    codes.insert(codes.end(), a.extra_deck.begin(), a.extra_deck.end());
    codes.insert(codes.end(), b.main_deck.begin(), b.main_deck.end());
    codes.insert(codes.end(), b.extra_deck.begin(), b.extra_deck.end());
    std::sort(codes.begin(), codes.end());
    codes.erase(std::unique(codes.begin(), codes.end()), codes.end());
    return codes;
}

ygo::observation::PlayerObservation build_world(std::uint32_t hidden_code, bool face_up) {
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(ygo::m3::canonical_rules().rules_bundle_id);
    config.duel_flags = ygo::m3::canonical_rules().duel_flags;
    config.required_script_codes = required_scripts(deck_a, deck_b);
    config.seed.words = {2, 0x9e3779b97f4a7c17ULL, 0x6a09e667f3bcc90bULL, 0xbb67ae8584caa73fULL};
    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.start_duel();
    host.load_fixture_card(1, hidden_code, LOCATION_HAND, 0,
                           face_up ? POS_FACEUP_ATTACK : POS_FACEDOWN_DEFENSE);
    (void)host.process();

    ygo::observation::ObservationBuildConfig observation_config;
    observation_config.knowledge.own_decklist_known = true;
    observation_config.knowledge.opponent_decklist_known = true;
    observation_config.own_deck.known = true;
    observation_config.own_deck.main_deck = deck_a.main_deck;
    observation_config.own_deck.extra_deck = deck_a.extra_deck;
    observation_config.opponent_deck.known = true;
    observation_config.opponent_deck.main_deck = deck_b.main_deck;
    observation_config.opponent_deck.extra_deck = deck_b.extra_deck;
    return ygo::observation::build_player_observation(host, 0, observation_config);
}

int run() {
    const auto hidden_ash = build_world(kAsh, false);
    const auto hidden_veiler = build_world(kVeiler, false);
    require(hidden_ash.observation_hash == hidden_veiler.observation_hash,
            "hidden opponent hand identity changed the perspective observation hash");
    require(ygo::observation::canonical_serialize(hidden_ash) ==
                ygo::observation::canonical_serialize(hidden_veiler),
            "hidden opponent hand identity changed canonical observation bytes");

    const auto visible_ash = build_world(kAsh, true);
    const auto visible_veiler = build_world(kVeiler, true);
    require(visible_ash.observation_hash != visible_veiler.observation_hash,
            "revealed opponent identity did not change the perspective observation hash");
    require(ygo::observation::canonical_serialize(visible_ash) !=
                ygo::observation::canonical_serialize(visible_veiler),
            "revealed opponent identity did not change canonical observation bytes");
    std::cout << "m3_real_deck_privacy=ok\n"
              << "hidden_observation_hash=" << hidden_ash.observation_hash << '\n'
              << "visible_ash_observation_hash=" << visible_ash.observation_hash << '\n'
              << "visible_veiler_observation_hash=" << visible_veiler.observation_hash << '\n';
    return 0;
}

}  // namespace

int main() {
    try {
        return run();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
