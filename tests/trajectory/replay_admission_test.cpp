#include "ygo/trajectory/admission.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/recorder.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "episodic_environment_test_access.hpp"
#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;
using namespace ygo::trajectory::admission;
using namespace ygo::trajectory::dataset;
using namespace trajectory_test;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct CollectedEpisode final {
    EpisodeEnvelope envelope;
    RestrictedCollectionEvidenceBundle evidence;
    CandidateTrajectoryShard shard;
    RunControl control;
};

ReplayOptions replay_options_for(const CollectedEpisode& collected) {
    ReplayOptions options;
    if (std::holds_alternative<TerminalClosure>(collected.envelope.closure)) {
        options.terminal_run_control = collected.control;
    } else {
        options.cancellation_source = collected.control.cancellation.source;
    }
    return options;
}

RestrictedReplayEvidence restricted_evidence_from(
    const EpisodeInterrupted& interruption,
    const std::string& episode_semantic_id) {
    RestrictedReplayEvidence result;
    result.episode_semantic_id = episode_semantic_id;
    result.interruption_reason = interruption.reason;
    result.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    result.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    result.observed_engine_process_count = interruption.run_control_evidence.engine_process_count;
    result.observed_semantic_action_count = interruption.run_control_evidence.semantic_action_count;
    result.final_engine_step_index = interruption.final_engine_step_index;
    return result;
}

void attach_single_entry_artifacts(CollectedEpisode& result) {
    result.shard.entries.push_back(shard_entry(result.envelope));
    result.evidence.candidate_shard_artifact_sha256 =
        candidate_shard_artifact_sha256(result.shard);
}

void attach_interruption_evidence(CollectedEpisode& result,
                                  const EpisodeInterrupted& interruption) {
    require(!result.shard.entries.empty(), "restricted evidence requires a shard entry");
    result.evidence.interrupted_episodes.push_back(InterruptedEvidenceEntry{
        result.shard.entries.front().episode_envelope_sha256,
        restricted_evidence_from(interruption, result.envelope.manifest.episode_semantic_id)});
}

CollectedEpisode collect_real_engine_budget_interruption() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(42);
    const auto provenance_value = provenance();
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real V2 engine-budget factory rejected certified environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 1;
    control.semantic_action_budget = 10000;
    control.cancellation.source = "phase3b-engine-budget";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr, "real V2 engine-budget reset was rejected");
    const auto* interruption = std::get_if<EpisodeInterrupted>(&reset_accepted->next);
    require(interruption != nullptr,
            "engine-process budget reset did not produce a direct interruption");
    require(interruption->reason == InterruptionReason::EngineProcessBudget,
            "engine-process budget reset produced the wrong interruption reason");

    TrajectoryRecorder recorder(config, spec, provenance_value,
                                test_provenance_resolver());
    require(recorder.on_reset_accepted(*reset_accepted),
            "recorder rejected direct engine-budget interruption");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && sealed->records.empty(),
            "direct engine-budget interruption fabricated a record");
    require(std::holds_alternative<InterruptedClosure>(sealed->closure) &&
                !std::get<InterruptedClosure>(sealed->closure).pending_unacted_frame.has_value(),
            "direct engine-budget interruption unexpectedly retained a pending frame");

    CollectedEpisode result{*sealed, {}, {}, control};
    attach_single_entry_artifacts(result);
    attach_interruption_evidence(result, *interruption);
    return result;
}

CollectedEpisode collect_real_semantic_budget_interruption() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(41);
    const auto provenance_value = provenance();
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real V2 semantic-budget factory rejected certified environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 1;
    control.cancellation.source = "phase3b-semantic-budget";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr && std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "real V2 semantic-budget reset did not publish an actionable frame");
    const auto initial = std::get<DecisionFrame>(reset_accepted->next);

    TrajectoryRecorder recorder(config, spec, provenance_value,
                                test_provenance_resolver());
    require(recorder.on_reset_accepted(*reset_accepted),
            "recorder rejected real V2 semantic-budget reset");
    const auto assignment_it = std::find_if(
        provenance_value.participant_assignments.begin(),
        provenance_value.participant_assignments.end(),
        [&](const auto& value) { return value.player == initial.acting_player; });
    require(assignment_it != provenance_value.participant_assignments.end(),
            "semantic-budget acting player has no test assignment");
    require(!initial.request.candidates.empty(), "semantic-budget domain is empty");

    const ActionSelection selection{
        std::string(kEpisodicEnvironmentV2ContractId), initial.episode_semantic_id,
        initial.public_semantic_decision_id, initial.submission_token,
        initial.request.candidates.front().public_action_key};
    const auto step = environment->step(selection);
    const auto* step_accepted = std::get_if<StepAccepted>(&step);
    require(step_accepted != nullptr, "real V2 rejected its own semantic-budget candidate");
    const auto* interruption = std::get_if<EpisodeInterrupted>(&step_accepted->next);
    require(interruption != nullptr &&
                interruption->reason == InterruptionReason::SemanticActionBudget,
            "semantic-action budget did not close the real V2 episode");
    require(recorder.on_step_accepted(
                *step_accepted,
                no_rng(assignment_it->participant_policy_assignment_id, initial.decision_index)),
            "recorder rejected real V2 semantic-budget action");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && sealed->records.size() == 1,
            "semantic-budget interruption did not preserve its accepted action");

    CollectedEpisode result{*sealed, {}, {}, control};
    attach_single_entry_artifacts(result);
    attach_interruption_evidence(result, *interruption);
    return result;
}

