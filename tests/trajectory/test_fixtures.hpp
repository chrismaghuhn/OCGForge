#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trace/sha256.hpp"

namespace trajectory_test {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;

inline PublicEnvironmentObservation observation(const std::uint8_t player,
                                                const std::uint64_t decision_index,
                                                const bool continuation = false) {
    observation::PlayerObservation source;
    source.perspective_player = player;
    source.decision_index = decision_index;
    source.match_context.perspective_player = player;
    source.match_context.own_deck.known = true;
    source.match_context.opponent_deck.known = false;
    source.decision_context.kind = continuation ? "card_selection" : "yes_no";
    source.decision_context.player = player;
    return project_public_observation(source);
}

inline PolicyArtifact deterministic_artifact() {
    PolicyArtifact value;
    value.policy_kind = PolicyKind::DeterministicHeuristic;
    value.producer_implementation_identity = "ocgforge.test.producer.v1";
    value.inference_adapter_identity = "ocgforge.test.inference.v1";
    value.observation_adapter_identity = "ocgforge.test.observation.v1";
    value.action_adapter_identity = "ocgforge.test.action.v1";
    value.sampling_contract_identity = "ocgforge.test.deterministic_sampling.v1";
    value.policy_rng_contract_identity = kNoPolicyRngContractId;
    value.policy_artifact_id = compute_policy_artifact_id(value);
    return value;
}

inline ParticipantPolicyAssignment assignment(const PolicyArtifact& policy,
                                              const std::uint8_t player) {
    ParticipantPolicyAssignment value;
    value.player = player;
    value.seat_role = player == 0 ? SeatRole::StartingPlayer : SeatRole::NonStartingPlayer;
    value.deck_role = player == 0 ? DeckRole::FirstLockedDeck : DeckRole::SecondLockedDeck;
    value.resolved_locked_deck_id = player == 0 ? "ocgforge.swordsoul_tenyi.ml_v1"
                                                : "ocgforge.salamangreat.ml_v1";
    value.resolved_locked_deck_sha256 =
        player == 0 ? "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7"
                    : "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
    value.policy_artifact_id = policy.policy_artifact_id;
    value.participant_policy_assignment_id = compute_participant_policy_assignment_id(value);
    return value;
}

inline PolicyProvenanceEnvelope provenance() {
    const auto policy = deterministic_artifact();
    PolicyProvenanceEnvelope value;
    value.policy_artifacts = {policy};
    value.participant_assignments = {assignment(policy, 0), assignment(policy, 1)};
    std::sort(value.participant_assignments.begin(), value.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    return value;
}

inline EpisodeSpec episode_spec(const std::uint64_t root_seed) {
    EpisodeSpec value;
    value.root_seed = root_seed;
    value.seat_assignment = SeatAssignment::Normal;
    value.starting_player = 0;
    return value;
}

inline EnvironmentActionCandidate candidate(const EnvironmentActionKind action_kind,
                                            const std::string& continuation_operation = "",
                                            const bool submits_engine_response = true) {
    PublicActionKeyInput key_input;
    key_input.action_kind = std::string(environment_action_kind_name(action_kind));
    if (action_kind == EnvironmentActionKind::Pick ||
        action_kind == EnvironmentActionKind::AssignAmount) {
        key_input.source_index = 0;
    }
    if (action_kind == EnvironmentActionKind::AssignAmount) {
        key_input.amount = 0;
    }
    key_input.continuation_operation = continuation_operation;
    EnvironmentActionCandidate value;
    value.action_kind = action_kind;
    value.source_index = key_input.source_index;
    value.amount = key_input.amount;
    value.continuation_operation = continuation_operation;
    value.submits_engine_response = submits_engine_response;
    value.public_action_key = public_action_key(key_input);
    return value;
}

inline PublicFrameSnapshot frame(const CertifiedEnvironmentConfig& config,
                                 const EpisodeSpec& spec,
                                 const std::uint64_t decision_index,
                                 const std::vector<EnvironmentActionCandidate>& candidates,
                                 const bool continuation = false) {
    PublicFrameSnapshot value;
    value.episode_semantic_id = episode_semantic_id(config, spec);
    value.decision_index = decision_index;
    value.acting_player = 0;
    value.public_observation = observation(0, decision_index, continuation);
    value.public_observation_digest = public_observation_digest(value.public_observation);
    value.request.kind = continuation ? EnvironmentDecisionKind::CardSelection
                                      : EnvironmentDecisionKind::YesNo;
    value.request.player = 0;
    if (continuation) {
        EnvironmentContinuationView continuation_view;
        continuation_view.continuation_kind = "unordered";
        continuation_view.continuation_step = static_cast<std::uint32_t>(decision_index);
        continuation_view.remaining_indices = {0, 1};
        continuation_view.min_count = 1;
        continuation_view.max_count = 1;
        continuation_view.available_mask = 3;
        continuation_view.continuation_steps = 2;
        continuation_view.can_finish = !candidates.empty() &&
                                       candidates.front().continuation_operation == "finish";
        value.request.continuation = continuation_view;
    }
    value.request.candidates = candidates;
    std::vector<std::string> keys;
    for (const auto& item : candidates) {
        keys.push_back(item.public_action_key);
    }
    value.public_candidate_domain_digest = public_candidate_domain_digest(
        std::string(environment_decision_kind_name(value.request.kind)), keys);
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = value.episode_semantic_id;
    identity.decision_index = value.decision_index;
    identity.acting_player = value.acting_player;
    identity.request_kind = std::string(environment_decision_kind_name(value.request.kind));
    identity.public_observation_digest = value.public_observation_digest;
    identity.public_candidate_domain_digest = value.public_candidate_domain_digest;
    value.public_semantic_decision_id = public_semantic_decision_id(identity);
    return value;
}

inline DecisionFrame v2_frame(const PublicFrameSnapshot& value,
                              const std::uint64_t engine_step_index) {
    return DecisionFrame{std::string(kEpisodicEnvironmentV2ContractId),
                         value.episode_semantic_id,
                         value.public_semantic_decision_id,
                         SubmissionToken{1, value.decision_index + 1},
                         value.decision_index,
                         engine_step_index,
                         value.acting_player,
                         value.public_observation,
                         value.request,
                         value.public_observation_digest,
                         value.public_candidate_domain_digest};
}

inline PolicyRngDecisionProvenance no_rng(const std::string& assignment_id,
                                          const std::uint64_t decision_index) {
    PolicyRngDecisionProvenance value;
    value.decision_index = decision_index;
    value.acting_policy_assignment_id = assignment_id;
    value.policy_rng_identity = kNoPolicyRngContractId;
    value.policy_rng_contract_identity = kNoPolicyRngContractId;
    value.policy_rng_stream_id = kNoPolicyRngContractId;
    value.policy_rng_initialization_identity = kNoPolicyRngContractId;
    value.mode = PolicyRngMode::None;
    return value;
}

inline EpisodeEnvelope terminal_envelope(const std::uint64_t root_seed) {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(root_seed);
    const auto policy = provenance();
    const auto p0 = std::find_if(policy.participant_assignments.begin(),
                                 policy.participant_assignments.end(),
                                 [](const auto& value) { return value.player == 0; });
    const auto yes = candidate(EnvironmentActionKind::YesNo);
    const auto public_frame = frame(config, spec, 0, {yes});
    EpisodeEnvelope envelope;
    envelope.manifest.environment_semantic_id = config.environment_semantic_id;
    envelope.manifest.environment_identity_input = canonical_environment_identity_bytes(config);
    envelope.manifest.episode_semantic_id = episode_semantic_id(config, spec);
    envelope.manifest.episode_identity_input = canonical_episode_identity_bytes(config, spec);
    envelope.manifest.policy_provenance = policy;
    DecisionRecord record;
    record.frame = public_frame;
    record.selected_public_action_key = yes.public_action_key;
    record.transition_class = TransitionClass::AtomicEngineResponse;
    record.successor.kind = SuccessorKind::Terminal;
    record.acting_policy_assignment_id = p0->participant_policy_assignment_id;
    record.policy_rng_decision_provenance = no_rng(p0->participant_policy_assignment_id, 0);
    envelope.records.push_back(record);
    TerminalClosure closure;
    closure.winner = 0;
    closure.win_reason = 1;
    closure.semantic_action_count = 1;
    closure.last_decision_index = 0;
    closure.terminal_view_player_0 = observation(0, 1);
    closure.terminal_view_player_0_digest = public_observation_digest(closure.terminal_view_player_0);
    closure.terminal_view_player_1 = observation(1, 1);
    closure.terminal_view_player_1_digest = public_observation_digest(closure.terminal_view_player_1);
    envelope.closure = std::move(closure);
    return envelope;
}

inline EpisodeEnvelope interrupted_envelope(const std::uint64_t root_seed,
                                            const bool with_pending_frame = true) {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(root_seed);
    const auto policy = provenance();
    const auto yes = candidate(EnvironmentActionKind::YesNo);
    const auto public_frame = frame(config, spec, 0, {yes});
    EpisodeEnvelope envelope;
    envelope.manifest.environment_semantic_id = config.environment_semantic_id;
    envelope.manifest.environment_identity_input = canonical_environment_identity_bytes(config);
    envelope.manifest.episode_semantic_id = episode_semantic_id(config, spec);
    envelope.manifest.episode_identity_input = canonical_episode_identity_bytes(config, spec);
    envelope.manifest.policy_provenance = policy;
    InterruptedClosure closure;
    closure.record_count = 0;
    if (with_pending_frame) {
        closure.pending_unacted_frame = public_frame;
    }
    envelope.closure = std::move(closure);
    return envelope;
}

inline ShardEntry shard_entry(const EpisodeEnvelope& envelope) {
    const auto bytes = canonical_episode_envelope_bytes(envelope);
    return ShardEntry{trace::sha256_bytes(bytes), bytes};
}

}  // namespace trajectory_test
