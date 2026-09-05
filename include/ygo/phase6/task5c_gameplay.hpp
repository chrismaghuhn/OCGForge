#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/model/encoded_model_input.hpp"
#include "ygo/model/logical_model_input.hpp"
#include "ygo/model/card_vocabulary.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/trajectory/admission.hpp"
#include "ygo/trajectory/policy_provenance.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::phase6::task5c {

inline constexpr std::string_view kGameplayJobResultSchemaId =
    "ocgforge.phase6.gameplay_job_result.v1";
inline constexpr std::string_view kGameplaySummarySchemaId =
    "ocgforge.phase6.gameplay_summary.v1";
inline constexpr std::string_view kReplayAdmissionSummarySchemaId =
    "ocgforge.phase6.task5.replay_admission_summary.v1";

inline constexpr std::string_view kGameplayJobResultIdentityDomain =
    "ocgforge.phase6.gameplay_job_result_identity.v1";
inline constexpr std::string_view kGameplayJobResultIdentityPrefix =
    "phase6_gameplay_job_result.v1.";
inline constexpr std::string_view kGameplaySummaryIdentityDomain =
    "ocgforge.phase6.gameplay_summary_identity.v1";
inline constexpr std::string_view kGameplaySummaryIdentityPrefix =
    "phase6_gameplay_summary.v1.";
inline constexpr std::string_view kReplayAdmissionSummaryIdentityDomain =
    "ocgforge.phase6.replay_admission_summary_identity.v1";
inline constexpr std::string_view kReplayAdmissionSummaryIdentityPrefix =
    "phase6_replay_admission_summary.v1.";

inline constexpr std::string_view kInferenceRequestSchemaId =
    "ocgforge.phase6.inference_request.v1";
inline constexpr std::string_view kInferenceRequestIdentityPrefix =
    "phase6_inference_request.v1.";
inline constexpr std::string_view kInferenceResponseSchemaId =
    "ocgforge.phase6.inference_response.v1";
inline constexpr std::string_view kInferenceResponseIdentityDomain =
    "ocgforge.phase6.inference_response_identity.v1";
inline constexpr std::string_view kInferenceResponseIdentityPrefix =
    "phase6_inference_response.v1.";

// This is the accepted Task5 contract identity from the T5A codec.  T5C
// consumes it; it does not create a second contract authority.
inline constexpr std::string_view kAcceptedEvaluationContractIdentity =
    "phase6_evaluation_contract.v1.f86139723c8d906cd1446e35e6aa7e3c47a8af1f6c68be9b5389c8aa62129541";
inline constexpr std::string_view kEvaluationIdentityPrefix =
    "phase6_evaluation.v1.";
inline constexpr std::string_view kEvaluationCorpusIdentityPrefix =
    "phase6_evaluation_corpus.v1.";
inline constexpr std::string_view kEvaluationJobManifestIdentityPrefix =
    "phase6_evaluation_job_manifest.v1.";
inline constexpr std::string_view kEvaluationJobIdentityPrefix =
    "phase6_evaluation_job.v1.";

inline constexpr std::string_view kImplementationAcceptanceProfile =
    "ocgforge.phase6.task5.evaluation_corpus.implementation_acceptance.v1";
inline constexpr std::string_view kImplementationAcceptanceKind =
    "IMPLEMENTATION_ACCEPTANCE";
inline constexpr std::string_view kGameplayJobKind = "GAMEPLAY";
inline constexpr std::string_view kEvaluatorSemanticVersion =
    "ocgforge.phase6.task5.evaluator.v1";
inline constexpr std::string_view kRulesBundleIdentity =
    "3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f";
inline constexpr std::string_view kMatchupIdentity =
    "ocgforge.matchup.swordsoul_salamangreat.v1";
inline constexpr std::string_view kFormatIdentity = "TCG_ADVANCED_2026_05_18";
inline constexpr std::string_view kDuelModeIdentity = "DUEL_MODE_MR5";
inline constexpr std::uint64_t kDuelFlags = 190464;
inline constexpr std::string_view kSwordsoulDeckIdentity =
    "ocgforge.swordsoul_tenyi.ml_v1";
inline constexpr std::string_view kSwordsoulDeckSha256 =
    "8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7";
inline constexpr std::string_view kSalamangreatDeckIdentity =
    "ocgforge.salamangreat.ml_v1";