CollectedEpisode collect_real_administrative_interruption() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(43);
    const auto provenance_value = provenance();
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real V2 administrative factory rejected certified environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 10000;
    control.cancellation.source = "phase3b-administrative";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr && std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "real V2 administrative reset did not publish a frame");
    const auto pending = std::get<DecisionFrame>(reset_accepted->next);

    TrajectoryRecorder recorder(config, spec, provenance_value,
                                test_provenance_resolver());
    require(recorder.on_reset_accepted(*reset_accepted),
            "recorder rejected real V2 administrative reset");
    const auto interrupted = environment->interrupt(InterruptRequest{
        std::string(kEpisodicEnvironmentV2ContractId), InterruptionReason::AdministrativeCancel});
    const auto* accepted = std::get_if<InterruptAccepted>(&interrupted);
    require(accepted != nullptr, "real V2 administrative interrupt was rejected");
    require(recorder.on_interrupt_accepted(pending, *accepted),
            "recorder rejected real V2 administrative interruption");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && sealed->records.empty(),
            "administrative interruption fabricated a record");
    const auto* closure = std::get_if<InterruptedClosure>(&sealed->closure);
    require(closure != nullptr && closure->pending_unacted_frame.has_value() &&
                closure->pending_unacted_frame->public_semantic_decision_id ==
                    pending.public_semantic_decision_id,
            "administrative interruption did not preserve its pending public frame");

    CollectedEpisode result{*sealed, {}, {}, control};
    attach_single_entry_artifacts(result);
    attach_interruption_evidence(result, accepted->interruption);
    return result;
}

CollectedEpisode collect_real_failure() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(44);
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real V2 failure factory rejected certified environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 128;
    control.semantic_action_budget = 8;
    control.cancellation.source = "phase3b-failure";
    detail::EpisodicEnvironmentTestAccess::force_next_reset_failure(*environment);
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr && std::holds_alternative<EpisodeFailure>(reset_accepted->next),
            "real V2 failure did not return an accepted failure boundary");
    const auto& failure = std::get<EpisodeFailure>(reset_accepted->next);
    require(failure.failure_code == FailureCode::UnsupportedProtocol &&
                failure.failure_stage == FailureStage::Advance &&
                !failure.mutation_may_have_occurred && failure.semantic_action_count == 0,
            "real V2 failure returned unexpected closure evidence");

    TrajectoryRecorder recorder(config, spec, provenance(), test_provenance_resolver());
    require(recorder.on_reset_accepted(*reset_accepted),
            "recorder rejected the real V2 failure boundary");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && sealed->records.empty() &&
                std::holds_alternative<FailedClosure>(sealed->closure),
            "real V2 failure was not structurally persisted as a failed closure");
    CollectedEpisode result{*sealed, {}, {}, control};
    attach_single_entry_artifacts(result);
    return result;
}

std::uint64_t fnv1a_append(const std::uint64_t hash, const std::string& value) {
    auto result = hash;
    for (const auto character : value) {
        result ^= static_cast<unsigned char>(character);
        result *= 1099511628211ULL;
    }
    return result;
}

std::uint64_t fnv1a_append_u64(const std::uint64_t hash, const std::uint64_t value) {
    auto result = hash;
    auto remaining = value;
    for (unsigned int index = 0; index < 8; ++index) {
        result ^= static_cast<unsigned char>(remaining & 0xffU);
        result *= 1099511628211ULL;
        remaining >>= 8;
    }
    return result;
}

std::size_t choose_public_hash_candidate(const DecisionFrame& frame,
                                         const std::uint64_t action_count,
                                         const std::uint64_t policy_salt) {
    require(!frame.request.candidates.empty(), "real V2 hash policy received an empty domain");
    auto hash = fnv1a_append(1469598103934665603ULL, frame.public_semantic_decision_id);
    hash = fnv1a_append(hash, frame.public_candidate_domain_digest);
    hash = fnv1a_append_u64(hash, action_count);
    hash = fnv1a_append_u64(hash, policy_salt);
    return static_cast<std::size_t>(hash % frame.request.candidates.size());
}

