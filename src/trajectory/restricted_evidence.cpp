#include "ygo/trajectory/restricted_evidence.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::trajectory {
namespace {

template <typename T>
DecodeResult<T> failure(std::string message) noexcept {
    DecodeResult<T> result;
    result.error = DecodeError{std::move(message)};
    return result;
}

template <typename T>
DecodeResult<T> success(T value) noexcept {
    DecodeResult<T> result;
    result.value = std::move(value);
    return result;
}

void require_length(const std::size_t size, const char* field) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error(std::string("restricted evidence ") + field +
                                " exceeds u32 length");
    }
}

void require_digest(const std::string& value, const char* field) {
    if (!is_lower_hex_digest(value)) {
        throw std::invalid_argument(std::string("restricted evidence ") + field +
                                    " is not a SHA-256 digest");
    }
}

void require_identity(const std::string& value, const std::string_view prefix,
                      const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("restricted evidence ") + field +
                                    " has invalid identity");
    }
}

void validate_bundle(const RestrictedCollectionEvidenceBundle& value) {
    require_digest(value.candidate_shard_artifact_sha256, "candidate shard artifact");
    require_length(value.interrupted_episodes.size(), "interrupted entry count");
    require_length(value.rng_initializations.size(), "RNG entry count");

    std::string previous_episode_digest;
    for (const auto& entry : value.interrupted_episodes) {
        require_digest(entry.episode_envelope_sha256, "interrupted envelope");
        if (!previous_episode_digest.empty() &&
            entry.episode_envelope_sha256 <= previous_episode_digest) {
            throw std::invalid_argument("interrupted evidence entries are not strictly sorted");
        }
        previous_episode_digest = entry.episode_envelope_sha256;
        (void)canonical_restricted_replay_evidence_bytes(entry.evidence);
    }

    std::string previous_initialization_id;
    for (const auto& entry : value.rng_initializations) {
        require_identity(entry.policy_rng_initialization_identity,
                         "policy_rng_initialization.v1.", "RNG initialization evidence");
        if (!previous_initialization_id.empty() &&
            entry.policy_rng_initialization_identity <= previous_initialization_id) {
            throw std::invalid_argument("RNG initialization evidence is not strictly sorted");
        }
        previous_initialization_id = entry.policy_rng_initialization_identity;
    }
}

bool read_restricted_evidence_direct(ByteReader& reader,
                                     RestrictedReplayEvidence& value) noexcept {
    std::string schema;
    std::uint8_t closure_kind = 0;
    std::uint8_t reason = 0;
    if (!reader.string(schema) || schema != kRestrictedReplayEvidenceSchemaId ||
        !reader.string(value.v2_contract_id) ||
        value.v2_contract_id != environment::kEpisodicEnvironmentV2ContractId ||
        !reader.string(value.episode_semantic_id) || !reader.u8(closure_kind) ||
        closure_kind != 1 || !reader.u8(reason) || reason > 2 ||
        !reader.u64be(value.engine_process_budget) ||
        !reader.u64be(value.semantic_action_budget) ||
        !reader.u64be(value.observed_engine_process_count) ||
        !reader.u64be(value.observed_semantic_action_count) ||
        !reader.u64be(value.final_engine_step_index)) {
        return false;
    }
    value.interruption_reason = static_cast<environment::InterruptionReason>(reason);
    try {
        (void)canonical_restricted_replay_evidence_bytes(value);
        return true;
    } catch (...) {
        return false;
    }
}

const InterruptedEvidenceEntry* find_interrupted_entry(
    const RestrictedCollectionEvidenceBundle& value, const std::string_view digest) noexcept {
    const auto it = std::lower_bound(
        value.interrupted_episodes.begin(), value.interrupted_episodes.end(), digest,
        [](const InterruptedEvidenceEntry& entry, const std::string_view key) {
            return entry.episode_envelope_sha256 < key;
        });
    if (it == value.interrupted_episodes.end() || it->episode_envelope_sha256 != digest) {
        return nullptr;
    }
    return &*it;
}

const RngInitializationEvidenceEntry* find_rng_entry(
    const RestrictedCollectionEvidenceBundle& value, const std::string_view identity) noexcept {
    const auto it = std::lower_bound(
        value.rng_initializations.begin(), value.rng_initializations.end(), identity,
        [](const RngInitializationEvidenceEntry& entry, const std::string_view key) {
            return entry.policy_rng_initialization_identity < key;
        });
    if (it == value.rng_initializations.end() || it->policy_rng_initialization_identity != identity) {
        return nullptr;
    }
    return &*it;
}

void set_error(std::string* error, const std::string& message) {
    if (error != nullptr) {
        *error = message;
    }
}

struct RequiredInitialization final {
    std::string identity;
    std::string contract;
    std::string stream;
};

}  // namespace

