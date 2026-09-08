#include "ygo/trajectory/codec.hpp"

#include <limits>
#include <stdexcept>

#include "ygo/trace/sha256.hpp"

namespace ygo::trajectory {
namespace {

void validate_v2_identity_input(const EpisodeEnvelopeV2& value) {
    (void)canonical_episode_envelope_bytes_v2(value);
}

}  // namespace

std::string public_gameplay_trajectory_id_v2(const EpisodeEnvelopeV2& value) {
    validate_v2_identity_input(value);
    if (std::holds_alternative<FailedClosureV2>(value.closure)) {
        throw std::invalid_argument("failed V2 envelope has no public gameplay identity");
    }

    ByteWriter writer;
    writer.string(kPublicGameplayIdentityV2Domain);
    writer.string(kPublicGameplayIdentityV2Domain);
    writer.string(kTrustedTrajectoryV2ContractId);
    writer.string(value.manifest.episodic_environment_contract_id);
    writer.string(value.manifest.environment_semantic_id);
    writer.string(value.manifest.episode_identity_schema_id);
    writer.string(value.manifest.episode_semantic_id);
    if (value.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("V2 public gameplay record count exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_public_decision_record_bytes_v2(record));
    }
    writer.raw(canonical_episode_closure_bytes_v2(value.closure));
    return "public_gameplay_trajectory.v2." + trace::sha256_bytes(writer.data());
}

std::string trajectory_record_id_v2(const EpisodeEnvelopeV2& value) {
    validate_v2_identity_input(value);
    if (!std::holds_alternative<TerminalClosureV2>(value.closure) &&
        !std::holds_alternative<InterruptedClosureV2>(value.closure)) {
        throw std::invalid_argument("failed V2 envelope has no trajectory record identity");
    }
    if (value.manifest.collection_disposition.kind != CollectionDispositionKind::Clean) {
        throw std::invalid_argument("quarantined V2 envelope has no trajectory record identity");
    }

    ByteWriter writer;
    writer.string(kTrajectoryRecordIdentityV2Domain);
    writer.string(kTrajectoryRecordIdentityV2Domain);
    writer.string(kTrustedTrajectoryV2ContractId);
    writer.string(public_gameplay_trajectory_id_v2(value));
    writer.raw(canonical_policy_provenance_envelope_bytes(value.manifest.policy_provenance));
    if (value.records.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::length_error("V2 trajectory record count exceeds u32");
    }
    writer.u32be(static_cast<std::uint32_t>(value.records.size()));
    for (const auto& record : value.records) {
        writer.raw(canonical_policy_decision_attribution_bytes_v2(record));
    }
    writer.raw(canonical_collection_disposition_bytes(value.manifest.collection_disposition));
    return "trajectory_record.v2." + trace::sha256_bytes(writer.data());
}

}  // namespace ygo::trajectory