inline constexpr std::string_view kSalamangreatDeckSha256 =
    "6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188";
inline constexpr std::string_view kSmokeCheckpointIdentity =
    "phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327";
inline constexpr std::string_view kSmokeCardVocabularyIdentity =
    "model_card_vocabulary.v1.a565d2b411ae16dd1fc192ed11add10efb948979024f41a0419f9a7222044820";
inline constexpr std::string_view kMeaningfulFixedMatchupProfile =
    "ocgforge.phase6.task5.evaluation_corpus.meaningful_fixed_matchup.v1";
inline constexpr std::string_view kMeaningfulFixedMatchupKind =
    "MEANINGFUL_FIXED_MATCHUP";
inline constexpr std::string_view kTask7MaterializationSchemaId =
    "ocgforge.phase6.task7.input_materialization.v1";
inline constexpr std::string_view kTask7MaterializationConfigIdentity =
    "phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a";

enum class GameplayJobStatus : std::uint8_t {
    TrustedWin = 0,
    TrustedLoss = 1,
    TrustedDraw = 2,
    Interrupted = 3,
    Failed = 4,
    Quarantined = 5,
};

enum class ReplayAdmissionStatus : std::uint8_t {
    NotRun = 0,
    Passed = 1,
    Failed = 2,
    Quarantined = 3,
};

enum class GameplayFailureStage : std::uint8_t {
    BeforePublicDecision = 0,
    PublicFrameValidation = 1,
    ModelInputValidation = 2,
    Inference = 3,
    Selection = 4,
    Environment = 5,
    Replay = 6,
    Admission = 7,
};

struct CheckpointPolicyFailureV1 final {
    GameplayFailureStage stage = GameplayFailureStage::Inference;
    std::string code;
};

namespace detail {

struct PolicyFailureClassificationV1 final {
    GameplayFailureStage stage = GameplayFailureStage::Inference;
    std::string code = "INFERENCE_FAILURE";
};

// Narrow failure attribution shared by the gameplay loop and its focused
// contract test.  A fixed opponent-policy failure is environment fixture
// failure, never checkpoint inference failure.
PolicyFailureClassificationV1 classify_policy_selection_failure(
    bool evaluated_turn,
    const std::optional<CheckpointPolicyFailureV1>& evaluated_failure) noexcept;

}  // namespace detail

std::string_view gameplay_job_status_name(GameplayJobStatus status) noexcept;
std::string_view replay_admission_status_name(ReplayAdmissionStatus status) noexcept;
std::string_view gameplay_failure_stage_name(GameplayFailureStage stage) noexcept;

