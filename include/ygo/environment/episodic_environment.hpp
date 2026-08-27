#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/candidate_domain_evidence.hpp"
#include "ygo/environment/identity_contract.hpp"

namespace ygo::environment {

inline constexpr std::string_view kEpisodicEnvironmentContractId =
    "ocgforge.episodic_environment.v1";
inline constexpr std::string_view kEnvironmentIdentitySchemaId =
    "ocgforge.environment_identity.v1";
inline constexpr std::string_view kEpisodeIdentitySchemaId =
    "ocgforge.episode_identity.v1";
inline constexpr std::string_view kSemanticDecisionIdentitySchemaId =
    "ocgforge.semantic_decision_identity.v1";
inline constexpr std::string_view kObservationContractId = "ygo.player_observation.v1";

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
    std::string candidate_digest_schema_id = std::string(kCandidateDomainSchemaId);
    std::string episode_identity_schema_id = std::string(kEpisodeIdentitySchemaId);
    std::string decision_identity_schema_id = std::string(kSemanticDecisionIdentitySchemaId);
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
