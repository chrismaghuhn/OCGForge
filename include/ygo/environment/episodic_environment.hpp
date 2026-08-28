#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ygo/environment/candidate_domain_evidence.hpp"
#include "ygo/environment/identity_contract.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/environment/public_environment_observation.hpp"

namespace ygo::environment {

inline constexpr std::string_view kEpisodicEnvironmentContractId =
    kEpisodicEnvironmentV2ContractId;
inline constexpr std::string_view kEnvironmentIdentitySchemaId =
    kEnvironmentIdentityV2SchemaId;
inline constexpr std::string_view kEpisodeIdentitySchemaId =
    "ocgforge.episode_identity.v1";
inline constexpr std::string_view kSemanticDecisionIdentitySchemaId =
    "ocgforge.semantic_decision_identity.v1";
inline constexpr std::string_view kObservationContractId = "ygo.player_observation.v1";

using PublicEnvironmentObservation = PublicEnvironmentObservationInput;

struct CertifiedDeckIdentity final {
    std::string id;
    std::string sha256;
};

struct CertifiedEnvironmentConfig final {
    std::string contract_id = std::string(kEpisodicEnvironmentContractId);
    std::string environment_semantic_id;

    std::string decision_contract_id = std::string(kDecisionContractId);
    std::string observation_contract_id = std::string(kObservationContractId);
    std::string action_identity_schema_id = std::string(kActionIdentitySchemaId);
    std::string public_action_identity_schema_id = std::string(kPublicActionIdentitySchemaId);
    std::string candidate_digest_schema_id = std::string(kCandidateDomainSchemaId);
    std::string public_candidate_digest_schema_id = std::string(kPublicCandidateDomainSchemaId);
    std::string episode_identity_schema_id = std::string(kEpisodeIdentitySchemaId);
    std::string decision_identity_schema_id = std::string(kSemanticDecisionIdentitySchemaId);
    std::string public_decision_identity_schema_id =
        std::string(kPublicSemanticDecisionIdentitySchemaId);
    std::string public_observation_contract_id =
        std::string(kPublicEnvironmentObservationSchemaId);
    std::string public_safe_state_schema_id = std::string(kPublicSafeStateSchemaId);
    std::string seed_derivation_id = std::string(kSeedDerivationId);

    std::string rules_bundle_id;
    std::string core_api_version;
    std::string ocgcore_commit;
    std::string ocgcore_resolved_checkout_sha256;
    std::string core_patchset_id;
    std::string core_patchset_sha256;
    std::string cardscripts_commit;
    std::string cardscripts_resolved_checkout_sha256;
    std::string database_commit;
    std::string database_resolved_checkout_sha256;
    std::string database_artifact_sha256;
    std::string format_id;
    std::string duel_mode;
    std::uint64_t duel_flags = 0;
    std::vector<CertifiedDeckIdentity> locked_decks;
    std::string required_script_closure_identity;

    static CertifiedEnvironmentConfig canonical();
};

enum class SeatAssignment : std::uint8_t {
    Normal = 0,
    Mirror = 1,
};

struct EpisodeSpec final {
    std::string contract_id = std::string(kEpisodicEnvironmentContractId);
    std::uint64_t root_seed = 0;
    SeatAssignment seat_assignment = SeatAssignment::Normal;
    std::uint8_t starting_player = 0;
};

enum class EnvironmentDecisionKind : std::uint8_t {
    IdleCommand,
    BattleCommand,
    Chain,
    Option,
    CardSelection,
    Tribute,
    Sum,
    Place,
    Counter,
    Ordering,
    Announcement,
    UnselectCard,
    Position,
    YesNo,
    Unsupported,
};

enum class EnvironmentActionKind : std::uint8_t {
    IdleCommand,
    BattleCommand,
    Chain,
    Option,
    CardSelection,
    Announcement,
    Place,
    Position,
    YesNo,
    Pick,
    Finish,
    Cancel,
    AssignAmount,
    Unsupported,
};

struct EnvironmentActionCandidate final {
    EnvironmentActionKind action_kind = EnvironmentActionKind::Unsupported;
    std::string public_action_key;
    std::optional<PublicChoice> choice;
    std::optional<PublicCardReference> source_reference;
    std::optional<PublicCardReference> target_reference;
    std::optional<std::uint32_t> phase;
    std::optional<std::uint8_t> position;
    std::optional<std::uint32_t> source_index;
    std::optional<std::int32_t> amount;
    std::string continuation_operation;
    bool submits_engine_response = true;
};

struct EnvironmentContinuationView final {
    std::string continuation_kind;
    std::uint32_t continuation_step = 0;
    std::vector<std::uint32_t> selected_indices;
    std::vector<std::uint32_t> remaining_indices;
    std::vector<std::uint16_t> assigned_amounts;
    std::uint32_t min_count = 0;
    std::uint32_t max_count = 0;
    std::uint32_t target_sum = 0;
    std::uint32_t required_amount = 0;
    std::uint64_t available_mask = 0;
    std::uint64_t selected_mask = 0;
    std::uint32_t continuation_steps = 0;
    bool exact_sum = true;
    bool greater_sum = false;
    bool can_finish = false;
    bool can_cancel = false;
};

struct EnvironmentDecisionRequest final {
    EnvironmentDecisionKind kind = EnvironmentDecisionKind::Unsupported;
    std::uint8_t player = 0;
    std::vector<EnvironmentActionCandidate> candidates;
    std::optional<EnvironmentContinuationView> continuation;
};

struct SubmissionToken final {
    std::uint64_t episode_incarnation = 0;
    std::uint64_t frame_generation = 0;

