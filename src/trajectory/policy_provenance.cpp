#include "ygo/trajectory/policy_provenance.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <utility>

#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {
namespace {

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

int kind_order(const ProvenanceKind kind) noexcept {
    return static_cast<int>(kind);
}

bool valid_provenance_kind(const ProvenanceKind kind) noexcept {
    return kind_order(kind) >= kind_order(ProvenanceKind::ProducerImplementation) &&
           kind_order(kind) <= kind_order(ProvenanceKind::ArtifactMetadataArtifact);
}

bool canonical_identity_token(const std::string_view value) noexcept {
    if (value.empty() || value.front() == '.' || value.back() == '.') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        const auto character = static_cast<unsigned char>(value[index]);
        if (character == '.' && (index == 0 || value[index - 1] == '.')) {
            return false;
        }
        if (!((character >= 'a' && character <= 'z') ||
              (character >= '0' && character <= '9') || character == '.' ||
              character == '_' || character == '-')) {
            return false;
        }
    }
    return true;
}

bool canonical_policy_rng_stream_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    const auto is_alphanumeric = [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9');
    };
    if (!is_alphanumeric(static_cast<unsigned char>(value.front())) ||
        !is_alphanumeric(static_cast<unsigned char>(value.back()))) {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const unsigned char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= '0' && character <= '9') || character == '_' || character == '-';
    });
}

bool canonical_version_suffix(const std::string_view value,
                              const std::size_t marker) noexcept {
    if (marker == std::string_view::npos || marker + 2 >= value.size() ||
        marker == 0 || value[marker] != '.' || value[marker + 1] != 'v') {
        return false;
    }
    if (value[marker + 2] == '0' && marker + 3 < value.size()) {
        return false;
    }
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(marker + 2), value.end(),
                       [](const unsigned char character) {
                           return character >= '0' && character <= '9';
                       });
}

bool versioned_contract_identity(const std::string_view value) noexcept {
    constexpr std::string_view prefix = "ocgforge.";
    if (value.size() <= prefix.size() || value.substr(0, prefix.size()) != prefix ||
        !canonical_identity_token(value)) {
        return false;
    }
    const auto marker = value.rfind(".v");
    return marker >= prefix.size() && canonical_version_suffix(value, marker) &&
           value.find(".v", prefix.size()) == marker;
}

bool content_address_identity(const std::string_view value) noexcept {
    constexpr std::size_t digest_size = 64;
    if (value.size() <= digest_size + 1 || value[value.size() - digest_size - 1] != '.') {
        return false;
    }
    const auto digest = value.substr(value.size() - digest_size);
    if (!is_lower_hex_digest(digest)) {
        return false;
    }
    const auto namespace_and_version = value.substr(0, value.size() - digest_size - 1);
    if (!canonical_identity_token(namespace_and_version)) {
        return false;
    }
    const auto marker = namespace_and_version.rfind(".v");
    return canonical_version_suffix(namespace_and_version, marker) &&
           namespace_and_version.find(".v") == marker;
}

bool valid_identity_for_kind(const ProvenanceKind kind, const std::string_view identity) noexcept {
    switch (kind) {
    case ProvenanceKind::SamplingContract:
    case ProvenanceKind::PolicyRngContract:
        return versioned_contract_identity(identity);
    case ProvenanceKind::ImmutableContentArtifact:
    case ProvenanceKind::ModelCheckpointArtifact:
    case ProvenanceKind::DemonstrationSourceArtifact:
    case ProvenanceKind::ArtifactMetadataArtifact:
        return content_address_identity(identity);
    case ProvenanceKind::ProducerImplementation:
    case ProvenanceKind::InferenceAdapter:
    case ProvenanceKind::ObservationAdapter:
    case ProvenanceKind::ActionAdapter:
    case ProvenanceKind::SearchContract:
        return versioned_contract_identity(identity) || content_address_identity(identity);
    }
    return false;
}

bool registration_less(const ProvenanceRegistration& left,
                       const ProvenanceRegistration& right) noexcept {
    if (left.kind != right.kind) {
        return kind_order(left.kind) < kind_order(right.kind);
    }
    return left.identity < right.identity;
}

ProvenanceRegistration no_policy_rng_registration() {
    ProvenanceRegistration entry;
    entry.kind = ProvenanceKind::PolicyRngContract;
    entry.identity = kNoPolicyRngContractId;
    return entry;
}

const ProvenanceRegistration* find_registration(
    const std::vector<ProvenanceRegistration>& registrations,
    const ProvenanceKind kind,
    const std::string_view identity) noexcept {
    const auto it = std::lower_bound(
        registrations.begin(), registrations.end(),
        ProvenanceRegistration{kind, std::string(identity), std::nullopt, std::nullopt},
        registration_less);
    if (it == registrations.end() || it->kind != kind || it->identity != identity) {
        return nullptr;
    }
    return &*it;
}

