#include "ygo/trajectory/dataset_manifest.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory::dataset {
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

void require_length(const std::size_t size) {
    if (size > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("dataset member count exceeds u32");
    }
}

void require_identity(const std::string& value, const std::string_view prefix,
                      const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("dataset ") + field + " has invalid identity");
    }
}

void validate_record_ids(const std::vector<std::string>& ids) {
    require_length(ids.size());
    std::string previous;
    for (const auto& id : ids) {
        require_identity(id, "trajectory_record.v1.", "record ID");
        if (!previous.empty() && id <= previous) {
            throw std::invalid_argument("dataset record IDs are not strictly sorted");
        }
        previous = id;
    }
}

void validate_member(const DatasetManifestMember& member) {
    require_identity(member.trajectory_record_id, "trajectory_record.v1.", "member record ID");
    require_identity(member.public_gameplay_trajectory_id,
                     "public_gameplay_trajectory.v1.", "member gameplay ID");
    require_identity(member.admission_receipt_id, "admission_receipt.v1.", "member receipt ID");
    if (!is_lower_hex_digest(member.candidate_shard_artifact_sha256) ||
        !is_lower_hex_digest(member.episode_envelope_sha256)) {
        throw std::invalid_argument("dataset member artifact identity is invalid");
    }
}

void validate_manifest(const DatasetManifest& value) {
    if (value.dataset_manifest_schema_id != kDatasetManifestContractId ||
        value.dataset_identity_schema_id != kDatasetIdentityContractId ||
        value.trusted_trajectory_contract_id != kTrustedTrajectoryContractId) {
        throw std::invalid_argument("dataset manifest contract is unknown");
    }
    if (!is_lower_hex_digest(value.dataset_semantic_id)) {
        throw std::invalid_argument("dataset semantic ID is not a digest");
    }
    require_length(value.members.size());
    std::string previous;
    std::vector<std::string> ids;
    ids.reserve(value.members.size());
    for (const auto& member : value.members) {
        validate_member(member);
        if (!previous.empty() && member.trajectory_record_id <= previous) {
            throw std::invalid_argument("dataset members are not strictly record sorted");
        }
        previous = member.trajectory_record_id;
        ids.push_back(member.trajectory_record_id);
    }
    if (dataset_semantic_id(ids) != value.dataset_semantic_id) {
        throw std::invalid_argument("dataset semantic ID does not match membership");
    }
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

const AdmissionReceipt* find_receipt(
    const std::vector<VerifiedAdmissionReceipt>& receipts,
    const std::string_view receipt_id) {
    for (const auto& verified : receipts) {
        if (admission_receipt_id(verified.receipt()) == receipt_id) {
            return &verified.receipt();
        }
    }
    return nullptr;
}

const AdmissionEntryCommitment* find_commitment(const AdmissionReceipt& receipt,
                                                const std::string_view record_id) noexcept {
    const auto it = std::lower_bound(
        receipt.entries.begin(), receipt.entries.end(), record_id,
        [](const AdmissionEntryCommitment& entry, const std::string_view key) {
            return entry.trajectory_record_id < key;
        });
    if (it == receipt.entries.end() || it->trajectory_record_id != record_id) {
        return nullptr;
    }
    return &*it;
}

}  // namespace

std::vector<std::uint8_t> canonical_dataset_identity_bytes(
    const std::vector<std::string>& trajectory_record_ids) {
    validate_record_ids(trajectory_record_ids);
    ByteWriter writer;
    writer.string(kDatasetIdentityContractId);
    writer.string(kDatasetIdentityContractId);
    writer.string(kTrustedTrajectoryContractId);
    writer.u32be(static_cast<std::uint32_t>(trajectory_record_ids.size()));
    for (const auto& id : trajectory_record_ids) {
        writer.string(id);
    }
    return std::move(writer).take();
}

std::string dataset_semantic_id(const std::vector<std::string>& trajectory_record_ids) {
    return trace::sha256_bytes(canonical_dataset_identity_bytes(trajectory_record_ids));
}