CollectedEpisode collect_real_continuation_interruption() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(4);
    const auto provenance_value = provenance();
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real V2 continuation factory rejected certified environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 20000;
    control.semantic_action_budget = 20000;
    control.cancellation.source = "phase3b-continuation";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr && std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "real V2 continuation reset did not publish a frame");

    TrajectoryRecorder recorder(config, spec, provenance_value,
                                test_provenance_resolver());
    require(recorder.on_reset_accepted(*reset_accepted),
            "recorder rejected real V2 continuation reset");
    bool saw_intermediate = false;
    bool saw_final = false;
    std::uint64_t action_count = 0;
    auto next = reset_accepted->next;
    while (const auto* frame = std::get_if<DecisionFrame>(&next)) {
        const auto candidate_index = choose_public_hash_candidate(*frame, action_count, 0);
        const auto assignment_it = std::find_if(
            provenance_value.participant_assignments.begin(),
            provenance_value.participant_assignments.end(),
            [&](const auto& value) { return value.player == frame->acting_player; });
        require(assignment_it != provenance_value.participant_assignments.end(),
                "continuation acting player has no test assignment");
        const auto selection = ActionSelection{
            std::string(kEpisodicEnvironmentV2ContractId), frame->episode_semantic_id,
            frame->public_semantic_decision_id, frame->submission_token,
            frame->request.candidates[candidate_index].public_action_key};
        const auto step = environment->step(selection);
        const auto* accepted = std::get_if<StepAccepted>(&step);
        require(accepted != nullptr, "real V2 hash policy action was rejected");
        if (frame->request.continuation.has_value()) {
            if (accepted->transition.core_response_submitted) {
                saw_final = true;
            } else {
                saw_intermediate = true;
            }
        }
        require(recorder.on_step_accepted(
                    *accepted,
                    no_rng(assignment_it->participant_policy_assignment_id,
                           frame->decision_index)),
                "recorder rejected real V2 continuation action");
        ++action_count;
        next = accepted->next;
        if (saw_intermediate && saw_final) {
            const auto* pending = std::get_if<DecisionFrame>(&next);
            require(pending != nullptr,
                    "final continuation did not publish the next public frame before interrupt");
            const auto interrupted = environment->interrupt(InterruptRequest{
                std::string(kEpisodicEnvironmentV2ContractId),
                InterruptionReason::AdministrativeCancel});
            const auto* accepted_interrupt = std::get_if<InterruptAccepted>(&interrupted);
            require(accepted_interrupt != nullptr,
                    "real V2 administrative interrupt after continuation was rejected");
            require(recorder.on_interrupt_accepted(*pending, *accepted_interrupt),
                    "recorder rejected continuation pending interruption");
            const auto sealed = recorder.seal();
            require(sealed.has_value(), "real V2 continuation recorder did not seal");
            require(sealed->records.size() == action_count,
                    "continuation recorder lost an accepted action");
            const auto* closure = std::get_if<InterruptedClosure>(&sealed->closure);
            require(closure != nullptr && closure->pending_unacted_frame.has_value(),
                    "continuation interruption did not retain a pending frame");
            require(std::any_of(
                        sealed->records.begin(), sealed->records.end(), [](const auto& record) {
                            return record.transition_class == TransitionClass::IntermediateContinuation;
                        }),
                    "real continuation did not persist an intermediate transition");
            require(std::any_of(
                        sealed->records.begin(), sealed->records.end(), [](const auto& record) {
                            return record.transition_class == TransitionClass::FinalContinuationResponse;
                        }),
                    "real continuation did not persist a final transition");

            CollectedEpisode result{*sealed, {}, {}, control};
            attach_single_entry_artifacts(result);
            attach_interruption_evidence(result, accepted_interrupt->interruption);
            return result;
        }
        require(action_count < 600, "real V2 continuation fixture did not reach a continuation");
    }
    throw std::runtime_error("real V2 continuation fixture closed before both continuation classes");
}

