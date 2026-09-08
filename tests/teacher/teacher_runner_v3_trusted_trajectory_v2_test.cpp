#include "ygo/policy/teacher_runner_v3_trajectory.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v2.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/replay_v2.hpp"
#include "ygo/trace/sha256.hpp"
#include "test_fixtures.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::policy;
using namespace ygo::teacher;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct Fixture final {
    CertifiedEnvironmentConfig environment_config = CertifiedEnvironmentConfig::canonical_v3();
    EpisodeSpec episode_spec;
    RunControl run_control;
    PolicyProvenanceEnvelope policy_provenance;
    TeacherRunnerV3Config runner_config;
};

const ParticipantPolicyAssignment& assignment_for_player(
    const std::vector<ParticipantPolicyAssignment>& assignments,
    const std::uint8_t player) {
    const auto it = std::find_if(
        assignments.begin(), assignments.end(),
        [player](const auto& assignment) { return assignment.player == player; });
    require(it != assignments.end(), "fixture lacks an assignment for the player");
    return *it;
}

Fixture fixture(const std::uint64_t root_seed = 2,
                const std::uint64_t semantic_action_budget = 1) {
    Fixture result;
    result.episode_spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    result.episode_spec.root_seed = root_seed;
    result.episode_spec.seat_assignment = SeatAssignment::Normal;
    result.episode_spec.starting_player = 0;
    result.run_control.engine_process_budget = 512;
    result.run_control.semantic_action_budget = semantic_action_budget;
    result.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    result.run_control.cancellation.source = "a4-teacher-runner";

    const auto swordsoul = make_swordsoul_tenyi_profile();
    const auto salamangreat = make_salamangreat_profile();
    const auto swordsoul_artifact = make_teacher_policy_artifact_v2(swordsoul);
    const auto salamangreat_artifact = make_teacher_policy_artifact_v2(salamangreat);
    const std::array<PolicyRole, 2> roles = {
        PolicyRole::Behavior, PolicyRole::Opponent};
    result.policy_provenance.policy_artifacts = {
        swordsoul_artifact, salamangreat_artifact};
    std::sort(result.policy_provenance.policy_artifacts.begin(),
              result.policy_provenance.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.policy_provenance.participant_assignments = make_teacher_participant_assignments(
        swordsoul_artifact, salamangreat_artifact, result.environment_config,
        result.episode_spec.seat_assignment, result.episode_spec.starting_player, roles);

    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto& assignment = assignment_for_player(
            result.policy_provenance.participant_assignments, player);
        const auto& profile = assignment.deck_role == DeckRole::FirstLockedDeck
                                  ? swordsoul
                                  : salamangreat;
        const auto& artifact = assignment.deck_role == DeckRole::FirstLockedDeck
                                   ? swordsoul_artifact
                                   : salamangreat_artifact;
        const auto binding = make_teacher_policy_binding_v2(profile);
        auto session = create_teacher_policy_session_v2(
            profile, binding, artifact, assignment);
        require(static_cast<bool>(session), "V2 Teacher session creation failed");
        result.runner_config.sessions[player] = std::move(*session.value);
    }
    return result;
}

struct Collected final {
    Fixture fixture;
    EpisodeEnvelopeV2 envelope;
    RestrictedReplayEvidenceV2 evidence;
    replay_v2::ReplayOptions replay_options;
};

Collected collect_bounded_run() {
    auto value = fixture();
    const auto cancellation_source = value.run_control.cancellation.source;
    auto created = TeacherRunnerV3TrajectoryRunner::create(
        TeacherRunnerV3TrajectoryConfig{value.environment_config, value.episode_spec,
                                        value.run_control, value.policy_provenance,
                                        std::move(value.runner_config)});
    require(static_cast<bool>(created), "V3 trajectory runner creation failed");
    auto result = created.value->run();
    require(static_cast<bool>(result),
            "V3 trajectory runner did not seal a V2 envelope: " + result.diagnostic);
    require(result.envelope.has_value() && result.replay_evidence.has_value(),
            "bounded V3 run did not return interruption evidence");
    require(result.envelope->records.size() == 1,
            "bounded V3 run did not record exactly one accepted action");
    Collected collected{std::move(value), *result.envelope, *result.replay_evidence, {}};
    collected.replay_options.cancellation_source = cancellation_source;
    return collected;
}

