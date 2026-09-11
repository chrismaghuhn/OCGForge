#include "ygo/trajectory/restricted_evidence_v3.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"

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
        throw std::length_error(std::string("V3 restricted evidence ") + field +
                                " exceeds u32 length");
    }
}


void validate_evidence_v3(const RestrictedReplayEvidenceV3& value) {
    if (value.restricted_replay_evidence_contract_id !=
            kRestrictedReplayEvidenceV3SchemaId ||
        value.trusted_trajectory_contract_id != kTrustedTrajectoryV3ContractId ||
        value.episodic_environment_contract_id !=
            environment::kEpisodicEnvironmentV4ContractId ||
        !is_lower_hex_digest(value.episode_semantic_id) ||
        value.closure_kind != 1 ||
        static_cast<std::uint8_t>(value.interruption_reason) > 2 ||
        value.engine_process_budget == 0 || value.semantic_action_budget == 0 ||
        value.observed_engine_process_count > value.engine_process_budget ||
        value.observed_semantic_action_count > value.semantic_action_budget) {
        throw std::invalid_argument("V3 restricted evidence is inconsistent");
    }
}

void validate_bundle(const RestrictedCollectionEvidenceBundleV3& value) {
    if (!is_lower_hex_digest(value.candidate_shard_artifact_sha256)) {
        throw std::invalid_argument("V3 evidence bundle shard digest is invalid");
    }
    require_length(value.interrupted_episodes.size(), "interrupted count");
    std::string previous;
    for (const auto& entry : value.interrupted_episodes) {
        if (!is_lower_hex_digest(entry.episode_envelope_sha256) ||
            (!previous.empty() && entry.episode_envelope_sha256 <= previous) ||
            entry.evidence.episode_semantic_id.empty()) {
            throw std::invalid_argument("V3 evidence bundle entry is invalid");
        }
        (void)canonical_restricted_replay_evidence_bytes_v3(entry.evidence);
        previous = entry.episode_envelope_sha256;
    }
}

}  // namespace


std::vector<std::uint8_t> canonical_restricted_replay_evidence_bytes_v3(
    const RestrictedReplayEvidenceV3& value) {
    validate_evidence_v3(value);
    ByteWriter writer;
    writer.string(kRestrictedReplayEvidenceV3SchemaId);
    writer.string(value.trusted_trajectory_contract_id);
    writer.string(value.episodic_environment_contract_id);
    writer.string(value.episode_semantic_id);
    writer.u8(value.closure_kind);
    writer.u8(static_cast<std::uint8_t>(value.interruption_reason));
    writer.u64be(value.engine_process_budget);
    writer.u64be(value.semantic_action_budget);
    writer.u64be(value.observed_engine_process_count);
    writer.u64be(value.observed_semantic_action_count);
    writer.u64be(value.final_engine_step_index);
    return std::move(writer).take();
}

DecodeResult<RestrictedReplayEvidenceV3> decode_restricted_replay_evidence_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        RestrictedReplayEvidenceV3 value;
        std::uint8_t reason = 0;
        if (!reader.string(value.restricted_replay_evidence_contract_id) ||
            !reader.string(value.trusted_trajectory_contract_id) ||
            !reader.string(value.episodic_environment_contract_id) ||
            !reader.string(value.episode_semantic_id) ||
            !reader.u8(value.closure_kind) || !reader.u8(reason) ||
            !reader.u64be(value.engine_process_budget) ||
            !reader.u64be(value.semantic_action_budget) ||
            !reader.u64be(value.observed_engine_process_count) ||
            !reader.u64be(value.observed_semantic_action_count) ||
            !reader.u64be(value.final_engine_step_index) || !reader.at_end()) {
            return failure<RestrictedReplayEvidenceV3>("malformed V3 restricted evidence");
        }
        value.interruption_reason =
            static_cast<environment::InterruptionReason>(reason);
        validate_evidence_v3(value);
        if (canonical_restricted_replay_evidence_bytes_v3(value) != bytes) {
            return failure<RestrictedReplayEvidenceV3>("noncanonical V3 restricted evidence");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<RestrictedReplayEvidenceV3>(error.what());
    } catch (...) {
        return failure<RestrictedReplayEvidenceV3>("V3 restricted evidence decode threw");
    }
}

