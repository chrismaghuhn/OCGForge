#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#include "common.h"
#include "ygo/core/core_error.hpp"
#include "ygo/core/core_host.hpp"
#include "ygo/core/rules_bundle.hpp"
#include "ygo/observation/observation_builder.hpp"
#include "ygo/observation/observation_session.hpp"
#include "ygo/observation/serialization.hpp"

#ifndef YGO_M2_PLAYER_A
#error "YGO_M2_PLAYER_A must be supplied by CMake"
#endif
#ifndef YGO_M2_PLAYER_B
#error "YGO_M2_PLAYER_B must be supplied by CMake"
#endif
#ifndef YGO_M2_CARDSCRIPTS
#error "YGO_M2_CARDSCRIPTS must be supplied by CMake"
#endif
#ifndef YGO_M2_SETUP
#error "YGO_M2_SETUP must be supplied by CMake"
#endif

namespace {

struct Arguments {
    std::string fixture = "m2";
    std::uint64_t seed = 0x0123456789abcdefULL;
    std::uint8_t player = 0;
    std::string output;
};

std::uint64_t parse_u64(const std::string& value) {
    std::size_t consumed = 0;
    const auto parsed = std::stoull(value, &consumed, 0);
    if (consumed != value.size()) {
        throw std::runtime_error("invalid seed: " + value);
    }
    return static_cast<std::uint64_t>(parsed);
}

Arguments parse_arguments(int argc, char** argv) {
    Arguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--fixture" && index + 1 < argc) {
            arguments.fixture = argv[++index];
        } else if (argument == "--seed" && index + 1 < argc) {
            arguments.seed = parse_u64(argv[++index]);
        } else if (argument == "--player" && index + 1 < argc) {
            const auto player = std::stoi(argv[++index]);
            if (player < 0 || player > 1) {
                throw std::runtime_error("player perspective must be 0 or 1");
            }
            arguments.player = static_cast<std::uint8_t>(player);
        } else if (argument == "--output" && index + 1 < argc) {
            arguments.output = argv[++index];
        } else {
            throw std::runtime_error(
                "usage: ygo_observation_probe [--fixture m2] [--seed N] [--player 0|1] [--output PATH]");
        }
    }
    return arguments;
}

ygo::core::SeedBundle seed_bundle(std::uint64_t seed) {
    return {{seed, seed ^ 0x9e3779b97f4a7c15ULL, seed + 0x6a09e667f3bcc909ULL,
             (seed << 1) ^ 0xbb67ae8584caa73bULL}};
}

ygo::observation::StaticDeckContext known_deck(const ygo::core::FixtureDeck& deck) {
    ygo::observation::StaticDeckContext context;
    context.known = true;
    context.main_deck = deck.main_deck;
    return context;
}

int run(const Arguments& arguments) {
    if (arguments.fixture != "m2") {
        throw std::runtime_error("unsupported fixture: " + arguments.fixture + " (only m2 is pinned in M2)");
    }
    const auto deck_a = ygo::core::load_fixture_deck(YGO_M2_PLAYER_A);
    const auto deck_b = ygo::core::load_fixture_deck(YGO_M2_PLAYER_B);

    ygo::core::CoreHostConfig config;
    config.rules.card_scripts_root = YGO_M2_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.duel_flags = DUEL_PZONE | DUEL_SEPARATE_PZONE;
    config.starting_draw_count = 0;
    config.draw_count_per_turn = 0;
    config.seed = seed_bundle(arguments.seed);

    ygo::core::CoreHost host(config);
    host.load_deck(0, deck_a);
    host.load_deck(1, deck_b);
    host.load_fixture_script(YGO_M2_SETUP);
    host.start_duel();

    ygo::observation::ObservationSession session(arguments.player,
                                                  static_cast<std::uint32_t>(config.duel_flags));
    const auto process_result = host.process();
    session.ingest(process_result.message, 0);

    ygo::observation::ObservationBuildConfig observation_config;
    observation_config.decision_index = 0;
    observation_config.engine_step_index = 0;
    observation_config.visible_events = session.visible_events();
    observation_config.knowledge.own_decklist_known = true;
    observation_config.knowledge.opponent_decklist_known = false;
    observation_config.own_deck = arguments.player == 0 ? known_deck(deck_a) : known_deck(deck_b);
    const auto observation = ygo::observation::build_player_observation(host, arguments.player,
                                                                         observation_config);
    const auto serialized = ygo::observation::canonical_serialize(observation);
    if (arguments.output.empty()) {
        std::cout << serialized;
    } else {
        std::ofstream stream(arguments.output, std::ios::binary);
        if (!stream) {
            throw std::runtime_error("cannot open observation output: " + arguments.output);
        }
        stream.write(serialized.data(), static_cast<std::streamsize>(serialized.size()));
    }
    std::cerr << "observation_hash=" << observation.observation_hash << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        return run(parse_arguments(argc, argv));
    } catch (const ygo::core::CoreError& error) {
        std::cerr << "core error: " << error.what() << '\n';
        return 4;
    } catch (const std::exception& error) {
        std::cerr << "probe error: " << error.what() << '\n';
        return 5;
    }
}