void require_v2_candidates_and_attribution(const EpisodeEnvelopeV2& envelope) {
    require(!envelope.records.empty(), "V2 envelope has no decision records");
    for (const auto& record : envelope.records) {
        require(record.acting_policy_assignment_id ==
                    record.policy_rng_decision_provenance.acting_policy_assignment_id,
                "record attribution is not bound to the acting assignment");
        require(record.policy_rng_decision_provenance.mode == PolicyRngMode::None &&
                    record.policy_rng_decision_provenance.policy_rng_identity ==
                        kNoPolicyRngContractId,
                "V2 Teacher record did not publish canonical NONE RNG attribution");
        for (const auto& candidate : record.frame.request.candidates) {
            require(is_public_action_key_v2(candidate.public_action_key),
                    "V3 runner recorded a non-V2 public action key");
            if (candidate.action_kind == EnvironmentActionKind::CardSelection) {
                if (record.frame.request.kind == EnvironmentDecisionKind::UnselectCard) {
                    require(candidate.card_selection_operation !=
                                PublicCardSelectionOperation::None,
                            "V3 runner dropped Select/Unselect metadata");
                } else {
                    require(candidate.card_selection_operation ==
                                PublicCardSelectionOperation::None,
                            "V3 runner recorded selection metadata on another request family");
                }
            }
        }
    }
}

void test_runner_v3_records_and_replays_v2() {
    auto collected = collect_bounded_run();
    require_v2_candidates_and_attribution(collected.envelope);
    require(std::holds_alternative<InterruptedClosureV2>(collected.envelope.closure),
            "bounded V3 run did not produce an InterruptedClosureV2");
    const auto replay = replay_v2::replay_episode_v2(
        collected.envelope, collected.evidence, collected.replay_options);
    require(replay.accepted, "sealed V2 Teacher trajectory did not replay: " + replay.error);
    require(public_gameplay_trajectory_id_v2(collected.envelope).rfind(
                "public_gameplay_trajectory.v2.", 0) == 0,
            "V3 runner returned a non-V2 gameplay identity");
    require(trajectory_record_id_v2(collected.envelope).rfind(
                "trajectory_record.v2.", 0) == 0,
            "V3 runner returned a non-V2 record identity");
}

void test_fresh_runs_are_byte_and_identity_deterministic() {
    const auto first = collect_bounded_run();
    const auto second = collect_bounded_run();
    require(canonical_episode_envelope_bytes_v2(first.envelope) ==
                canonical_episode_envelope_bytes_v2(second.envelope),
            "fresh V3 runner runs produced different V2 envelope bytes");
    require(public_gameplay_trajectory_id_v2(first.envelope) ==
                public_gameplay_trajectory_id_v2(second.envelope) &&
                trajectory_record_id_v2(first.envelope) ==
                    trajectory_record_id_v2(second.envelope),
            "fresh V3 runner runs produced different V2 identities");
}

void test_v3_boundary_and_historical_boundary_are_explicit() {
    auto invalid = fixture();
    invalid.environment_config = CertifiedEnvironmentConfig::canonical();
    invalid.environment_config.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    const auto created = TeacherRunnerV3TrajectoryRunner::create(
        TeacherRunnerV3TrajectoryConfig{invalid.environment_config, invalid.episode_spec,
                                        invalid.run_control, invalid.policy_provenance,
                                        std::move(invalid.runner_config)});
    require(!created, "V3 trajectory runner accepted a V2 environment configuration");
}

EnvironmentActionCandidate synthetic_v2_yes_no() {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::YesNo;
    result.choice = PublicChoice{PublicChoiceKind::YesNo, 0, std::nullopt};
    result.submits_engine_response = true;
    PublicActionKeyInput key;
    key.action_kind = "yes_no";
    key.choice = result.choice;
    result.public_action_key = public_action_key_v2(key);
    return result;
}

