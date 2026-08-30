#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "ygo/policy/production.hpp"
#include "ygo/policy/random_legal.hpp"
#include "ygo/policy/runner.hpp"
#include "policy/runner_test_support.hpp"
#include "ygo/trajectory/admission.hpp"
#include "ygo/trajectory/dataset_manifest.hpp"
#include "ygo/trajectory/receipt.hpp"
#include "ygo/trajectory/shard.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trace/sha256.hpp"

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

struct RunnerFixture final {
    ygo::policy::PolicyRunnerConfig config;
};

RunnerFixture make_fixture() {
    RunnerFixture fixture;
    fixture.config.environment_config =
        ygo::environment::CertifiedEnvironmentConfig::canonical();
    fixture.config.episode_spec.contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    fixture.config.episode_spec.root_seed = 2;
    fixture.config.episode_spec.seat_assignment = ygo::environment::SeatAssignment::Normal;
    fixture.config.episode_spec.starting_player = 0;
    fixture.config.run_control.engine_process_budget = 512;
    fixture.config.run_control.semantic_action_budget = 1;
    fixture.config.run_control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    fixture.config.run_control.cancellation.source = "policy-runner-integration";

    const auto artifact = ygo::policy::make_random_legal_policy_artifact();
    const std::array<ygo::trajectory::PolicyRole, 2> roles = {
        ygo::trajectory::PolicyRole::Behavior, ygo::trajectory::PolicyRole::Opponent};
    const auto assignments = ygo::policy::make_random_legal_participant_assignments(
        artifact, fixture.config.environment_config,
        fixture.config.episode_spec.seat_assignment,
        fixture.config.episode_spec.starting_player, roles);
    fixture.config.policy_provenance.policy_artifacts = {artifact};
    fixture.config.policy_provenance.participant_assignments = assignments;

    for (std::uint8_t player = 0; player < 2; ++player) {
        const auto assignment = std::find_if(
            assignments.begin(), assignments.end(),
            [player](const auto& value) { return value.player == player; });
        require(assignment != assignments.end(), "runner fixture lacks a participant assignment");
        const auto stream_id = player == 0 ? std::string("player0") : std::string("player1");
        const auto root = player == 0 ? 0x1111111111111111ULL : 0x2222222222222222ULL;
        auto session = ygo::policy::create_random_legal_policy_session(
            artifact, *assignment, root, stream_id);
        require(static_cast<bool>(session), "runner fixture RandomLegal session construction failed");
        fixture.config.sessions[player] = std::move(*session.value);
    }
    return fixture;
}

void test_clean_random_legal_runner_records_and_admits() {
    auto fixture = make_fixture();
    auto created = ygo::policy::PolicyRunner::create(std::move(fixture.config));
    require(static_cast<bool>(created), "clean policy runner construction failed");
    auto result = created.value->run();
    require(result.disposition == ygo::policy::PolicyRunnerDisposition::CleanAdmitted,
            "clean RandomLegal runner was not admitted");
    require(result.envelope.has_value() && result.candidate_shard.has_value() &&
                result.restricted_evidence.has_value() &&
                result.admission_verification.has_value() && result.admission_receipt.has_value() &&
                result.dataset_manifest.has_value(),
            "clean runner did not return the complete trajectory/admission bundle");
    require(result.envelope->records.size() == 1,
            "bounded runner did not record exactly one accepted action");
    const auto& record = result.envelope->records.front();
    const auto& attribution = record.policy_rng_decision_provenance;
    require(attribution.mode == ygo::trajectory::PolicyRngMode::Cursor,
            "accepted RandomLegal action was not recorded in CURSOR mode");
    require(attribution.pre_cursor.has_value() && attribution.post_cursor.has_value() &&
                *attribution.post_cursor >= *attribution.pre_cursor,
            "accepted RandomLegal action has an invalid cursor span");
    require(attribution.acting_policy_assignment_id == record.acting_policy_assignment_id,
            "record attribution lost the acting assignment");
    require(!record.selected_public_action_key.empty(),
            "accepted record lost the selected public action key");
    require(result.restricted_evidence->rng_initializations.size() == 1,
            "clean runner did not retain exact RNG initialization evidence");
    require(result.restricted_evidence->interrupted_episodes.size() == 1,
            "bounded semantic-action interruption lacked restricted evidence");

    const auto& shard_entry = result.candidate_shard->entries.front();
    require(shard_entry.episode_envelope_sha256 ==
                ygo::trace::sha256_bytes(shard_entry.envelope_bytes),
            "candidate shard entry digest is not bound to its envelope bytes");
    const auto decoded_envelope =
        ygo::trajectory::decode_episode_envelope(shard_entry.envelope_bytes);
    require(static_cast<bool>(decoded_envelope), "candidate shard envelope failed strict decode");
    const auto decoded_shard = ygo::trajectory::decode_candidate_trajectory_shard(
        ygo::trajectory::canonical_candidate_trajectory_shard_bytes(*result.candidate_shard));
    require(static_cast<bool>(decoded_shard), "candidate shard failed strict round-trip");
    const auto decoded_evidence = ygo::trajectory::decode_restricted_collection_evidence_bundle(
        ygo::trajectory::canonical_restricted_collection_evidence_bundle_bytes(
            *result.restricted_evidence));
    require(static_cast<bool>(decoded_evidence), "restricted evidence failed strict round-trip");
    const auto decoded_receipt = ygo::trajectory::decode_admission_receipt(
        ygo::trajectory::canonical_admission_receipt_bytes(
            result.admission_receipt->receipt()));
    require(static_cast<bool>(decoded_receipt), "admission receipt failed strict round-trip");
    std::string manifest_error;
    require(ygo::trajectory::dataset::validate_dataset_manifest(
                *result.dataset_manifest,
                std::vector<ygo::trajectory::VerifiedAdmissionReceipt>{
                    *result.admission_receipt},
                &manifest_error),
            "dataset manifest rejected the admitted runner receipt: " + manifest_error);
}

