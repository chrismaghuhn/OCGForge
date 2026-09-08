#include "ygo/trajectory/admission_v2.hpp"

#include <algorithm>
#include <utility>

#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/identity_resolver.hpp"

namespace ygo::trajectory::admission_v2 {
namespace {

void set_error(std::string* error, std::string message) {
    if (error != nullptr) {
        *error = std::move(message);
    }
}

const ParticipantPolicyAssignment* active_assignment(
    const PolicyProvenanceEnvelope& provenance,
    const std::uint8_t player,
    const std::uint64_t decision_index,
    std::string& error) {
    const ParticipantPolicyAssignment* selected = nullptr;
    for (const auto& assignment : provenance.participant_assignments) {
        if (assignment.player != player ||
            assignment.effective_from_decision_index > decision_index) {
            continue;
        }
        if (selected == nullptr ||
            assignment.effective_from_decision_index > selected->effective_from_decision_index) {
            selected = &assignment;
        } else if (assignment.effective_from_decision_index ==
                   selected->effective_from_decision_index) {
            error = "V2 admission found two active assignments at one decision index";
            return nullptr;
        }
    }
    if (selected == nullptr) {
        error = "V2 admission found no active participant assignment";
    }
    return selected;
}

const PolicyArtifact* artifact_by_id(const PolicyProvenanceEnvelope& provenance,
                                     const std::string_view id) noexcept {
    const auto it = std::find_if(
        provenance.policy_artifacts.begin(), provenance.policy_artifacts.end(),
        [id](const auto& artifact) { return artifact.policy_artifact_id == id; });
    return it == provenance.policy_artifacts.end() ? nullptr : &*it;
}

bool validate_record_attribution(const EpisodeEnvelopeV2& envelope,
                                 const DecisionRecordV2& record,
                                 const std::string& error_prefix,
                                 std::string& error) {
    const auto& attribution = record.policy_rng_decision_provenance;
    if (record.acting_policy_assignment_id != attribution.acting_policy_assignment_id ||
        attribution.decision_index != record.frame.decision_index) {
        error = error_prefix + ": attribution does not match the record";
        return false;
    }
    const auto* assignment = active_assignment(
        envelope.manifest.policy_provenance, record.frame.acting_player,
        record.frame.decision_index, error);
    if (assignment == nullptr || assignment->participant_policy_assignment_id !=
                                    record.acting_policy_assignment_id) {
        if (error.empty()) {
            error = error_prefix + ": assignment is not active for the frame";
        }
        return false;
    }
    const auto* artifact = artifact_by_id(
        envelope.manifest.policy_provenance, assignment->policy_artifact_id);
    if (artifact == nullptr) {
        error = error_prefix + ": assignment references an unknown policy artifact";
        return false;
    }
    // RestrictedReplayEvidenceV2 deliberately carries interruption budgets,
    // not policy RNG initialization material.  A stochastic attribution is
    // therefore not replay-provable at this boundary and must fail closed.
    if (attribution.mode != PolicyRngMode::None) {
        error = error_prefix + ": V2 admission lacks RNG initialization evidence";
        return false;
    }
    if (artifact->policy_rng_contract_identity != kNoPolicyRngContractId) {
        error = error_prefix + ": stochastic artifact has NONE RNG attribution";
        return false;
    }
    try {
        (void)canonical_policy_decision_attribution_bytes_v2(record);
        return true;
    } catch (const std::exception& exception) {
        error = error_prefix + ": " + exception.what();
        return false;
    }
}

}  // namespace

std::optional<AdmissionVerification> verify_episode_for_admission_v2(
    const EpisodeEnvelopeV2& envelope,
    const std::optional<RestrictedReplayEvidenceV2>& evidence,
    const replay_v2::ReplayOptions& options,
    const ProvenanceResolver& resolver,
    std::string* error) {
    try {
        (void)canonical_episode_envelope_bytes_v2(envelope);
        if (std::holds_alternative<FailedClosureV2>(envelope.closure)) {
            set_error(error, "V2 failed envelope is not admissible");
            return std::nullopt;
        }
        if (envelope.manifest.collection_disposition.kind != CollectionDispositionKind::Clean ||
            !envelope.manifest.collection_disposition.policy_rejections.empty()) {
            set_error(error, "V2 quarantined or rejected collection is not admissible");
            return std::nullopt;
        }
        if (evidence.has_value()) {
            (void)canonical_restricted_replay_evidence_bytes_v2(*evidence);
            if (evidence->episode_semantic_id != envelope.manifest.episode_semantic_id ||
                evidence->trusted_trajectory_contract_id != kTrustedTrajectoryV2ContractId ||
                evidence->episodic_environment_contract_id !=
                    environment::kEpisodicEnvironmentV3ContractId) {
                set_error(error, "V2 restricted evidence is not bound to this V3 episode");
                return std::nullopt;
            }
        }
        if (std::holds_alternative<TerminalClosureV2>(envelope.closure) && evidence.has_value()) {
            set_error(error, "terminal V2 admission received interruption evidence");
            return std::nullopt;
        }
        environment::CertifiedEnvironmentConfig config;
        environment::EpisodeSpec spec;
        const auto decoded_config = decode_environment_identity_input_v3(
            envelope.manifest.environment_identity_input);
        if (!decoded_config || !is_current_certified_environment_v3(*decoded_config.value)) {
            set_error(error, "V2 admission environment is not the current V3 identity");
            return std::nullopt;
        }
        config = *decoded_config.value;
        const auto decoded_spec = decode_episode_identity_input_v3(
            envelope.manifest.episode_identity_input, config);
        if (!decoded_spec) {
            set_error(error, "V2 admission episode identity is not V3-canonical");
            return std::nullopt;
        }
        spec = *decoded_spec.value;
        std::string provenance_error;
        if (!resolver.validate(envelope.manifest.policy_provenance, config, spec,
                               &provenance_error)) {
            set_error(error, "V2 admission policy provenance is invalid: " + provenance_error);
            return std::nullopt;
        }
        for (const auto& record : envelope.records) {
            if (!validate_record_attribution(envelope, record,
                                             "V2 admission record attribution", provenance_error)) {
                set_error(error, std::move(provenance_error));
                return std::nullopt;
            }
        }

        const auto replay = replay_v2::replay_episode_v2(envelope, evidence, options);
        if (!replay.accepted) {
            set_error(error, "V2 replay proof failed: " + replay.error);
            return std::nullopt;
        }
        return AdmissionVerification(
            public_gameplay_trajectory_id_v2(envelope), trajectory_record_id_v2(envelope),
            envelope.manifest.environment_semantic_id,
            envelope.manifest.episode_semantic_id, replay.final_engine_step_index);
    } catch (const std::exception& exception) {
        set_error(error, exception.what());
        return std::nullopt;
    } catch (...) {
        set_error(error, "V2 admission threw");
        return std::nullopt;
    }
}

}  // namespace ygo::trajectory::admission_v2
