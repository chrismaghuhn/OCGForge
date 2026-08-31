#include "ygo/teacher/goal_line_controller.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/teacher/deterministic_resolver.hpp"
#include "ygo/teacher/recovery_controller.hpp"

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

bool sorted_unique_ids(const std::vector<std::string>& values) noexcept {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index]) ||
            (index > 0 && !(values[index - 1] < values[index]))) {
            return false;
        }
    }
    return true;
}

bool valid_snapshot(const PublicFactSnapshot& snapshot) noexcept {
    try {
        const auto& registry = PublicFactRegistry::canonical();
        for (std::size_t index = 0; index < snapshot.values.size(); ++index) {
            if (!registry.validate(snapshot.values[index])) {
                return false;
            }
            if (index > 0 &&
                (snapshot.values[index - 1].fact_id == snapshot.values[index].fact_id ||
                 !(canonical_public_fact_value_bytes(snapshot.values[index - 1]) <
                   canonical_public_fact_value_bytes(snapshot.values[index])))) {
                return false;
            }
        }
        return true;
    } catch (...) {
        return false;
    }
}

bool valid_state_for_profile(const EpisodeLocalStrategyStateV1& state,
                             const StrategyProfileV1& profile) noexcept {
    if (!validate_strategy_state(state) || state.strategy_profile_id != profile.profile_id) {
        return false;
    }
    const auto goal_exists = [&profile](const std::string& id) noexcept {
        return std::any_of(profile.goals.begin(), profile.goals.end(), [&](const auto& goal) {
            return goal.goal_id == id;
        });
    };
    for (const auto& id : state.achieved_goal_ids) {
        if (!goal_exists(id)) {
            return false;
        }
    }
    if (state.active_goal_id.has_value() && !goal_exists(*state.active_goal_id)) {
        return false;
    }
    if (!state.active_line_id.has_value()) {
        return state.completed_line_node_ids.empty();
    }
    const auto line = std::find_if(
        profile.lines.begin(), profile.lines.end(), [&](const auto& value) {
            return value.line_id == *state.active_line_id;
        });
    if (line == profile.lines.end() || !state.active_goal_id.has_value() ||
        line->goal_id != *state.active_goal_id) {
        return false;
    }
    for (const auto& node_id : state.completed_line_node_ids) {
        if (std::none_of(line->nodes.begin(), line->nodes.end(), [&](const auto& node) {
                return node.node_id == node_id;
            })) {
            return false;
        }
    }
    return true;
}

const GoalDefinition* find_goal(const StrategyProfileV1& profile,
                                const std::string_view id) noexcept {
    const auto it = std::find_if(profile.goals.begin(), profile.goals.end(), [&](const auto& goal) {
        return goal.goal_id == id;
    });
    return it == profile.goals.end() ? nullptr : &*it;
}

const LineDefinition* find_line(const StrategyProfileV1& profile,
                                const std::string_view id) noexcept {
    const auto it = std::find_if(profile.lines.begin(), profile.lines.end(), [&](const auto& line) {
        return line.line_id == id;
    });
    return it == profile.lines.end() ? nullptr : &*it;
}

const ResourceDefinition* find_resource(const StrategyProfileV1& profile,
                                        const std::string_view id) noexcept {
    const auto it = std::find_if(
        profile.resources.begin(), profile.resources.end(), [&](const auto& resource) {
            return resource.resource_id == id;
        });
    return it == profile.resources.end() ? nullptr : &*it;
}

const CandidateIntentDefinition* find_intent(const StrategyProfileV1& profile,
                                             const std::string_view id) noexcept {
    const auto it = std::find_if(
        profile.candidate_intents.begin(), profile.candidate_intents.end(),
        [&](const auto& intent) { return intent.intent_id == id; });
    return it == profile.candidate_intents.end() ? nullptr : &*it;
}

const PublicFactDefinition* find_fact(const std::string_view id) noexcept {
    const auto& definitions = PublicFactRegistry::canonical().definitions();
    const auto it = std::find_if(definitions.begin(), definitions.end(), [&](const auto& value) {
        return value.fact_id == id;
    });
    return it == definitions.end() ? nullptr : &*it;
}

bool contains_id(const std::vector<std::string>& values, const std::string_view id) noexcept {
    return std::binary_search(values.begin(), values.end(), id);
}

void append_sorted_unique(std::vector<std::string>& values, const std::string& id) {
    values.push_back(id);
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}

