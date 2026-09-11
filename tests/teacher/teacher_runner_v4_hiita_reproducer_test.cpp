#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/policy/production_provenance.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_runner_v4.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/teacher/strategy_profile.hpp"
#include "ygo/teacher/teacher_core_v3.hpp"
#include "ygo/teacher/teacher_explanation.hpp"
#include "ygo/trajectory/types.hpp"

#include "ygo/environment/public_safe_state.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

constexpr std::uint64_t kRootSeed = 4;
constexpr std::uint64_t kDecisionLimit = 236;
constexpr std::string_view kHiitaLocator =
    "p1:EXTRA_DECK:public:48815792:0";

void require(const bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture final {
    CertifiedEnvironmentConfig environment_config =
        CertifiedEnvironmentConfig::canonical_v4();
    EpisodeSpec episode_spec;
    RunControl run_control;
    PolicyProvenanceEnvelope policy_provenance;
    TeacherRunnerV4Config runner_config;
};

const ParticipantPolicyAssignment& assignment_for(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto found = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& value) { return value.player == player; });
    require(found != assignments.end(), "V4 fixture lacks a participant assignment");
    return *found;
}

Fixture fixture() {
    Fixture result;
    result.episode_spec.contract_id = std::string(kEpisodicEnvironmentV4ContractId);
    result.episode_spec.root_seed = kRootSeed;
    result.episode_spec.seat_assignment = SeatAssignment::Normal;
    result.episode_spec.starting_player = 0;
    result.run_control.engine_process_budget = 4096;
    result.run_control.semantic_action_budget = 4096;
    result.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    result.run_control.cancellation.source = "task8-hiita-reproducer";

    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact_v3(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact_v3(salamangreat);
    result.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(result.policy_provenance.policy_artifacts.begin(),
              result.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.policy_provenance.participant_assignments =
        make_teacher_participant_assignments(
            swordsoul_artifact, salamangreat_artifact, result.environment_config,
            result.episode_spec.seat_assignment, result.episode_spec.starting_player,
            {PolicyRole::Behavior, PolicyRole::Opponent});

    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for(
            result.policy_provenance.participant_assignments, player);
        const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = make_teacher_policy_binding_v3(profile);
        auto session = create_teacher_policy_session_v3(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session), "V3 Teacher session creation failed");
        result.runner_config.sessions[player] = std::move(*session.value);
    }
    return result;
}

const EnvironmentActionCandidate* candidate_for_key(
    const DecisionFrame& frame, const std::string_view key) {
    const auto found = std::find_if(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [key](const auto& candidate) { return candidate.public_action_key == key; });
    return found == frame.request.candidates.end() ? nullptr : &*found;
}

const TeacherCandidateEvaluationDiagnosticsV3* evaluation_for_key(
    const TeacherRankingDiagnosticsV3& diagnostics, const std::string_view key) {
    const auto found = std::find_if(
        diagnostics.evaluations.begin(), diagnostics.evaluations.end(),
        [key](const auto& evaluation) { return evaluation.public_action_key == key; });
    return found == diagnostics.evaluations.end() ? nullptr : &*found;
}

bool is_hiita_idle_frame(const DecisionFrame& frame) {
    if (frame.request.kind != EnvironmentDecisionKind::IdleCommand) return false;
    const auto safe = decode_canonical_public_safe_state(
        frame.public_observation.canonical_safe_state_bytes());
    if (!safe) return false;
    const auto entity = std::find_if(
        safe.value->entities().begin(), safe.value->entities().end(),
        [](const auto& value) {
            return value.locator.value == kHiitaLocator && value.identity_known &&
                   value.passcode.has_value() && *value.passcode == 48815792;
        });
    if (entity == safe.value->entities().end()) return false;
    return std::any_of(
        frame.request.candidates.begin(), frame.request.candidates.end(),
        [](const auto& value) {
            return value.action_kind == EnvironmentActionKind::IdleCommand &&
                   value.source_reference.has_value() &&
                   value.source_reference->kind == PublicCardReferenceKind::VisibleCard &&
                   value.source_reference->observation_locator == kHiitaLocator;
        });
}

