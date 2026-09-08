#include "ygo/teacher/goal_line_controller.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <string_view>
#include <utility>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_safe_state.hpp"
#include "ygo/teacher/deterministic_resolver.hpp"
#include "ygo/teacher/recovery_controller.hpp"
#include "ygo/trajectory/codec.hpp"
#include "strategy_state_common.hpp"
#include "teacher_validation.hpp"

namespace ygo::teacher {
namespace {

bool valid_snapshot(const PublicFactSnapshot& snapshot) noexcept {
    try {
        return internal::valid_public_fact_vector(snapshot.values);
    } catch (...) {
        return false;
    }
}

template <typename State, typename StateValidator>
bool valid_state_for_profile_common(const State& state,
                                    const StrategyProfileV1& profile,
                                    StateValidator state_validator) noexcept {
    return state_validator(state) && internal::valid_plan_references(state, profile);
}

bool valid_state_for_profile(const EpisodeLocalStrategyStateV1& state,
                             const StrategyProfileV1& profile) noexcept {
    return valid_state_for_profile_common(state, profile, validate_strategy_state);
}

bool valid_state_for_profile_v2(const EpisodeLocalStrategyStateV2& state,
                                const StrategyProfileV1& profile) noexcept {
    return valid_state_for_profile_common(state, profile, validate_strategy_state_v2);
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

bool valid_choice_v2(const environment::PublicChoice& choice) noexcept {
    switch (choice.kind) {
    case environment::PublicChoiceKind::YesNo:
    case environment::PublicChoiceKind::EffectYesNo:
        return choice.value <= 1 && !choice.response_index.has_value();
    case environment::PublicChoiceKind::EffectChoice:
        return choice.value <= std::numeric_limits<std::uint32_t>::max() &&
               !choice.response_index.has_value();
    case environment::PublicChoiceKind::OptionValue:
    case environment::PublicChoiceKind::AnnouncementNumber:
        return choice.response_index.has_value();
    }
    return false;
}

bool valid_candidate_metadata_v2(
    const environment::EnvironmentActionCandidate& candidate) noexcept {
    if (static_cast<std::uint8_t>(candidate.action_kind) >
            static_cast<std::uint8_t>(environment::EnvironmentActionKind::Unsupported) ||
        !environment::is_public_action_key_v2(candidate.public_action_key)) {
        return false;
    }
    if (candidate.choice.has_value() && !valid_choice_v2(*candidate.choice)) {
        return false;
    }
    for (const auto* reference : {&candidate.source_reference, &candidate.target_reference}) {
        if (reference->has_value() &&
            (static_cast<std::uint8_t>(reference->value().kind) >
                 static_cast<std::uint8_t>(environment::PublicCardReferenceKind::RedactedSlot) ||
             reference->value().observation_locator.empty() ||
             !std::all_of(reference->value().observation_locator.begin(),
                          reference->value().observation_locator.end(),
                          [](const unsigned char character) {
                              return character >= 0x20 && character != 0x7f;
                          }) ||
             !trajectory::is_valid_utf8(reference->value().observation_locator))) {
            return false;
        }
    }
    return candidate.continuation_operation.empty() ||
           detail::canonical_token(candidate.continuation_operation);
}

bool role_contains_v2(const std::uint32_t passcode,
                      const std::string_view role_id,
                      const StrategyProfileV1& profile) noexcept {
    const auto it = std::lower_bound(
        profile.card_roles.begin(), profile.card_roles.end(), passcode,
        [](const CardRoleEntry& entry, const std::uint32_t value) {
            return entry.passcode < value;
        });
    return it != profile.card_roles.end() && it->passcode == passcode &&
           std::binary_search(it->role_ids.begin(), it->role_ids.end(), role_id);
}

PredicateEvaluationStatus evaluate_role_reference_v2(
    const std::optional<environment::PublicCardReference>& reference,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant,
    const std::string_view role_id,
    const StrategyProfileV1& profile) noexcept {
    if (!reference.has_value()) {
        return PredicateEvaluationStatus::False;
    }
    if (reference->kind == environment::PublicCardReferenceKind::RedactedSlot) {
        return PredicateEvaluationStatus::Unsupported;
    }
    if (reference->kind != environment::PublicCardReferenceKind::VisibleCard ||
        owning_participant > 1 || observation.perspective_player != owning_participant) {
        return PredicateEvaluationStatus::Invalid;
    }
    const auto decoded = environment::decode_canonical_public_safe_state(
        observation.canonical_safe_state_bytes());
    if (!decoded || decoded.value->match_context().perspective_player != owning_participant) {
        return PredicateEvaluationStatus::Invalid;
    }
    const auto& entities = decoded.value->entities();
    const auto matches = std::count_if(
        entities.begin(), entities.end(), [&](const auto& entity) {
            return entity.locator.value == reference->observation_locator;
        });
    if (matches != 1) {
        return PredicateEvaluationStatus::Invalid;
    }
    const auto it = std::find_if(entities.begin(), entities.end(), [&](const auto& entity) {
        return entity.locator.value == reference->observation_locator;
    });
    if (it == entities.end() || !it->identity_known || !it->passcode.has_value()) {
        return PredicateEvaluationStatus::Invalid;
    }
    return role_contains_v2(*it->passcode, role_id, profile)
               ? PredicateEvaluationStatus::True
               : PredicateEvaluationStatus::False;
}

PredicateEvaluationStatus compare_visibility_v2(
    const std::optional<environment::PublicCardReference>& reference,
    const std::string_view expected) noexcept {
    std::string_view actual = "absent";
    if (reference.has_value()) {
        if (reference->kind == environment::PublicCardReferenceKind::VisibleCard) {
            actual = "visible";
        } else if (reference->kind == environment::PublicCardReferenceKind::RedactedSlot) {
            actual = "redacted";
        } else {
            return PredicateEvaluationStatus::Invalid;
        }
    }
    return actual == expected ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False;
}

PredicateEvaluationStatus evaluate_candidate_predicate_v2(
    const PredicateRef& value,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept {
    try {
        const auto& registry = TeacherPredicateRegistryV1::canonical();
        if (!registry.validate_profile_ref(value, profile) ||
            value.scope != PredicateScope::Candidate ||
            !valid_candidate_metadata_v2(candidate) || owning_participant > 1 ||
            observation.perspective_player != owning_participant) {
            return PredicateEvaluationStatus::Invalid;
        }
        const auto& id = value.predicate_id;
        if (id == "candidate.action_kind_equals") {
            return environment::environment_action_kind_name(candidate.action_kind) ==
                           value.arguments[0].token
                       ? PredicateEvaluationStatus::True
                       : PredicateEvaluationStatus::False;
        }
        if (id == "candidate.choice_present") {
            return candidate.choice.has_value() ? PredicateEvaluationStatus::True
                                                : PredicateEvaluationStatus::False;
        }
        if (id == "candidate.choice_value_equals") {
            return !candidate.choice.has_value()
                       ? PredicateEvaluationStatus::False
                       : (candidate.choice->value == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.source_visibility_equals") {
            return compare_visibility_v2(candidate.source_reference, value.arguments[0].token);
        }
        if (id == "candidate.target_visibility_equals") {
            return compare_visibility_v2(candidate.target_reference, value.arguments[0].token);
        }
        if (id == "candidate.source_role_contains") {
            return evaluate_role_reference_v2(candidate.source_reference, observation,
                                               owning_participant, value.arguments[0].token,
                                               profile);
        }
        if (id == "candidate.target_role_contains") {
            return evaluate_role_reference_v2(candidate.target_reference, observation,
                                               owning_participant, value.arguments[0].token,
                                               profile);
        }
        if (id == "candidate.phase_equals") {
            return !candidate.phase.has_value()
                       ? PredicateEvaluationStatus::False
                       : (*candidate.phase == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.position_equals") {
            return !candidate.position.has_value()
                       ? PredicateEvaluationStatus::False
                       : (*candidate.position == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.source_index_equals") {
            return !candidate.source_index.has_value()
                       ? PredicateEvaluationStatus::False
                       : (*candidate.source_index == value.arguments[0].u64
                              ? PredicateEvaluationStatus::True
                              : PredicateEvaluationStatus::False);
        }
        if (id == "candidate.continuation_present") {
            return candidate.continuation_operation.empty()
                       ? PredicateEvaluationStatus::False
                       : PredicateEvaluationStatus::True;
        }
        if (id == "candidate.submits_engine_response") {
            return candidate.submits_engine_response ? PredicateEvaluationStatus::True
                                                     : PredicateEvaluationStatus::False;
        }
        return PredicateEvaluationStatus::Invalid;
    } catch (...) {
        return PredicateEvaluationStatus::Invalid;
    }
}

PredicateEvaluationStatus match_candidate_intent_set_v2(
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
            !internal::valid_sorted_id_vector(intent_ids)) {
            return PredicateEvaluationStatus::Invalid;
        }
        if (intent_ids.empty()) {
            return PredicateEvaluationStatus::False;
        }
        bool saw_unsupported = false;
        bool saw_invalid = false;
        for (const auto& intent_id : intent_ids) {
            const auto intent = std::find_if(
                profile.candidate_intents.begin(), profile.candidate_intents.end(),
                [&](const auto& value) { return value.intent_id == intent_id; });
            if (intent == profile.candidate_intents.end()) {
                saw_invalid = true;
                continue;
            }
            std::vector<PredicateEvaluationStatus> statuses;
            statuses.reserve(intent->public_predicates.size());
            for (const auto& predicate : intent->public_predicates) {
                if (predicate.scope != PredicateScope::Candidate) {
                    statuses.push_back(PredicateEvaluationStatus::Invalid);
                } else {
                    statuses.push_back(evaluate_candidate_predicate_v2(
                        predicate, candidate, observation, owning_participant, profile));
                }
            }
            const auto status = combine_predicate_statuses(statuses);
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
        return saw_unsupported ? PredicateEvaluationStatus::Unsupported
                               : PredicateEvaluationStatus::False;
    } catch (...) {
        matched_ids.clear();
        return PredicateEvaluationStatus::Invalid;
    }
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

GoalLineSelection select_goal_and_line_v2(const StrategyProfileV1& profile,
                                          const EpisodeLocalStrategyStateV2& state,
                                          const PublicFactSnapshot& public_facts) noexcept {
    GoalLineSelection result;
    try {
        if (!validate_strategy_profile(profile) ||
            !valid_state_for_profile_v2(state, profile) || !valid_snapshot(public_facts)) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }

        const auto goal_is_achieved = [&state](const std::string_view id) noexcept {
            return contains_id(state.achieved_goal_ids, id);
        };
        const auto eligible_goal = [&](const GoalDefinition& goal) {
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
                saw_unsupported =
                    saw_unsupported || status == PredicateEvaluationStatus::Unsupported;
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
            !internal::valid_sorted_id_vector(intent_ids)) {
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

PredicateEvaluationStatus evaluate_node_completion_v2(
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
            !environment::is_public_action_key_v2(
                accepted_transition.selected_public_action_key)) {
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

PredicateEvaluationStatus evaluate_goal_completion_v2(
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
            !environment::is_public_action_key_v2(
                accepted_transition.selected_public_action_key)) {
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

RecoverySelection select_recovery_edge_v2(
    const StrategyProfileV1& profile,
    const EpisodeLocalStrategyStateV2& pre_reconciliation_state,
    const environment::PublicEnvironmentObservation& current_observation,
    const std::uint8_t owning_participant) noexcept {
    RecoverySelection result;
    try {
        if (!validate_strategy_profile(profile) ||
            !valid_state_for_profile_v2(pre_reconciliation_state, profile) ||
            owning_participant > 1 ||
            current_observation.perspective_player != owning_participant) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        const auto reconciliation = reconcile_strategy_state_with_evidence_v2(
            pre_reconciliation_state, owning_participant, current_observation);
        if (!reconciliation.has_value() ||
            !valid_state_for_profile_v2(reconciliation->state, profile)) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }
        const auto facts = extract_public_fact_snapshot(current_observation);
        if (!facts.valid) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }

        std::vector<std::string> ready_node_ids;
        if (pre_reconciliation_state.active_line_id.has_value()) {
            const auto* line = find_line(profile, *pre_reconciliation_state.active_line_id);
            if (line == nullptr) {
                result.status = PredicateEvaluationStatus::Invalid;
                return result;
            }
            ready_node_ids = ready_nodes(*line,
                                         pre_reconciliation_state.completed_line_node_ids);
        }
        const auto reason_ids = reconciliation->invalidation_reason_ids;
        if (!internal::valid_sorted_id_vector(ready_node_ids) ||
            !internal::valid_sorted_id_vector(reason_ids)) {
            result.status = PredicateEvaluationStatus::Invalid;
            return result;
        }

        const auto source_matches = [&](const RecoveryEdge& edge) noexcept {
            if (edge.source_kind == RecoverySourceKind::Goal) {
                return pre_reconciliation_state.active_goal_id.has_value() &&
                       *pre_reconciliation_state.active_goal_id == edge.source_id;
            }
            if (edge.source_kind == RecoverySourceKind::Line) {
                return pre_reconciliation_state.active_line_id.has_value() &&
                       *pre_reconciliation_state.active_line_id == edge.source_id;
            }
            return std::binary_search(ready_node_ids.begin(), ready_node_ids.end(),
                                      edge.source_id);
        };
        const auto has_all_reasons = [&](const std::vector<std::string>& required) noexcept {
            return std::all_of(required.begin(), required.end(), [&](const auto& reason) {
                return std::binary_search(reason_ids.begin(), reason_ids.end(), reason);
            });
        };

        const RecoveryEdge* selected = nullptr;
        bool saw_unsupported = false;
        bool saw_invalid = false;
        for (const auto& edge : profile.recovery_edges) {
            if (!source_matches(edge) || edge.invalidation_reason_ids.empty() ||
                !has_all_reasons(edge.invalidation_reason_ids)) {
                continue;
            }
            if (pre_reconciliation_state.active_line_id.has_value() &&
                (edge.source_kind == RecoverySourceKind::Line ||
                 edge.source_kind == RecoverySourceKind::Node)) {
                const auto* line = find_line(profile, *pre_reconciliation_state.active_line_id);
                if (line == nullptr ||
                    !std::binary_search(line->recovery_edge_ids.begin(),
                                        line->recovery_edge_ids.end(), edge.recovery_edge_id)) {
                    continue;
                }
            }
            const auto status = edge.preconditions.empty()
                                    ? PredicateEvaluationStatus::True
                                    : evaluate_public_predicate_conjunction(
                                          edge.preconditions, facts.snapshot, profile);
            if (status == PredicateEvaluationStatus::Invalid) {
                saw_invalid = true;
                continue;
            }
            if (status == PredicateEvaluationStatus::Unsupported) {
                saw_unsupported = true;
                continue;
            }
            if (status != PredicateEvaluationStatus::True) {
                continue;
            }
            const auto* target_goal = find_goal(profile, edge.target_goal_id);
            if (target_goal == nullptr) {
                saw_invalid = true;
                continue;
            }
            const auto* selected_goal =
                selected == nullptr ? nullptr : find_goal(profile, selected->target_goal_id);
            const bool preferred =
                selected == nullptr || selected_goal == nullptr ||
                target_goal->priority > selected_goal->priority ||
                (target_goal->priority == selected_goal->priority &&
                 static_cast<std::uint8_t>(edge.confidence_cap) <
                     static_cast<std::uint8_t>(selected->confidence_cap)) ||
                (target_goal->priority == selected_goal->priority &&
                 edge.confidence_cap == selected->confidence_cap &&
                 edge.recovery_edge_id < selected->recovery_edge_id);
            if (preferred) {
                selected = &edge;
            }
        }

        if (selected == nullptr) {
            result.status = saw_invalid ? PredicateEvaluationStatus::Invalid
                                         : (saw_unsupported ? PredicateEvaluationStatus::Unsupported
                                                            : PredicateEvaluationStatus::False);
            return result;
        }
        result.status = PredicateEvaluationStatus::True;
        result.recovery_edge_id = selected->recovery_edge_id;
        result.target_goal_id = selected->target_goal_id;
        result.target_line_id = selected->target_line_id;
        return result;
    } catch (...) {
        result = {};
        result.status = PredicateEvaluationStatus::Invalid;
        return result;
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
            if (!selection.goal_id.has_value() ||
                !internal::valid_sorted_id_vector(selection.ready_node_ids) ||
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

PublicEvaluatorOutcome evaluate_goal_line_progress_v2(
    const StrategyProfileV1& profile,
    const GoalLineSelection& selection,
    const RecoverySelection& recovery,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    const std::uint8_t owning_participant,
    const bool reconciled_continuation_commitment) noexcept {
    PublicEvaluatorOutcome outcome;
    outcome.public_action_key = candidate.public_action_key;
    try {
        if (!validate_strategy_profile(profile) || owning_participant > 1 ||
            observation.perspective_player != owning_participant ||
            !valid_candidate_metadata_v2(candidate)) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        const auto facts = extract_public_fact_snapshot(observation);
        if (!facts.valid) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        const auto decision_kind = facts.snapshot.value("public.decision_context.kind");
        if (!decision_kind.has_value() || decision_kind->value_kind != PublicFactValueKind::Token) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        const auto operation_code = static_cast<std::uint8_t>(
            candidate.card_selection_operation);
        if (operation_code > static_cast<std::uint8_t>(
                                 environment::PublicCardSelectionOperation::Unselect) ||
            (decision_kind->token_value == "unselect_card" &&
             candidate.action_kind == environment::EnvironmentActionKind::CardSelection &&
             candidate.card_selection_operation ==
                 environment::PublicCardSelectionOperation::None) ||
            (decision_kind->token_value != "unselect_card" &&
             candidate.card_selection_operation !=
                 environment::PublicCardSelectionOperation::None) ||
            (candidate.action_kind != environment::EnvironmentActionKind::CardSelection &&
             candidate.card_selection_operation !=
                 environment::PublicCardSelectionOperation::None)) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }

        const bool native_unselect = decision_kind->token_value == "unselect_card";
        const bool continuation_boundary =
            reconciled_continuation_commitment && native_unselect;

        const auto valid_status = [](const PredicateEvaluationStatus status) noexcept {
            return static_cast<std::uint8_t>(status) <=
                   static_cast<std::uint8_t>(PredicateEvaluationStatus::Invalid);
        };
        if (!valid_status(selection.status) || !valid_status(recovery.status) ||
            selection.status == PredicateEvaluationStatus::Invalid ||
            recovery.status == PredicateEvaluationStatus::Invalid) {
            outcome.status = CandidateEvaluationStatus::Invalid;
            return outcome;
        }
        if ((selection.status == PredicateEvaluationStatus::Unsupported ||
             recovery.status == PredicateEvaluationStatus::Unsupported) &&
            !continuation_boundary) {
            outcome.status = CandidateEvaluationStatus::Unsupported;
            return outcome;
        }

        if (selection.status == PredicateEvaluationStatus::True) {
            if (!selection.goal_id.has_value() ||
                !internal::valid_sorted_id_vector(selection.ready_node_ids) ||
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

        const bool active_applicable = selection.status == PredicateEvaluationStatus::True &&
                                       selection.line_id.has_value();
        const bool recovery_applicable = recovery.status == PredicateEvaluationStatus::True;
        const bool generic_select =
            continuation_boundary &&
            candidate.action_kind == environment::EnvironmentActionKind::CardSelection &&
            candidate.card_selection_operation ==
                environment::PublicCardSelectionOperation::Select;
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
                const auto status = match_candidate_intent_set_v2(
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
            if (edge == profile.recovery_edges.end() ||
                recovery.target_goal_id != std::optional<std::string>(edge->target_goal_id) ||
                recovery.target_line_id != edge->target_line_id) {
                outcome.status = CandidateEvaluationStatus::Invalid;
                return outcome;
            }
            std::vector<std::string> matched;
            const auto status = match_candidate_intent_set_v2(
                profile, edge->candidate_intent_ids, candidate, observation,
                owning_participant, matched);
            if (status == PredicateEvaluationStatus::True) {
                recovery_match = true;
                outcome.matched_intent_ids.insert(outcome.matched_intent_ids.end(),
                                                  matched.begin(), matched.end());
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
        if (!active_applicable && !recovery_applicable && !continuation_boundary) {
            outcome.status = CandidateEvaluationStatus::NotApplicable;
            return outcome;
        }

        outcome.status = CandidateEvaluationStatus::Supported;
        const auto existing_contribution =
            active_match ? 3 : (recovery_match ? 2 : 0);
        const auto contribution = std::max(existing_contribution, generic_select ? 1 : 0);
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