// C++ transport mirror of the already accepted T5A EvaluationJobV1 identity
// input.  It is deliberately not a second artifact authority: every identity
// is recomputed from these fields using the T5A field order and primitive
// encoding before a gameplay job can run.
struct EvaluationJobV1 final {
    std::string identity_domain =
        "ocgforge.phase6.evaluation_job_identity.v1";
    std::string identity_schema =
        "ocgforge.phase6.evaluation_job_identity.v1";
    std::string evaluation_schema_id =
        "ocgforge.phase6.task5.evaluation_job.v1";
    std::string evaluation_schema_version = "v1";
    std::string evaluation_contract_identity =
        std::string(kAcceptedEvaluationContractIdentity);
    std::string corpus_profile_identity =
        std::string(kImplementationAcceptanceProfile);
    std::string job_kind = std::string(kGameplayJobKind);
    std::string matchup_id = std::string(kMatchupIdentity);
    std::string rules_bundle_id = std::string(kRulesBundleIdentity);
    std::string format_id = std::string(kFormatIdentity);
    std::string duel_mode_id = std::string(kDuelModeIdentity);
    std::uint64_t duel_flags = kDuelFlags;
    std::string seat_0_deck_role_id = std::string(kSwordsoulDeckIdentity);
    std::string seat_0_deck_content_sha256 = std::string(kSwordsoulDeckSha256);
    std::string seat_1_deck_role_id = std::string(kSalamangreatDeckIdentity);
    std::string seat_1_deck_content_sha256 = std::string(kSalamangreatDeckSha256);
    std::string evaluated_policy_checkpoint_identity =
        std::string(kSmokeCheckpointIdentity);
    std::uint8_t evaluated_policy_seat = 0;
    std::string evaluated_policy_deck_role_id =
        std::string(kSwordsoulDeckIdentity);
    std::uint8_t opponent_policy_seat = 1;
    std::string opponent_policy_deck_role_id =
        std::string(kSalamangreatDeckIdentity);
    std::string phase5_logical_model_input_contract_id =
        "ocgforge.model_logical_input.v1";
    std::string phase5_encoded_model_input_contract_id =
        "ocgforge.model_encoded_input.v1";
    std::string phase5_batch_layout_contract_id =
        "ocgforge.model_batch_layout.v1";
    std::string card_vocabulary_contract_id =
        "ocgforge.model_card_vocabulary.v1";
    std::string teacher_policy_producer_id = "ocgforge.policy.teacher_core.v1";
    std::string teacher_policy_sampling_id =
        "ocgforge.policy.deterministic_lexicographic_argmax.v1";
    std::string teacher_policy_rng_id = "ocgforge.no_policy_rng.v1";
    std::string teacher_policy_artifact_role_0_id;
    std::string teacher_policy_binding_role_0_id;
    std::string teacher_policy_artifact_role_1_id;
    std::string teacher_policy_binding_role_1_id;
    std::string opponent_policy_artifact_id;
    std::string opponent_policy_binding_id;
    std::string opponent_policy_role_id;
    std::optional<std::string> source_dataset_identity;
    std::optional<std::string> dataset_split_identity;
    std::uint64_t deterministic_seed = 1;
    std::uint8_t starting_player = 0;
    std::string evaluator_semantic_version = std::string(kEvaluatorSemanticVersion);
    std::string evaluator_semantic_source_commit;
};

std::vector<std::uint8_t> canonical_evaluation_job_bytes(
    const EvaluationJobV1& job);
std::string evaluation_job_identity(const EvaluationJobV1& job);
bool validate_evaluation_job(const EvaluationJobV1& job,
                             std::string* error = nullptr) noexcept;

// Loader-issued, immutable validation capability for the meaningful Task5C
// path.  The public API intentionally has no default or fieldwise constructor:
// only the accepted checkpoint/inference loader may issue an instance.  The
// test-only friend is a consumer-side rejection seam and is not a checkpoint
// loader or production attestation.
class MeaningfulCheckpointBindingV1 final {
public:
    MeaningfulCheckpointBindingV1(const MeaningfulCheckpointBindingV1&) = default;
    MeaningfulCheckpointBindingV1& operator=(
        const MeaningfulCheckpointBindingV1&) = delete;
    MeaningfulCheckpointBindingV1(MeaningfulCheckpointBindingV1&&) = default;
    MeaningfulCheckpointBindingV1& operator=(MeaningfulCheckpointBindingV1&&) = delete;

    const std::string& checkpoint_identity() const noexcept { return checkpoint_identity_; }
    bool manifest_validated() const noexcept { return manifest_validated_; }
    const std::string& model_architecture_config_identity() const noexcept {
        return model_architecture_config_identity_;
    }
    const std::string& phase5_logical_model_input_contract_identity() const noexcept {
        return phase5_logical_model_input_contract_identity_;
    }
    const std::string& phase5_encoded_model_input_contract_identity() const noexcept {
        return phase5_encoded_model_input_contract_identity_;
    }
    const std::string& phase5_batch_layout_contract_identity() const noexcept {
        return phase5_batch_layout_contract_identity_;
    }
    const std::string& card_vocabulary_identity() const noexcept {
        return card_vocabulary_identity_;
    }
    const std::string& dataset_identity() const noexcept { return dataset_identity_; }
    const std::string& dataset_split_identity() const noexcept {
        return dataset_split_identity_;
    }
    const std::string& training_contract_identity() const noexcept {
        return training_contract_identity_;
    }
    const std::string& canonical_weight_export_codec_identity() const noexcept {
        return canonical_weight_export_codec_identity_;
    }
    const std::string& canonical_weight_content_identity() const noexcept {
        return canonical_weight_content_identity_;
    }
    const std::string& task7_materialization_schema_id() const noexcept {
        return task7_materialization_schema_id_;
    }
    const std::string& task7_materialization_config_identity() const noexcept {
        return task7_materialization_config_identity_;
    }

private:
    struct IssuerToken final {};