void validate_registration(const ProvenanceRegistration& entry) {
    if (!valid_provenance_kind(entry.kind)) {
        throw std::invalid_argument("provenance registry contains an unknown category");
    }
    if (entry.identity.empty()) {
        throw std::invalid_argument("provenance registry contains an empty identity");
    }
    if (!valid_identity_for_kind(entry.kind, entry.identity)) {
        throw std::invalid_argument("provenance registry contains a noncanonical identity");
    }
    if (entry.kind != ProvenanceKind::SamplingContract &&
        entry.sampling_capabilities.has_value()) {
        throw std::invalid_argument("sampling capabilities have the wrong provenance category");
    }
    if (entry.kind != ProvenanceKind::PolicyRngContract && entry.policy_rng_descriptor.has_value()) {
        throw std::invalid_argument("RNG descriptor has the wrong provenance category");
    }
    if (entry.kind == ProvenanceKind::SamplingContract &&
        !entry.sampling_capabilities.has_value()) {
        throw std::invalid_argument("sampling contract lacks typed capabilities");
    }
    if (entry.kind == ProvenanceKind::PolicyRngContract) {
        if (entry.identity == kNoPolicyRngContractId) {
            if (entry.policy_rng_descriptor.has_value()) {
                throw std::invalid_argument("no-RNG contract has an RNG descriptor");
            }
            return;
        }
        if (!entry.policy_rng_descriptor.has_value() ||
            !entry.policy_rng_descriptor->initialization_material_is_canonical ||
            (!entry.policy_rng_descriptor->state_is_canonical &&
             !entry.policy_rng_descriptor->cursor_is_unique)) {
            throw std::invalid_argument("non-NONE RNG contract lacks typed capabilities");
        }
    }
}

