#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v2.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/strategy_state_v2.hpp"
#include "ygo/teacher/teacher_core_v2.hpp"
#include "ygo/teacher/teacher_core_v3.hpp"
#include "ygo/teacher/teacher_decision_v3.hpp"
#include "ygo/trajectory/codec.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

PublicEnvironmentObservation observation(const std::uint64_t decision_index = 234) {
    ygo::observation::PlayerObservation source;
    source.schema_version = "ygo.player_observation.v1";
    source.perspective_player = 0;
    source.decision_index = decision_index;
    source.globals.life_points = {8000, 7000};
    source.globals.player_to_act = 0;
    source.globals.turn_player = 0;
    source.globals.turn_count = 16;
    source.globals.phase = 4;
    source.globals.chain_length = 0;
    source.globals.terminal = false;
    source.match_context.perspective_player = 0;
    source.match_context.knowledge.own_decklist_known = true;
    source.match_context.knowledge.opponent_decklist_known = false;
    source.decision_context.kind = "unselect_card";
    source.decision_context.player = 0;
    return project_public_observation(source);
}

EnvironmentActionCandidate card_candidate(
    const std::string& locator,
    const PublicCardSelectionOperation operation) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.source_reference = PublicCardReference{
        PublicCardReferenceKind::VisibleCard, locator};
    result.card_selection_operation = operation;
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_reference = result.source_reference;
    input.card_selection_operation = operation;
    result.public_action_key = public_action_key_v3(input);
    return result;
}

EnvironmentActionCandidate cancel_candidate() {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::Cancel;
    PublicActionKeyInput input;
    input.action_kind = "cancel";
    result.public_action_key = public_action_key_v3(input);
    return result;
}

EpisodeLocalStrategyStateV3 retained_state(const StrategyProfileV1& profile) {
    const auto reset = reset_strategy_state_v3(profile);
    require(reset.has_value(), "V3 strategy state reset failed");
    auto result = *reset;
    result.active_goal_id = "goal.main1.salamangreat";
    result.active_line_id = "line.main1.salamangreat";
    return result;
}

const CandidateEvaluation& evaluation_for(
    const TeacherRankingResultV3& result, const std::string& key) {
    const auto found = std::find_if(
        result.evaluations.begin(), result.evaluations.end(),
        [&](const auto& value) { return value.public_action_key == key; });
    require(found != result.evaluations.end(), "V3 result omitted a candidate evaluation");
    return *found;
}

void require_progress(const TeacherRankingResultV3& result,
                      const std::string& key, const std::int64_t expected) {
    const auto& evaluation = evaluation_for(result, key);
    require(evaluation.status == CandidateEvaluationStatus::Supported &&
                evaluation.score.has_value() &&
                evaluation.score->values[static_cast<std::size_t>(
                    ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress)] == expected,
            "V3 continuation progress did not match the contract");
}

void test_v3_select_progress_and_generation_rejection() {
    const auto profile = make_salamangreat_profile();
    const auto first = card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select);
    const auto second = card_candidate(
        "p0:MONSTER_ZONE:1", PublicCardSelectionOperation::Select);
    const auto cancel = cancel_candidate();
    const auto input = ygo::policy::PolicyInput{
        observation(), {first, second, cancel}};

    const auto result = TeacherCoreV3{}.propose(
        input, profile, retained_state(profile));
    std::string diagnostic;
    require(result.status == TeacherRankingStatus::Selected &&
                validate_teacher_ranking_result_v3(result, &diagnostic),
            "V3 Teacher did not produce a valid selected result: " + diagnostic);
    require(result.fallback_level ==
                std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F0},
            "V3 retained commitment did not resolve at F0");
    require_progress(result, first.public_action_key, 1);
    require_progress(result, second.public_action_key, 1);
    require_progress(result, cancel.public_action_key, 0);
    require(result.selected_public_action_key ==
                std::optional<std::string>(first.public_action_key) ||
                result.selected_public_action_key ==
                    std::optional<std::string>(second.public_action_key),
            "V3 Teacher did not select a legal Select candidate");

    auto v2_candidate = first;
    PublicActionKeyInput v2_input;
    v2_input.action_kind = "card_selection";
    v2_input.source_reference = first.source_reference;
    v2_input.card_selection_operation = PublicCardSelectionOperation::Select;
    v2_candidate.public_action_key = public_action_key_v2(v2_input);
    const auto v2_result = TeacherCoreV3{}.propose(
        ygo::policy::PolicyInput{observation(), {v2_candidate, cancel}},
        profile, retained_state(profile));
    require(v2_result.status == TeacherRankingStatus::InvalidInput,
            "V3 Teacher accepted a V2 public-action key");

    const auto v3_in_v2_result = TeacherCoreV2{}.propose(
        ygo::policy::PolicyInput{observation(), {first, second, cancel}},
        profile, *reset_strategy_state_v2(profile));
    require(v3_in_v2_result.status == TeacherRankingStatus::InvalidInput,
            "historical V2 Teacher accepted a V3 public-action domain");

    const auto mixed_result = TeacherCoreV3{}.propose(
        ygo::policy::PolicyInput{observation(), {first, v2_candidate, cancel}},
        profile, retained_state(profile));
    require(mixed_result.status == TeacherRankingStatus::InvalidInput,
            "V3 Teacher accepted a mixed V2/V3 candidate domain");

    TeacherRankingResultV3 mixed_ranking;
    mixed_ranking.status = TeacherRankingStatus::Selected;
    CandidateEvaluation v3_evaluation;
    v3_evaluation.public_action_key = first.public_action_key;
    v3_evaluation.status = CandidateEvaluationStatus::Supported;
    v3_evaluation.score = ScoreVector{};
    CandidateEvaluation v2_evaluation = v3_evaluation;
    v2_evaluation.public_action_key = v2_candidate.public_action_key;
    mixed_ranking.evaluations = {v3_evaluation, v2_evaluation};
    mixed_ranking.selected_public_action_key = first.public_action_key;
    mixed_ranking.selected_score_vector = ScoreVector{};
    mixed_ranking.fallback_level = TeacherFallbackLevel::F4;
    require(!validate_teacher_ranking_result_v3(mixed_ranking),
            "V3 ranking validator accepted mixed generation evaluation keys");
}