DecisionFrame synthetic_v3_frame(const CertifiedEnvironmentConfig& config,
                                 const EpisodeSpec& spec) {
    DecisionFrame result;
    result.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    result.episode_semantic_id = episode_semantic_id(config, spec);
    result.public_semantic_decision_id = "";
    result.submission_token = SubmissionToken{1, 1};
    result.decision_index = 0;
    result.engine_step_index = 7;
    result.acting_player = 0;
    result.public_observation = trajectory_test::observation(0, 0);
    result.public_observation_digest = public_observation_digest(result.public_observation);
    result.request.kind = EnvironmentDecisionKind::YesNo;
    result.request.player = 0;
    result.request.candidates = {synthetic_v2_yes_no()};
    result.public_candidate_domain_digest = public_candidate_domain_digest_v2(
        "yes_no", {result.request.candidates.front().public_action_key});
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = result.episode_semantic_id;
    identity.decision_index = result.decision_index;
    identity.acting_player = result.acting_player;
    identity.request_kind = "yes_no";
    identity.public_observation_digest = result.public_observation_digest;
    identity.public_candidate_domain_digest = result.public_candidate_domain_digest;
    result.public_semantic_decision_id = public_semantic_decision_id_v2(identity);
    return result;
}

EnvironmentActionCandidate synthetic_card_selection(
    const std::string& locator,
    const PublicCardSelectionOperation operation) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::CardSelection;
    result.source_reference =
        PublicCardReference{PublicCardReferenceKind::VisibleCard, locator};
    result.card_selection_operation = operation;
    result.submits_engine_response = true;
    PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.source_reference = result.source_reference;
    key.card_selection_operation = operation;
    result.public_action_key = public_action_key_v2(key);
    return result;
}

