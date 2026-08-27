#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ygo/core/rules_bundle.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trace/engine_trace.hpp"

#ifdef YGO_M4_PERFORMANCE_AUDIT
#include "ygo/observation/performance_audit.hpp"
#endif

namespace ygo::environment {

struct EpisodeDriverConfig final {
    core::RulesBundlePaths rules;
    core::FixtureDeck player_zero_deck;
    core::FixtureDeck player_one_deck;
    std::uint64_t seed = 0;
    std::uint64_t duel_flags = 0;
    std::uint32_t starting_draw_count = 5;
    std::uint32_t draw_count_per_turn = 1;
    std::uint8_t starting_player = 0;
    std::uint64_t engine_process_budget = 0;
    std::uint64_t semantic_action_budget = std::numeric_limits<std::uint64_t>::max();
    bool build_full_observation = true;
    std::vector<std::uint32_t> required_script_codes;
    std::filesystem::path fixture_setup_script;
    bool instrumentation = false;
    bool force_unsupported_for_test = false;
#ifdef YGO_M4_PERFORMANCE_AUDIT
    observation::PerformanceAuditCollector* performance_audit = nullptr;
#endif
};

struct DriverDecisionBoundary final {
    const protocol::DecisionRequest* request = nullptr;
    const observation::PlayerObservation* observation = nullptr;
};

// DriverBoundary references remain valid only until the next mutating
// EpisodeDriver call or driver destruction. The request is non-null for every
// decision boundary; observation is non-null in full-observation mode and is
// null only when the existing diagnostic observation mode is disabled.
struct DriverGameTerminal final {
    std::uint8_t winner = 255;
    std::uint8_t win_reason = 255;
    std::optional<observation::PlayerObservation> player_zero_observation;
    std::optional<observation::PlayerObservation> player_one_observation;
};

struct DriverProcessBudgetExceeded final {
    std::uint64_t process_calls = 0;
};

struct DriverSemanticActionBudgetExceeded final {
    std::uint64_t semantic_actions = 0;
};

struct DriverAdministrativeInterrupt final {
    std::uint64_t process_calls = 0;
    std::uint64_t semantic_actions = 0;
};

struct DriverErrorCounters final {
    std::uint64_t retries = 0;
    std::uint64_t unsupported = 0;
    std::uint64_t automatic = 0;
    std::uint64_t truncated = 0;
    std::uint64_t core_errors = 0;
};

struct DriverFailure final {
    DriverErrorCounters errors;
    std::string failure_code;
    std::string error_message;
    std::string failure_stage;
    bool mutation_may_have_occurred = false;
};

struct DriverTimingMetrics final {
    std::uint64_t core_process_us = 0;
    std::uint64_t protocol_candidate_us = 0;
    std::uint64_t continuation_us = 0;
    std::uint64_t observation_us = 0;
    std::uint64_t trace_hash_us = 0;
};

struct DriverOperationMetrics final {
    std::uint64_t ocg_duel_process = 0;
    std::uint64_t ocg_duel_query = 0;
    std::uint64_t ocg_duel_query_location = 0;
    std::uint64_t ocg_duel_query_field = 0;
    std::uint64_t ocg_duel_query_count = 0;
    std::uint64_t script_reader_requests = 0;
    std::uint64_t script_loads = 0;
    std::uint64_t observations = 0;
    std::uint64_t entities_projected = 0;
    std::uint64_t candidate_sets = 0;
    std::uint64_t candidate_total = 0;
    std::uint64_t candidate_max = 0;
};

struct DriverMetrics final {
    DriverErrorCounters errors;
    DriverTimingMetrics timing;
    DriverOperationMetrics operations;
    std::uint64_t process_call_count = 0;
    std::uint64_t response_submission_count = 0;
    std::uint32_t interactive_decisions = 0;
    std::uint64_t semantic_action_count = 0;
    std::uint32_t continuation_intermediate_steps = 0;
    std::uint32_t turns = 0;
    std::uint32_t battle_command_count = 0;
    std::uint64_t visible_life_points_event_count = 0;
    std::uint64_t visible_destroyed_event_count = 0;
    std::uint64_t visible_win_event_count = 0;
    std::uint64_t observation_entity_total = 0;
    std::uint64_t observation_event_total = 0;
    std::uint64_t response_build_time_us_total = 0;
    std::uint64_t response_build_time_us_max = 0;
};

using DriverBoundary = std::variant<DriverDecisionBoundary, DriverGameTerminal,
                                    DriverProcessBudgetExceeded, DriverSemanticActionBudgetExceeded,
                                    DriverAdministrativeInterrupt, DriverFailure>;

struct DriverAcceptedAction final {
    std::string selected_semantic_key;
    bool core_response_submitted = false;
    std::optional<std::string> final_response_sha256;
};

struct DriverApplyResult final {
    std::optional<DriverAcceptedAction> accepted;
    DriverBoundary next;
};

class EpisodeDriver final {
public:
    explicit EpisodeDriver(EpisodeDriverConfig config);
    ~EpisodeDriver();

    EpisodeDriver(const EpisodeDriver&) = delete;
    EpisodeDriver& operator=(const EpisodeDriver&) = delete;
    EpisodeDriver(EpisodeDriver&&) = delete;
    EpisodeDriver& operator=(EpisodeDriver&&) = delete;

    DriverBoundary advance_until_boundary();
    DriverApplyResult apply_semantic_key(const std::string& semantic_key);
    DriverBoundary administrative_interrupt();

    const trace::EngineTrace& trace() const noexcept;
    const DriverMetrics& metrics() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace ygo::environment