    bool valid() const noexcept { return episode_incarnation != 0 && frame_generation != 0; }
    bool operator==(const SubmissionToken& other) const noexcept {
        return episode_incarnation == other.episode_incarnation &&
               frame_generation == other.frame_generation;
    }
    bool operator!=(const SubmissionToken& other) const noexcept { return !(*this == other); }
};

struct DecisionFrame final {
    std::string contract_id;
    std::string episode_semantic_id;
    std::string public_semantic_decision_id;
    SubmissionToken submission_token;
    std::uint64_t decision_index = 0;
    // Safe engine-progress evidence. It is deliberately excluded from the
    // public semantic decision identity and from public observation bytes.
    std::uint64_t engine_step_index = 0;
    std::uint8_t acting_player = 0;
    PublicEnvironmentObservation public_observation;
    EnvironmentDecisionRequest request;
    std::string public_observation_digest;
    std::string public_candidate_domain_digest;
};

struct AcceptedActionTransition final {
    std::string episode_semantic_id;
    std::string public_semantic_decision_id;
    std::uint64_t decision_index = 0;
    std::string selected_public_action_key;
    bool core_response_submitted = false;
    std::optional<std::string> final_response_sha256;
};

enum class InterruptionReason : std::uint8_t {
    EngineProcessBudget,
    SemanticActionBudget,
    AdministrativeCancel,
};

struct RunControlEvidence final {
    std::uint64_t engine_process_budget = 0;
    std::uint64_t semantic_action_budget = 0;
    std::uint64_t engine_process_count = 0;
    std::uint64_t semantic_action_count = 0;
};

struct EpisodeTerminal final {
    std::string contract_id;
    std::string episode_semantic_id;
    std::uint8_t winner = 255;
    std::uint8_t win_reason = 255;
    std::uint64_t semantic_action_count = 0;
    std::optional<std::uint64_t> last_decision_index;
    std::uint64_t final_engine_step_index = 0;
    std::string semantic_gameplay_hash;
    std::string final_audit_prefix_hash;
};

struct EpisodeInterrupted final {
    std::string contract_id;
    std::string episode_semantic_id;
    InterruptionReason reason = InterruptionReason::AdministrativeCancel;
    std::uint64_t semantic_action_count = 0;
    std::optional<std::string> last_public_semantic_decision_id;
    std::optional<std::uint64_t> last_decision_index;
    std::uint64_t final_engine_step_index = 0;
    std::string last_valid_audit_prefix_hash;
    RunControlEvidence run_control_evidence;
};

enum class FailureCode : std::uint8_t {
    RetryFailure,
    CoreError,
    UnsupportedProtocol,
    MalformedProtocol,
    IncompleteCandidates,
    DuplicateCandidates,
    ResponseInconsistency,
    CandidateObservationInconsistency,
    PrivacyInvariant,
    PublicFrameInvariant,
    InvalidAuthoritativeState,
    ResponseSubmissionFailure,
    ObservationFailure,
    InternalDomainDivergence,
    TokenNamespaceExhausted,
    ResourceIdentityMismatch,
};

enum class FailureStage : std::uint8_t {
    Validation,
    Construction,
    Advance,
    Projection,
    Action,
    Interruption,
    Teardown,
};

struct EpisodeFailure final {
    std::string contract_id;
    std::optional<std::string> episode_semantic_id;
    FailureCode failure_code = FailureCode::InvalidAuthoritativeState;
    FailureStage failure_stage = FailureStage::Validation;
    std::uint64_t semantic_action_count = 0;
    std::optional<std::string> last_public_semantic_decision_id;
    std::optional<std::string> last_valid_audit_prefix_hash;
    bool mutation_may_have_occurred = false;
    std::optional<std::string> restricted_diagnostic_reference;
};

enum class Lifecycle : std::uint8_t {
    Empty,
    AwaitingAction,
    GameTerminal,
    Interrupted,
    Failed,
};

enum class RejectionCode : std::uint8_t {
    IncompatibleContract,
    InvalidLifecycle,
    WrongEpisode,
    StaleSubmissionToken,
    WrongPublicSemanticDecision,
    UnknownPublicActionKey,
    PublicActionDomainDivergence,
    UnsupportedInterruptionReason,
};

struct ActionSelection final {
    std::string contract_id;
    std::string episode_semantic_id;
    std::string public_semantic_decision_id;
    SubmissionToken submission_token;
    std::string public_action_key;
};

struct StepRejected final {
    std::string contract_id;
    RejectionCode rejection_code = RejectionCode::InvalidLifecycle;
    std::string submitted_episode_semantic_id;
    std::string submitted_public_semantic_decision_id;
    SubmissionToken submitted_submission_token;
    std::string submitted_public_action_key;
    std::string current_episode_semantic_id;
    std::string current_public_semantic_decision_id;
    std::string current_public_candidate_domain_digest;
    bool authoritative_state_unchanged = true;
};

struct ResetControl final {
    std::string contract_id;
};

struct CancellationMetadata final {
    std::string reason = "ADMINISTRATIVE_CANCEL";
    std::string source;
};

struct RunControl final {
    std::uint64_t engine_process_budget = 0;
    std::uint64_t semantic_action_budget = 0;
    CancellationMetadata cancellation;
};

enum class ResetRejectionCode : std::uint8_t {
    ResetWhileAwaitingAction,
    InvalidContract,
    InvalidEnvironmentId,
    InvalidEpisodeSpec,
    InvalidStartingPlayer,
    InvalidRunControl,
    ResourceIdentityMismatch,
    TokenNamespaceExhausted,
    UnsupportedResetConfiguration,
};

struct ResetAccepted final {
    std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure> next;
};

struct ResetRejected final {
    ResetRejectionCode rejection_code = ResetRejectionCode::InvalidContract;
    Lifecycle observed_lifecycle = Lifecycle::Empty;
};

struct StepAccepted final {
    AcceptedActionTransition transition;
    std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure> next;
};

struct InterruptRequest final {
    std::string contract_id;
    InterruptionReason reason = InterruptionReason::AdministrativeCancel;
};

struct InterruptAccepted final {
    EpisodeInterrupted interruption;
};

struct InterruptRejected final {
    RejectionCode rejection_code = RejectionCode::InvalidLifecycle;
    Lifecycle observed_lifecycle = Lifecycle::Empty;
};

struct EnvironmentFactoryRejected final {
    ResetRejectionCode rejection_code = ResetRejectionCode::InvalidEnvironmentId;
};

using EnvironmentFactoryResult =
    std::variant<std::unique_ptr<class EpisodicEnvironment>, EnvironmentFactoryRejected>;
using ResetResult = std::variant<ResetAccepted, ResetRejected>;
using StepResult = std::variant<StepAccepted, StepRejected>;
using InterruptResult = std::variant<InterruptAccepted, InterruptRejected, EpisodeFailure>;

namespace detail {
struct EpisodicEnvironmentTestAccess;
}

std::string_view environment_decision_kind_name(EnvironmentDecisionKind kind) noexcept;
std::string_view environment_action_kind_name(EnvironmentActionKind kind) noexcept;
std::string_view interruption_reason_name(InterruptionReason reason) noexcept;
std::string_view failure_code_name(FailureCode code) noexcept;
std::string_view failure_stage_name(FailureStage stage) noexcept;
std::string_view lifecycle_name(Lifecycle lifecycle) noexcept;
std::string_view rejection_code_name(RejectionCode code) noexcept;
std::string_view reset_rejection_code_name(ResetRejectionCode code) noexcept;

class EpisodicEnvironment final {
public:
    static EnvironmentFactoryResult create(CertifiedEnvironmentConfig config);