    MeaningfulCheckpointBindingV1(
        IssuerToken,
        bool manifest_validated,
        std::string checkpoint_identity,
        std::string model_architecture_config_identity,
        std::string phase5_logical_model_input_contract_identity,
        std::string phase5_encoded_model_input_contract_identity,
        std::string phase5_batch_layout_contract_identity,
        std::string card_vocabulary_identity,
        std::string dataset_identity,
        std::string dataset_split_identity,
        std::string training_contract_identity,
        std::string canonical_weight_export_codec_identity,
        std::string canonical_weight_content_identity,
        std::string task7_materialization_schema_id,
        std::string task7_materialization_config_identity)
        : manifest_validated_(manifest_validated),
          checkpoint_identity_(std::move(checkpoint_identity)),
          model_architecture_config_identity_(std::move(model_architecture_config_identity)),
          phase5_logical_model_input_contract_identity_(
              std::move(phase5_logical_model_input_contract_identity)),
          phase5_encoded_model_input_contract_identity_(
              std::move(phase5_encoded_model_input_contract_identity)),
          phase5_batch_layout_contract_identity_(
              std::move(phase5_batch_layout_contract_identity)),
          card_vocabulary_identity_(std::move(card_vocabulary_identity)),
          dataset_identity_(std::move(dataset_identity)),
          dataset_split_identity_(std::move(dataset_split_identity)),
          training_contract_identity_(std::move(training_contract_identity)),
          canonical_weight_export_codec_identity_(
              std::move(canonical_weight_export_codec_identity)),
          canonical_weight_content_identity_(std::move(canonical_weight_content_identity)),
          task7_materialization_schema_id_(std::move(task7_materialization_schema_id)),
          task7_materialization_config_identity_(
              std::move(task7_materialization_config_identity)) {}

    friend class AcceptedTask7CheckpointInferenceLoader;
    friend struct Task7CheckpointBindingTestAccess;

    bool manifest_validated_ = false;
    std::string checkpoint_identity_;
    std::string model_architecture_config_identity_;
    std::string phase5_logical_model_input_contract_identity_;
    std::string phase5_encoded_model_input_contract_identity_;
    std::string phase5_batch_layout_contract_identity_;
    std::string card_vocabulary_identity_;
    std::string dataset_identity_;
    std::string dataset_split_identity_;
    std::string training_contract_identity_;
    std::string canonical_weight_export_codec_identity_;
    std::string canonical_weight_content_identity_;
    std::string task7_materialization_schema_id_;
    std::string task7_materialization_config_identity_;
};

struct EvaluationContextV1 final {
    std::string evaluation_identity;
    std::string evaluation_contract_identity =
        std::string(kAcceptedEvaluationContractIdentity);
    std::string evaluation_corpus_identity;
    std::string evaluation_job_manifest_identity;
    std::string checkpoint_identity = std::string(kSmokeCheckpointIdentity);
    std::string evaluator_semantic_version = std::string(kEvaluatorSemanticVersion);
    std::string evaluator_semantic_source_commit;
    std::string corpus_profile_identity =
        std::string(kImplementationAcceptanceProfile);
    std::string corpus_kind = std::string(kImplementationAcceptanceKind);
    std::vector<EvaluationJobV1> jobs;
};

struct MeaningfulFixedMatchupContextV1 final {
    EvaluationContextV1 evaluation_context;
    MeaningfulCheckpointBindingV1 checkpoint_binding;
};

std::string evaluation_corpus_identity(const EvaluationContextV1& context);
std::string evaluation_identity(const EvaluationContextV1& context);
std::string evaluation_job_manifest_identity(const EvaluationContextV1& context);
bool validate_evaluation_context(const EvaluationContextV1& context,
                                 std::string* error = nullptr) noexcept;
EvaluationContextV1 make_implementation_acceptance_context(
    std::string evaluator_semantic_source_commit,
    std::string checkpoint_identity = std::string(kSmokeCheckpointIdentity));

struct InferenceRequestV1 final {
    std::string schema_id = std::string(kInferenceRequestSchemaId);
    std::string checkpoint_identity;
    std::string model_input_identity;
    std::string ordered_candidate_domain_identity;
    std::optional<std::string> public_semantic_decision_id;
    std::uint8_t perspective_player = 0;
    std::uint64_t decision_index = 0;
    std::string request_identity;
};

