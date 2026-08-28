#include "ygo/trajectory/policy_provenance.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {
namespace {

bool versioned_contract_identity(const std::string_view value) noexcept {
    if (value.size() < 8 || value.substr(0, 8) != "ocgforge") {
        return false;
    }
    const auto version = value.rfind(".v");
    if (version == std::string_view::npos || version + 2 >= value.size()) {
        return false;
    }
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(version + 2), value.end(),
                       [](const unsigned char character) {
                           return character >= '0' && character <= '9';
                       });
}

bool known_contract_identity(const std::string_view value) noexcept {
    if (!versioned_contract_identity(value)) {
        return false;
    }
    static constexpr std::array<std::string_view, 34> known = {
        kTrustedTrajectoryContractId,
        kPolicyProvenanceContractId,
        kPublicGameplayIdentityDomain,
        kTrajectoryRecordIdentityDomain,
        kPolicyArtifactIdentityDomain,
        kParticipantAssignmentIdentityDomain,
        kPolicyRngInitializationIdentityDomain,
        kPolicyRngStreamIdentityDomain,
        kPolicyRngDecisionProvenanceDomain,
        kNoPolicyRngContractId,
        kRestrictedReplayEvidenceSchemaId,
        environment::kEpisodicEnvironmentV2ContractId,
        environment::kEnvironmentIdentityV2SchemaId,
        environment::kEpisodeIdentitySchemaId,
        environment::kSemanticDecisionIdentitySchemaId,
        environment::kDecisionContractId,
        environment::kActionIdentitySchemaId,
        environment::kCandidateDomainSchemaId,
        environment::kPublicCandidateDomainSchemaId,
        environment::kPublicActionIdentitySchemaId,
        environment::kPublicSemanticDecisionIdentitySchemaId,
        environment::kPublicEnvironmentObservationSchemaId,
        environment::kPublicSafeStateSchemaId,
        environment::kSeedDerivationId,
        environment::kScriptResolutionContractId,
        environment::kRequiredScriptClosureSchemaId,
        environment::kRequiredScriptClosureDomain,
        "ocgforge.test.producer.v1",
        "ocgforge.test.inference.v1",
        "ocgforge.test.observation.v1",
        "ocgforge.test.action.v1",
        "ocgforge.test.deterministic_sampling.v1",
        "ocgforge.test.rng.v1",
        "ocgforge.test.v1",
    };
    return std::find(known.begin(), known.end(), value) != known.end();
}

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

bool validate_policy_kind(const PolicyArtifact& artifact, std::string* error) {
    const auto reject = [&](const char* message) {
        set_error(error, message);
        return false;
    };
    switch (artifact.policy_kind) {
        case PolicyKind::RandomLegal:
            if (artifact.policy_rng_contract_identity == kNoPolicyRngContractId ||
                artifact.model_checkpoint_identity.has_value() ||
                artifact.search_contract_identity.has_value() ||
                artifact.demonstration_source_identity.has_value()) {
                return reject("RANDOM_LEGAL policy kind has incompatible provenance");
            }
            return true;
        case PolicyKind::DeterministicHeuristic:
            if (artifact.policy_rng_contract_identity != kNoPolicyRngContractId ||
                artifact.model_checkpoint_identity.has_value() ||
                artifact.search_contract_identity.has_value() ||
                artifact.demonstration_source_identity.has_value()) {
                return reject("DETERMINISTIC_HEURISTIC policy kind has incompatible provenance");
            }
            return true;
        case PolicyKind::NeuralCheckpoint:
            if (!artifact.model_checkpoint_identity.has_value()) {
                return reject("NEURAL_CHECKPOINT policy kind lacks a checkpoint identity");
            }
            return true;
        case PolicyKind::SearchAssisted:
            if (!artifact.search_contract_identity.has_value()) {
                return reject("SEARCH_ASSISTED policy kind lacks a search identity");
            }
            return true;
        case PolicyKind::ImportedDemonstration:
            if (!artifact.demonstration_source_identity.has_value()) {
                return reject("IMPORTED_DEMONSTRATION policy kind lacks a source identity");
            }
            return true;
    }
    return reject("policy kind is unknown");
}

}  // namespace

