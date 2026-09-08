#include "ygo/trajectory/dataset_manifest_v2.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory::dataset_v2 {
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
        throw std::length_error("V2 dataset member count exceeds u32");
    }
}

void require_identity(const std::string& value, const std::string_view prefix,
                      const char* field) {
    if (!is_canonical_identity(value, prefix)) {
        throw std::invalid_argument(std::string("V2 dataset ") + field +
                                    " has invalid identity");
    }
}

void validate_ids(const std::vector<std::string>& ids) {
    require_length(ids.size());
    std::string previous;
    for (const auto& id : ids) {
        require_identity(id, "trajectory_record.v2.", "record ID");
        if (!previous.empty() && id <= previous) {
            throw std::invalid_argument("V2 dataset record IDs are not strictly sorted");
        }
        previous = id;
    }
}

void validate_manifest(const DatasetManifestV2& value) {
    if (value.dataset_manifest_schema_id != kDatasetManifestV2ContractId ||
        value.dataset_identity_schema_id != kDatasetIdentityV2ContractId ||
        value.trusted_trajectory_contract_id != kTrustedTrajectoryV2ContractId ||
        !is_lower_hex_digest(value.dataset_semantic_id)) {
        throw std::invalid_argument("V2 dataset manifest contract is invalid");
    }
    require_length(value.members.size());
    std::vector<std::string> ids;
    ids.reserve(value.members.size());
    std::string previous;
    for (const auto& member : value.members) {
        require_identity(member.trajectory_record_id, "trajectory_record.v2.", "record ID");
        require_identity(member.public_gameplay_trajectory_id,
                         "public_gameplay_trajectory.v2.", "gameplay ID");
        require_identity(member.admission_receipt_id, "admission_receipt.v2.", "receipt ID");
        if (!is_lower_hex_digest(member.candidate_shard_artifact_sha256) ||
            !is_lower_hex_digest(member.episode_envelope_sha256) ||
            (!previous.empty() && member.trajectory_record_id <= previous)) {
            throw std::invalid_argument("V2 dataset member is invalid or unsorted");
        }
        previous = member.trajectory_record_id;
        ids.push_back(member.trajectory_record_id);
    }
    if (dataset_semantic_id_v2(ids) != value.dataset_semantic_id) {
        throw std::invalid_argument("V2 dataset semantic ID does not match membership");
    }
}

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

const AdmissionReceiptV2* find_receipt(
    const std::vector<VerifiedAdmissionReceiptV2>& receipts,
    const std::string_view id) {
    for (const auto& receipt : receipts) {
        if (admission_receipt_id_v2(receipt.receipt()) == id) {
            return &receipt.receipt();
        }
    }
    return nullptr;
}

const AdmissionEntryCommitmentV2* find_commitment(
    const AdmissionReceiptV2& receipt, const std::string_view id) {
    const auto it = std::lower_bound(
        receipt.entries.begin(), receipt.entries.end(), id,
        [](const auto& entry, const std::string_view key) {
            return entry.trajectory_record_id < key;
        });
    return it == receipt.entries.end() || it->trajectory_record_id != id ? nullptr : &*it;
}

}  // namespace

std::vector<std::uint8_t> canonical_dataset_identity_bytes_v2(
    const std::vector<std::string>& trajectory_record_ids) {
    validate_ids(trajectory_record_ids);
    ByteWriter writer;
    writer.string(kDatasetIdentityV2ContractId);
    writer.string(kDatasetIdentityV2ContractId);
    writer.string(kTrustedTrajectoryV2ContractId);
    writer.u32be(static_cast<std::uint32_t>(trajectory_record_ids.size()));
    for (const auto& id : trajectory_record_ids) {
        writer.string(id);
    }
    return std::move(writer).take();
}

std::string dataset_semantic_id_v2(
    const std::vector<std::string>& trajectory_record_ids) {
    return trace::sha256_bytes(canonical_dataset_identity_bytes_v2(trajectory_record_ids));
}

std::vector<std::uint8_t> canonical_dataset_manifest_bytes_v2(
    const DatasetManifestV2& value) {
    validate_manifest(value);
    ByteWriter writer;
    writer.string(kDatasetManifestV2ContractId);
    writer.string(kDatasetManifestV2ContractId);
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

DecodeResult<DatasetManifestV2> decode_dataset_manifest_v2(
    const std::vector<std::uint8_t>& bytes) noexcept {
    try {
        ByteReader reader(bytes);
        DatasetManifestV2 value;
        std::string domain;
        std::string schema;
        std::uint32_t count = 0;
        if (!reader.string(domain) || domain != kDatasetManifestV2ContractId ||
            !reader.string(schema) || schema != kDatasetManifestV2ContractId ||
            !reader.string(value.dataset_identity_schema_id) ||
            !reader.string(value.trusted_trajectory_contract_id) ||
            !reader.string(value.dataset_semantic_id) || !reader.u32be(count) ||
            count > reader.remaining() / 5) {
            return failure<DatasetManifestV2>("malformed V2 dataset manifest header");
        }
        value.members.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            DatasetManifestMemberV2 member;
            if (!reader.string(member.trajectory_record_id) ||
                !reader.string(member.public_gameplay_trajectory_id) ||
                !reader.string(member.admission_receipt_id) ||
                !reader.string(member.candidate_shard_artifact_sha256) ||
                !reader.string(member.episode_envelope_sha256)) {
                return failure<DatasetManifestV2>("truncated V2 dataset member");
            }
            value.members.push_back(std::move(member));
        }
        if (!reader.at_end() || canonical_dataset_manifest_bytes_v2(value) != bytes) {
            return failure<DatasetManifestV2>("noncanonical V2 dataset manifest");
        }
        return success(std::move(value));
    } catch (const std::exception& error) {
        return failure<DatasetManifestV2>(error.what());
    } catch (...) {
        return failure<DatasetManifestV2>("V2 dataset manifest decode threw");
    }
}

bool validate_dataset_manifest_v2(
    const DatasetManifestV2& value,
    const std::vector<VerifiedAdmissionReceiptV2>& receipts,
    std::string* error) {
    try {
        validate_manifest(value);
        std::vector<std::string> receipt_ids;
        receipt_ids.reserve(receipts.size());
        for (const auto& receipt : receipts) {
            (void)canonical_admission_receipt_bytes_v2(receipt.receipt());
            receipt_ids.push_back(admission_receipt_id_v2(receipt.receipt()));
        }
        std::sort(receipt_ids.begin(), receipt_ids.end());
        if (std::adjacent_find(receipt_ids.begin(), receipt_ids.end()) != receipt_ids.end()) {
            set_error(error, "V2 dataset validation received a duplicate receipt identity");
            return false;
        }
        for (const auto& member : value.members) {
            const auto* receipt = find_receipt(receipts, member.admission_receipt_id);
            if (receipt == nullptr) {
                set_error(error, "V2 dataset member references an unknown receipt");
                return false;
            }
            const auto* commitment = find_commitment(*receipt, member.trajectory_record_id);
            if (commitment == nullptr ||
                commitment->public_gameplay_trajectory_id != member.public_gameplay_trajectory_id ||
                commitment->episode_envelope_sha256 != member.episode_envelope_sha256 ||
                receipt->candidate_shard_artifact_sha256 !=
                    member.candidate_shard_artifact_sha256) {
                set_error(error, "V2 dataset member conflicts with its receipt");
                return false;
            }
        }
        return true;
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return false;
    } catch (...) {
        set_error(error, "V2 dataset manifest validation threw");
        return false;
    }
}

}  // namespace ygo::trajectory::dataset_v2