bool validate_policy_kind(const PolicyArtifact& artifact,
                         const ProvenanceResolver& resolver,
                         std::string* error) {
    const auto reject = [&](const char* message) {
        set_error(error, message);
        return false;
    };
    const auto* sampling = resolver.sampling_contract_capabilities(
        artifact.sampling_contract_identity);
    if (sampling == nullptr) {
        return reject("sampling contract identity lacks typed authority");
    }
    if (!sampling->complete) {
        return reject("sampling contract does not prove complete candidate sampling");
    }
    const bool artifact_uses_rng = artifact.policy_rng_contract_identity != kNoPolicyRngContractId;
    if (!resolver.can_resolve(ProvenanceKind::PolicyRngContract,
                              artifact.policy_rng_contract_identity)) {
        return reject("policy RNG identity lacks typed authority");
    }
    if (artifact_uses_rng &&
        resolver.policy_rng_contract_descriptor(artifact.policy_rng_contract_identity) == nullptr) {
        return reject("non-NONE policy RNG identity lacks a typed descriptor");
    }
    if (!sampling->deterministic && !artifact_uses_rng) {
        return reject("stochastic sampling requires a non-NONE policy RNG contract");
    }
    switch (artifact.policy_kind) {
        case PolicyKind::RandomLegal:
            if (artifact.policy_rng_contract_identity == kNoPolicyRngContractId ||
                sampling->deterministic ||
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
            if (!sampling->deterministic) {
                return reject("DETERMINISTIC_HEURISTIC policy kind lacks deterministic sampling");
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

bool is_canonical_provenance_identity(const ProvenanceKind kind,
                                      const std::string_view identity) noexcept {
    return valid_identity_for_kind(kind, identity);
}

bool is_canonical_policy_rng_stream_token(const std::string_view value) noexcept {
    return canonical_policy_rng_stream_token(value);
}

ProvenanceResolver::ProvenanceResolver() : registrations_{no_policy_rng_registration()} {}

ProvenanceResolver::ProvenanceResolver(
    std::vector<ProvenanceRegistration> registrations)
    : registrations_(std::move(registrations)) {
    for (const auto& registration : registrations_) {
        validate_registration(registration);
    }
    std::sort(registrations_.begin(), registrations_.end(), registration_less);
    for (std::size_t index = 1; index < registrations_.size(); ++index) {
        if (registrations_[index - 1].kind == registrations_[index].kind &&
            registrations_[index - 1].identity == registrations_[index].identity) {
            throw std::invalid_argument("provenance registry contains a duplicate typed identity");
        }
    }
}

bool ProvenanceResolver::can_resolve(const ProvenanceKind kind,
                                     const std::string_view identity) const noexcept {
    return find_registration(registrations_, kind, identity) != nullptr;
}

const SamplingContractCapabilities* ProvenanceResolver::sampling_contract_capabilities(
    const std::string_view identity) const noexcept {
    const auto* registration =
        find_registration(registrations_, ProvenanceKind::SamplingContract, identity);
    return registration == nullptr || !registration->sampling_capabilities.has_value()
               ? nullptr
               : &*registration->sampling_capabilities;
}

const PolicyRngContractDescriptor* ProvenanceResolver::policy_rng_contract_descriptor(
    const std::string_view identity) const noexcept {
    const auto* registration =
        find_registration(registrations_, ProvenanceKind::PolicyRngContract, identity);
    return registration == nullptr || !registration->policy_rng_descriptor.has_value()
               ? nullptr
               : &*registration->policy_rng_descriptor;
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
    const ProvenanceResolver resolver;
    return validate_policy_rng_initialization_material(identity, raw_material, resolver, error);
}

bool validate_policy_rng_initialization_material(
    const PolicyRngInitializationIdentity& identity,
    const std::vector<std::uint8_t>& raw_material,
    const ProvenanceResolver& resolver,
    std::string* error) {
    try {
        if (identity.policy_rng_contract_identity == kNoPolicyRngContractId) {
            if (!resolver.can_resolve(ProvenanceKind::PolicyRngContract,
                                      kNoPolicyRngContractId)) {
                set_error(error, "NONE RNG contract lacks typed authority");
                return false;
            }
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
        const auto* descriptor =
            resolver.policy_rng_contract_descriptor(identity.policy_rng_contract_identity);
        if (descriptor == nullptr || !descriptor->initialization_material_is_canonical) {
            set_error(error, "RNG contract lacks canonical initialization authority");
            return false;
        }
        if (!descriptor->initialization_material_is_canonical(raw_material)) {
            set_error(error, "RNG initialization material is not canonical for its contract");
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
        return true;
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
        if (spec.seat_assignment != environment::SeatAssignment::Normal &&
            spec.seat_assignment != environment::SeatAssignment::Mirror) {
            set_error(error, "EpisodeSpec has an unknown seat assignment");
            return false;
        }
        std::vector<std::string> artifact_ids;
        for (const auto& artifact : value.policy_artifacts) {
            const auto encoded = canonical_policy_artifact_bytes(artifact);
            const auto decoded = decode_policy_artifact(encoded);
            if (!decoded) {
                set_error(error, "policy artifact does not pass strict codec validation");
                return false;
            }
            if (!validate_policy_kind(artifact, resolver, error)) {
                return false;
            }
            if (!resolver.can_resolve(ProvenanceKind::ProducerImplementation,
                                      artifact.producer_implementation_identity) ||
                !resolver.can_resolve(ProvenanceKind::InferenceAdapter,
                                      artifact.inference_adapter_identity) ||
                !resolver.can_resolve(ProvenanceKind::ObservationAdapter,
                                      artifact.observation_adapter_identity) ||
                !resolver.can_resolve(ProvenanceKind::ActionAdapter,
                                      artifact.action_adapter_identity)) {
                set_error(error, "policy artifact implementation identity lacks typed authority");
                return false;
            }
            if (artifact.model_checkpoint_identity.has_value() &&
                !resolver.can_resolve(ProvenanceKind::ModelCheckpointArtifact,
                                      *artifact.model_checkpoint_identity)) {
                set_error(error, "model checkpoint identity is not immutable content");
                return false;
            }
            if (artifact.search_contract_identity.has_value() &&
                !resolver.can_resolve(ProvenanceKind::SearchContract,
                                      *artifact.search_contract_identity)) {
                set_error(error, "search identity lacks typed authority");
                return false;
            }
            if (artifact.demonstration_source_identity.has_value() &&
                !resolver.can_resolve(ProvenanceKind::DemonstrationSourceArtifact,
                                      *artifact.demonstration_source_identity)) {
                set_error(error, "demonstration source identity is not immutable content");
                return false;
            }
            if (artifact.artifact_metadata_identity.has_value() &&
                !resolver.can_resolve(ProvenanceKind::ArtifactMetadataArtifact,
                                      *artifact.artifact_metadata_identity)) {
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

            const auto expected_deck_index = spec.seat_assignment == environment::SeatAssignment::Mirror
                                                 ? 1u - assignment.player
                                                 : static_cast<unsigned int>(assignment.player);
            const auto expected_deck_role = expected_deck_index == 0
                                                ? DeckRole::FirstLockedDeck
                                                : DeckRole::SecondLockedDeck;
            if (assignment.deck_role != expected_deck_role || config.locked_decks.size() != 2 ||
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
