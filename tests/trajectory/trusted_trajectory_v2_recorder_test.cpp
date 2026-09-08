#include "ygo/trajectory/recorder_v2.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "provenance_test_support.hpp"
#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnvironmentActionCandidate candidate_v2(
    const EnvironmentActionKind kind,
    const PublicCardSelectionOperation operation) {
    PublicActionKeyInput key;
    key.action_kind = std::string(environment_action_kind_name(kind));
    key.card_selection_operation = operation;
    EnvironmentActionCandidate value;
    value.action_kind = kind;
    value.card_selection_operation = operation;
    value.public_action_key = public_action_key_v2(key);
    value.submits_engine_response = true;
    return value;
}

DecisionFrame frame_v3(const CertifiedEnvironmentConfig& config,
                       const EpisodeSpec& spec,
                       const std::uint64_t engine_step,
                       const SubmissionToken token) {
    DecisionFrame frame;
    frame.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    frame.episode_semantic_id = episode_semantic_id(config, spec);
    frame.decision_index = 0;
    frame.engine_step_index = engine_step;
    frame.acting_player = 0;
    frame.submission_token = token;
    frame.public_observation = trajectory_test::observation(0, 0, true);
    frame.public_observation_digest = public_observation_digest(frame.public_observation);
    frame.request.kind = EnvironmentDecisionKind::UnselectCard;
    frame.request.player = 0;
    frame.request.candidates = {
        candidate_v2(EnvironmentActionKind::CardSelection,
                     PublicCardSelectionOperation::Select),
        candidate_v2(EnvironmentActionKind::Cancel,
                     PublicCardSelectionOperation::None),
    };
    std::vector<std::string> keys;
    for (const auto& candidate : frame.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    frame.public_candidate_domain_digest =
        public_candidate_domain_digest_v2("unselect_card", keys);
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = frame.episode_semantic_id;
    identity.decision_index = frame.decision_index;
    identity.acting_player = frame.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = frame.public_observation_digest;
    identity.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    frame.public_semantic_decision_id = public_semantic_decision_id_v2(identity);
    return frame;
}

TerminalViews terminal_views() {
    return TerminalViews{trajectory_test::observation(0, 1),
                          trajectory_test::observation(1, 1)};
}

EpisodeEnvelopeV2 record_one_terminal_episode(
    const std::uint64_t engine_step,
    const SubmissionToken token) {
    auto config = CertifiedEnvironmentConfig::canonical_v3();
    auto spec = trajectory_test::episode_spec(19);
    spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    const auto provenance = trajectory_test::provenance();
    TrajectoryRecorderV2 recorder(config, spec, provenance,
                                  trajectory_test::test_provenance_resolver());

    ResetAccepted reset;
    reset.next = frame_v3(config, spec, engine_step, token);
    const auto& initial_frame = std::get<DecisionFrame>(reset.next);
    std::string error;
    require(recorder.on_reset_accepted(reset, std::nullopt, &error),
            "V2 recorder rejected V3 reset frame: " + error);

    const auto p0 = std::find_if(
        provenance.participant_assignments.begin(),
        provenance.participant_assignments.end(),
        [](const auto& assignment) { return assignment.player == 0; });
    require(p0 != provenance.participant_assignments.end(),
            "fixture lacks player-zero assignment");

    StepAccepted accepted;
    accepted.transition.episode_semantic_id = initial_frame.episode_semantic_id;
    accepted.transition.public_semantic_decision_id =
        initial_frame.public_semantic_decision_id;
    accepted.transition.decision_index = 0;
    accepted.transition.selected_public_action_key =
        initial_frame.request.candidates.front().public_action_key;
    accepted.transition.core_response_submitted = true;

    EpisodeTerminal terminal;
    terminal.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    terminal.episode_semantic_id = initial_frame.episode_semantic_id;
    terminal.winner = 0;
    terminal.win_reason = 1;
    terminal.semantic_action_count = 1;
    terminal.last_decision_index = 0;
    accepted.next = terminal;
    require(recorder.on_step_accepted(
                accepted,
                trajectory_test::no_rng(p0->participant_policy_assignment_id, 0),
                terminal_views(), &error),
            "V2 recorder rejected terminal transition: " + error);

    const auto sealed = recorder.seal(&error);
    require(sealed.has_value(), "V2 recorder failed to seal: " + error);
    return *sealed;
}

void test_v3_recorder_lifecycle_and_public_projection() {
    const auto envelope = record_one_terminal_episode(11, SubmissionToken{1, 1});
    require(envelope.manifest.episodic_environment_contract_id ==
                kEpisodicEnvironmentV3ContractId,
            "V2 recorder manifest is not V3-bound");
    require(envelope.records.size() == 1, "V2 recorder did not retain one record");
    require(std::holds_alternative<TerminalClosureV2>(envelope.closure),
            "V2 recorder did not retain terminal closure");
    require(envelope.records.front().frame.request.candidates.front()
                .card_selection_operation == PublicCardSelectionOperation::Select,
            "V2 recorder dropped Select metadata");
    require(public_gameplay_trajectory_id_v2(envelope).rfind(
                "public_gameplay_trajectory.v2.", 0) == 0,
            "V2 public gameplay identity has wrong domain");
    require(trajectory_record_id_v2(envelope).rfind("trajectory_record.v2.", 0) == 0,
            "V2 trajectory record identity has wrong domain");
}

void test_private_engine_metadata_does_not_change_public_trajectory() {
    const auto first = record_one_terminal_episode(11, SubmissionToken{1, 1});
    const auto second = record_one_terminal_episode(999, SubmissionToken{77, 88});
    require(canonical_episode_envelope_bytes_v2(first) ==
                canonical_episode_envelope_bytes_v2(second),
            "private engine/token metadata changed V2 public envelope bytes");
    require(public_gameplay_trajectory_id_v2(first) ==
                public_gameplay_trajectory_id_v2(second),
            "private engine/token metadata changed public gameplay identity");
}

void test_collection_provenance_changes_record_identity_only() {
    auto first = record_one_terminal_episode(11, SubmissionToken{1, 1});
    auto second = first;
    auto extra = second.manifest.policy_provenance.participant_assignments.front();
    extra.assignment_epoch = 7;
    extra.participant_policy_assignment_id =
        compute_participant_policy_assignment_id(extra);
    second.manifest.policy_provenance.participant_assignments.push_back(extra);
    std::sort(second.manifest.policy_provenance.participant_assignments.begin(),
              second.manifest.policy_provenance.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    require(public_gameplay_trajectory_id_v2(first) ==
                public_gameplay_trajectory_id_v2(second),
            "collection provenance changed public gameplay identity");
    require(trajectory_record_id_v2(first) != trajectory_record_id_v2(second),
            "collection provenance did not change trajectory record identity");
}

void test_v2_recorder_rejects_historical_frame() {
    auto config = CertifiedEnvironmentConfig::canonical_v3();
    auto spec = trajectory_test::episode_spec(19);
    spec.contract_id = std::string(kEpisodicEnvironmentV3ContractId);
    TrajectoryRecorderV2 recorder(
        config, spec, trajectory_test::provenance(),
        trajectory_test::test_provenance_resolver());
    auto frame = frame_v3(config, spec, 0, SubmissionToken{1, 1});
    frame.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    ResetAccepted reset;
    reset.next = frame;
    std::string error;
    require(!recorder.on_reset_accepted(reset, std::nullopt, &error),
            "V2 recorder accepted historical V2 frame");
}

}  // namespace

int main() {
    try {
        test_v3_recorder_lifecycle_and_public_projection();
        test_private_engine_metadata_does_not_change_public_trajectory();
        test_collection_provenance_changes_record_identity_only();
        test_v2_recorder_rejects_historical_frame();
        std::cout << "trusted trajectory V2 recorder tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