std::vector<std::uint8_t> canonical_restricted_collection_evidence_bundle_bytes(
    const RestrictedCollectionEvidenceBundle& value) {
    validate_bundle(value);
    ByteWriter writer;
    writer.string(kRestrictedCollectionEvidenceBundleContractId);
    writer.string(kRestrictedCollectionEvidenceBundleContractId);
    writer.string(value.candidate_shard_artifact_sha256);
    writer.u32be(static_cast<std::uint32_t>(value.interrupted_episodes.size()));
    for (const auto& entry : value.interrupted_episodes) {
        writer.string(entry.episode_envelope_sha256);
        writer.raw(canonical_restricted_replay_evidence_bytes(entry.evidence));
    }
    writer.u32be(static_cast<std::uint32_t>(value.rng_initializations.size()));
    for (const auto& entry : value.rng_initializations) {
        writer.string(entry.policy_rng_initialization_identity);
        writer.bytes(entry.initialization_material);
    }
    return std::move(writer).take();
}

DecodeResult<RestrictedCollectionEvidenceBundle>
decode_restricted_collection_evidence_bundle(const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        RestrictedCollectionEvidenceBundle value;
        std::string domain;
        std::string schema;
        std::uint32_t interrupted_count = 0;
        std::uint32_t rng_count = 0;
        if (!reader.string(domain) || domain != kRestrictedCollectionEvidenceBundleContractId ||
            !reader.string(schema) || schema != kRestrictedCollectionEvidenceBundleContractId ||
            !reader.string(value.candidate_shard_artifact_sha256) ||
            !reader.u32be(interrupted_count)) {
            return failure<RestrictedCollectionEvidenceBundle>(
                "malformed restricted evidence bundle header");
        }
        if (interrupted_count > reader.remaining() / 72) {
            return failure<RestrictedCollectionEvidenceBundle>(
                "restricted interrupted count exceeds input");
        }
        value.interrupted_episodes.reserve(interrupted_count);
        std::string previous_episode_digest;
        for (std::uint32_t index = 0; index < interrupted_count; ++index) {
            InterruptedEvidenceEntry entry;
            if (!reader.string(entry.episode_envelope_sha256) ||
                (!previous_episode_digest.empty() &&
                 entry.episode_envelope_sha256 <= previous_episode_digest) ||
                !read_restricted_evidence_direct(reader, entry.evidence)) {
                return failure<RestrictedCollectionEvidenceBundle>(
                    "malformed or unsorted interrupted evidence");
            }
            previous_episode_digest = entry.episode_envelope_sha256;
            value.interrupted_episodes.push_back(std::move(entry));
        }
        if (!reader.u32be(rng_count)) {
            return failure<RestrictedCollectionEvidenceBundle>("missing restricted RNG count");
        }
        if (rng_count > reader.remaining() / 12) {
            return failure<RestrictedCollectionEvidenceBundle>(
                "restricted RNG count exceeds input");
        }
        value.rng_initializations.reserve(rng_count);
        std::string previous_initialization_id;
        for (std::uint32_t index = 0; index < rng_count; ++index) {
            RngInitializationEvidenceEntry entry;
            if (!reader.string(entry.policy_rng_initialization_identity) ||
                (!previous_initialization_id.empty() &&
                 entry.policy_rng_initialization_identity <= previous_initialization_id) ||
                !reader.bytes(entry.initialization_material)) {
                return failure<RestrictedCollectionEvidenceBundle>(
                    "malformed or unsorted restricted RNG evidence");
            }
            previous_initialization_id = entry.policy_rng_initialization_identity;
            value.rng_initializations.push_back(std::move(entry));
        }
        if (!reader.at_end()) {
            return failure<RestrictedCollectionEvidenceBundle>(
                "restricted evidence bundle has trailing bytes");
        }
        validate_bundle(value);
        if (canonical_restricted_collection_evidence_bundle_bytes(value) != bytes) {
            return failure<RestrictedCollectionEvidenceBundle>(
                "noncanonical restricted evidence bundle");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<RestrictedCollectionEvidenceBundle>(error.what());
    } catch (...) {
        return failure<RestrictedCollectionEvidenceBundle>(
            "restricted evidence bundle decode threw");
    }
}

std::string restricted_collection_evidence_artifact_sha256(
    const RestrictedCollectionEvidenceBundle& value) {
    return trace::sha256_bytes(canonical_restricted_collection_evidence_bundle_bytes(value));
}

bool validate_restricted_collection_evidence_bundle(
    const RestrictedCollectionEvidenceBundle& value,
    const CandidateTrajectoryShard& shard,
    const std::string_view expected_shard_artifact_sha256,
    std::string* error) {
    const ProvenanceResolver resolver;
    return validate_restricted_collection_evidence_bundle(
        value, shard, expected_shard_artifact_sha256, resolver, error);
}