PredicateEvaluationStatus evaluate_conjunction(
    const std::vector<PredicateRef>& predicates,
    const PublicFactSnapshot& public_facts,
    const StrategyProfileV1& profile,
    const environment::EnvironmentActionCandidate* candidate,
    const environment::PublicEnvironmentObservation* observation,
    const std::uint8_t owning_participant) noexcept {
    try {
        if (!valid_snapshot(public_facts) || !validate_strategy_profile(profile)) {
            return PredicateEvaluationStatus::Invalid;
        }
        if (predicates.empty()) {
            return PredicateEvaluationStatus::True;
        }
        std::vector<PredicateEvaluationStatus> statuses;
        statuses.reserve(predicates.size());
        for (const auto& predicate : predicates) {
            if (predicate.scope == PredicateScope::Observation) {
                statuses.push_back(evaluate_observation_predicate(predicate, public_facts));
            } else if (predicate.scope == PredicateScope::ProfileStatic) {
                statuses.push_back(evaluate_profile_static_predicate(predicate, profile));
            } else if (predicate.scope == PredicateScope::Candidate && candidate != nullptr &&
                       observation != nullptr) {
                statuses.push_back(evaluate_candidate_predicate(
                    predicate, *candidate, *observation, owning_participant, profile));
            } else {
                statuses.push_back(PredicateEvaluationStatus::Invalid);
            }
        }
        return combine_predicate_statuses(statuses);
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

PredicateEvaluationStatus goal_eligibility(const GoalDefinition& goal,
                                           const PublicFactSnapshot& facts,
                                           const StrategyProfileV1& profile) noexcept {
    const auto precondition = evaluate_conjunction(
        goal.preconditions, facts, profile, nullptr, nullptr, 0);
    const auto stop = goal.stop_predicates.empty()
                          ? PredicateEvaluationStatus::False
                          : evaluate_conjunction(goal.stop_predicates, facts, profile, nullptr,
                                                 nullptr, 0);
    if (precondition == PredicateEvaluationStatus::Invalid ||
        stop == PredicateEvaluationStatus::Invalid) {
        return PredicateEvaluationStatus::Invalid;
    }
    if (precondition == PredicateEvaluationStatus::Unsupported ||
        stop == PredicateEvaluationStatus::Unsupported) {
        return PredicateEvaluationStatus::Unsupported;
    }
    return precondition == PredicateEvaluationStatus::True &&
                   stop == PredicateEvaluationStatus::False
               ? PredicateEvaluationStatus::True
               : PredicateEvaluationStatus::False;
}

PredicateEvaluationStatus line_eligibility(const LineDefinition& line,
                                            const StrategyProfileV1& profile,
                                            const PublicFactSnapshot& facts) noexcept {
    const auto applicability = line.applicability_predicates.empty()
                                   ? PredicateEvaluationStatus::True
                                   : evaluate_conjunction(line.applicability_predicates, facts,
                                                          profile, nullptr, nullptr, 0);
    bool saw_false = applicability == PredicateEvaluationStatus::False;
    bool saw_unsupported = applicability == PredicateEvaluationStatus::Unsupported;
    bool saw_invalid = applicability == PredicateEvaluationStatus::Invalid;
    for (const auto& requirement : line.required_resources) {
        const auto status = evaluate_resource_requirement(requirement, profile, facts);
        saw_false = saw_false || status == PredicateEvaluationStatus::False;
        saw_unsupported = saw_unsupported || status == PredicateEvaluationStatus::Unsupported;
        saw_invalid = saw_invalid || status == PredicateEvaluationStatus::Invalid;
    }
    if (saw_invalid) {
        return PredicateEvaluationStatus::Invalid;
    }
    if (saw_unsupported) {
        return PredicateEvaluationStatus::Unsupported;
    }
    return saw_false ? PredicateEvaluationStatus::False : PredicateEvaluationStatus::True;
}

std::int32_t line_preference(const StrategyProfileV1& profile,
                             const std::string_view line_id) noexcept {
    for (const auto& preference : profile.preferences) {
        if (preference.dimension == ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress &&
            preference.subject_kind == PreferenceSubjectKind::Line &&
            preference.subject_id == line_id) {
            return preference.value;
        }
    }
    return 0;
}

std::vector<std::string> ready_nodes(const LineDefinition& line,
                                     const std::vector<std::string>& completed) {
    std::vector<std::string> result;
    for (const auto& node : line.nodes) {
        if (contains_id(completed, node.node_id)) {
            continue;
        }
        bool ready = true;
        for (const auto& dependency : line.dependencies) {
            if (dependency.successor_node_id == node.node_id &&
                !contains_id(completed, dependency.predecessor_node_id)) {
                ready = false;
                break;
            }
        }
        if (ready) {
            result.push_back(node.node_id);
        }
    }
    return result;
}

PredicateEvaluationStatus evaluate_candidate_conjunction(
    const std::vector<PredicateRef>& predicates,
    const PublicFactSnapshot& public_facts,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owner,
    const StrategyProfileV1& profile) noexcept {
    if (predicates.empty()) {
        return PredicateEvaluationStatus::True;
    }
    std::vector<PredicateEvaluationStatus> statuses;
    statuses.reserve(predicates.size());
    for (const auto& predicate : predicates) {
        if (predicate.scope == PredicateScope::Candidate) {
            statuses.push_back(evaluate_candidate_predicate(predicate, candidate, observation,
                                                             owner, profile));
        } else if (predicate.scope == PredicateScope::Observation) {
            statuses.push_back(evaluate_observation_predicate(predicate, public_facts));
        } else if (predicate.scope == PredicateScope::ProfileStatic) {
            statuses.push_back(evaluate_profile_static_predicate(predicate, profile));
        } else {
            statuses.push_back(PredicateEvaluationStatus::Invalid);
        }
    }
    return combine_predicate_statuses(statuses);
}

void sort_evidence(PublicEvaluatorOutcome& outcome) {
    auto normalize = [](std::vector<std::string>& values) {
        std::sort(values.begin(), values.end());
        values.erase(std::unique(values.begin(), values.end()), values.end());
    };
    normalize(outcome.matched_intent_ids);
    normalize(outcome.matched_goal_ids);
    normalize(outcome.matched_line_ids);
    normalize(outcome.reason_ids);
}

}  // namespace

PredicateEvaluationStatus evaluate_public_predicate_conjunction(
    const std::vector<PredicateRef>& predicates,
    const PublicFactSnapshot& public_facts,
    const StrategyProfileV1& profile) noexcept {
    return evaluate_conjunction(predicates, public_facts, profile, nullptr, nullptr, 0);
}

PredicateEvaluationStatus evaluate_resource_requirement(
    const ResourceRequirement& requirement,
    const StrategyProfileV1& profile,
    const PublicFactSnapshot& public_facts) noexcept {
    try {
        if (!validate_strategy_profile(profile) || !valid_snapshot(public_facts)) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto* resource = find_resource(profile, requirement.resource_id);
        if (resource == nullptr) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto* definition = find_fact(resource->public_fact_id);
        if (definition == nullptr ||
            definition->source_classification == PublicFactSourceClassification::Blocked ||
            definition->value_kind != PublicFactValueKind::U64 ||
            (definition->u64_maximum.has_value() &&
             resource->max_value > *definition->u64_maximum) ||
            requirement.minimum > resource->max_value) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto current = public_facts.value(resource->public_fact_id);
        if (!current.has_value()) {
            return PredicateEvaluationStatus::Unsupported;
        }
        if (current->value_kind != PublicFactValueKind::U64 ||
            current->validity_scope != PublicFactValidityScope::CurrentReconciliation) {
            return PredicateEvaluationStatus::Invalid;
        }
        if (current->u64_value > resource->max_value) {
            return PredicateEvaluationStatus::Invalid;
        }
        return current->u64_value >= requirement.minimum ? PredicateEvaluationStatus::True
                                                          : PredicateEvaluationStatus::False;
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

GoalLineSelection select_goal_and_line(const StrategyProfileV1& profile,
                                       const EpisodeLocalStrategyStateV1& state,
                                       const PublicFactSnapshot& public_facts) noexcept {
    GoalLineSelection result;
    try {
        if (!validate_strategy_profile(profile) || !valid_state_for_profile(state, profile) ||
            !valid_snapshot(public_facts)) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }

        const auto goal_is_achieved = [&state](const std::string_view id) noexcept {
            return contains_id(state.achieved_goal_ids, id);
        };
        const auto eligible_goal = [&](const GoalDefinition& goal) noexcept {
            return !goal_is_achieved(goal.goal_id) &&
                   goal_eligibility(goal, public_facts, profile) == PredicateEvaluationStatus::True;
        };

        const GoalDefinition* selected_goal = nullptr;
        if (state.active_goal_id.has_value()) {
            const auto* active = find_goal(profile, *state.active_goal_id);
            if (active != nullptr && eligible_goal(*active)) {
                selected_goal = active;
            }
        }

        bool saw_unsupported = false;
        bool saw_invalid = false;
        if (selected_goal == nullptr) {
            std::vector<const GoalDefinition*> eligible;
            for (const auto& goal : profile.goals) {
                if (goal_is_achieved(goal.goal_id)) {
                    continue;
                }
                const auto status = goal_eligibility(goal, public_facts, profile);
                saw_invalid = saw_invalid || status == PredicateEvaluationStatus::Invalid;
                saw_unsupported = saw_unsupported ||
                                  status == PredicateEvaluationStatus::Unsupported;
                if (status == PredicateEvaluationStatus::True) {
                    eligible.push_back(&goal);
                }
            }
            std::sort(eligible.begin(), eligible.end(), [](const auto* left, const auto* right) {
                if (left->priority != right->priority) {
                    return left->priority > right->priority;
                }
                return left->goal_id < right->goal_id;
            });
            if (!eligible.empty()) {
                selected_goal = eligible.front();
            }
        }

        if (selected_goal == nullptr) {
            result.status = saw_invalid ? PredicateEvaluationStatus::Invalid
                                         : (saw_unsupported ? PredicateEvaluationStatus::Unsupported
                                                            : PredicateEvaluationStatus::False);
            return result;
        }

        result.status = PredicateEvaluationStatus::True;
        result.goal_id = selected_goal->goal_id;

        const LineDefinition* selected_line = nullptr;
        if (state.active_line_id.has_value()) {
            const auto* active_line = find_line(profile, *state.active_line_id);
            if (active_line != nullptr && active_line->goal_id == selected_goal->goal_id &&
                line_eligibility(*active_line, profile, public_facts) ==
                    PredicateEvaluationStatus::True) {
                selected_line = active_line;
            }
        }

        if (selected_line == nullptr) {
            std::vector<const LineDefinition*> eligible;
            for (const auto& line : profile.lines) {
                if (line.goal_id != selected_goal->goal_id) {
                    continue;
                }
                if (line_eligibility(line, profile, public_facts) ==
                    PredicateEvaluationStatus::True) {
                    eligible.push_back(&line);
                }
            }
            std::sort(eligible.begin(), eligible.end(), [&](const auto* left, const auto* right) {
                const auto left_preference = line_preference(profile, left->line_id);
                const auto right_preference = line_preference(profile, right->line_id);
                if (left_preference != right_preference) {
                    return left_preference > right_preference;
                }
                return left->line_id < right->line_id;
            });
            if (!eligible.empty()) {
                selected_line = eligible.front();
            }
        }

        if (selected_line != nullptr) {
            result.line_id = selected_line->line_id;
            result.ready_node_ids = ready_nodes(*selected_line, state.completed_line_node_ids);
        }
        return result;
    } catch (...) {
        result = {};
        result.status = PredicateEvaluationStatus::Invalid;
        return result;
    }
}

PredicateEvaluationStatus match_candidate_intent_set(
    const StrategyProfileV1& profile,
    const std::vector<std::string>& intent_ids,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant,
    std::vector<std::string>& matched_ids) noexcept {
    try {
        matched_ids.clear();
        if (!validate_strategy_profile(profile) || owning_participant > 1 ||
            observation.perspective_player != owning_participant ||
            !sorted_unique_ids(intent_ids)) {
            return PredicateEvaluationStatus::Invalid;
        }
        if (intent_ids.empty()) {
            return PredicateEvaluationStatus::False;
        }
        const auto facts = extract_public_fact_snapshot(observation);
        if (!facts.valid) {
            return PredicateEvaluationStatus::Invalid;
        }
        bool saw_unsupported = false;
        bool saw_invalid = false;
        for (const auto& intent_id : intent_ids) {
            const auto* intent = find_intent(profile, intent_id);
            if (intent == nullptr) {
                saw_invalid = true;
                continue;
            }
            const auto status = evaluate_candidate_conjunction(
                intent->public_predicates, facts.snapshot, candidate, observation,
                owning_participant, profile);
            if (status == PredicateEvaluationStatus::True) {
                matched_ids.push_back(intent_id);
            } else if (status == PredicateEvaluationStatus::Unsupported) {
                saw_unsupported = true;
            } else if (status == PredicateEvaluationStatus::Invalid) {
                saw_invalid = true;
            }
        }
        if (!matched_ids.empty()) {
            return PredicateEvaluationStatus::True;
        }
        if (saw_invalid) {
            return PredicateEvaluationStatus::Invalid;
        }
        if (saw_unsupported) {
            return PredicateEvaluationStatus::Unsupported;
        }
        return PredicateEvaluationStatus::False;
    } catch (...) {
        matched_ids.clear();
        return PredicateEvaluationStatus::Invalid;
    }
}

PredicateEvaluationStatus evaluate_node_completion(
    const LineNode& node,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    const std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept {
    try {
        if (node.completion_predicates.empty()) {
            return PredicateEvaluationStatus::False;
        }
        if (owning_participant > 1 ||
            subsequent_observation.perspective_player != owning_participant ||
            subsequent_observation.decision_index <= accepted_transition.decision_index ||
            !environment::is_public_action_key(accepted_transition.selected_public_action_key)) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto facts = extract_public_fact_snapshot(subsequent_observation);
        if (!facts.valid) {
            return PredicateEvaluationStatus::Invalid;
        }
        return evaluate_public_predicate_conjunction(node.completion_predicates, facts.snapshot,
                                                    profile);
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

PredicateEvaluationStatus evaluate_goal_completion(
    const GoalDefinition& goal,
    const environment::AcceptedActionTransition& accepted_transition,
    const environment::PublicEnvironmentObservation& subsequent_observation,
    const std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept {
    try {
        if (goal.completion_predicates.empty()) {
            return PredicateEvaluationStatus::False;
        }
        if (owning_participant > 1 ||
            subsequent_observation.perspective_player != owning_participant ||
            subsequent_observation.decision_index <= accepted_transition.decision_index ||
            !environment::is_public_action_key(accepted_transition.selected_public_action_key)) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto facts = extract_public_fact_snapshot(subsequent_observation);
        if (!facts.valid) {
            return PredicateEvaluationStatus::Invalid;
        }
        return evaluate_public_predicate_conjunction(goal.completion_predicates, facts.snapshot,
                                                    profile);
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

PublicEvaluatorOutcome evaluate_goal_line_progress(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant) noexcept {
    PublicEvaluatorOutcome outcome;
    outcome.public_action_key = candidate.public_action_key;
    try {
        if (!validate_strategy_profile(profile) || owning_participant > 1 ||
            observation.perspective_player != owning_participant) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        const auto valid_status = [](const PredicateEvaluationStatus status) noexcept {
            return static_cast<std::uint8_t>(status) <=
                   static_cast<std::uint8_t>(PredicateEvaluationStatus::Invalid);
        };
        if (!valid_status(selection.status) || !valid_status(recovery.status)) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        if (selection.status == PredicateEvaluationStatus::Invalid ||
            recovery.status == PredicateEvaluationStatus::Invalid) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        if (selection.status == PredicateEvaluationStatus::Unsupported ||
            recovery.status == PredicateEvaluationStatus::Unsupported) {
            outcome.status = CandidateEvaluationStatus::Unsupported;
            return outcome;
        }

        if (selection.status == PredicateEvaluationStatus::True) {
            if (!selection.goal_id.has_value() || !sorted_unique_ids(selection.ready_node_ids) ||
                find_goal(profile, *selection.goal_id) == nullptr ||
                (!selection.line_id.has_value() && !selection.ready_node_ids.empty())) {
                outcome.status = CandidateEvaluationStatus::Invalid;
                return outcome;
            }
            if (selection.line_id.has_value()) {
                const auto* selected_line = find_line(profile, *selection.line_id);
                if (selected_line == nullptr || selected_line->goal_id != *selection.goal_id ||
                    std::any_of(selection.ready_node_ids.begin(), selection.ready_node_ids.end(),
                                [&](const auto& node_id) {
                                    return std::none_of(
                                        selected_line->nodes.begin(), selected_line->nodes.end(),
                                        [&](const auto& node) { return node.node_id == node_id; });
                                })) {
                    outcome.status = CandidateEvaluationStatus::Invalid;
                    return outcome;
                }
            }
        }
        if (recovery.status == PredicateEvaluationStatus::True &&
            (!recovery.recovery_edge_id.has_value() || !recovery.target_goal_id.has_value())) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }

        bool active_applicable = selection.status == PredicateEvaluationStatus::True &&
                                 selection.line_id.has_value();
        bool recovery_applicable = recovery.status == PredicateEvaluationStatus::True;
        bool active_match = false;
        bool recovery_match = false;
        bool saw_unsupported = false;
        bool saw_invalid = false;

        if (active_applicable) {
            const auto* line = find_line(profile, *selection.line_id);
            if (line == nullptr || !selection.goal_id.has_value() ||
                line->goal_id != *selection.goal_id) {
                outcome.status = CandidateEvaluationStatus::Invalid;
                return outcome;
            }
            for (const auto& node_id : selection.ready_node_ids) {
                const auto node = std::find_if(
                    line->nodes.begin(), line->nodes.end(), [&](const auto& value) {
                        return value.node_id == node_id;
                    });
                if (node == line->nodes.end()) {
                    saw_invalid = true;
                    continue;
                }
                std::vector<std::string> matched;
                const auto status = match_candidate_intent_set(
                    profile, node->candidate_intent_ids, candidate, observation,
                    owning_participant, matched);
                if (status == PredicateEvaluationStatus::True) {
                    active_match = true;
                    outcome.matched_intent_ids.insert(outcome.matched_intent_ids.end(),
                                                      matched.begin(), matched.end());
                    append_sorted_unique(outcome.matched_goal_ids, *selection.goal_id);
                    append_sorted_unique(outcome.matched_line_ids, *selection.line_id);
                } else if (status == PredicateEvaluationStatus::Unsupported) {
                    saw_unsupported = true;
                } else if (status == PredicateEvaluationStatus::Invalid) {
                    saw_invalid = true;
                }
            }
        }

        if (recovery_applicable) {
            if (!recovery.recovery_edge_id.has_value()) {
                outcome.status = CandidateEvaluationStatus::Invalid;
                return outcome;
            }
            const auto edge = std::find_if(
                profile.recovery_edges.begin(), profile.recovery_edges.end(),
                [&](const auto& value) {
                    return value.recovery_edge_id == *recovery.recovery_edge_id;
                });
            if (edge == profile.recovery_edges.end()) {
                outcome.status = CandidateEvaluationStatus::Invalid;
                return outcome;
            }
            if (recovery.target_goal_id != std::optional<std::string>(edge->target_goal_id) ||
                recovery.target_line_id != edge->target_line_id) {
                outcome.status = CandidateEvaluationStatus::Invalid;
                return outcome;
            }
            std::vector<std::string> matched;
            const auto status = match_candidate_intent_set(
                profile, edge->candidate_intent_ids, candidate, observation, owning_participant,
                matched);
            if (status == PredicateEvaluationStatus::True) {
                recovery_match = true;
                outcome.matched_intent_ids.insert(outcome.matched_intent_ids.end(), matched.begin(),
                                                  matched.end());
                append_sorted_unique(outcome.matched_goal_ids, edge->target_goal_id);
                if (edge->target_line_id.has_value()) {
                    append_sorted_unique(outcome.matched_line_ids, *edge->target_line_id);
                }
            } else if (status == PredicateEvaluationStatus::Unsupported) {
                saw_unsupported = true;
            } else if (status == PredicateEvaluationStatus::Invalid) {
                saw_invalid = true;
            }
        }

        sort_evidence(outcome);
        if (saw_invalid) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            outcome.contributions.clear();
            return outcome;
        }
        if (saw_unsupported) {
            outcome.status = CandidateEvaluationStatus::Unsupported;
            outcome.contributions.clear();
            return outcome;
        }
        if (!active_applicable && !recovery_applicable) {
            outcome.status = CandidateEvaluationStatus::NotApplicable;
            return outcome;
        }

        outcome.status = CandidateEvaluationStatus::Supported;
        const auto contribution = active_match ? 3 : (recovery_match ? 2 : 0);
        ScoreVector checked_score;
        if (!add_score_contribution(
                checked_score, ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress,
                contribution)) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            outcome.contributions.clear();
            return outcome;
        }
        outcome.contributions.push_back(
            {ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress, contribution});
        return outcome;
    } catch (...) {
        outcome.status = CandidateEvaluationStatus::Invalid;
        outcome.contributions.clear();
        return outcome;
    }
}

}  // namespace ygo::teacher
