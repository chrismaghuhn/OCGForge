#include "ygo/teacher/strategy_state.hpp"
#include "ygo/teacher/teacher_decision.hpp"

#include <cstddef>
#include <exception>
#include <string_view>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/trajectory/codec.hpp"

namespace ygo::teacher {
namespace {

bool canonical_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '_' || byte == '-')) {
            return false;
        }
    }
    return value.front() != '.' && value.back() != '.' &&
           value.find("..") == std::string_view::npos;
}

bool valid_profile_id(const std::string_view value) noexcept {
    return trajectory::is_canonical_identity(value, kStrategyProfileIdentityPrefix);
}

bool valid_optional_id(const std::optional<std::string>& value) noexcept {
    return !value.has_value() || canonical_token(*value);
}

bool valid_id_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_fact_vector(const std::vector<PublicFactValue>& values) {
    const auto& registry = PublicFactRegistry::canonical();
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!registry.validate(values[index])) {
            return false;
        }
        if (index > 0) {
            if (values[index - 1].fact_id == values[index].fact_id ||
                !(canonical_public_fact_value_bytes(values[index - 1]) <
                  canonical_public_fact_value_bytes(values[index]))) {
                return false;
            }
        }
    }
    return true;
}

bool valid_reason_vector(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index]) ||
            !is_registered_invalidation_reason(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool validate_strategy_state(const EpisodeLocalStrategyStateV1& state) noexcept {
    try {
        return valid_profile_id(state.strategy_profile_id) &&
               valid_optional_id(state.active_goal_id) &&
               valid_optional_id(state.active_line_id) &&
               valid_id_vector(state.completed_line_node_ids) &&
               valid_id_vector(state.achieved_goal_ids) &&
               valid_fact_vector(state.public_resource_facts) &&
               valid_fact_vector(state.public_restriction_facts) &&
               valid_fact_vector(state.public_threat_facts) &&
               (!state.last_accepted_public_action_key.has_value() ||
                environment::is_public_action_key(*state.last_accepted_public_action_key));
    } catch (...) {
        return false;
    }
}

bool validate_teacher_state_delta(const TeacherStateDeltaV1& delta) noexcept {
    try {
        return valid_profile_id(delta.strategy_profile_id) &&
               (!delta.base_last_accepted_public_action_key.has_value() ||
                environment::is_public_action_key(*delta.base_last_accepted_public_action_key)) &&
               environment::is_public_action_key(delta.proposed_for_public_action_key) &&
               valid_optional_id(delta.active_goal_id) && valid_optional_id(delta.active_line_id) &&
               valid_id_vector(delta.completed_line_node_ids) &&
               valid_id_vector(delta.achieved_goal_ids) &&
               valid_fact_vector(delta.public_resource_facts) &&
               valid_fact_vector(delta.public_restriction_facts) &&
               valid_fact_vector(delta.public_threat_facts) &&
               valid_reason_vector(delta.invalidation_reason_ids);
    } catch (...) {
        return false;
    }
}

std::optional<EpisodeLocalStrategyStateV1> reset_strategy_state(
    const StrategyProfileV1& validated_profile) noexcept {
    try {
        if (!validate_strategy_profile(validated_profile) ||
            !valid_profile_id(validated_profile.profile_id)) {
            return std::nullopt;
        }
        EpisodeLocalStrategyStateV1 state;
        state.strategy_profile_id = validated_profile.profile_id;
        if (!validate_strategy_state(state)) {
            return std::nullopt;
        }
        return state;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<TeacherStateDeltaV1> propose_teacher_state_delta(
    const EpisodeLocalStrategyStateV1& current_state,
    const StrategyProfileV1& validated_profile,
    const TeacherStateDeltaV1& requested_replacement) noexcept {
    try {
        if (!validate_strategy_state(current_state) ||
            !validate_strategy_profile(validated_profile) ||
            !validate_teacher_state_delta(requested_replacement) ||
            current_state.strategy_profile_id != validated_profile.profile_id ||
            requested_replacement.strategy_profile_id != validated_profile.profile_id ||
            requested_replacement.base_last_accepted_decision_index !=
                current_state.last_accepted_decision_index ||
            requested_replacement.base_last_accepted_public_action_key !=
                current_state.last_accepted_public_action_key) {
            return std::nullopt;
        }

        TeacherStateDeltaV1 result = requested_replacement;
        result.strategy_profile_id = validated_profile.profile_id;
        result.base_last_accepted_decision_index = current_state.last_accepted_decision_index;
        result.base_last_accepted_public_action_key =
            current_state.last_accepted_public_action_key;
        return validate_teacher_state_delta(result) ? std::optional<TeacherStateDeltaV1>(result)
                                                    : std::nullopt;
    } catch (...) {
        return std::nullopt;
    }
}

bool commit_teacher_state_delta(
    EpisodeLocalStrategyStateV1& current_state,
    const TeacherRankingResult& ranking_result,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& next_observation) noexcept {
    try {
        if (!validate_strategy_state(current_state) ||
            ranking_result.status != TeacherRankingStatus::Selected ||
            !ranking_result.selected_public_action_key.has_value() ||
            !ranking_result.proposed_state_delta.has_value() ||
            !validate_teacher_state_delta(*ranking_result.proposed_state_delta) ||
            !environment::is_public_action_key(
                *ranking_result.selected_public_action_key) ||
            !environment::is_public_action_key(accepted_transition.selected_public_action_key) ||
            ranking_result.proposed_state_delta->strategy_profile_id !=
                current_state.strategy_profile_id ||
            ranking_result.proposed_state_delta->base_last_accepted_decision_index !=
                current_state.last_accepted_decision_index ||
            ranking_result.proposed_state_delta->base_last_accepted_public_action_key !=
                current_state.last_accepted_public_action_key ||
            ranking_result.proposed_state_delta->proposed_for_public_action_key !=
                *ranking_result.selected_public_action_key ||
            accepted_transition.selected_public_action_key !=
                *ranking_result.selected_public_action_key) {
            return false;
        }

        const auto& delta = *ranking_result.proposed_state_delta;
        auto next_state = current_state;
        next_state.active_goal_id = delta.active_goal_id;
        next_state.active_line_id = delta.active_line_id;
        next_state.completed_line_node_ids = delta.completed_line_node_ids;
        next_state.achieved_goal_ids = delta.achieved_goal_ids;
        next_state.public_resource_facts = delta.public_resource_facts;
        next_state.public_restriction_facts = delta.public_restriction_facts;
        next_state.public_threat_facts = delta.public_threat_facts;
        next_state.last_accepted_decision_index = accepted_transition.decision_index;
        next_state.last_accepted_public_action_key =
            accepted_transition.selected_public_action_key;

        const auto reconciled = reconcile_strategy_state(next_state, next_observation);
        if (!reconciled.has_value()) {
            return false;
        }
        next_state = *reconciled;
        if (!validate_strategy_state(next_state)) {
            return false;
        }
        current_state = std::move(next_state);
        return true;
    } catch (...) {
        return false;
    }
}

bool observe_step_rejected(EpisodeLocalStrategyStateV1& state,
                           const environment::StepRejected& rejection) noexcept {
    try {
        return rejection.authoritative_state_unchanged && validate_strategy_state(state);
    } catch (...) {
        return false;
    }
}

}  // namespace ygo::teacher