void test_mismatched_random_legal_policy_binding_is_rejected() {
    auto fixture = make_fixture();
    const auto& artifact = fixture.config.policy_provenance.policy_artifacts.front();
    const auto assignment = std::find_if(
        fixture.config.policy_provenance.participant_assignments.begin(),
        fixture.config.policy_provenance.participant_assignments.end(),
        [](const auto& value) { return value.player == 0; });
    require(assignment != fixture.config.policy_provenance.participant_assignments.end(),
            "binding mismatch fixture lacks player zero assignment");

    fixture.config.sessions[0]->binding = ygo::policy::make_random_legal_execution_binding(
        artifact, *assignment, 0x3333333333333333ULL, "player0");
    const auto created = ygo::policy::PolicyRunner::create(std::move(fixture.config));
    require(!created && created.error.has_value(),
            "RandomLegal policy with a forged RNG binding was constructible");

    auto stream_fixture = make_fixture();
    stream_fixture.config.sessions[0]->binding.initialization.policy_rng_stream_id = "player1";
    const auto stream_created =
        ygo::policy::PolicyRunner::create(std::move(stream_fixture.config));
    require(!stream_created && stream_created.error.has_value(),
            "RNG initialization with a mismatched stream identity was constructible");
}

void test_policy_step_rejection_quarantines_without_retry_or_admission() {
    auto fixture = make_fixture();
    const auto selector_calls = std::make_shared<std::size_t>(0);
    auto created = ygo::policy::PolicyRunner::create(std::move(fixture.config));
    require(static_cast<bool>(created), "rejection policy runner construction failed");
    const auto result = ygo::policy::detail::run_with_test_selector(
        *created.value, 0, ygo::policy::detail::TestSelectorBehavior::InvalidPublicAction,
        selector_calls);
    require(result.disposition == ygo::policy::PolicyRunnerDisposition::Quarantined,
            "policy-origin StepRejected did not quarantine the collection");
    require(*selector_calls == 1, "policy-origin StepRejected caused a selector retry");
    require(result.envelope.has_value() && result.envelope->records.empty(),
            "policy-origin StepRejected created a decision record");
    require(std::holds_alternative<ygo::trajectory::InterruptedClosure>(result.envelope->closure) &&
                std::get<ygo::trajectory::InterruptedClosure>(result.envelope->closure)
                    .pending_unacted_frame.has_value(),
            "quarantined rejection envelope lost the unchanged pending frame");
    require(result.envelope->manifest.collection_disposition.kind ==
                ygo::trajectory::CollectionDispositionKind::QuarantinedAfterPolicyRejection,
            "rejected collection was not marked quarantined");
    require(result.envelope->manifest.collection_disposition.policy_rejections.size() == 1,
            "quarantined collection did not retain its policy rejection classification");
    require(result.candidate_shard.has_value() && result.restricted_evidence.has_value(),
            "quarantined runner did not seal candidate/evidence artifacts");
    require(!result.admission_verification.has_value() && !result.admission_receipt.has_value() &&
                !result.dataset_manifest.has_value(),
            "quarantined envelope reached clean admission outputs");
}

void test_policy_failure_is_structured_without_submitting_a_key() {
    auto fixture = make_fixture();
    auto created = ygo::policy::PolicyRunner::create(std::move(fixture.config));
    require(static_cast<bool>(created), "policy-failure runner construction failed");
    const auto result = ygo::policy::detail::run_with_test_selector(
        *created.value, 0, ygo::policy::detail::TestSelectorBehavior::PolicyFailure);
    require(result.disposition == ygo::policy::PolicyRunnerDisposition::Failed,
            "policy failure did not fail the runner closed");
    require(result.policy_error.has_value() &&
                result.policy_error->code == ygo::policy::PolicyErrorCode::InvalidConfiguration,
            "policy failure lost its structured error");
    require(!result.envelope.has_value(),
            "policy failure fabricated a trajectory envelope without a submitted key");
}

}  // namespace

int main() {
    try {
        test_clean_random_legal_runner_records_and_admits();
        test_mismatched_random_legal_policy_binding_is_rejected();
        test_policy_step_rejection_quarantines_without_retry_or_admission();
        test_policy_failure_is_structured_without_submitting_a_key();
        std::cout << "policy_runner_integration_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