std::string restricted_replay_evidence_artifact_sha256_v3(
    const RestrictedReplayEvidenceV3& value) {
    return trace::sha256_bytes(canonical_restricted_replay_evidence_bytes_v3(value));
}

std::vector<std::uint8_t> canonical_restricted_collection_evidence_bundle_bytes_v3(
    const RestrictedCollectionEvidenceBundleV3& value) {
    validate_bundle(value);
    ByteWriter writer;
    writer.string(kRestrictedCollectionEvidenceBundleV3ContractId);
    writer.string(kRestrictedCollectionEvidenceBundleV3ContractId);
    writer.string(value.candidate_shard_artifact_sha256);
    writer.u32be(static_cast<std::uint32_t>(value.interrupted_episodes.size()));
    for (const auto& entry : value.interrupted_episodes) {
        writer.string(entry.episode_envelope_sha256);
        writer.raw(canonical_restricted_replay_evidence_bytes_v3(entry.evidence));
    }
    return std::move(writer).take();
}

DecodeResult<RestrictedCollectionEvidenceBundleV3>
decode_restricted_collection_evidence_bundle_v3(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        RestrictedCollectionEvidenceBundleV3 value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kRestrictedCollectionEvidenceBundleV3ContractId ||
            !reader.string(schema) || schema != kRestrictedCollectionEvidenceBundleV3ContractId ||
            !reader.string(value.candidate_shard_artifact_sha256) || !reader.u32be(count) ||
            count > reader.remaining() / 72) {
            return failure<RestrictedCollectionEvidenceBundleV3>("malformed V3 evidence header");
        }
        value.interrupted_episodes.reserve(count);
        std::string previous;
        for (std::uint32_t index = 0; index < count; ++index) {
            InterruptedEvidenceEntryV3 entry;
            if (!reader.string(entry.episode_envelope_sha256) ||
                !is_lower_hex_digest(entry.episode_envelope_sha256)) {
                return failure<RestrictedCollectionEvidenceBundleV3>("malformed V3 evidence entry");
            }
            // The nested evidence codec is self-delimiting only through the
            // fixed field layout, so read its canonical values directly.
            std::string schema_id;
            std::uint8_t reason = 0;
            if (!reader.string(schema_id) || schema_id != kRestrictedReplayEvidenceV3SchemaId ||
                !reader.string(entry.evidence.trusted_trajectory_contract_id) ||
                !reader.string(entry.evidence.episodic_environment_contract_id) ||
                !reader.string(entry.evidence.episode_semantic_id) ||
                !reader.u8(entry.evidence.closure_kind) || !reader.u8(reason) ||
                !reader.u64be(entry.evidence.engine_process_budget) ||
                !reader.u64be(entry.evidence.semantic_action_budget) ||
                !reader.u64be(entry.evidence.observed_engine_process_count) ||
                !reader.u64be(entry.evidence.observed_semantic_action_count) ||
                !reader.u64be(entry.evidence.final_engine_step_index)) {
                return failure<RestrictedCollectionEvidenceBundleV3>("truncated V3 evidence entry");
            }
            entry.evidence.interruption_reason =
                static_cast<environment::InterruptionReason>(reason);
            if ((!previous.empty() && entry.episode_envelope_sha256 <= previous)) {
                return failure<RestrictedCollectionEvidenceBundleV3>("unsorted V3 evidence entry");
            }
            (void)canonical_restricted_replay_evidence_bytes_v3(entry.evidence);
            previous = entry.episode_envelope_sha256;
            value.interrupted_episodes.push_back(std::move(entry));
        }
        if (!reader.at_end() || canonical_restricted_collection_evidence_bundle_bytes_v3(value) != bytes) {
            return failure<RestrictedCollectionEvidenceBundleV3>("noncanonical V3 evidence bundle");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<RestrictedCollectionEvidenceBundleV3>(error.what());
    } catch (...) {
        return failure<RestrictedCollectionEvidenceBundleV3>("V3 evidence bundle decode threw");
    }
}

std::string restricted_collection_evidence_artifact_sha256_v3(
    const RestrictedCollectionEvidenceBundleV3& value) {
    return trace::sha256_bytes(canonical_restricted_collection_evidence_bundle_bytes_v3(value));
}

}  // namespace ygo::trajectory