struct InferenceResponseV1 final {
    std::string schema_id = std::string(kInferenceResponseSchemaId);
    std::string request_identity;
    std::string checkpoint_identity;
    std::string model_input_identity;
    std::string ordered_candidate_domain_identity;
    std::uint32_t score_count = 0;
    std::vector<std::string> score_f32_bits;
    std::uint32_t selected_candidate_ordinal = 0;
    std::string selected_public_action_key;
    std::string response_identity;
};

std::vector<std::uint8_t> canonical_inference_request_bytes(
    const InferenceRequestV1& request);
std::string inference_request_identity(const InferenceRequestV1& request);
std::vector<std::uint8_t> canonical_inference_response_identity_bytes(
    const InferenceResponseV1& response);
std::string inference_response_identity(const InferenceResponseV1& response);

struct InferenceResponseCreateResult final {
    std::optional<InferenceResponseV1> value;
    std::optional<std::string> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

InferenceResponseCreateResult make_inference_response(
    const InferenceRequestV1& request,
    std::vector<std::string> score_f32_bits,
    const std::vector<std::string>& ordered_candidate_keys) noexcept;
bool validate_inference_response(const InferenceRequestV1& request,
                                 const InferenceResponseV1& response,
                                 std::string* error = nullptr) noexcept;

using CheckpointInferenceProviderV1 = std::function<InferenceResponseCreateResult(
    const InferenceRequestV1&,
    const model::LogicalModelInputV1&,
    const model::EncodedModelInputV1&)>;

struct CheckpointBoundPolicyCreateResult;

class CheckpointBoundPolicyV1 final {
public:
    CheckpointBoundPolicyV1(const CheckpointBoundPolicyV1&) = delete;
    CheckpointBoundPolicyV1& operator=(const CheckpointBoundPolicyV1&) = delete;
    CheckpointBoundPolicyV1(CheckpointBoundPolicyV1&&) = default;
    CheckpointBoundPolicyV1& operator=(CheckpointBoundPolicyV1&&) = default;

    policy::PolicySelection select(
        const environment::DecisionFrame& frame) noexcept;
    bool commit(const environment::AcceptedActionTransition& transition) noexcept;
    void reject_pending_proposal() noexcept;
    policy::PolicyExecutionBinding execution_binding() const;
    const std::optional<CheckpointPolicyFailureV1>& last_failure() const noexcept {
        return last_failure_;
    }
    const std::string& checkpoint_identity() const noexcept {
        return checkpoint_identity_;
    }

private:
    friend struct CheckpointBoundPolicyCreateResult;
    friend CheckpointBoundPolicyCreateResult create_checkpoint_bound_policy(
        std::string, std::uint8_t, std::string, std::string,
        model::CardVocabularyV1, CheckpointInferenceProviderV1) noexcept;
    CheckpointBoundPolicyV1(std::string checkpoint_identity,
                           std::uint8_t participant,
                           std::string assignment_id,
                           std::string policy_artifact_id,
                           model::CardVocabularyV1 vocabulary,
                           CheckpointInferenceProviderV1 provider)
        : checkpoint_identity_(std::move(checkpoint_identity)),
          participant_(participant),
          assignment_id_(std::move(assignment_id)),
          policy_artifact_id_(std::move(policy_artifact_id)),
          vocabulary_(std::move(vocabulary)),
        provider_(std::move(provider)) {}

    policy::PolicySelection fail_with_stage(
        GameplayFailureStage stage, std::string code,
        policy::PolicyErrorCode policy_code, std::string message) noexcept;

    struct Pending final {
        std::string episode_semantic_id;
        std::string public_semantic_decision_id;
        std::string selected_public_action_key;
    };

