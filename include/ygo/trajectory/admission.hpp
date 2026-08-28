#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ygo/trajectory/policy_provenance.hpp"
#include "ygo/trajectory/restricted_evidence.hpp"

namespace ygo::trajectory::admission {

struct ReplayOptions final {
    // A terminal episode has no persisted run-control evidence because run
    // control is not part of the accepted gameplay identity.  Admission must
    // therefore receive an explicit valid control value for that case.
    std::optional<environment::RunControl> terminal_run_control;
    // V2 requires a safe cancellation source even though it is not persisted.
    // It is an explicit replay input, never a canonical trajectory default.
    std::optional<std::string> cancellation_source;
};

struct ReplayResult final {
    bool accepted = false;
    std::uint64_t final_engine_step_index = 0;
    std::string error;

    explicit operator bool() const noexcept { return accepted; }
};

ReplayResult replay_episode(const EpisodeEnvelope& envelope,
                            const std::optional<RestrictedReplayEvidence>& evidence,
                            const ReplayOptions& options);

class AdmissionVerification final {
public:
    const std::string& candidate_shard_artifact_sha256() const noexcept {
        return candidate_shard_artifact_sha256_;
    }
    const std::string& restricted_evidence_artifact_sha256() const noexcept {
        return restricted_evidence_artifact_sha256_;
    }
    const std::vector<AdmissionEntryCommitment>& entries() const noexcept { return entries_; }

private:
    AdmissionVerification(std::string candidate_shard_artifact_sha256,
                          std::string restricted_evidence_artifact_sha256,
                          std::vector<AdmissionEntryCommitment> entries)
        : candidate_shard_artifact_sha256_(std::move(candidate_shard_artifact_sha256)),
          restricted_evidence_artifact_sha256_(std::move(restricted_evidence_artifact_sha256)),
          entries_(std::move(entries)) {}

    friend std::optional<AdmissionVerification> verify_candidate_shard_for_admission(
        const CandidateTrajectoryShard&, const RestrictedCollectionEvidenceBundle&,
        std::string_view, std::string_view, const ReplayOptions&, const ProvenanceResolver&,
        std::string*);

    std::string candidate_shard_artifact_sha256_;
    std::string restricted_evidence_artifact_sha256_;
    std::vector<AdmissionEntryCommitment> entries_;
};

std::optional<AdmissionVerification> verify_candidate_shard_for_admission(
    const CandidateTrajectoryShard& shard,
    const RestrictedCollectionEvidenceBundle& restricted_evidence,
    std::string_view candidate_shard_artifact_sha256,
    std::string_view restricted_evidence_artifact_sha256,
    const ReplayOptions& options,
    const ProvenanceResolver& resolver,
    std::string* error = nullptr);

}  // namespace ygo::trajectory::admission
