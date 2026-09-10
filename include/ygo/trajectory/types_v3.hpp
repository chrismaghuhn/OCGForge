#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ygo/trajectory/types.hpp"

namespace ygo::trajectory {

inline constexpr char kTrustedTrajectoryV3ContractId[] =
    "ocgforge.trusted_trajectory.v3";
inline constexpr char kPublicGameplayIdentityV3Domain[] =
    "ocgforge.public_gameplay_trajectory_identity.v3";
inline constexpr char kTrajectoryRecordIdentityV3Domain[] =
    "ocgforge.trajectory_record_identity.v3";

struct EpisodeManifestV3 final {
    std::string trusted_trajectory_contract_id = kTrustedTrajectoryV3ContractId;
    std::string episodic_environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV4ContractId);
    std::string environment_semantic_id;
    std::vector<std::uint8_t> environment_identity_input;
    std::string episode_identity_schema_id = std::string(environment::kEpisodeIdentitySchemaId);
    std::string episode_semantic_id;
    std::vector<std::uint8_t> episode_identity_input;
    PolicyProvenanceEnvelope policy_provenance;
    CollectionDisposition collection_disposition;
};

struct PublicFrameSnapshotV3 final {
    std::string episodic_environment_contract_id =
        std::string(environment::kEpisodicEnvironmentV4ContractId);
    std::string episode_semantic_id;
    std::string public_semantic_decision_id;
    std::uint64_t decision_index = 0;
    std::uint8_t acting_player = 0;
    environment::PublicEnvironmentObservation public_observation;
    std::string public_observation_digest;
    environment::EnvironmentDecisionRequest request;
    std::string public_candidate_domain_digest;
};

struct DecisionRecordV3 final {
    PublicFrameSnapshotV3 frame;
    std::string selected_public_action_key;
    TransitionClass transition_class = TransitionClass::AtomicEngineResponse;
    Successor successor;
    std::string acting_policy_assignment_id;
    PolicyRngDecisionProvenance policy_rng_decision_provenance;
};

struct TerminalClosureV3 final {
    std::uint8_t winner = 255;
    std::uint8_t win_reason = 255;
    std::uint64_t semantic_action_count = 0;
    std::optional<std::uint64_t> last_decision_index;
    environment::PublicEnvironmentObservation terminal_view_player_0;
    std::string terminal_view_player_0_digest;
    environment::PublicEnvironmentObservation terminal_view_player_1;
    std::string terminal_view_player_1_digest;
};

struct InterruptedClosureV3 final {
    std::uint64_t record_count = 0;
    std::optional<PublicFrameSnapshotV3> pending_unacted_frame;
};

struct FailedClosureV3 final {
    environment::FailureCode failure_code = environment::FailureCode::InvalidAuthoritativeState;
    environment::FailureStage failure_stage = environment::FailureStage::Validation;
    bool mutation_may_have_occurred = false;
    std::uint64_t record_count = 0;
};

using EpisodeClosureV3 =
    std::variant<TerminalClosureV3, InterruptedClosureV3, FailedClosureV3>;

struct EpisodeEnvelopeV3 final {
    EpisodeManifestV3 manifest;
    std::vector<DecisionRecordV3> records;
    EpisodeClosureV3 closure;
};

}  // namespace ygo::trajectory