DecisionFrame synthetic_v3_unselect_frame(const CertifiedEnvironmentConfig& config,
                                          const EpisodeSpec& spec) {
    auto result = synthetic_v3_frame(config, spec);
    result.public_observation = trajectory_test::observation(0, 0, true);
    result.public_observation_digest = public_observation_digest(result.public_observation);
    result.request.kind = EnvironmentDecisionKind::UnselectCard;
    result.request.candidates = {
        synthetic_card_selection("p0:MONSTER_ZONE:0", PublicCardSelectionOperation::Select),
        synthetic_card_selection("p0:MONSTER_ZONE:1", PublicCardSelectionOperation::Unselect),
        synthetic_v2_yes_no()};
    result.request.candidates.back().action_kind = EnvironmentActionKind::Cancel;
    result.request.candidates.back().choice.reset();
    PublicActionKeyInput cancel_key;
    cancel_key.action_kind = "cancel";
    result.request.candidates.back().public_action_key = public_action_key_v2(cancel_key);
    std::vector<std::string> keys;
    for (const auto& candidate : result.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    result.public_candidate_domain_digest = public_candidate_domain_digest_v2(
        "unselect_card", keys);
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = result.episode_semantic_id;
    identity.decision_index = result.decision_index;
    identity.acting_player = result.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = result.public_observation_digest;
    identity.public_candidate_domain_digest = result.public_candidate_domain_digest;
    result.public_semantic_decision_id = public_semantic_decision_id_v2(identity);
    return result;
}

void test_recorder_step_rejected_and_terminal_boundaries() {
    const auto config = CertifiedEnvironmentConfig::canonical_v3();
    EpisodeSpec spec;
    spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    spec.root_seed = 4305;
    const auto provenance = trajectory_test::provenance();
    const auto frame = synthetic_v3_frame(config, spec);

    TrajectoryRecorderV2 rejected_recorder(
        config, spec, provenance, trajectory_test::test_provenance_resolver());
    std::string error;
    require(rejected_recorder.on_reset_accepted(ResetAccepted{frame}, std::nullopt, &error),
            "V2 recorder rejected synthetic V3 reset: " + error);
    StepRejected rejected;
    rejected.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    rejected.rejection_code = RejectionCode::StaleSubmissionToken;
    rejected.current_episode_semantic_id = frame.episode_semantic_id;
    rejected.current_public_semantic_decision_id = frame.public_semantic_decision_id;
    rejected.current_public_candidate_domain_digest = frame.public_candidate_domain_digest;
    rejected.authoritative_state_unchanged = true;
    require(rejected_recorder.on_step_rejected(rejected, true, &error),
            "V2 recorder rejected StepRejected boundary: " + error);
    EpisodeInterrupted interruption;
    interruption.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    interruption.episode_semantic_id = frame.episode_semantic_id;
    interruption.reason = InterruptionReason::AdministrativeCancel;
    interruption.semantic_action_count = 0;
    interruption.last_decision_index = frame.decision_index;
    interruption.last_public_semantic_decision_id = frame.public_semantic_decision_id;
    interruption.final_engine_step_index = frame.engine_step_index;
    interruption.run_control_evidence.engine_process_budget = 10;
    interruption.run_control_evidence.semantic_action_budget = 10;
    require(rejected_recorder.on_interrupt_accepted(
                std::optional<DecisionFrame>{frame}, InterruptAccepted{interruption}, &error),
            "V2 recorder rejected StepRejected interruption closure: " + error);
    const auto rejected_envelope = rejected_recorder.seal(&error);
    require(rejected_envelope.has_value() && rejected_envelope->records.empty() &&
                std::holds_alternative<InterruptedClosureV2>(rejected_envelope->closure) &&
                std::get<InterruptedClosureV2>(rejected_envelope->closure)
                    .pending_unacted_frame.has_value(),
            "StepRejected produced a fake record or lost the pending V2 frame");

    TrajectoryRecorderV2 terminal_recorder(
        config, spec, provenance, trajectory_test::test_provenance_resolver());
    EpisodeTerminal terminal;
    terminal.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    terminal.episode_semantic_id = episode_semantic_id(config, spec);
    terminal.winner = 0;
    terminal.win_reason = 1;
    terminal.semantic_action_count = 0;
    terminal.last_decision_index.reset();
    const auto terminal_views = TerminalViews{trajectory_test::observation(0, 1),
                                              trajectory_test::observation(1, 1)};
    require(terminal_recorder.on_reset_accepted(
                ResetAccepted{terminal}, terminal_views, &error),
            "V2 recorder rejected synthetic terminal boundary: " + error);
    const auto terminal_envelope = terminal_recorder.seal(&error);
    require(terminal_envelope.has_value() &&
                std::holds_alternative<TerminalClosureV2>(terminal_envelope->closure),
            "V2 recorder did not seal a terminal V2 closure");

    const auto operation_frame = synthetic_v3_unselect_frame(config, spec);
    TrajectoryRecorderV2 operation_recorder(
        config, spec, provenance, trajectory_test::test_provenance_resolver());
    require(operation_recorder.on_reset_accepted(
                ResetAccepted{operation_frame}, std::nullopt, &error),
            "V2 recorder rejected synthetic Select/Unselect frame: " + error);
    interruption.last_public_semantic_decision_id =
        operation_frame.public_semantic_decision_id;
    require(operation_recorder.on_interrupt_accepted(
                std::optional<DecisionFrame>{operation_frame},
                InterruptAccepted{interruption}, &error),
            "V2 recorder rejected Select/Unselect interruption: " + error);
    const auto operation_envelope = operation_recorder.seal(&error);
    require(operation_envelope.has_value(),
            "V2 recorder did not seal Select/Unselect frame: " + error);
    const auto& pending = std::get<InterruptedClosureV2>(operation_envelope->closure)
                              .pending_unacted_frame;
    require(pending.has_value() &&
                pending->request.candidates[0].card_selection_operation ==
                    PublicCardSelectionOperation::Select &&
                pending->request.candidates[1].card_selection_operation ==
                    PublicCardSelectionOperation::Unselect,
            "V2 recorder did not preserve Select/Unselect metadata");
}

}  // namespace

int main() {
    try {
        test_runner_v3_records_and_replays_v2();
        test_fresh_runs_are_byte_and_identity_deterministic();
        test_v3_boundary_and_historical_boundary_are_explicit();
        test_recorder_step_rejected_and_terminal_boundaries();
        const auto stable = collect_bounded_run();
        std::cout << "V2_CANONICAL_SHA256="
                  << ygo::trace::sha256_bytes(
                         canonical_episode_envelope_bytes_v2(stable.envelope))
                  << "\nV2_GAMEPLAY_ID="
                  << public_gameplay_trajectory_id_v2(stable.envelope)
                  << "\nV2_RECORD_ID="
                  << trajectory_record_id_v2(stable.envelope)
                  << "\nteacher_runner_v3_trusted_trajectory_v2_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_runner_v3_trusted_trajectory_v2_test: " << error.what() << '\n';
        return 1;
    }
}
