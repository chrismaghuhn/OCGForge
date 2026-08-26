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
        const auto boundary = driver.advance_until_boundary();
        const auto* decision = std::get_if<ygo::environment::DriverDecisionBoundary>(&boundary);
        if (decision == nullptr || decision->request == nullptr) {
            throw std::runtime_error("stale-key fixture did not publish an interactive decision");
        }

        const auto* request_before = decision->request;
        const auto* observation_before = decision->observation;
        const auto decision_id_before = request_before->decision_id;
        const auto candidate_count_before = request_before->candidates.size();
        const auto process_count_before = driver.metrics().process_call_count;
        const auto response_count_before = driver.metrics().response_submission_count;
        const auto candidate_sets_before = driver.metrics().operations.candidate_sets;
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
        assert(driver.metrics().process_call_count == process_count_before);
        assert(driver.metrics().response_submission_count == response_count_before);
        assert(driver.metrics().operations.candidate_sets == candidate_sets_before);
        assert(driver.trace().steps.size() == trace_steps_before);
        assert(request_before == decision->request);
        assert(observation_before == decision->observation);
        assert(request_before->decision_id == decision_id_before);
        assert(request_before->candidates.size() == candidate_count_before);
        std::cout << "episode_driver_stale_key=ok\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