std::vector<std::uint8_t> canonical_dataset_manifest_bytes(const DatasetManifest& value) {
    validate_manifest(value);
    ByteWriter writer;
    writer.string(kDatasetManifestContractId);
    writer.string(kDatasetManifestContractId);
    writer.string(value.dataset_identity_schema_id);
    writer.string(value.trusted_trajectory_contract_id);
    writer.string(value.dataset_semantic_id);
    writer.u32be(static_cast<std::uint32_t>(value.members.size()));
    for (const auto& member : value.members) {
        writer.string(member.trajectory_record_id);
        writer.string(member.public_gameplay_trajectory_id);
        writer.string(member.admission_receipt_id);
        writer.string(member.candidate_shard_artifact_sha256);
        writer.string(member.episode_envelope_sha256);
    }
    return std::move(writer).take();
}

DecodeResult<DatasetManifest> decode_dataset_manifest(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        DatasetManifest value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kDatasetManifestContractId ||
            !reader.string(schema) || schema != kDatasetManifestContractId ||
            !reader.string(value.dataset_identity_schema_id) ||
            !reader.string(value.trusted_trajectory_contract_id) ||
            !reader.string(value.dataset_semantic_id) || !reader.u32be(count)) {
            return failure<DatasetManifest>("malformed dataset manifest header");
        }
        if (count > reader.remaining() / 5) {
            return failure<DatasetManifest>("dataset manifest member count exceeds input");
        }
        value.members.reserve(count);
        std::string previous;
        for (std::uint32_t index = 0; index < count; ++index) {
            DatasetManifestMember member;
            if (!reader.string(member.trajectory_record_id) ||
                !reader.string(member.public_gameplay_trajectory_id) ||
                !reader.string(member.admission_receipt_id) ||
                !reader.string(member.candidate_shard_artifact_sha256) ||
                !reader.string(member.episode_envelope_sha256)) {
                return failure<DatasetManifest>("truncated dataset manifest member");
            }
            if (!previous.empty() && member.trajectory_record_id <= previous) {
                return failure<DatasetManifest>("unsorted or duplicate dataset member");
            }
            previous = member.trajectory_record_id;
            value.members.push_back(std::move(member));
        }
        if (!reader.at_end()) {
            return failure<DatasetManifest>("dataset manifest has trailing bytes");
        }
        validate_manifest(value);
        if (canonical_dataset_manifest_bytes(value) != bytes) {
            return failure<DatasetManifest>("noncanonical dataset manifest");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<DatasetManifest>(error.what());
    } catch (...) {
        return failure<DatasetManifest>("dataset manifest decode threw");
    }
}

bool validate_dataset_manifest(const DatasetManifest& value,
                               const std::vector<VerifiedAdmissionReceipt>& verified_receipts,
                               std::string* error) {
    try {
        validate_manifest(value);
        std::vector<std::string> verified_receipt_ids;
        verified_receipt_ids.reserve(verified_receipts.size());
        for (const auto& verified : verified_receipts) {
            (void)canonical_admission_receipt_bytes(verified.receipt());
            verified_receipt_ids.push_back(admission_receipt_id(verified.receipt()));
        }
        std::sort(verified_receipt_ids.begin(), verified_receipt_ids.end());
        if (std::adjacent_find(verified_receipt_ids.begin(), verified_receipt_ids.end()) !=
            verified_receipt_ids.end()) {
            set_error(error, "verified receipt inputs contain a duplicate receipt identity");
            return false;
        }
        for (const auto& member : value.members) {
            const auto* receipt = find_receipt(verified_receipts, member.admission_receipt_id);
            if (receipt == nullptr) {
                set_error(error, "dataset member references an unknown admission receipt");
                return false;
            }
            const auto* commitment = find_commitment(*receipt, member.trajectory_record_id);
            if (commitment == nullptr ||
                commitment->public_gameplay_trajectory_id != member.public_gameplay_trajectory_id ||
                commitment->episode_envelope_sha256 != member.episode_envelope_sha256 ||
                !is_lower_hex_digest(commitment->environment_semantic_id) ||
                !is_lower_hex_digest(commitment->episode_semantic_id) ||
                receipt->candidate_shard_artifact_sha256 != member.candidate_shard_artifact_sha256) {
                set_error(error, "dataset member conflicts with its admission commitment");
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "dataset manifest validation threw");
        return false;
    }
}

}  // namespace ygo::trajectory::dataset