ProvenanceResolver::ProvenanceResolver(std::vector<std::string> immutable_content_ids)
    : immutable_content_ids_(std::move(immutable_content_ids)) {
    std::sort(immutable_content_ids_.begin(), immutable_content_ids_.end());
    if (std::adjacent_find(immutable_content_ids_.begin(), immutable_content_ids_.end()) !=
        immutable_content_ids_.end()) {
        throw std::invalid_argument("provenance resolver has duplicate content identities");
    }
}

bool ProvenanceResolver::can_resolve_contract(const std::string_view identity) const noexcept {
    return known_contract_identity(identity);
}

bool ProvenanceResolver::can_resolve_content(const std::string_view identity) const noexcept {
    // Contract literals remain a separate identity category even if a caller
    // accidentally includes one in the local immutable-content registry.
    if (versioned_contract_identity(identity)) {
        return false;
    }
    return std::binary_search(immutable_content_ids_.begin(), immutable_content_ids_.end(),
                              identity);
}

bool ProvenanceResolver::can_resolve(const std::string_view identity) const noexcept {
    return can_resolve_contract(identity) || can_resolve_content(identity);
}

bool ProvenanceResolver::validate(const PolicyProvenanceEnvelope& value,
                                  const environment::CertifiedEnvironmentConfig& config,
                                  const environment::EpisodeSpec& spec,
                                  std::string* error) const {
    return validate_policy_provenance(value, config, spec, *this, error);
}

bool validate_policy_rng_initialization_material(
    const PolicyRngInitializationIdentity& identity,
    const std::vector<std::uint8_t>& raw_material,
    std::string* error) {
    try {
        if (identity.policy_rng_contract_identity == kNoPolicyRngContractId) {
            if (identity.policy_rng_stream_id != kNoPolicyRngContractId ||
                identity.policy_rng_initialization_identity != kNoPolicyRngContractId ||
                !identity.initialization_material.empty() || !raw_material.empty()) {
                set_error(error, "NONE RNG initialization is not canonical");
                return false;
            }
            return true;
        }
        if (identity.initialization_material != raw_material) {
            set_error(error, "RNG initialization material differs from declared identity");
            return false;
        }
        auto candidate = identity;
        candidate.initialization_material = raw_material;
        const auto encoded = canonical_policy_rng_initialization_identity_bytes(candidate);
        const auto decoded = decode_policy_rng_initialization_identity(encoded);
        if (!decoded || decoded.value->policy_rng_initialization_identity !=
                            identity.policy_rng_initialization_identity) {
            set_error(error, "RNG initialization identity cannot be recomputed");
            return false;
        }
        // Phase 3B has no registered policy-owned RNG state codec. The
        // accepted Phase 3A contract makes the declared contract responsible
        // for proving canonical initialization bytes and, for CURSOR, unique
        // stream state. Treating opaque bytes as proven would accept arbitrary
        // producer material, so non-NONE provenance is not admission-eligible
        // until such a codec is explicitly registered.
        set_error(error, "non-NONE RNG initialization lacks a registered canonical state codec");
        return false;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "RNG initialization validation threw");
        return false;
    }
}