    ~EpisodicEnvironment();
    EpisodicEnvironment(const EpisodicEnvironment&) = delete;
    EpisodicEnvironment& operator=(const EpisodicEnvironment&) = delete;
    EpisodicEnvironment(EpisodicEnvironment&&) = delete;
    EpisodicEnvironment& operator=(EpisodicEnvironment&&) = delete;

    ResetResult reset(const EpisodeSpec& spec, const RunControl& control);
    StepResult step(const ActionSelection& selection);
    InterruptResult interrupt(const InterruptRequest& request);
    std::optional<PublicEnvironmentObservation> perspective_terminal_view(
        std::uint8_t player) const;

    Lifecycle lifecycle() const noexcept;
    const CertifiedEnvironmentConfig& config() const noexcept;

private:
    friend struct detail::EpisodicEnvironmentTestAccess;

    explicit EpisodicEnvironment(CertifiedEnvironmentConfig config);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

std::vector<std::uint8_t> canonical_environment_identity_bytes(
    const CertifiedEnvironmentConfig& config);
std::string environment_semantic_id(const CertifiedEnvironmentConfig& config);

std::vector<std::uint8_t> canonical_episode_identity_bytes(
    const CertifiedEnvironmentConfig& config, const EpisodeSpec& spec);
std::string episode_semantic_id(const CertifiedEnvironmentConfig& config, const EpisodeSpec& spec);

std::vector<std::uint8_t> canonical_semantic_decision_identity_bytes(
    std::string_view episode_id, std::uint64_t decision_index, std::string_view protocol_decision_id,
    std::uint8_t acting_player, std::uint64_t engine_step_index, std::string_view observation_hash,
    std::string_view candidate_digest);
std::string semantic_decision_id(std::string_view episode_id, std::uint64_t decision_index,
                                 std::string_view protocol_decision_id, std::uint8_t acting_player,
                                 std::uint64_t engine_step_index, std::string_view observation_hash,
                                 std::string_view candidate_digest);

}  // namespace ygo::environment
