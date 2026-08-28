#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"

namespace ygo::trajectory {

inline constexpr char kTrustedTrajectoryContractId[] = "ocgforge.trusted_trajectory.v1";
inline constexpr char kPolicyProvenanceContractId[] = "ocgforge.policy_provenance.v1";
inline constexpr char kPublicGameplayIdentityDomain[] =
    "ocgforge.public_gameplay_trajectory_identity.v1";
inline constexpr char kTrajectoryRecordIdentityDomain[] = "ocgforge.trajectory_record_identity.v1";
inline constexpr char kPolicyArtifactIdentityDomain[] = "ocgforge.policy_artifact_identity.v1";
inline constexpr char kParticipantAssignmentIdentityDomain[] =
    "ocgforge.participant_policy_assignment_identity.v1";
inline constexpr char kPolicyRngInitializationIdentityDomain[] =
    "ocgforge.policy_rng_initialization_identity.v1";
inline constexpr char kPolicyRngStreamIdentityDomain[] =
    "ocgforge.policy_rng_stream_identity.v1";
inline constexpr char kPolicyRngDecisionProvenanceDomain[] =
    "ocgforge.policy_rng_decision_provenance.v1";
inline constexpr char kNoPolicyRngContractId[] = "ocgforge.no_policy_rng.v1";
inline constexpr char kRestrictedReplayEvidenceSchemaId[] =
    "ocgforge.restricted_replay_evidence.v1";

enum class PolicyKind : std::uint8_t {
    RandomLegal = 0,
    DeterministicHeuristic = 1,
    NeuralCheckpoint = 2,
    SearchAssisted = 3,
    ImportedDemonstration = 4,
};

enum class PolicyRole : std::uint8_t {
    Behavior = 0,
    Opponent = 1,
    Evaluation = 2,
    Demonstrator = 3,
    SelfPlay = 4,
};

enum class SeatRole : std::uint8_t {
    StartingPlayer = 0,
    NonStartingPlayer = 1,
};

enum class DeckRole : std::uint8_t {
    FirstLockedDeck = 0,
    SecondLockedDeck = 1,
};

enum class PolicyRngMode : std::uint8_t {
    None = 0,
    Cursor = 1,
    State = 2,
};

struct LeagueContext final {
    std::uint64_t league_generation = 0;
    std::string league_member_id;
    std::string league_role;
};

struct PolicyArtifact final {
    PolicyKind policy_kind = PolicyKind::DeterministicHeuristic;
    std::string producer_implementation_identity;
    std::string inference_adapter_identity;
    std::string observation_adapter_identity;
    std::string action_adapter_identity;
    std::string sampling_contract_identity;
    std::string policy_rng_contract_identity;
    std::optional<std::string> model_checkpoint_identity;
    std::optional<std::string> search_contract_identity;
    std::optional<std::string> demonstration_source_identity;
    std::optional<std::string> artifact_metadata_identity;
    std::string policy_artifact_id;
};

struct ParticipantPolicyAssignment final {
    std::uint8_t player = 0;
    SeatRole seat_role = SeatRole::StartingPlayer;
    DeckRole deck_role = DeckRole::FirstLockedDeck;
    std::string resolved_locked_deck_id;
    std::string resolved_locked_deck_sha256;
    PolicyRole policy_role = PolicyRole::Behavior;
    std::string policy_artifact_id;
    std::uint32_t assignment_epoch = 0;
    std::uint64_t effective_from_decision_index = 0;
    std::optional<LeagueContext> league_context;
    std::string participant_policy_assignment_id;
};

struct PolicyRngInitializationIdentity final {
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::vector<std::uint8_t> initialization_material;
    std::string policy_rng_initialization_identity;
};

struct PolicyRngStreamIdentity final {
    std::string policy_artifact_id;
    std::string participant_policy_assignment_id;
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::string policy_rng_initialization_identity;
    std::string policy_rng_identity;
};

struct PolicyRngDecisionProvenance final {
    std::uint64_t decision_index = 0;
    std::string acting_policy_assignment_id;
    std::string policy_rng_identity;
    std::string policy_rng_contract_identity;
    std::string policy_rng_stream_id;
    std::string policy_rng_initialization_identity;
    PolicyRngMode mode = PolicyRngMode::None;
    std::optional<std::uint64_t> pre_cursor;
    std::optional<std::uint64_t> post_cursor;
    std::optional<std::vector<std::uint8_t>> pre_state;
    std::optional<std::vector<std::uint8_t>> post_state;
};

struct PolicyProvenanceEnvelope final {
    std::vector<PolicyArtifact> policy_artifacts;
    std::vector<ParticipantPolicyAssignment> participant_assignments;
};

enum class CollectionDispositionKind : std::uint8_t {
    Clean = 0,
    QuarantinedAfterPolicyRejection = 1,
};

struct CollectionDisposition final {
    CollectionDispositionKind kind = CollectionDispositionKind::Clean;
    std::vector<environment::RejectionCode> policy_rejections;
};

struct EpisodeManifest final {
    std::string trusted_trajectory_contract_id = kTrustedTrajectoryContractId;
    std::string v2_contract_id = std::string(environment::kEpisodicEnvironmentV2ContractId);
    std::string environment_semantic_id;
    std::vector<std::uint8_t> environment_identity_input;
    std::string episode_identity_schema_id = std::string(environment::kEpisodeIdentitySchemaId);
    std::string episode_semantic_id;
    std::vector<std::uint8_t> episode_identity_input;
    PolicyProvenanceEnvelope policy_provenance;
    CollectionDisposition collection_disposition;
};

struct PublicFrameSnapshot final {
    std::string v2_contract_id = std::string(environment::kEpisodicEnvironmentV2ContractId);
    std::string episode_semantic_id;
    std::string public_semantic_decision_id;
    std::uint64_t decision_index = 0;
    std::uint8_t acting_player = 0;
    environment::PublicEnvironmentObservation public_observation;
    std::string public_observation_digest;
    environment::EnvironmentDecisionRequest request;
    std::string public_candidate_domain_digest;
};

enum class TransitionClass : std::uint8_t {
    AtomicEngineResponse = 0,
    IntermediateContinuation = 1,
    FinalContinuationResponse = 2,
};

enum class NextFrameTargetKind : std::uint8_t {
    NextDecisionRecord = 0,
    InterruptionPendingUnactedFrame = 1,
};

struct NextFrameTarget final {
    NextFrameTargetKind kind = NextFrameTargetKind::NextDecisionRecord;
    std::uint64_t next_decision_index = 0;
    std::string next_public_semantic_decision_id;
};

enum class SuccessorKind : std::uint8_t {
    NextFrame = 0,
    Terminal = 1,
    Interrupted = 2,
    Failed = 3,
};

struct Successor final {
    SuccessorKind kind = SuccessorKind::Failed;
    std::optional<NextFrameTarget> next_frame;
};

struct DecisionRecord final {
    PublicFrameSnapshot frame;
    std::string selected_public_action_key;
    TransitionClass transition_class = TransitionClass::AtomicEngineResponse;
    Successor successor;
    std::string acting_policy_assignment_id;
    PolicyRngDecisionProvenance policy_rng_decision_provenance;
};

struct TerminalClosure final {
    std::uint8_t winner = 255;
    std::uint8_t win_reason = 255;
    std::uint64_t semantic_action_count = 0;
    std::optional<std::uint64_t> last_decision_index;
    environment::PublicEnvironmentObservation terminal_view_player_0;
    std::string terminal_view_player_0_digest;
    environment::PublicEnvironmentObservation terminal_view_player_1;
    std::string terminal_view_player_1_digest;
};

struct InterruptedClosure final {
    std::uint64_t record_count = 0;
    std::optional<PublicFrameSnapshot> pending_unacted_frame;
};

struct FailedClosure final {
    environment::FailureCode failure_code = environment::FailureCode::InvalidAuthoritativeState;
    environment::FailureStage failure_stage = environment::FailureStage::Validation;
    bool mutation_may_have_occurred = false;
    std::uint64_t record_count = 0;
};

using EpisodeClosure = std::variant<TerminalClosure, InterruptedClosure, FailedClosure>;

struct EpisodeEnvelope final {
    EpisodeManifest manifest;
    std::vector<DecisionRecord> records;
    EpisodeClosure closure;
};

struct RestrictedReplayEvidence final {
    std::string v2_contract_id = std::string(environment::kEpisodicEnvironmentV2ContractId);
    std::string episode_semantic_id;
    environment::InterruptionReason interruption_reason =
        environment::InterruptionReason::AdministrativeCancel;
    std::uint64_t engine_process_budget = 0;
    std::uint64_t semantic_action_budget = 0;
    std::uint64_t observed_engine_process_count = 0;
    std::uint64_t observed_semantic_action_count = 0;
    std::uint64_t final_engine_step_index = 0;
};

struct ShardEntry final {
    std::string episode_envelope_sha256;
    std::vector<std::uint8_t> envelope_bytes;
};

struct CandidateTrajectoryShard final {
    std::vector<ShardEntry> entries;
};

struct InterruptedEvidenceEntry final {
    std::string episode_envelope_sha256;
    RestrictedReplayEvidence evidence;
};

struct RngInitializationEvidenceEntry final {
    std::string policy_rng_initialization_identity;
    std::vector<std::uint8_t> initialization_material;
};

struct RestrictedCollectionEvidenceBundle final {
    std::string candidate_shard_artifact_sha256;
    std::vector<InterruptedEvidenceEntry> interrupted_episodes;
    std::vector<RngInitializationEvidenceEntry> rng_initializations;
};

struct AdmissionEntryCommitment final {
    std::string trajectory_record_id;
    std::string public_gameplay_trajectory_id;
    std::string environment_semantic_id;
    std::string episode_semantic_id;
    std::string episode_envelope_sha256;
    std::uint8_t closure_kind = 0;
};

struct AdmissionReceipt final {
    std::string admission_contract_id = "ocgforge.admission_receipt.v1";
    std::string candidate_shard_artifact_sha256;
    std::string restricted_evidence_artifact_sha256;
    std::vector<AdmissionEntryCommitment> entries;
};

struct DatasetManifestMember final {
    std::string trajectory_record_id;
    std::string public_gameplay_trajectory_id;
    std::string admission_receipt_id;
    std::string candidate_shard_artifact_sha256;
    std::string episode_envelope_sha256;
};

struct DatasetManifest final {
    std::string dataset_manifest_schema_id = "ocgforge.dataset_manifest.v1";
    std::string dataset_identity_schema_id = "ocgforge.dataset_identity.v1";
    std::string trusted_trajectory_contract_id = kTrustedTrajectoryContractId;
    std::string dataset_semantic_id;
    std::vector<DatasetManifestMember> members;
};

}  // namespace ygo::trajectory