CollectedEpisode collect_real_terminal() {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(2);
    const auto provenance_value = provenance();
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "real V2 terminal factory rejected certified environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 20000;
    control.semantic_action_budget = 20000;
    control.cancellation.source = "phase3b-terminal";
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ResetAccepted>(&reset);
    require(reset_accepted != nullptr && std::holds_alternative<DecisionFrame>(reset_accepted->next),
            "real V2 terminal reset did not publish a frame");
    TrajectoryRecorder recorder(config, spec, provenance_value,
                                test_provenance_resolver());
    require(recorder.on_reset_accepted(*reset_accepted),
            "recorder rejected real V2 terminal reset");

    auto next = reset_accepted->next;
    std::uint64_t action_count = 0;
    while (const auto* frame = std::get_if<DecisionFrame>(&next)) {
        require(!frame->request.candidates.empty(), "real V2 terminal domain is empty");
        const auto assignment_it = std::find_if(
            provenance_value.participant_assignments.begin(),
            provenance_value.participant_assignments.end(),
            [&](const auto& value) { return value.player == frame->acting_player; });
        require(assignment_it != provenance_value.participant_assignments.end(),
                "terminal acting player has no test assignment");
        const auto selection = ActionSelection{
            std::string(kEpisodicEnvironmentV2ContractId), frame->episode_semantic_id,
            frame->public_semantic_decision_id, frame->submission_token,
            frame->request.candidates.front().public_action_key};
        const auto step = environment->step(selection);
        const auto* accepted = std::get_if<StepAccepted>(&step);
        require(accepted != nullptr, "real V2 terminal policy action was rejected");
        std::optional<TerminalViews> views;
        if (std::holds_alternative<EpisodeTerminal>(accepted->next)) {
            const auto player_zero = environment->perspective_terminal_view(0);
            const auto player_one = environment->perspective_terminal_view(1);
            require(player_zero.has_value() && player_one.has_value(),
                    "real V2 terminal did not expose both perspective views");
            views = TerminalViews{*player_zero, *player_one};
        }
        require(recorder.on_step_accepted(
                    *accepted,
                    no_rng(assignment_it->participant_policy_assignment_id,
                           frame->decision_index),
                    views),
                "recorder rejected real V2 terminal action");
        ++action_count;
        next = accepted->next;
        require(action_count < 1000, "real V2 terminal fixture exceeded its bounded test horizon");
    }
    require(std::holds_alternative<EpisodeTerminal>(next),
            "real V2 terminal fixture closed without a terminal boundary");
    const auto sealed = recorder.seal();
    require(sealed.has_value() && std::holds_alternative<TerminalClosure>(sealed->closure),
            "real V2 terminal recorder did not seal a terminal closure");
    CollectedEpisode result{*sealed, {}, {}, control};
    attach_single_entry_artifacts(result);
    return result;
}

std::optional<RestrictedReplayEvidence> interruption_evidence(
    const CollectedEpisode& collected) {
    if (collected.evidence.interrupted_episodes.empty()) {
        return std::nullopt;
    }
    return collected.evidence.interrupted_episodes.front().evidence;
}

void replay_collected(const CollectedEpisode& collected) {
    const auto replay = replay_episode(
        collected.envelope, interruption_evidence(collected), replay_options_for(collected));
    require(replay.accepted, "real V2 semantic replay failed: " + replay.error);
}

std::optional<AdmissionVerification> admit_collected(
    const CandidateTrajectoryShard& shard,
    const RestrictedCollectionEvidenceBundle& evidence,
    const ReplayOptions& options,
    std::string* error) {
    const auto shard_sha256 = candidate_shard_artifact_sha256(shard);
    const auto evidence_sha256 = restricted_collection_evidence_artifact_sha256(evidence);
    const auto admitted = verify_candidate_shard_for_admission(
        shard, evidence, shard_sha256, evidence_sha256, options,
        test_provenance_resolver(), error);
    if (!admitted.has_value()) {
        return std::nullopt;
    }
    return admitted;
}

std::string stochastic_stream_name(const std::uint8_t player) {
    return "phase3b-player-" + std::to_string(player);
}

PolicyRngInitializationIdentity stochastic_initialization(
    const std::string& stream_id) {
    PolicyRngInitializationIdentity value;
    value.policy_rng_contract_identity = "ocgforge.test.rng.v1";
    value.policy_rng_stream_id = stream_id;
    value.initialization_material = {0x11, 0x22, 0x33};
    value.policy_rng_initialization_identity =
        compute_policy_rng_initialization_id(value);
    return value;
}

