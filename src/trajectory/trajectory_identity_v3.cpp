#include "ygo/trajectory/codec_v3.hpp"
#include "ygo/trajectory/types_v3.hpp"

#include <limits>
#include <stdexcept>

#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {
namespace {

void validate_v3_identity_input(const EpisodeEnvelopeV3& value) {
    (void)canonical_episode_envelope_bytes_v3(value);
}

}  // namespace

std::string public_gameplay_trajectory_id_v3(const EpisodeEnvelopeV3& value) {
    validate_v3_identity_input(value);
    if (std::holds_alternative<FailedClosureV3>(value.closure)) {
        throw std::invalid_argument("failed V3 envelope has no public gameplay identity");
    }

    ByteWriter writer;
    writer.string(kPublicGameplayIdentityV3Domain);
    writer.string(kPublicGameplayIdentityV3Domain);
    writer.string(kTrustedTrajectoryV3ContractId);
    writer.string(value.manifest.episodic_environment_contract_id);
    writer.string(value.manifest.environment_semantic_id);
    writer.string(value.manifest.episode_identity_schema_id);
    writer.string(value.manifest.episode_semantic_id);
    if (value.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("V3 public gameplay record count exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_public_decision_record_bytes_v3(record));
    }
    writer.raw(canonical_episode_closure_bytes_v3(value.closure));
    return "public_gameplay_trajectory.v3." + trace::sha256_bytes(writer.data());
}

std::string trajectory_record_id_v3(const EpisodeEnvelopeV3& value) {
    validate_v3_identity_input(value);
    if (!std::holds_alternative<TerminalClosureV3>(value.closure) &&
        !std::holds_alternative<InterruptedClosureV3>(value.closure)) {
        throw std::invalid_argument("failed V3 envelope has no trajectory record identity");
    }
    if (value.manifest.collection_disposition.kind != CollectionDispositionKind::Clean) {
        throw std::invalid_argument("quarantined V3 envelope has no trajectory record identity");
    }

    ByteWriter writer;
    writer.string(kTrajectoryRecordIdentityV3Domain);
    writer.string(kTrajectoryRecordIdentityV3Domain);
    writer.string(kTrustedTrajectoryV3ContractId);
    writer.string(public_gameplay_trajectory_id_v3(value));
    writer.raw(canonical_policy_provenance_envelope_bytes(value.manifest.policy_provenance));
    if (value.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("V3 trajectory record count exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_policy_decision_attribution_bytes_v3(record));
    }
    writer.raw(canonical_collection_disposition_bytes(value.manifest.collection_disposition));
    return "trajectory_record.v3." + trace::sha256_bytes(writer.data());
}

}  // namespace ygo::trajectory