void test_v3_unselect_progress_and_policy_session() {
    const auto profile = make_salamangreat_profile();
    const auto unselect = card_candidate(
        "p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Unselect);
    const auto select = card_candidate(
        "p0:MONSTER_ZONE:1", PublicCardSelectionOperation::Select);
    const auto cancel = cancel_candidate();
    const auto result = TeacherCoreV3{}.propose(
        ygo::policy::PolicyInput{observation(), {unselect, select, cancel}},
        profile, retained_state(profile));
    require(result.status == TeacherRankingStatus::Selected &&
                result.selected_public_action_key ==
                    std::optional<std::string>(select.public_action_key),
            "V3 Teacher did not select the remaining Select candidate");
    require_progress(result, unselect.public_action_key, 0);
    require_progress(result, select.public_action_key, 1);
    require_progress(result, cancel.public_action_key, 0);

    const auto binding = ygo::policy::make_teacher_policy_binding_v3(profile);
    const auto artifact = ygo::policy::make_teacher_policy_artifact_v3(profile);
    const auto v2_binding = ygo::policy::make_teacher_policy_binding_v2(profile);
    const auto v2_artifact = ygo::policy::make_teacher_policy_artifact_v2(profile);
    require(binding.teacher_policy_binding_id != v2_binding.teacher_policy_binding_id &&
                artifact.policy_artifact_id != v2_artifact.policy_artifact_id &&
                binding.teacher_core_artifact_identity ==
                    ygo::policy::kTeacherProducerImplementationIdentityV3 &&
                artifact.action_adapter_identity ==
                    ygo::policy::kPublicActionKeyAdapterIdentityV3,
            "V3 Teacher provenance reused a V2 identity");
    const auto assignments = ygo::policy::make_teacher_participant_assignments(
        ygo::policy::make_teacher_policy_artifact_v3(make_swordsoul_tenyi_profile()),
        artifact, CertifiedEnvironmentConfig::canonical_v4(),
        SeatAssignment::Normal, 0,
        {ygo::trajectory::PolicyRole::Behavior,
         ygo::trajectory::PolicyRole::Opponent});
    const auto assignment = std::find_if(
        assignments.begin(), assignments.end(),
        [](const auto& value) { return value.player == 1; });
    require(assignment != assignments.end(), "V3 assignment fixture omitted player one");
    const auto session = ygo::policy::create_teacher_policy_session_v3(
        profile, binding, artifact, *assignment);
    require(static_cast<bool>(session),
            "V3 Teacher policy session was rejected: " +
                (session.error.has_value() ? session.error->message : "no diagnostic"));
    require(session.value->execution_binding().policy_artifact_id ==
                artifact.policy_artifact_id,
            "V3 execution binding did not retain its artifact identity");

    auto wrong_artifact = artifact;
    wrong_artifact.action_adapter_identity =
        std::string(ygo::policy::kPublicActionKeyAdapterIdentityV2);
    wrong_artifact.policy_artifact_id =
        ygo::trajectory::compute_policy_artifact_id(wrong_artifact);
    const auto rejected = ygo::policy::create_teacher_policy_session_v3(
        profile, binding, wrong_artifact, *assignment);
    require(!static_cast<bool>(rejected),
            "V3 policy session accepted a V2 action adapter");

    auto wrong_producer = binding;
    wrong_producer.teacher_core_artifact_identity =
        std::string(ygo::policy::kTeacherProducerImplementationIdentityV2);
    wrong_producer.teacher_policy_binding_id =
        ygo::teacher::teacher_policy_binding_id(wrong_producer);
    const auto rejected_producer = ygo::policy::create_teacher_policy_session_v3(
        profile, wrong_producer, artifact, *assignment);
    require(!static_cast<bool>(rejected_producer),
            "V3 policy session accepted a historical Teacher producer");

    auto extra_provenance = artifact;
    extra_provenance.search_contract_identity = "search.contract.v1.example";
    extra_provenance.policy_artifact_id =
        ygo::trajectory::compute_policy_artifact_id(extra_provenance);
    const auto rejected_extra = ygo::policy::create_teacher_policy_session_v3(
        profile, binding, extra_provenance, *assignment);
    require(!static_cast<bool>(rejected_extra),
            "V3 policy session accepted extra provenance");
}

}  // namespace

int main() {
    try {
        test_v3_select_progress_and_generation_rejection();
        test_v3_unselect_progress_and_policy_session();
        std::cout << "teacher V3 public action successor tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
