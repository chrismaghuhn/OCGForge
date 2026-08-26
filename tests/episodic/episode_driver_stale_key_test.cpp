#include "ygo/environment/episode_driver.hpp"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

#include "ygo/m3/canonical_rules.hpp"
#include "ygo/protocol/protocol_error.hpp"

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
    config.starting_player = 0;
    config.engine_process_budget = 64;
    config.build_full_observation = false;
    return config;
}

}  // namespace

int main() {
    try {
        ygo::environment::EpisodeDriver driver(make_config());
        std::string valid_semantic_key;
        std::string decision_id_before;
        std::size_t candidate_count_before = 0;
        {
            const auto boundary = driver.advance_until_boundary();
            const auto* decision = std::get_if<ygo::environment::DriverDecisionBoundary>(&boundary);
            if (decision == nullptr || decision->request == nullptr) {
                throw std::runtime_error("stale-key fixture did not publish an interactive decision");
            }
            if (decision->request->candidates.empty()) {
                throw std::runtime_error("stale-key fixture published an empty candidate set");
            }
            valid_semantic_key = decision->request->candidates.front().semantic_key;
            decision_id_before = decision->request->decision_id;
            candidate_count_before = decision->request->candidates.size();
        }
        const auto metrics_before = driver.metrics();
        const auto trace_steps_before = driver.trace().steps.size();

        bool rejected = false;
        try {
            (void)driver.apply_semantic_key("stale.semantic.key");
        } catch (const ygo::protocol::ProtocolError& error) {
            rejected = true;
            if (error.code() != ygo::protocol::ProtocolErrorCode::InvalidSemanticKey) {
                throw;
            }
        }
        if (!rejected) {
            throw std::runtime_error("stale semantic key was accepted");
        }
        const auto& metrics_after_invalid = driver.metrics();
        assert(metrics_after_invalid.process_call_count == metrics_before.process_call_count);
        assert(metrics_after_invalid.response_submission_count == metrics_before.response_submission_count);
        assert(metrics_after_invalid.semantic_action_count == metrics_before.semantic_action_count);
        assert(metrics_after_invalid.operations.candidate_sets == metrics_before.operations.candidate_sets);
        assert(metrics_after_invalid.operations.candidate_total == metrics_before.operations.candidate_total);
        assert(metrics_after_invalid.operations.candidate_max == metrics_before.operations.candidate_max);
        assert(driver.trace().steps.size() == trace_steps_before);

        const auto valid_boundary = driver.apply_semantic_key(valid_semantic_key);
        if (std::holds_alternative<ygo::environment::DriverFailure>(valid_boundary) ||
            std::holds_alternative<ygo::environment::DriverProcessBudgetExceeded>(valid_boundary)) {
            throw std::runtime_error("copied valid semantic key did not advance decision " + decision_id_before +
                                     " with " + std::to_string(candidate_count_before) + " candidates");
        }
        assert(driver.metrics().semantic_action_count == metrics_before.semantic_action_count + 1);
        assert(driver.trace().steps.size() == trace_steps_before + 1);
        std::cout << "episode_driver_stale_key=ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