PolicyRngDecisionProvenance stochastic_state_attribution(
    const PolicyArtifact& artifact,
    const ParticipantPolicyAssignment& assignment_value,
    const std::uint64_t decision_index) {
    const auto initialization = stochastic_initialization(
        stochastic_stream_name(assignment_value.player));
    PolicyRngStreamIdentity stream;
    stream.policy_artifact_id = artifact.policy_artifact_id;
    stream.participant_policy_assignment_id =
        assignment_value.participant_policy_assignment_id;
    stream.policy_rng_contract_identity =
        initialization.policy_rng_contract_identity;
    stream.policy_rng_stream_id = initialization.policy_rng_stream_id;
    stream.policy_rng_initialization_identity =
        initialization.policy_rng_initialization_identity;
    stream.policy_rng_identity = compute_policy_rng_stream_id(stream);

    PolicyRngDecisionProvenance value;
    value.decision_index = decision_index;
    value.acting_policy_assignment_id =
        assignment_value.participant_policy_assignment_id;
    value.policy_rng_identity = stream.policy_rng_identity;
    value.policy_rng_contract_identity = stream.policy_rng_contract_identity;
    value.policy_rng_stream_id = stream.policy_rng_stream_id;
    value.policy_rng_initialization_identity =
        stream.policy_rng_initialization_identity;
    value.mode = PolicyRngMode::State;
    value.pre_state = std::vector<std::uint8_t>{1, 2};
    value.post_state = std::vector<std::uint8_t>{3, 4};
    return value;
}

void rebuild_single_entry_artifacts(CollectedEpisode& value) {
    value.shard = {};
    attach_single_entry_artifacts(value);
}

CollectedEpisode stochastic_terminal_for_admission(
    const CollectedEpisode& source) {
    CollectedEpisode result = source;
    const auto policy = stochastic_artifact();
    PolicyProvenanceEnvelope policy_provenance;
    policy_provenance.policy_artifacts = {policy};
    policy_provenance.participant_assignments = {
        assignment(policy, 0), assignment(policy, 1)};
    std::sort(policy_provenance.participant_assignments.begin(),
              policy_provenance.participant_assignments.end(),
              [](const auto& left, const auto& right) {
                  return left.participant_policy_assignment_id <
                         right.participant_policy_assignment_id;
              });
    result.envelope.manifest.policy_provenance = policy_provenance;

    std::vector<std::uint8_t> active_players;
    for (auto& record : result.envelope.records) {
        const auto assignment_it = std::find_if(
            policy_provenance.participant_assignments.begin(),
            policy_provenance.participant_assignments.end(),
            [&](const auto& value) {
                return value.player == record.frame.acting_player;
            });
        require(assignment_it != policy_provenance.participant_assignments.end(),
                "stochastic terminal record has no participant assignment");
        record.acting_policy_assignment_id =
            assignment_it->participant_policy_assignment_id;
        record.policy_rng_decision_provenance =
            stochastic_state_attribution(policy, *assignment_it,
                                         record.frame.decision_index);
        if (std::find(active_players.begin(), active_players.end(),
                      record.frame.acting_player) == active_players.end()) {
            active_players.push_back(record.frame.acting_player);
        }
    }
    require(!active_players.empty(),
            "stochastic terminal fixture has no accepted decision records");
    require(std::find(active_players.begin(), active_players.end(), 0) !=
                active_players.end() &&
                std::find(active_players.begin(), active_players.end(), 1) !=
                active_players.end(),
            "stochastic terminal fixture did not exercise both participant assignments");

    result.evidence = {};
    rebuild_single_entry_artifacts(result);
    for (const auto player : active_players) {
        const auto initialization = stochastic_initialization(
            stochastic_stream_name(player));
        result.evidence.rng_initializations.push_back(
            RngInitializationEvidenceEntry{
                initialization.policy_rng_initialization_identity,
                initialization.initialization_material});
    }
    std::sort(result.evidence.rng_initializations.begin(),
              result.evidence.rng_initializations.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_rng_initialization_identity <
                         right.policy_rng_initialization_identity;
              });
    return result;
}