void test_teacher_v4_selects_first_hiita_material() {
    auto value = fixture();
    auto environment_factory = EpisodicEnvironment::create(value.environment_config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(
                environment_factory),
            "V4 environment creation failed");
    auto environment = std::move(
        std::get<std::unique_ptr<EpisodicEnvironment>>(environment_factory));
    auto runner_result = TeacherRunnerV4::create(std::move(value.runner_config));
    require(static_cast<bool>(runner_result), "V4 Teacher runner creation failed");
    auto runner = std::move(*runner_result.value);

    const auto reset = environment->reset(value.episode_spec, value.run_control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr, "V4 reset was rejected");
    auto boundary = reset_accepted->next;
    std::optional<DecisionFrame> hiita_frame;
    std::optional<std::string> hiita_action_key;
    std::optional<DecisionFrame> material_frame;
    std::optional<TeacherRankingDiagnosticsV3> material_diagnostics;
    std::optional<std::string> material_selected_key;
    std::optional<StepAccepted> material_transition;

    for (std::size_t guard = 0; guard < kDecisionLimit + 1; ++guard) {
        const auto* frame = std::get_if<DecisionFrame>(&boundary);
        require(frame != nullptr, "V4 bounded prefix did not expose a decision frame");
        require(frame->decision_index <= kDecisionLimit,
                "V4 bounded prefix exceeded the requested decision limit");

        TeacherRankingDiagnosticsV3 diagnostics;
        const auto selection = runner.select_with_diagnostics(*frame, diagnostics);
        require(static_cast<bool>(selection) && selection.value.has_value(),
                "V4 Teacher failed before the Hiita material boundary");

        const auto hiita_candidate = [&]() -> const EnvironmentActionCandidate* {
            if (!is_hiita_idle_frame(*frame)) return nullptr;
            for (const auto& candidate : frame->request.candidates) {
                if (candidate.action_kind == EnvironmentActionKind::IdleCommand &&
                    candidate.source_reference.has_value() &&
                    candidate.source_reference->observation_locator == kHiitaLocator) {
                    return &candidate;
                }
            }
            return nullptr;
        }();
        if (hiita_candidate != nullptr) {
            require(selection.value->public_action_key == hiita_candidate->public_action_key,
                    "Teacher did not select public Hiita from IdleCommand");
            hiita_frame = *frame;
            hiita_action_key = hiita_candidate->public_action_key;
        }

        const bool is_material =
            hiita_frame.has_value() &&
            frame->decision_index == hiita_frame->decision_index + 1 &&
            frame->request.kind == EnvironmentDecisionKind::UnselectCard;
        if (is_material) {
            material_frame = *frame;
            material_diagnostics = diagnostics;
            material_selected_key = selection.value->public_action_key;
        }

        const auto stepped = environment->step(ActionSelection{
            frame->contract_id, frame->episode_semantic_id,
            frame->public_semantic_decision_id, frame->submission_token,
            selection.value->public_action_key});
        const auto* accepted = std::get_if<StepAccepted>(&stepped);
        require(accepted != nullptr, "V4 Teacher produced a rejected public action");
        require(runner.commit(*accepted), "V4 Teacher did not commit the accepted transition");
        if (is_material) {
            material_transition = *accepted;
            break;
        }
        boundary = accepted->next;
    }

    require(hiita_frame.has_value() && material_frame.has_value() &&
                hiita_action_key.has_value() && material_diagnostics.has_value() &&
                material_selected_key.has_value() &&
                material_transition.has_value(),
            "bounded V4 Teacher run did not reach the first Hiita material decision");
    const auto& hiita = *hiita_frame;
    const auto& material = *material_frame;
    const auto& diagnostics = *material_diagnostics;
    const auto* selected = candidate_for_key(material, *material_selected_key);
    require(selected != nullptr && selected->action_kind == EnvironmentActionKind::CardSelection &&
                selected->card_selection_operation == PublicCardSelectionOperation::Select,
            "Teacher selected a non-Select candidate at the first material boundary");
    const auto cancel = std::find_if(
        material.request.candidates.begin(), material.request.candidates.end(),
        [](const auto& candidate) {
            return candidate.action_kind == EnvironmentActionKind::Cancel;
        });
    require(cancel != material.request.candidates.end() &&
                cancel->card_selection_operation == PublicCardSelectionOperation::None,
            "first material boundary lacks a valid Cancel/None candidate");

    const auto* selected_evaluation = evaluation_for_key(diagnostics, *material_selected_key);
    const auto* cancel_evaluation = evaluation_for_key(diagnostics, cancel->public_action_key);
    require(selected_evaluation != nullptr && cancel_evaluation != nullptr &&
                selected_evaluation->status == CandidateEvaluationStatus::Supported &&
                cancel_evaluation->status == CandidateEvaluationStatus::Supported &&
                selected_evaluation->score.has_value() && cancel_evaluation->score.has_value(),
            "Teacher ranking omitted a supported material or Cancel evaluation");
    const auto progress_index = static_cast<std::size_t>(
        ScoreDimension::ActiveGoalLineOrValidatedRecoveryProgress);
    require(selected_evaluation->score->values[progress_index] >
                cancel_evaluation->score->values[progress_index] &&
                selected_evaluation->score->values[progress_index] > 0 &&
                cancel_evaluation->score->values[progress_index] == 0,
            "Select did not outrank Cancel on the corrected continuation progress");

    require(diagnostics.reconciled_continuation_commitment && diagnostics.f0_applicable &&
                !diagnostics.f1_applicable &&
                diagnostics.fallback_level ==
                    std::optional<TeacherFallbackLevel>{TeacherFallbackLevel::F0} &&
                diagnostics.effective_goal_id ==
                    std::optional<std::string>{"goal.main1.salamangreat"} &&
                diagnostics.effective_line_id ==
                    std::optional<std::string>{"line.main1.salamangreat"},
            "corrected material decision did not retain the Salamangreat F0 commitment");
    require(material_transition->transition.selected_public_action_key == *material_selected_key,
            "accepted material transition is detached from the Teacher selection");
    require(std::holds_alternative<DecisionFrame>(material_transition->next),
            "accepted material selection did not produce a forward decision successor");
    const auto& successor = std::get<DecisionFrame>(material_transition->next);
    const bool immediate_identical_idle_hiita =
        is_hiita_idle_frame(successor) &&
        successor.public_observation.canonical_safe_state_bytes() ==
            hiita.public_observation.canonical_safe_state_bytes() &&
        successor.public_candidate_domain_digest == hiita.public_candidate_domain_digest;
    require(!immediate_identical_idle_hiita,
            "corrected material selection immediately returned to the identical Hiita Idle state");

    std::cout << "HIITA_DECISION_INDEX=" << hiita.decision_index << '\n'
              << "HIITA_PUBLIC_ACTION_KEY=" << *hiita_action_key
              << '\n'
              << "FIRST_MATERIAL_DECISION_INDEX=" << material.decision_index << '\n'
              << "SELECTED_MATERIAL_PUBLIC_ACTION_KEY=" << *material_selected_key << '\n'
              << "FIRST_MATERIAL_DOMAIN_DIGEST=" << material.public_candidate_domain_digest
              << '\n';
}

}  // namespace

int main() {
    try {
        test_teacher_v4_selects_first_hiita_material();
        std::cout << "teacher_runner_v4_hiita_reproducer_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_runner_v4_hiita_reproducer_test: "
                  << error.what() << '\n';
        return 1;
    }
}