bool validate_restricted_collection_evidence_bundle(
    const RestrictedCollectionEvidenceBundle& value,
    const CandidateTrajectoryShard& shard,
    const std::string_view expected_shard_artifact_sha256,
    const ProvenanceResolver& resolver,
    std::string* error) {
    try {
        validate_bundle(value);
        if (expected_shard_artifact_sha256 != ygo::trajectory::candidate_shard_artifact_sha256(shard)) {
            set_error(error, "candidate shard artifact digest is not exact");
            return false;
        }
        if (value.candidate_shard_artifact_sha256 != expected_shard_artifact_sha256) {
            set_error(error, "restricted evidence is bound to another shard");
            return false;
        }

        for (const auto& shard_entry : shard.entries) {
            const auto decoded = decode_episode_envelope(shard_entry.envelope_bytes);
            if (!decoded) {
                set_error(error, "shard entry cannot be decoded for evidence validation");
                return false;
            }
            const auto& envelope = *decoded.value;
            const auto* interrupted = std::get_if<InterruptedClosure>(&envelope.closure);
            const auto* evidence_entry = find_interrupted_entry(
                value, shard_entry.episode_envelope_sha256);
            if (interrupted == nullptr) {
                if (evidence_entry != nullptr) {
                    set_error(error, "restricted evidence references a non-interrupted episode");
                    return false;
                }
                continue;
            }
            if (evidence_entry == nullptr ||
                evidence_entry->evidence.episode_semantic_id !=
                    envelope.manifest.episode_semantic_id) {
                set_error(error, "interrupted episode is missing exact restricted evidence");
                return false;
            }
            if (interrupted->pending_unacted_frame.has_value()) {
                if (evidence_entry->evidence.interruption_reason !=
                    environment::InterruptionReason::AdministrativeCancel) {
                    set_error(error, "pending interruption has a non-administrative reason");
                    return false;
                }
            } else if (evidence_entry->evidence.interruption_reason ==
                           environment::InterruptionReason::AdministrativeCancel) {
                set_error(error, "administrative interruption has no pending frame");
                return false;
            }
        }
        const auto interrupted_count = static_cast<std::size_t>(std::count_if(
            shard.entries.begin(), shard.entries.end(), [](const ShardEntry& entry) {
                const auto decoded = decode_episode_envelope(entry.envelope_bytes);
                return decoded &&
                       std::holds_alternative<InterruptedClosure>(decoded.value->closure);
            }));
        if (value.interrupted_episodes.size() != interrupted_count) {
            set_error(error, "restricted interrupted evidence has an unreferenced entry");
            return false;
        }

        std::vector<RequiredInitialization> required;
        for (const auto& shard_entry : shard.entries) {
            const auto decoded = decode_episode_envelope(shard_entry.envelope_bytes);
            if (!decoded) {
                set_error(error, "shard entry cannot be decoded for RNG validation");
                return false;
            }
            for (const auto& record : decoded.value->records) {
                const auto& attribution = record.policy_rng_decision_provenance;
                if (attribution.mode == PolicyRngMode::None) {
                    continue;
                }
                const auto it = std::find_if(
                    required.begin(), required.end(), [&](const RequiredInitialization& item) {
                        return item.identity == attribution.policy_rng_initialization_identity;
                    });
                if (it == required.end()) {
                    required.push_back(RequiredInitialization{
                        attribution.policy_rng_initialization_identity,
                        attribution.policy_rng_contract_identity,
                        attribution.policy_rng_stream_id});
                } else if (it->contract != attribution.policy_rng_contract_identity ||
                           it->stream != attribution.policy_rng_stream_id) {
                    set_error(error, "one RNG initialization identity has conflicting streams");
                    return false;
                }
            }
        }
        std::sort(required.begin(), required.end(),
                  [](const auto& left, const auto& right) { return left.identity < right.identity; });
        if (required.size() != value.rng_initializations.size()) {
            set_error(error, "restricted RNG evidence has missing or extra entries");
            return false;
        }
        for (const auto& item : required) {
            const auto* evidence = find_rng_entry(value, item.identity);
            if (evidence == nullptr) {
                set_error(error, "required RNG initialization evidence is missing");
                return false;
            }
            PolicyRngInitializationIdentity identity;
            identity.policy_rng_contract_identity = item.contract;
            identity.policy_rng_stream_id = item.stream;
            identity.initialization_material = evidence->initialization_material;
            identity.policy_rng_initialization_identity = evidence->policy_rng_initialization_identity;
            std::string material_error;
            if (!validate_policy_rng_initialization_material(identity,
                                                             evidence->initialization_material,
                                                             resolver,
                                                             &material_error)) {
                set_error(error, material_error);
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "restricted evidence validation threw");
        return false;
    }
}

}  // namespace ygo::trajectory