    std::string checkpoint_identity_;
    std::uint8_t participant_ = 0;
    std::string assignment_id_;
    std::string policy_artifact_id_;
    model::CardVocabularyV1 vocabulary_;
    CheckpointInferenceProviderV1 provider_;
    std::optional<Pending> pending_;
    std::optional<CheckpointPolicyFailureV1> last_failure_;
};

struct CheckpointBoundPolicyCreateResult final {
    std::optional<CheckpointBoundPolicyV1> value;
    std::optional<policy::PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

CheckpointBoundPolicyCreateResult create_checkpoint_bound_policy(
    std::string checkpoint_identity,
    std::uint8_t participant,
    std::string participant_policy_assignment_id,
    std::string policy_artifact_id,
    model::CardVocabularyV1 vocabulary,
    CheckpointInferenceProviderV1 provider) noexcept;

struct TerminalOutcomeV1 final {
    bool terminal = true;
    std::optional<std::uint8_t> winner;
    std::optional<std::uint8_t> win_reason;
};

struct ReplayAdmissionSummaryV1 final {
    std::string schema_id = std::string(kReplayAdmissionSummarySchemaId);
    std::string evaluation_identity;
    std::string evaluation_job_identity;
    std::optional<std::string> trajectory_record_id;
    std::optional<std::string> public_gameplay_trajectory_id;
    ReplayAdmissionStatus replay_status = ReplayAdmissionStatus::NotRun;
    ReplayAdmissionStatus admission_status = ReplayAdmissionStatus::NotRun;
    std::optional<GameplayFailureStage> failure_stage;
    std::optional<std::string> failure_code;
    bool fallback_assisted = false;
};

std::vector<std::uint8_t> canonical_replay_admission_summary_bytes(
    const ReplayAdmissionSummaryV1& summary);
std::string replay_admission_summary_identity(
    const ReplayAdmissionSummaryV1& summary);
bool validate_replay_admission_summary(
    const ReplayAdmissionSummaryV1& summary,
    std::string* error = nullptr) noexcept;

struct GameplayJobResultV1 final {
    std::string schema_id = std::string(kGameplayJobResultSchemaId);
    std::string evaluation_identity;
    std::string evaluation_job_identity;
    std::string checkpoint_identity;
    GameplayJobStatus status = GameplayJobStatus::Failed;
    bool started = true;
    bool terminal_observed = false;
    bool fallback_assisted = false;
    std::optional<TerminalOutcomeV1> terminal_outcome;
    std::optional<std::string> trajectory_record_id;
    std::optional<std::string> public_gameplay_trajectory_id;
    std::string replay_admission_summary_identity;
    std::optional<GameplayFailureStage> failure_stage;
    std::optional<std::string> failure_code;
};

std::vector<std::uint8_t> canonical_gameplay_job_result_bytes(
    const GameplayJobResultV1& result);
std::string gameplay_job_result_identity(const GameplayJobResultV1& result);
bool validate_gameplay_job_result(const GameplayJobResultV1& result,
                                  std::string* error = nullptr) noexcept;

namespace detail {

// The summary denominator counts only final authoritative inference-stage
// failures.  Other policy, environment, replay, and admission failures are
// deliberately excluded.
bool counts_as_inference_failure(const GameplayJobResultV1& result) noexcept;

}  // namespace detail

struct GameplaySummaryV1 final {
    std::string schema_id = std::string(kGameplaySummarySchemaId);
    std::string evaluation_identity;
    std::string evaluation_corpus_identity;
    std::string evaluation_job_manifest_identity;
    std::string checkpoint_identity;
    std::vector<std::string> gameplay_job_result_identities;
    std::uint32_t scheduled_job_count = 0;
    std::uint32_t started_job_count = 0;
    std::uint32_t completed_terminal_job_count = 0;
    std::uint32_t trusted_win_count = 0;
    std::uint32_t trusted_loss_count = 0;
    std::uint32_t trusted_draw_count = 0;
    std::uint32_t interrupted_job_count = 0;
    std::uint32_t failed_job_count = 0;
    std::uint32_t quarantined_job_count = 0;
    std::uint32_t fallback_assisted_job_count = 0;
    std::uint32_t replay_failure_count = 0;
    std::uint32_t admission_failure_count = 0;
    std::uint32_t inference_failure_count = 0;
    std::string wilson_metric_identity =
        "ocgforge.phase6.gameplay_metrics.wilson_95.v1";
    std::uint32_t wilson_numerator = 0;
    std::uint32_t wilson_denominator = 0;
    std::string wilson_interval_status = "NOT_APPLICABLE";
};

std::vector<std::uint8_t> canonical_gameplay_summary_bytes(
    const GameplaySummaryV1& summary);
std::string gameplay_summary_identity(const GameplaySummaryV1& summary);
bool validate_gameplay_summary(const GameplaySummaryV1& summary,
                               std::string* error = nullptr) noexcept;

std::string encode_replay_admission_summary_json(
    const ReplayAdmissionSummaryV1& summary);
ReplayAdmissionSummaryV1 decode_replay_admission_summary_json(
    std::string_view data);
std::string encode_gameplay_job_result_json(const GameplayJobResultV1& result);
GameplayJobResultV1 decode_gameplay_job_result_json(std::string_view data);
std::string encode_gameplay_summary_json(const GameplaySummaryV1& summary);
GameplaySummaryV1 decode_gameplay_summary_json(std::string_view data);

std::string encode_gameplay_job_results_jsonl(
    const std::vector<GameplayJobResultV1>& results,
    const std::vector<std::string>& ordered_job_identities);
std::vector<GameplayJobResultV1> decode_gameplay_job_results_jsonl(
    std::string_view data,
    const std::vector<std::string>& ordered_job_identities);

struct GameplayEvaluationResultV1 final {
    std::vector<GameplayJobResultV1> job_results;
    std::vector<ReplayAdmissionSummaryV1> replay_admission_summaries;
    GameplaySummaryV1 summary;
};

struct FrozenGameplayEvaluatorConfigV1 final {
    EvaluationContextV1 evaluation_context;
    environment::CertifiedEnvironmentConfig environment_config;
    trajectory::PolicyArtifact evaluated_policy_artifact;
    model::CardVocabularyV1 card_vocabulary;
    CheckpointInferenceProviderV1 inference_provider;
    trajectory::ProvenanceResolver provenance_resolver;
    environment::RunControl run_control;
};

struct MeaningfulFixedMatchupEvaluatorConfigV1 final {
    MeaningfulFixedMatchupContextV1 evaluation_context;
    environment::CertifiedEnvironmentConfig environment_config;
    trajectory::PolicyArtifact evaluated_policy_artifact;
    model::CardVocabularyV1 card_vocabulary;
    CheckpointInferenceProviderV1 inference_provider;
    trajectory::ProvenanceResolver provenance_resolver;
    environment::RunControl run_control;
};

struct FrozenGameplayEvaluatorCreateResult;

class FrozenGameplayEvaluator final {
public:
    FrozenGameplayEvaluator(const FrozenGameplayEvaluator&) = delete;
    FrozenGameplayEvaluator& operator=(const FrozenGameplayEvaluator&) = delete;
    FrozenGameplayEvaluator(FrozenGameplayEvaluator&&) = default;
    FrozenGameplayEvaluator& operator=(FrozenGameplayEvaluator&&) = default;

