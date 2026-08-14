#include <algorithm>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include "ocgapi_constants.h"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/trace/sha256.hpp"

#ifndef YGO_M2_PLAYER_A
#error "YGO_M2_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M2_PLAYER_B
#error "YGO_M2_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M2_CARDSCRIPTS
#error "YGO_M2_CARDSCRIPTS must be supplied by CMake"
#endif

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::observation::ObservationBuildConfig make_observation_config() {
    ygo::observation::ObservationBuildConfig config;
    config.own_deck.known = true;
    config.own_deck.main_deck = {32864, 549481, 732302};
    config.own_deck.extra_deck = {146746};
    config.opponent_deck.known = false;
    return config;
}

std::unique_ptr<ygo::core::CoreHost> make_world(std::uint32_t code, std::uint32_t location,
                                                std::uint32_t position, std::uint8_t team = 1) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 5;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    auto host = std::make_unique<ygo::core::CoreHost>(core_config);
    host->load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host->load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host->load_fixture_card(team, code, location, 0, position);
    host->start_duel();
    (void)host->process();
    return host;
}

std::unique_ptr<ygo::core::CoreHost> make_hidden_deck_world(bool reverse_opponent_deck) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    auto host = std::make_unique<ygo::core::CoreHost>(core_config);
    const auto player_a = ygo::core::load_fixture_deck(YGO_M2_PLAYER_A);
    auto player_b = ygo::core::load_fixture_deck(YGO_M2_PLAYER_B);
    if (reverse_opponent_deck) {
        std::reverse(player_b.main_deck.begin(), player_b.main_deck.end());
    }
    host->load_deck(0, player_a);
    host->load_deck(1, player_b);
    host->start_duel();
    (void)host->process();
    return host;
}

std::unique_ptr<ygo::core::CoreHost> make_hidden_hand_world(std::uint32_t code) {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 0;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    auto host = std::make_unique<ygo::core::CoreHost>(core_config);
    host->load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host->load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host->load_fixture_card(1, code, LOCATION_HAND, 0, POS_FACEDOWN_DEFENSE);
    host->start_duel();
    (void)host->process();
    return host;
}