bool validate_policy_provenance(const PolicyProvenanceEnvelope& value,
                                const environment::CertifiedEnvironmentConfig& config,
                                const environment::EpisodeSpec& spec,
                                const ProvenanceResolver& resolver,
                                std::string* error) {
    try {
        std::vector<std::string> artifact_ids;
        for (const auto& artifact : value.policy_artifacts) {
            const auto encoded = canonical_policy_artifact_bytes(artifact);
            const auto decoded = decode_policy_artifact(encoded);
            if (!decoded) {
                set_error(error, "policy artifact does not pass strict codec validation");
                return false;
            }
            if (!validate_policy_kind(artifact, error)) {
                return false;
            }
            for (const auto* identity : {&artifact.producer_implementation_identity,
                                         &artifact.inference_adapter_identity,
                                         &artifact.observation_adapter_identity,
                                         &artifact.action_adapter_identity}) {
                if (!resolver.can_resolve(*identity)) {
                    set_error(error, "policy artifact implementation identity cannot be resolved");
                    return false;
                }
            }
            if (!resolver.can_resolve_contract(artifact.sampling_contract_identity) ||
                !resolver.can_resolve_contract(artifact.policy_rng_contract_identity)) {
                set_error(error, "policy sampling or RNG identity is not an exact known contract");
                return false;
            }
            if (artifact.model_checkpoint_identity.has_value() &&
                !resolver.can_resolve_content(*artifact.model_checkpoint_identity)) {
                set_error(error, "model checkpoint identity is not immutable content");
                return false;
            }
            if (artifact.search_contract_identity.has_value() &&
                !resolver.can_resolve(*artifact.search_contract_identity)) {
                set_error(error, "search identity cannot be resolved as contract or content");
                return false;
            }
            if (artifact.demonstration_source_identity.has_value() &&
                !resolver.can_resolve_content(*artifact.demonstration_source_identity)) {
                set_error(error, "demonstration source identity is not immutable content");
                return false;
            }
            if (artifact.artifact_metadata_identity.has_value() &&
                !resolver.can_resolve_content(*artifact.artifact_metadata_identity)) {
                set_error(error, "artifact metadata identity is not immutable content");
                return false;
            }
            artifact_ids.push_back(artifact.policy_artifact_id);
        }
        if (!std::is_sorted(artifact_ids.begin(), artifact_ids.end()) ||
            std::adjacent_find(artifact_ids.begin(), artifact_ids.end()) != artifact_ids.end()) {
            set_error(error, "policy artifacts are not uniquely ordered");
            return false;
        }

        std::vector<std::string> assignment_ids;
        std::array<std::vector<const ParticipantPolicyAssignment*>, 2> assignments_by_player;
        for (const auto& assignment : value.participant_assignments) {
            const auto encoded = canonical_participant_policy_assignment_bytes(assignment);
            const auto decoded = decode_participant_policy_assignment(encoded);
            if (!decoded) {
                set_error(error, "participant assignment does not pass strict codec validation");
                return false;
            }
            if (assignment.player > 1 ||
                std::find(artifact_ids.begin(), artifact_ids.end(), assignment.policy_artifact_id) ==
                    artifact_ids.end()) {
                set_error(error, "assignment references an undeclared policy artifact");
                return false;
            }
            assignments_by_player[assignment.player].push_back(&assignment);
            assignment_ids.push_back(assignment.participant_policy_assignment_id);

            const auto expected_deck_index = assignment.deck_role == DeckRole::FirstLockedDeck ? 0u : 1u;
            if (config.locked_decks.size() != 2 ||
                assignment.resolved_locked_deck_id != config.locked_decks[expected_deck_index].id ||
                assignment.resolved_locked_deck_sha256 != config.locked_decks[expected_deck_index].sha256) {
                set_error(error, "assignment deck does not match certified V2 input");
                return false;
            }
            const auto expected_seat = assignment.player == spec.starting_player
                                           ? SeatRole::StartingPlayer
                                           : SeatRole::NonStartingPlayer;
            if (assignment.seat_role != expected_seat) {
                set_error(error, "assignment seat role does not match EpisodeSpec");
                return false;
            }
        }
        if (!std::is_sorted(assignment_ids.begin(), assignment_ids.end()) ||
            std::adjacent_find(assignment_ids.begin(), assignment_ids.end()) != assignment_ids.end()) {
            set_error(error, "participant assignments are not uniquely ordered");
            return false;
        }
        for (std::uint8_t player = 0; player < 2; ++player) {
            auto& assignments = assignments_by_player[player];
            std::sort(assignments.begin(), assignments.end(),
                      [](const auto* left, const auto* right) {
                          if (left->assignment_epoch != right->assignment_epoch) {
                              return left->assignment_epoch < right->assignment_epoch;
                          }
                          return left->effective_from_decision_index <
                                 right->effective_from_decision_index;
                      });
            if (assignments.empty() || assignments.front()->assignment_epoch != 0 ||
                assignments.front()->effective_from_decision_index != 0) {
                set_error(error, "provenance lacks an epoch-zero assignment for a player");
                return false;
            }
            for (std::size_t index = 1; index < assignments.size(); ++index) {
                const auto& previous = *assignments[index - 1];
                const auto& current = *assignments[index];
                if (current.assignment_epoch <= previous.assignment_epoch ||
                    current.effective_from_decision_index <=
                        previous.effective_from_decision_index) {
                    set_error(error, "participant assignment epochs are not increasing");
                    return false;
                }
            }
        }
        (void)spec;
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "policy provenance validation threw");
        return false;
    }
}

}  // namespace ygo::trajectory