RestrictedCollectionEvidenceBundle evidence_for(
    const CandidateTrajectoryShard& shard,
    const std::vector<CollectedEpisode*>& episodes) {
    RestrictedCollectionEvidenceBundle result;
    result.candidate_shard_artifact_sha256 = candidate_shard_artifact_sha256(shard);
    for (const auto* episode : episodes) {
        result.interrupted_episodes.insert(result.interrupted_episodes.end(),
                                           episode->evidence.interrupted_episodes.begin(),
                                           episode->evidence.interrupted_episodes.end());
    }
    std::sort(result.interrupted_episodes.begin(), result.interrupted_episodes.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    return result;
}

CandidateTrajectoryShard shard_for(const std::vector<CollectedEpisode*>& episodes) {
    CandidateTrajectoryShard result;
    for (const auto* episode : episodes) {
        result.entries.insert(result.entries.end(), episode->shard.entries.begin(),
                              episode->shard.entries.end());
    }
    std::sort(result.entries.begin(), result.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    return result;
}

DatasetManifest manifest_for(const std::vector<VerifiedAdmissionReceipt>& verified_receipts) {
    DatasetManifest result;
    for (const auto& verified : verified_receipts) {
        const auto& receipt = verified.receipt();
        const auto receipt_id = admission_receipt_id(receipt);
        for (const auto& commitment : receipt.entries) {
            result.members.push_back(DatasetManifestMember{
                commitment.trajectory_record_id,
                commitment.public_gameplay_trajectory_id,
                receipt_id,
                receipt.candidate_shard_artifact_sha256,
                commitment.episode_envelope_sha256});
        }
    }
    std::sort(result.members.begin(), result.members.end(),
              [](const auto& left, const auto& right) {
                  return left.trajectory_record_id < right.trajectory_record_id;
              });
    std::vector<std::string> record_ids;
    record_ids.reserve(result.members.size());
    for (const auto& member : result.members) {
        record_ids.push_back(member.trajectory_record_id);
    }
    result.dataset_semantic_id = dataset::dataset_semantic_id(record_ids);
    return result;
}

void test_real_replay_and_admission() {
    auto engine_budget = collect_real_engine_budget_interruption();
    auto semantic_budget = collect_real_semantic_budget_interruption();
    auto administrative = collect_real_administrative_interruption();
    auto failure = collect_real_failure();
    auto continuation = collect_real_continuation_interruption();
    auto terminal = collect_real_terminal();

    replay_collected(engine_budget);
    replay_collected(semantic_budget);
    replay_collected(administrative);
    replay_collected(continuation);
    require(!replay_episode(failure.envelope, std::nullopt, ReplayOptions{}).accepted,
            "failed V2 envelope was accepted for semantic replay");
    require(!replay_episode(terminal.envelope, std::nullopt, ReplayOptions{}).accepted,
            "terminal replay accepted without explicit run-control input");
    replay_collected(terminal);
    auto draw_expectation = terminal;
    std::get<TerminalClosure>(draw_expectation.envelope.closure).winner = 2;
    require(!replay_episode(draw_expectation.envelope,
                            interruption_evidence(draw_expectation),
                            replay_options_for(draw_expectation))
                 .accepted,
            "replay ignored a changed terminal winner while comparing closure data");

    std::string error;
    auto stochastic_terminal = stochastic_terminal_for_admission(terminal);
    replay_collected(stochastic_terminal);
    const auto stochastic_admission = admit_collected(
        stochastic_terminal.shard, stochastic_terminal.evidence,
        replay_options_for(stochastic_terminal), &error);
    require(stochastic_admission.has_value(),
            "registered stochastic RNG admission failed: " + error);
    require(stochastic_admission->entries().size() == 1,
            "registered stochastic RNG admission did not commit one entry");

    auto stochastic_none = stochastic_terminal;
    stochastic_none.evidence.rng_initializations.clear();
    for (auto& record : stochastic_none.envelope.records) {
        record.policy_rng_decision_provenance =
            no_rng(record.acting_policy_assignment_id, record.frame.decision_index);
    }
    rebuild_single_entry_artifacts(stochastic_none);
    require(!admit_collected(stochastic_none.shard, stochastic_none.evidence,
                             replay_options_for(stochastic_none), &error),
            "stochastic policy with per-decision NONE provenance was admitted");

    auto wrong_rng_contract = stochastic_terminal;
    for (auto& record : wrong_rng_contract.envelope.records) {
        record.policy_rng_decision_provenance.policy_rng_contract_identity =
            "ocgforge.test.unregistered_rng.v1";
    }
    rebuild_single_entry_artifacts(wrong_rng_contract);
    require(!admit_collected(wrong_rng_contract.shard, wrong_rng_contract.evidence,
                             replay_options_for(wrong_rng_contract), &error),
            "record with an unregistered RNG contract was admitted");

    auto ambiguous_cursor = stochastic_terminal;
    for (auto& record : ambiguous_cursor.envelope.records) {
        auto& attribution = record.policy_rng_decision_provenance;
        attribution.mode = PolicyRngMode::Cursor;
        attribution.pre_cursor = 0;
        attribution.post_cursor = 1;
        attribution.pre_state.reset();
        attribution.post_state.reset();
    }
    rebuild_single_entry_artifacts(ambiguous_cursor);
    require(!admit_collected(ambiguous_cursor.shard, ambiguous_cursor.evidence,
                             replay_options_for(ambiguous_cursor), &error),
            "ambiguous CURSOR provenance was admitted");

    auto missing_initialization = stochastic_terminal;
    missing_initialization.evidence.rng_initializations.clear();
    require(!admit_collected(missing_initialization.shard,
                             missing_initialization.evidence,
                             replay_options_for(missing_initialization), &error),
            "missing restricted RNG initialization was admitted");

    auto extra_initialization = stochastic_terminal;
    extra_initialization.evidence.rng_initializations.push_back(
        RngInitializationEvidenceEntry{
            "policy_rng_initialization.v1." + std::string(64, 'f'),
            {0x11, 0x22, 0x33}});
    std::sort(extra_initialization.evidence.rng_initializations.begin(),
              extra_initialization.evidence.rng_initializations.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_rng_initialization_identity <
                         right.policy_rng_initialization_identity;
              });
    require(!admit_collected(extra_initialization.shard,
                             extra_initialization.evidence,
                             replay_options_for(extra_initialization), &error),
            "extra restricted RNG initialization was admitted");

    auto shared_rng_identity = stochastic_terminal;
    const auto player_zero_record = std::find_if(
        shared_rng_identity.envelope.records.begin(),
        shared_rng_identity.envelope.records.end(),
        [](const auto& record) { return record.frame.acting_player == 0; });
    const auto player_one_record = std::find_if(
        shared_rng_identity.envelope.records.begin(),
        shared_rng_identity.envelope.records.end(),
        [](const auto& record) { return record.frame.acting_player == 1; });
    require(player_zero_record != shared_rng_identity.envelope.records.end() &&
                player_one_record != shared_rng_identity.envelope.records.end(),
            "stochastic terminal fixture lost one participant before shared-RNG test");
    player_one_record->policy_rng_decision_provenance.policy_rng_identity =
        player_zero_record->policy_rng_decision_provenance.policy_rng_identity;
    rebuild_single_entry_artifacts(shared_rng_identity);
    require(!admit_collected(shared_rng_identity.shard,
                             shared_rng_identity.evidence,
                             replay_options_for(shared_rng_identity), &error),
            "one policy RNG identity was shared across participant assignments");

    const std::vector<CollectedEpisode*> continuation_only{&continuation};
    const auto continuation_shard = shard_for(continuation_only);
    const auto continuation_evidence = evidence_for(continuation_shard, continuation_only);
    const auto continuation_verification = admit_collected(
        continuation_shard, continuation_evidence, replay_options_for(continuation), &error);
    require(continuation_verification.has_value(),
            "real V2 continuation shard admission failed: " + error);
    require(continuation_verification->entries().size() == 1,
            "real V2 continuation admission did not commit one entry");
    const auto continuation_shard_sha256 = candidate_shard_artifact_sha256(continuation_shard);
    const auto continuation_evidence_sha256 =
        restricted_collection_evidence_artifact_sha256(continuation_evidence);
    require(!verify_candidate_shard_for_admission(
                continuation_shard, continuation_evidence, std::string(64, '0'),
                continuation_evidence_sha256, replay_options_for(continuation),
                test_provenance_resolver(), &error),
            "admission accepted a receipt binding to the wrong candidate shard artifact");
    require(!verify_candidate_shard_for_admission(
                continuation_shard, continuation_evidence, continuation_shard_sha256,
                std::string(64, '0'), replay_options_for(continuation),
                test_provenance_resolver(), &error),
            "admission accepted a receipt binding to the wrong restricted evidence artifact");
    const auto continuation_receipt = issue_admission_receipt(
        *continuation_verification, &error);
    require(continuation_receipt.has_value(),
            "real V2 admission receipt issuance failed: " + error);
    const auto decoded_receipt = decode_admission_receipt(
        canonical_admission_receipt_bytes(continuation_receipt->receipt()));
    require(static_cast<bool>(decoded_receipt),
            "real V2 admission receipt did not strict round-trip");
    require(admission_receipt_id(continuation_receipt->receipt()) ==
                "admission_receipt.v1." +
                    trace::sha256_bytes(canonical_admission_receipt_bytes(
                        continuation_receipt->receipt())),
            "real V2 admission receipt identity is not content-addressed");

    const std::vector<CollectedEpisode*> combined{&engine_budget, &administrative};
    const auto shard_a = shard_for(combined);
    const auto evidence_a = evidence_for(shard_a, combined);
    const auto verification_a = admit_collected(
        shard_a, evidence_a, replay_options_for(engine_budget), &error);
    require(verification_a.has_value() && verification_a->entries().size() == 2,
            "whole-shard admission did not return both real interruption entries: " + error);
    const auto packed_receipt = issue_admission_receipt(*verification_a, &error);
    require(packed_receipt.has_value(), "packed admission receipt issuance failed: " + error);

    auto quarantined = failure.envelope;
    quarantined.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    quarantined.manifest.collection_disposition.policy_rejections = {
        RejectionCode::StaleSubmissionToken};
    const auto quarantined_entry = shard_entry(quarantined);
    auto mixed_shard = shard_a;
    mixed_shard.entries.push_back(quarantined_entry);
    std::sort(mixed_shard.entries.begin(), mixed_shard.entries.end(),
              [](const auto& left, const auto& right) {
                  return left.episode_envelope_sha256 < right.episode_envelope_sha256;
              });
    auto mixed_evidence = evidence_for(mixed_shard, combined);
    mixed_evidence.candidate_shard_artifact_sha256 =
        candidate_shard_artifact_sha256(mixed_shard);
    const auto mixed_admission = admit_collected(
        mixed_shard, mixed_evidence, replay_options_for(engine_budget), &error);
    require(!mixed_admission.has_value(),
            "whole-shard admission partially admitted a mixed good/quarantined shard");

    const std::vector<CollectedEpisode*> split_left{&engine_budget};
    const std::vector<CollectedEpisode*> split_right{&administrative};
    const auto shard_b_left = shard_for(split_left);
    const auto shard_b_right = shard_for(split_right);
    const auto evidence_b_left = evidence_for(shard_b_left, split_left);
    const auto evidence_b_right = evidence_for(shard_b_right, split_right);
    const auto verification_b_left = admit_collected(
        shard_b_left, evidence_b_left, replay_options_for(engine_budget), &error);
    const auto verification_b_right = admit_collected(
        shard_b_right, evidence_b_right, replay_options_for(administrative), &error);
    require(verification_b_left.has_value() && verification_b_right.has_value(),
            "split-shard admission failed: " + error);
    const auto split_receipt_left = issue_admission_receipt(*verification_b_left, &error);
    const auto split_receipt_right = issue_admission_receipt(*verification_b_right, &error);
    require(split_receipt_left.has_value() && split_receipt_right.has_value(),
            "split admission receipt issuance failed: " + error);
    require(candidate_shard_artifact_sha256(shard_a) !=
                candidate_shard_artifact_sha256(shard_b_left),
            "re-sharding unexpectedly preserved the physical shard artifact hash");
    const auto packed_manifest = manifest_for({*packed_receipt});
    const auto split_manifest = manifest_for({*split_receipt_left, *split_receipt_right});
    require(packed_manifest.dataset_semantic_id == split_manifest.dataset_semantic_id,
            "dataset semantic identity changed under re-sharding");
    require(canonical_dataset_manifest_bytes(packed_manifest) !=
                canonical_dataset_manifest_bytes(split_manifest),
            "re-sharding unexpectedly preserved the physical manifest bytes");
    require(validate_dataset_manifest(packed_manifest, {*packed_receipt}, &error),
            "packed dataset manifest validation failed: " + error);
    require(validate_dataset_manifest(split_manifest,
                                     {*split_receipt_left, *split_receipt_right}, &error),
            "split dataset manifest validation failed: " + error);

    auto unknown_receipt = packed_manifest;
    unknown_receipt.members.front().admission_receipt_id =
        "admission_receipt.v1." + std::string(64, 'f');
    require(!validate_dataset_manifest(unknown_receipt, {*packed_receipt}, &error),
            "dataset manifest accepted an unknown verified receipt identity");
    auto conflicting_physical_binding = packed_manifest;
    conflicting_physical_binding.members.front().candidate_shard_artifact_sha256 =
        std::string(64, 'f');
    require(!validate_dataset_manifest(conflicting_physical_binding, {*packed_receipt}, &error),
            "dataset manifest accepted conflicting physical receipt provenance");
    require(!validate_dataset_manifest(packed_manifest,
                                      {*packed_receipt, *packed_receipt}, &error),
            "dataset manifest accepted duplicate verified receipt inputs");

    CandidateTrajectoryShard rejected_shard;
    rejected_shard.entries.push_back(shard_entry(engine_budget.envelope));
    auto rejected_evidence = evidence_for(rejected_shard, {&engine_budget});
    auto rejected_envelope = engine_budget.envelope;
    rejected_envelope.manifest.collection_disposition.kind =
        CollectionDispositionKind::QuarantinedAfterPolicyRejection;
    rejected_envelope.manifest.collection_disposition.policy_rejections = {
        RejectionCode::StaleSubmissionToken};
    rejected_shard.entries.front() = shard_entry(rejected_envelope);
    rejected_evidence = evidence_for(rejected_shard, {});
    require(!admit_collected(rejected_shard, rejected_evidence,
                             replay_options_for(engine_budget), &error),
            "quarantined episode was admitted");
}

}  // namespace

int main() {
    try {
        test_real_replay_and_admission();
        std::cout << "replay, admission, receipt, and dataset tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "replay, admission, receipt, and dataset tests failed: " << error.what()
                  << '\n';
        return 1;
    }
}
