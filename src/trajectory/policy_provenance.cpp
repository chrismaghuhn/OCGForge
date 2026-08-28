#include "ygo/trajectory/policy_provenance.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <utility>

#include "ygo/trajectory/codec.hpp"

namespace ygo::trajectory {
namespace {

bool exact_contract_identity(const std::string_view value) noexcept {
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

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
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

bool ProvenanceResolver::can_resolve(const std::string_view identity) const noexcept {
    if (identity == kNoPolicyRngContractId || exact_contract_identity(identity)) {
        return true;
    }
    return std::binary_search(immutable_content_ids_.begin(), immutable_content_ids_.end(),
                              identity);
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
        std::vector<std::string> artifact_ids;
        for (const auto& artifact : value.policy_artifacts) {
            const auto encoded = canonical_policy_artifact_bytes(artifact);
            const auto decoded = decode_policy_artifact(encoded);
            if (!decoded) {
                set_error(error, "policy artifact does not pass strict codec validation");
                return false;
            }
            for (const auto* identity : {&artifact.producer_implementation_identity,
                                         &artifact.inference_adapter_identity,
                                         &artifact.observation_adapter_identity,
                                         &artifact.action_adapter_identity,
                                         &artifact.sampling_contract_identity,
                                         &artifact.policy_rng_contract_identity}) {
                if (!resolver.can_resolve(*identity)) {
                    set_error(error, "policy artifact identity cannot be resolved");
                    return false;
                }
            }
            for (const auto* optional_identity : {&artifact.model_checkpoint_identity,
                                                  &artifact.search_contract_identity,
                                                  &artifact.demonstration_source_identity,
                                                  &artifact.artifact_metadata_identity}) {
                if (optional_identity->has_value() && !resolver.can_resolve(**optional_identity)) {
                    set_error(error, "optional policy artifact identity cannot be resolved");
                    return false;
                }
            }
            artifact_ids.push_back(artifact.policy_artifact_id);
        }
        if (!std::is_sorted(artifact_ids.begin(), artifact_ids.end()) ||
            std::adjacent_find(artifact_ids.begin(), artifact_ids.end()) != artifact_ids.end()) {
            set_error(error, "policy artifacts are not uniquely ordered");
            return false;
        }

        std::vector<std::string> assignment_ids;
        std::array<bool, 2> epoch_zero{};
        std::array<std::uint32_t, 2> previous_epoch{};
        std::array<std::uint64_t, 2> previous_index{};
        std::array<bool, 2> have_previous{};
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
            if (assignment.assignment_epoch == 0 &&
                assignment.effective_from_decision_index == 0) {
                if (epoch_zero[assignment.player]) {
                    set_error(error, "duplicate epoch-zero participant assignment");
                    return false;
                }
                epoch_zero[assignment.player] = true;
            }
            if (have_previous[assignment.player] &&
                (assignment.assignment_epoch <= previous_epoch[assignment.player] ||
                 assignment.effective_from_decision_index <= previous_index[assignment.player])) {
                set_error(error, "participant assignment epochs are not increasing");
                return false;
            }
            previous_epoch[assignment.player] = assignment.assignment_epoch;
            previous_index[assignment.player] = assignment.effective_from_decision_index;
            have_previous[assignment.player] = true;
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
        if (!epoch_zero[0] || !epoch_zero[1] ||
            !std::is_sorted(assignment_ids.begin(), assignment_ids.end()) ||
            std::adjacent_find(assignment_ids.begin(), assignment_ids.end()) != assignment_ids.end()) {
            set_error(error, "provenance lacks exactly ordered epoch-zero assignments");
            return false;
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
