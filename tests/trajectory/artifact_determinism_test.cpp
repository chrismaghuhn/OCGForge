#include "ygo/trajectory/admission.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/recorder.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "test_fixtures.hpp"

namespace {

using namespace ygo;
using namespace ygo::environment;
using namespace ygo::trajectory;
using namespace ygo::trajectory::admission;
using namespace ygo::trajectory::dataset;
using namespace trajectory_test;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_artifact(const std::filesystem::path& path,
                    const std::vector<std::uint8_t>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    require(output.good(), "determinism fixture could not open artifact output");
    output.write(reinterpret_cast<const char*>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    output.flush();
    require(output.good(), "determinism fixture could not write artifact output");
    output.close();
    require(std::filesystem::file_size(path) == bytes.size(),
            "determinism fixture artifact output was truncated");
}

RestrictedReplayEvidence evidence_from(const EpisodeInterrupted& interruption,
                                       const std::string& episode_id) {
    RestrictedReplayEvidence value;
    value.episode_semantic_id = episode_id;
    value.interruption_reason = interruption.reason;
    value.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    value.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    value.observed_engine_process_count = interruption.run_control_evidence.engine_process_count;
    value.observed_semantic_action_count = interruption.run_control_evidence.semantic_action_count;
    value.final_engine_step_index = interruption.final_engine_step_index;
    return value;
}

VerifiedAdmissionReceipt receipt_from(const admission::AdmissionVerification& verification) {
    std::string error;
    const auto receipt = issue_admission_receipt(verification, &error);
    require(receipt.has_value(), "determinism fixture receipt issuance failed: " + error);
    return *receipt;
}

DatasetManifest manifest_from(const VerifiedAdmissionReceipt& verified) {
    const auto& receipt = verified.receipt();
    DatasetManifest manifest;
    const auto receipt_id = admission_receipt_id(receipt);
    for (const auto& entry : receipt.entries) {
        manifest.members.push_back(DatasetManifestMember{
            entry.trajectory_record_id,
            entry.public_gameplay_trajectory_id,
            receipt_id,
            receipt.candidate_shard_artifact_sha256,
            entry.episode_envelope_sha256});
    }
    std::vector<std::string> record_ids;
    for (const auto& member : manifest.members) {
        record_ids.push_back(member.trajectory_record_id);
    }
    manifest.dataset_semantic_id = dataset::dataset_semantic_id(record_ids);
    return manifest;
}

void run_fixture(const std::filesystem::path* output_directory) {
    const auto config = CertifiedEnvironmentConfig::canonical();
    const auto spec = episode_spec(42);
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "determinism fixture could not create certified V2 environment");
    auto environment = std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));

    RunControl control;
    control.engine_process_budget = 1;
    control.semantic_action_budget = 10000;
    control.cancellation.source = "phase3b-determinism";
    const auto reset = environment->reset(spec, control);
    const auto* accepted_reset = std::get_if<ResetAccepted>(&reset);
    require(accepted_reset != nullptr, "determinism fixture reset was rejected");
    const auto* interruption = std::get_if<EpisodeInterrupted>(&accepted_reset->next);
    require(interruption != nullptr &&
                interruption->reason == InterruptionReason::EngineProcessBudget,
            "determinism fixture did not produce the expected budget interruption");

    TrajectoryRecorder recorder(config, spec, provenance(), test_provenance_resolver());
    require(recorder.on_reset_accepted(*accepted_reset),
            "determinism fixture recorder rejected reset");
    const auto envelope = recorder.seal();
    require(envelope.has_value(), "determinism fixture recorder did not seal");
    const auto envelope_bytes = canonical_episode_envelope_bytes(*envelope);

    CandidateTrajectoryShard shard;
    shard.entries.push_back(ShardEntry{trace::sha256_bytes(envelope_bytes), envelope_bytes});
    const auto shard_bytes = canonical_candidate_trajectory_shard_bytes(shard);
    const auto shard_sha256 = candidate_shard_artifact_sha256(shard);

    RestrictedCollectionEvidenceBundle evidence;
    evidence.candidate_shard_artifact_sha256 = shard_sha256;
    evidence.interrupted_episodes.push_back(InterruptedEvidenceEntry{
        shard.entries.front().episode_envelope_sha256,
        evidence_from(*interruption, envelope->manifest.episode_semantic_id)});
    const auto evidence_bytes = canonical_restricted_collection_evidence_bundle_bytes(evidence);
    const auto evidence_sha256 = restricted_collection_evidence_artifact_sha256(evidence);

    ReplayOptions options;
    options.cancellation_source = control.cancellation.source;
    std::string error;
    const auto verification = verify_candidate_shard_for_admission(
        shard, evidence, shard_sha256, evidence_sha256, options,
        test_provenance_resolver(), &error);
    require(verification.has_value(), "determinism fixture admission failed: " + error);
    const auto receipt = receipt_from(*verification);
    const auto receipt_bytes = canonical_admission_receipt_bytes(receipt.receipt());
    const auto manifest = manifest_from(receipt);
    const auto manifest_bytes = canonical_dataset_manifest_bytes(manifest);
    require(validate_dataset_manifest(manifest, {receipt}, &error),
            "determinism fixture manifest failed validation: " + error);

    if (output_directory != nullptr) {
        std::filesystem::create_directories(*output_directory);
        write_artifact(*output_directory / "episode_envelope.bin", envelope_bytes);
        write_artifact(*output_directory / "candidate_shard.bin", shard_bytes);
        write_artifact(*output_directory / "restricted_evidence.bin", evidence_bytes);
        write_artifact(*output_directory / "admission_receipt.bin", receipt_bytes);
        write_artifact(*output_directory / "dataset_manifest.bin", manifest_bytes);
    }

    std::cout << "episode_envelope_sha256=" << trace::sha256_bytes(envelope_bytes) << '\n'
              << "candidate_shard_artifact_sha256=" << shard_sha256 << '\n'
              << "restricted_evidence_artifact_sha256=" << evidence_sha256 << '\n'
              << "public_gameplay_trajectory_id=" << public_gameplay_trajectory_id(*envelope)
              << '\n'
              << "trajectory_record_id=" << trajectory_record_id(*envelope) << '\n'
              << "admission_receipt_id=" << admission_receipt_id(receipt.receipt()) << '\n'
              << "dataset_semantic_id=" << manifest.dataset_semantic_id << '\n'
              << "dataset_manifest_artifact_sha256=" << trace::sha256_bytes(manifest_bytes)
              << '\n'
              << "canonical_bytes_sha256="
              << trace::sha256_bytes(envelope_bytes) << ':'
              << trace::sha256_bytes(shard_bytes) << ':'
              << trace::sha256_bytes(evidence_bytes) << ':'
              << trace::sha256_bytes(receipt_bytes) << ':'
              << trace::sha256_bytes(manifest_bytes) << '\n';
}

}  // namespace

int main(const int argc, char** argv) {
    try {
        std::filesystem::path output_directory;
        const std::filesystem::path* output = nullptr;
        if (argc == 3 && std::string(argv[1]) == "--output-dir") {
            output_directory = std::filesystem::path(argv[2]);
            output = &output_directory;
        } else if (argc != 1) {
            throw std::runtime_error(
                "usage: trajectory_artifact_determinism_test [--output-dir PATH]");
        }
        run_fixture(output);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "trajectory artifact determinism fixture failed: " << error.what() << '\n';
        return 1;
    }
}