    GameplayEvaluationResultV1 run() noexcept;

private:
    friend struct FrozenGameplayEvaluatorCreateResult;
    friend FrozenGameplayEvaluatorCreateResult create_frozen_gameplay_evaluator(
        FrozenGameplayEvaluatorConfigV1) noexcept;
    friend FrozenGameplayEvaluatorCreateResult create_meaningful_frozen_gameplay_evaluator(
        MeaningfulFixedMatchupEvaluatorConfigV1) noexcept;
    explicit FrozenGameplayEvaluator(FrozenGameplayEvaluatorConfigV1 config)
        : config_(std::move(config)) {}

    FrozenGameplayEvaluatorConfigV1 config_;
    bool has_run_ = false;
};

struct FrozenGameplayEvaluatorCreateResult final {
    std::optional<FrozenGameplayEvaluator> value;
    std::optional<policy::PolicyError> error;

    explicit operator bool() const noexcept {
        return value.has_value() && !error.has_value();
    }
};

FrozenGameplayEvaluatorCreateResult create_frozen_gameplay_evaluator(
    FrozenGameplayEvaluatorConfigV1 config) noexcept;

MeaningfulFixedMatchupContextV1 make_meaningful_fixed_matchup_context(
    MeaningfulCheckpointBindingV1 checkpoint_binding,
    const model::CardVocabularyV1& concrete_vocabulary,
    std::string evaluator_semantic_source_commit);

FrozenGameplayEvaluatorCreateResult create_meaningful_frozen_gameplay_evaluator(
    MeaningfulFixedMatchupEvaluatorConfigV1 config) noexcept;

}  // namespace ygo::phase6::task5c
