#include "ygo/environment/episode_driver.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>
#include <variant>

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

ygo::environment::EpisodeDriverConfig make_config() {
    const auto& rules = ygo::m3::canonical_rules();
    ygo::environment::EpisodeDriverConfig config;
    config.rules.card_scripts_root = YGO_M3_CARDSCRIPTS;
    config.rules.card_data_tsv = YGO_M0_CARD_DATA_TSV;
    config.rules.bundle_id = std::string(rules.rules_bundle_id);
    config.rules.core_patchset_id = std::string(rules.core_patchset_id);
    config.rules.core_patchset_sha256 = std::string(rules.core_patchset_sha256);
    config.player_zero_deck = ygo::core::load_fixture_deck(YGO_M3_DECK_A);
    config.player_one_deck = ygo::core::load_fixture_deck(YGO_M3_DECK_B);
    config.seed = 2;
    config.duel_flags = rules.duel_flags;
    config.engine_process_budget = 64;
    config.semantic_action_budget = 64;
    config.build_full_observation = false;
    config.force_unsupported_for_test = true;
    return config;
}

void test_forced_protocol_failure_closes_driver() {
    ygo::environment::EpisodeDriver driver(make_config());
    const auto boundary = driver.advance_until_boundary();
    const auto* failure = std::get_if<ygo::environment::DriverFailure>(&boundary);
    if (failure == nullptr) {
        throw std::runtime_error("forced unsupported fixture did not fail closed");
    }
    if (failure->failure_code != "forced_unsupported" || failure->failure_stage != "advance" ||
        failure->mutation_may_have_occurred) {
        throw std::runtime_error("forced unsupported fixture returned the wrong typed failure");
    }
    assert(driver.metrics().semantic_action_count == 0);
    bool closed = false;
    try {
        (void)driver.advance_until_boundary();
    } catch (const std::logic_error&) {
        closed = true;
    }
    if (!closed) {
        throw std::runtime_error("failed driver remained advanceable");
    }
}

}  // namespace

int main() {
    try {
        test_forced_protocol_failure_closes_driver();
        std::cout << "episodic_fault_injection_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