int run() {
    ygo::core::CoreHostConfig core_config;
    core_config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    core_config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    core_config.starting_draw_count = 5;
    core_config.draw_count_per_turn = 0;
    core_config.seed.words = {0x0123456789abcdefULL, 0xfedcba9876543210ULL,
                              0x13579bdf2468ace0ULL, 0x0eca8642fdb97531ULL};
    ygo::core::CoreHost host(core_config);
    host.load_deck(0, ygo::core::load_fixture_deck(YGO_M2_PLAYER_A));
    host.load_deck(1, ygo::core::load_fixture_deck(YGO_M2_PLAYER_B));
    host.start_duel();
    (void)host.process();

    const auto before_engine = ygo::trace::sha256_bytes(host.query_field());
    const auto before_process_count = host.process_call_count();
    const auto config = make_observation_config();
    const auto first = ygo::observation::build_player_observation(host, 0, config);
    const auto second = ygo::observation::build_player_observation(host, 0, config);
    require(ygo::observation::canonical_serialize(first) == ygo::observation::canonical_serialize(second),
            "repeated observations were not canonical-deterministic");
    require(first.observation_hash == second.observation_hash, "repeated observation hash changed");
    require(before_engine == ygo::trace::sha256_bytes(host.query_field()),
            "observation changed engine state");
    require(before_process_count == host.process_call_count(), "observation advanced engine processing");

    bool own_hand_visible = false;
    bool opponent_hand_entity = false;
    for (const auto& entity : first.entities) {
        require(entity.zone != ygo::observation::SemanticZone::MainDeck,
                "current Main Deck entity leaked into observation");
        if (entity.zone == ygo::observation::SemanticZone::Hand && entity.controller.value_or(2) == 0) {
            own_hand_visible = own_hand_visible || entity.identity_known;
        }
        if (entity.zone == ygo::observation::SemanticZone::Hand && entity.controller.value_or(2) == 1) {
            opponent_hand_entity = true;
        }
    }
    require(own_hand_visible, "own hand identity was over-redacted");
    require(!opponent_hand_entity, "opponent hidden hand entity leaked");
    require(!first.match_context.opponent_deck.known, "opponent decklist defaulted to omniscient");
    require(first.match_context.own_deck.known, "own static deck knowledge was not retained");

    const auto mirrored = ygo::observation::build_player_observation(host, 1, config);
    require(mirrored.perspective_player == 1, "mirrored perspective was not explicit");
    require(mirrored.observation_hash != first.observation_hash,
            "opposite perspective collapsed to the same observation");

    const auto hidden_field_a = make_world(146746, LOCATION_MZONE, POS_FACEDOWN_DEFENSE);
    const auto hidden_field_b = make_world(41546, LOCATION_MZONE, POS_FACEDOWN_DEFENSE);
    const auto hidden_a = ygo::observation::build_player_observation(*hidden_field_a, 0, config);
    const auto hidden_b = ygo::observation::build_player_observation(*hidden_field_b, 0, config);
    require(ygo::observation::canonical_serialize(hidden_a) == ygo::observation::canonical_serialize(hidden_b),
            "hidden opponent field identity changed the player observation");
    require(hidden_a.observation_hash == hidden_b.observation_hash,
            "hidden opponent field identity changed the observation hash");

    const auto hidden_hand_a = make_hidden_hand_world(146746);
    const auto hidden_hand_b = make_hidden_hand_world(359563);
    const auto hidden_hand_observation_a = ygo::observation::build_player_observation(*hidden_hand_a, 0, config);
    const auto hidden_hand_observation_b = ygo::observation::build_player_observation(*hidden_hand_b, 0, config);
    require(ygo::observation::canonical_serialize(hidden_hand_observation_a) ==
                ygo::observation::canonical_serialize(hidden_hand_observation_b),
            "hidden opponent Hand identity changed the player observation");
    require(hidden_hand_observation_a.observation_hash == hidden_hand_observation_b.observation_hash,
            "hidden opponent Hand identity changed the observation hash");

    const auto revealed_field_a = make_world(146746, LOCATION_MZONE, POS_FACEUP_ATTACK);
    const auto revealed_field_b = make_world(41546, LOCATION_MZONE, POS_FACEUP_ATTACK);
    const auto revealed_a = ygo::observation::build_player_observation(*revealed_field_a, 0, config);
    const auto revealed_b = ygo::observation::build_player_observation(*revealed_field_b, 0, config);
    require(ygo::observation::canonical_serialize(revealed_a) != ygo::observation::canonical_serialize(revealed_b),
            "public field reveal did not change the player observation");

    const auto hidden_extra_a = make_world(146746, LOCATION_EXTRA, POS_FACEDOWN_DEFENSE);
    const auto hidden_extra_b = make_world(359563, LOCATION_EXTRA, POS_FACEDOWN_DEFENSE);
    const auto extra_a = ygo::observation::build_player_observation(*hidden_extra_a, 0, config);
    const auto extra_b = ygo::observation::build_player_observation(*hidden_extra_b, 0, config);
    const auto extra_bytes_a = ygo::observation::canonical_serialize(extra_a);
    const auto extra_bytes_b = ygo::observation::canonical_serialize(extra_b);
    require(extra_bytes_a == extra_bytes_b,
            "hidden opponent Extra Deck identity changed the player observation");
    const auto public_extra_a = make_world(146746, LOCATION_EXTRA, POS_FACEUP_ATTACK);
    const auto public_extra_b = make_world(359563, LOCATION_EXTRA, POS_FACEUP_ATTACK);
    const auto public_a = ygo::observation::build_player_observation(*public_extra_a, 0, config);
    const auto public_b = ygo::observation::build_player_observation(*public_extra_b, 0, config);
    require(ygo::observation::canonical_serialize(public_a) != ygo::observation::canonical_serialize(public_b),
            "public Extra Deck reveal did not change the player observation");

    const auto hidden_deck_a = make_hidden_deck_world(false);
    const auto hidden_deck_b = make_hidden_deck_world(true);
    for (const auto perspective : {std::uint8_t{0}, std::uint8_t{1}}) {
        const auto observation_a = ygo::observation::build_player_observation(*hidden_deck_a, perspective, config);
        const auto observation_b = ygo::observation::build_player_observation(*hidden_deck_b, perspective, config);
        require(ygo::observation::canonical_serialize(observation_a) ==
                    ygo::observation::canonical_serialize(observation_b),
                "hidden deck order changed the perspective-safe observation");
        require(observation_a.observation_hash == observation_b.observation_hash,
                "hidden deck order changed the perspective-safe observation hash");
    }

    const auto own_set = make_world(146746, LOCATION_MZONE, POS_FACEDOWN_DEFENSE, 0);
    const auto own_set_observation = ygo::observation::build_player_observation(*own_set, 0, config);
    const auto own_set_card = std::find_if(
        own_set_observation.entities.begin(), own_set_observation.entities.end(), [](const auto& entity) {
            return entity.zone == ygo::observation::SemanticZone::MonsterZone &&
                   entity.controller.value_or(2) == 0 && entity.identity_known &&
                   entity.passcode.value_or(0) == 146746;
        });
    require(own_set_card != own_set_observation.entities.end(),
            "own face-down card identity was over-redacted");
    std::cout << "observation_builder=ok\n"
              << "player0_hash=" << first.observation_hash << '\n'
              << "player1_hash=" << mirrored.observation_hash << '\n';
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
