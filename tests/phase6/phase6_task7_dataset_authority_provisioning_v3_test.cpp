#include "ygo/phase6/task7_dataset_authority_provisioning_v3.hpp"

#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/model/encoded_model_input_v2.hpp"
#include "ygo/model/logical_model_input_v2.hpp"
#include "ygo/phase6/supervision_dataset_v2.hpp"
#include "ygo/phase6/task7_dataset_authority_provisioning_v2.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/policy/teacher.hpp"
#include "ygo/policy/teacher_v3.hpp"
#include "ygo/teacher/salamangreat_profile.hpp"
#include "ygo/teacher/swordsoul_tenyi_profile.hpp"
#include "ygo/trajectory/admission_v3.hpp"
#include "ygo/trajectory/recorder_v3.hpp"
#include "ygo/trajectory/restricted_evidence_v3.hpp"
#include "ygo/trajectory/shard_v3.hpp"
#include "ygo/trajectory/trajectory_identity_v3.hpp"
#include "ygo/trace/sha256.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using namespace ygo::phase6;

constexpr std::string_view kBaseCommit =
    "2b6ee8bc69d7a920e636380aefcfd8b743d97c24";
void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ygo::environment::PublicEnvironmentObservation model_observation() {
    ygo::observation::PlayerObservation source;
    source.perspective_player = 0;
    source.match_context.perspective_player = 0;
    source.match_context.own_deck.known = true;
    source.match_context.own_deck.main_deck = {100};
    source.decision_context.kind = "unselect_card";
    source.decision_context.player = 0;
    return ygo::environment::project_public_observation(source);
}

ygo::environment::EnvironmentActionCandidate model_card_candidate(
    const ygo::environment::PublicCardSelectionOperation operation) {
    ygo::environment::EnvironmentActionCandidate result;
    result.action_kind = ygo::environment::EnvironmentActionKind::CardSelection;
    result.card_selection_operation = operation;
    ygo::environment::PublicActionKeyInput key;
    key.action_kind = "card_selection";
    key.card_selection_operation = operation;
    result.public_action_key = ygo::environment::public_action_key_v3(key);
    return result;
}

ygo::environment::EnvironmentActionCandidate model_cancel_candidate() {
    ygo::environment::EnvironmentActionCandidate result;
    result.action_kind = ygo::environment::EnvironmentActionKind::Cancel;
    result.card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::None;
    ygo::environment::PublicActionKeyInput key;
    key.action_kind = "cancel";
    result.public_action_key = ygo::environment::public_action_key_v3(key);
    return result;
}

void test_model_v2_select_unselect_contract() {
    const auto observation = model_observation();
    const std::vector<ygo::environment::EnvironmentActionCandidate> candidates = {
        model_card_candidate(ygo::environment::PublicCardSelectionOperation::Select),
        model_card_candidate(ygo::environment::PublicCardSelectionOperation::Unselect),
        model_cancel_candidate()};
    const auto vocabulary = ygo::model::CardVocabularyV1::from_ascending_passcodes({100});
    require(static_cast<bool>(vocabulary), "V3 model vocabulary fixture failed");
    const auto logical = ygo::model::project_logical_model_input_v2(observation, candidates);
    require(static_cast<bool>(logical), "LogicalModelInputV2 projection failed");
    require(logical.value->candidate_features[0].card_selection_operation ==
                ygo::environment::PublicCardSelectionOperation::Select &&
                logical.value->candidate_features[1].card_selection_operation ==
                    ygo::environment::PublicCardSelectionOperation::Unselect &&
                logical.value->candidate_features[2].card_selection_operation ==
                    ygo::environment::PublicCardSelectionOperation::None,
            "LogicalModelInputV2 did not preserve Select/Unselect metadata");
    const auto encoded = ygo::model::encode_model_input_v2(*logical.value, *vocabulary.value);
    require(static_cast<bool>(encoded),
            "EncodedModelInputV2 encoding failed: " +
                (encoded.error.has_value() ? encoded.error->diagnostic : "unknown"));
    require(encoded.value->candidate_features[0].card_selection_operation_code == 1 &&
                encoded.value->candidate_features[1].card_selection_operation_code == 2 &&
                encoded.value->candidate_features[2].card_selection_operation_code == 0,
            "EncodedModelInputV2 lost Select/Unselect metadata");
    require(encoded.value->routing_keys == std::vector<std::string>{
                candidates[0].public_action_key, candidates[1].public_action_key,
                candidates[2].public_action_key},
            "EncodedModelInputV2 changed candidate order or routing keys");
    const auto identity = ygo::model::model_input_identity_v2(*logical.value, *encoded.value);
    require(identity.rfind("model_input.v2.", 0) == 0,
            "V2 model input used the wrong identity generation");
    std::cout << "MODEL_INPUT_V2_ID=" << identity << '\n';

    auto stale = *logical.value;
    stale.candidate_features[0].card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::Unselect;
    require(!ygo::model::encode_model_input_v2(stale, *vocabulary.value),
            "LogicalModelInputV2 accepted an operation/key mismatch");

    auto changed_candidates = candidates;
    changed_candidates[0].card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::Unselect;
    ygo::environment::PublicActionKeyInput changed_key;
    changed_key.action_kind = "card_selection";
    changed_key.card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::Unselect;
    changed_candidates[0].public_action_key =
        ygo::environment::public_action_key_v3(changed_key);
    changed_candidates[1].card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::Select;
    changed_key.card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::Select;
    changed_candidates[1].public_action_key =
        ygo::environment::public_action_key_v3(changed_key);
    const auto changed_logical =
        ygo::model::project_logical_model_input_v2(observation, changed_candidates);
    require(static_cast<bool>(changed_logical), "changed V3 operation/key fixture was invalid");
    const auto changed_encoded =
        ygo::model::encode_model_input_v2(*changed_logical.value, *vocabulary.value);
    require(static_cast<bool>(changed_encoded) &&
                ygo::model::model_input_identity_v2(
                    *changed_logical.value, *changed_encoded.value) != identity,
            "V2 model identity ignored corrected operation semantics");

    auto historical_candidates = candidates;
    ygo::environment::PublicActionKeyInput historical_key;
    historical_key.action_kind = "card_selection";
    historical_key.card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::Select;
    historical_candidates[0].public_action_key =
        ygo::environment::public_action_key_v2(historical_key);
    require(!ygo::model::project_logical_model_input_v2(
                 observation, historical_candidates),
            "LogicalModelInputV2 accepted a V2 public action key");

    auto invalid_none = candidates;
    invalid_none[0] = model_card_candidate(
        ygo::environment::PublicCardSelectionOperation::None);
    require(!ygo::model::project_logical_model_input_v2(observation, invalid_none),
            "unselect_card accepted CardSelection with None operation");

    auto card_selection_observation = observation;
    card_selection_observation.decision_context.kind = "card_selection";
    require(!ygo::model::project_logical_model_input_v2(
                 card_selection_observation, candidates),
            "card_selection accepted Select/Unselect operations");

    auto idle_observation = observation;
    idle_observation.decision_context.kind = "idle_command";
    auto idle_candidates = std::vector<ygo::environment::EnvironmentActionCandidate>{
        model_card_candidate(ygo::environment::PublicCardSelectionOperation::Unselect)};
    require(!ygo::model::project_logical_model_input_v2(idle_observation, idle_candidates),
            "idle_command accepted a CardSelection operation");

    auto missing_context_observation = observation;
    missing_context_observation.decision_context.kind.reset();
    require(!ygo::model::project_logical_model_input_v2(
                 missing_context_observation, candidates),
            "nonempty domain accepted a missing request kind");

    auto invalid_logical = *logical.value;
    invalid_logical.candidate_features[0].card_selection_operation =
        ygo::environment::PublicCardSelectionOperation::None;
    invalid_logical.candidate_routing[0].public_action_key =
        model_card_candidate(ygo::environment::PublicCardSelectionOperation::None)
            .public_action_key;
    std::vector<std::string> invalid_keys;
    for (const auto& routing : invalid_logical.candidate_routing) {
        invalid_keys.push_back(routing.public_action_key);
    }
    invalid_logical.public_candidate_domain_digest =
        ygo::environment::public_candidate_domain_digest_v3(
            "unselect_card", invalid_keys);
    require(!ygo::model::encode_model_input_v2(invalid_logical, *vocabulary.value),
            "Logical-to-encoded validation accepted matching None semantics");

    auto missing_logical_context = *logical.value;
    missing_logical_context.public_observation_context_kind.reset();
    missing_logical_context.public_candidate_domain_digest.reset();
    require(!ygo::model::encode_model_input_v2(
                 missing_logical_context, *vocabulary.value),
            "Logical-to-encoded validation accepted missing request kind");
}

void test_task7_v3_generation_matrix() {
    const auto v3_schedule = make_task7_collection_schedule_v3(std::string(kBaseCommit));
    const auto v3_job_bytes = canonical_task7_collection_job_bytes_v3(v3_schedule.jobs.front());
    const auto v3_schedule_bytes = canonical_task7_collection_schedule_bytes_v3(v3_schedule);
    require(static_cast<bool>(decode_task7_collection_job_v3(v3_job_bytes)),
            "Task7 V3 job did not round-trip through its canonical codec");

    const auto v2_schedule = ygo::phase6::make_task7_collection_schedule_v2(
        std::string(kBaseCommit));
    const auto v2_job_bytes =
        ygo::phase6::canonical_task7_collection_job_bytes_v2(v2_schedule.jobs.front());
    const auto v2_schedule_bytes =
        ygo::phase6::canonical_task7_collection_schedule_bytes_v2(v2_schedule);
    require(!decode_task7_collection_job_v3(v2_job_bytes),
            "Task7 V3 job decoder accepted canonical V2 job bytes");
    require(!decode_task7_collection_schedule_v3(v2_schedule_bytes),
            "Task7 V3 schedule decoder accepted canonical V2 schedule bytes");
    require(!ygo::phase6::decode_task7_collection_job_v2(v3_job_bytes),
            "Task7 V2 job decoder accepted canonical V3 job bytes");
    require(!ygo::phase6::decode_task7_collection_schedule_v2(v3_schedule_bytes),
            "Task7 V2 schedule decoder accepted canonical V3 schedule bytes");

    auto wrong_environment = v3_schedule.jobs.front();
    wrong_environment.environment_contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV3ContractId);
    bool rejected = false;
    try {
        (void)canonical_task7_collection_job_bytes_v3(wrong_environment);
    } catch (...) {
        rejected = true;
    }
    require(rejected, "Task7 V3 job accepted historical Environment V3");

    auto wrong_teacher = v3_schedule.jobs.front();
    wrong_teacher.teacher_producer_identity = "ocgforge.policy.teacher_core.v2";
    rejected = false;
    try {
        (void)canonical_task7_collection_job_bytes_v3(wrong_teacher);
    } catch (...) {
        rejected = true;
    }
    require(rejected, "Task7 V3 job accepted historical Teacher provenance");
}

struct AdmittedV3Fixture final {
    ygo::trajectory::EpisodeEnvelopeV3 envelope;
    ygo::trajectory::VerifiedAdmissionReceiptV3 receipt;
    ygo::trajectory::dataset_v3::DatasetManifestV3 manifest;
    ygo::model::CardVocabularyV1 vocabulary;
};

ygo::trajectory::PolicyProvenanceEnvelope production_v3_provenance_for_test(
    const ygo::environment::CertifiedEnvironmentConfig& config,
    const ygo::environment::EpisodeSpec& spec) {
    const auto swordsoul = ygo::policy::make_teacher_policy_artifact_v3(
        ygo::teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat = ygo::policy::make_teacher_policy_artifact_v3(
        ygo::teacher::make_salamangreat_profile());
    ygo::trajectory::PolicyProvenanceEnvelope result;
    result.policy_artifacts = {swordsoul, salamangreat};
    std::sort(result.policy_artifacts.begin(), result.policy_artifacts.end(),
              [](const auto& left, const auto& right) {
                  return left.policy_artifact_id < right.policy_artifact_id;
              });
    result.participant_assignments = ygo::policy::make_teacher_participant_assignments(
        swordsoul, salamangreat, config, spec.seat_assignment, spec.starting_player,
        {ygo::trajectory::PolicyRole::Behavior, ygo::trajectory::PolicyRole::Opponent});
    return result;
}

AdmittedV3Fixture make_admitted_v3_fixture() {
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical_v4();
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV4ContractId);
    spec.root_seed = 4311;
    const auto provenance = production_v3_provenance_for_test(config, spec);
    ygo::environment::RunControl control;
    control.engine_process_budget = 10000;
    control.semantic_action_budget = 1;
    control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    control.cancellation.source = "task7-v3-model-fixture";

    auto factory = ygo::environment::EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "V3 model fixture environment creation failed");
    auto environment = std::move(
        std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
    const auto reset = environment->reset(spec, control);
    const auto* reset_accepted = std::get_if<ygo::environment::ResetAccepted>(&reset);
    require(reset_accepted != nullptr &&
                std::holds_alternative<ygo::environment::DecisionFrame>(reset_accepted->next),
            "V3 model fixture reset did not publish a frame");
    const auto frame = std::get<ygo::environment::DecisionFrame>(reset_accepted->next);
    const auto assignment = std::find_if(
        provenance.participant_assignments.begin(), provenance.participant_assignments.end(),
        [&](const auto& value) { return value.player == frame.acting_player; });
    require(assignment != provenance.participant_assignments.end(),
            "V3 model fixture lacks the acting assignment");

    ygo::trajectory::TrajectoryRecorderV3 recorder(
        config, spec, provenance, ygo::policy::make_production_policy_provenance_resolver());
    std::string error;
    require(recorder.on_reset_accepted(*reset_accepted, std::nullopt, &error),
            "V3 model fixture recorder rejected reset: " + error);
    const auto stepped = environment->step(ygo::environment::ActionSelection{
        std::string(ygo::environment::kEpisodicEnvironmentV4ContractId),
        frame.episode_semantic_id, frame.public_semantic_decision_id,
        frame.submission_token, frame.request.candidates.front().public_action_key});
    const auto* accepted = std::get_if<ygo::environment::StepAccepted>(&stepped);
    require(accepted != nullptr &&
                std::holds_alternative<ygo::environment::EpisodeInterrupted>(accepted->next),
            "V3 model fixture did not reach a bounded interruption");
    ygo::trajectory::PolicyRngDecisionProvenance attribution;
    attribution.decision_index = frame.decision_index;
    attribution.acting_policy_assignment_id = assignment->participant_policy_assignment_id;
    attribution.policy_rng_identity = ygo::trajectory::kNoPolicyRngContractId;
    attribution.policy_rng_contract_identity = ygo::trajectory::kNoPolicyRngContractId;
    attribution.policy_rng_stream_id = ygo::trajectory::kNoPolicyRngContractId;
    attribution.policy_rng_initialization_identity = ygo::trajectory::kNoPolicyRngContractId;
    attribution.mode = ygo::trajectory::PolicyRngMode::None;
    require(recorder.on_step_accepted(*accepted, attribution, std::nullopt, &error),
            "V3 model fixture recorder rejected accepted step: " + error);
    const auto envelope = recorder.seal(&error);
    require(envelope.has_value(), "V3 model fixture did not seal: " + error);

    ygo::trajectory::CandidateTrajectoryShardV3 shard;
    const auto envelope_bytes = ygo::trajectory::canonical_episode_envelope_bytes_v3(*envelope);
    shard.entries.push_back({ygo::trace::sha256_bytes(envelope_bytes), envelope_bytes});
    ygo::trajectory::RestrictedCollectionEvidenceBundleV3 evidence;
    evidence.candidate_shard_artifact_sha256 =
        ygo::trajectory::candidate_shard_artifact_sha256_v3(shard);
    std::string evidence_error;
    ygo::trajectory::replay_v3::ReplayOptions replay_options;
    replay_options.cancellation_source = control.cancellation.source;
    const auto& interruption = std::get<ygo::environment::EpisodeInterrupted>(accepted->next);
    ygo::trajectory::RestrictedReplayEvidenceV3 replay_evidence;
    replay_evidence.episode_semantic_id = envelope->manifest.episode_semantic_id;
    replay_evidence.interruption_reason = interruption.reason;
    replay_evidence.engine_process_budget = interruption.run_control_evidence.engine_process_budget;
    replay_evidence.semantic_action_budget = interruption.run_control_evidence.semantic_action_budget;
    replay_evidence.observed_engine_process_count = interruption.run_control_evidence.engine_process_count;
    replay_evidence.observed_semantic_action_count = interruption.run_control_evidence.semantic_action_count;
    replay_evidence.final_engine_step_index = interruption.final_engine_step_index;
    evidence.interrupted_episodes.push_back(
        {shard.entries.front().episode_envelope_sha256, replay_evidence});
    const auto verification = ygo::trajectory::admission_v3::verify_collection_for_admission_v3(
        shard, evidence, ygo::trajectory::candidate_shard_artifact_sha256_v3(shard),
        ygo::trajectory::restricted_collection_evidence_artifact_sha256_v3(evidence),
        replay_options, ygo::policy::make_production_policy_provenance_resolver(),
        &evidence_error);
    require(verification.has_value(), "V3 model fixture admission failed: " + evidence_error);
    const auto receipt = ygo::trajectory::issue_admission_receipt_v3(*verification, &evidence_error);
    require(receipt.has_value(), "V3 model fixture receipt failed: " + evidence_error);
    ygo::trajectory::dataset_v3::DatasetManifestV3 manifest;
    const auto& commitment = verification->entries().front();
    manifest.members.push_back({
        commitment.trajectory_record_id, commitment.public_gameplay_trajectory_id,
        ygo::trajectory::admission_receipt_id_v3(receipt->receipt()),
        ygo::trajectory::candidate_shard_artifact_sha256_v3(shard),
        commitment.episode_envelope_sha256});
    manifest.dataset_semantic_id = ygo::trajectory::dataset_v3::dataset_semantic_id_v3({
        commitment.trajectory_record_id});
    const auto vocabulary = derive_card_vocabulary_v1_from_public_observations_v3({
        envelope->records.front().frame.public_observation});
    require(static_cast<bool>(vocabulary), "V3 model fixture vocabulary failed");
    return {*envelope, *receipt, std::move(manifest), *vocabulary.value};
}

void test_v3_supervision_materialization() {
    const auto fixture = make_admitted_v3_fixture();
    const auto& record = fixture.envelope.records.front();
    const auto logical = ygo::model::project_logical_model_input_v2(
        record.frame.public_observation, record.frame.request.candidates);
    require(static_cast<bool>(logical), "V2 logical model fixture failed");
    const auto encoded = ygo::model::encode_model_input_v2(
        *logical.value, fixture.vocabulary);
    require(static_cast<bool>(encoded), "V2 encoded model fixture failed");
    const auto sample = materialize_phase6_sample_v2(
        fixture.manifest, std::vector<ygo::trajectory::VerifiedAdmissionReceiptV3>{fixture.receipt},
        fixture.envelope, 0, fixture.vocabulary);
    require(static_cast<bool>(sample) && sample.value.has_value(),
            "V2 supervision sample materialization failed");
    require(sample.value->supervision.selected_public_action_key ==
                record.selected_public_action_key &&
                sample.value->supervision.source_public_semantic_decision_id ==
                    record.frame.public_semantic_decision_id &&
                sample.value->supervision.selected_public_action_key ==
                    record.selected_public_action_key,
            "V2 supervision sample lost V3 selection binding");
    std::cout << "BC_SAMPLE_V2_ID=" << sample.value->sample_identity << '\n';
    auto tampered = *logical.value;
    tampered.candidate_routing.front().public_action_key =
        "public_action.v2." + std::string(64, '0');
    require(!ygo::model::materialize_model_supervision_sample_v2(
                 fixture.envelope, fixture.receipt, 0, tampered, *encoded.value,
                 fixture.vocabulary),
            "V2 supervision materialization accepted detached V3 routing input");

    auto tampered_manifest = fixture.manifest;
    tampered_manifest.members.front().candidate_shard_artifact_sha256 =
        std::string(64, '0');
    require(!materialize_phase6_sample_v2(
                 tampered_manifest,
                 std::vector<ygo::trajectory::VerifiedAdmissionReceiptV3>{fixture.receipt},
                 fixture.envelope, 0, fixture.vocabulary),
            "single-sample materialization accepted a detached shard digest");
    require(!materialize_phase6_sample_v2(
                 fixture.manifest, {}, fixture.envelope, 0, fixture.vocabulary),
            "single-sample materialization accepted an unknown receipt");
    require(!materialize_phase6_sample_v2(
                 fixture.manifest,
                 std::vector<ygo::trajectory::VerifiedAdmissionReceiptV3>{
                     fixture.receipt, fixture.receipt},
                 fixture.envelope, 0, fixture.vocabulary),
            "single-sample materialization accepted duplicate receipt identities");

    auto mismatched_member = fixture.manifest;
    mismatched_member.members.front().public_gameplay_trajectory_id =
        "public_gameplay_trajectory.v3." + std::string(64, '0');
    require(!materialize_phase6_sample_v2(
                 mismatched_member,
                 std::vector<ygo::trajectory::VerifiedAdmissionReceiptV3>{fixture.receipt},
                 fixture.envelope, 0, fixture.vocabulary),
            "single-sample materialization accepted a receipt/member mismatch");
}

void test_task7_v3_rejects_unexpected_replay_evidence() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kBaseCommit));
    Task7V3JobOutcome outcome;
    outcome.job = schedule.jobs.front();
    outcome.replay_evidence = ygo::trajectory::RestrictedReplayEvidenceV3{};
    const auto inspection = inspect_task7_v3_job_run(outcome.job, outcome);
    require(!inspection.eligible,
            "Task7 V3 inspection accepted unexpected terminal replay evidence");
    require(std::find(inspection.failed_conditions.begin(), inspection.failed_conditions.end(),
                      "UNEXPECTED_REPLAY_EVIDENCE") != inspection.failed_conditions.end(),
            "Task7 V3 inspection did not classify unexpected replay evidence");
}

void test_exact_schedule_and_identity() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kBaseCommit));
    require(schedule.jobs.size() == 16, "Task7 V3 schedule did not contain 16 jobs");
    require(schedule.environment_contract_id ==
                "ocgforge.episodic_environment.v4",
            "Task7 V3 schedule was not bound to EpisodicEnvironment V3");

    std::size_t index = 0;
    for (const auto seed : std::array<std::uint64_t, 4>{4, 6, 8, 9}) {
        for (const auto placement :
             std::array<ygo::environment::SeatAssignment, 2>{
                 ygo::environment::SeatAssignment::Normal,
                 ygo::environment::SeatAssignment::Mirror}) {
            for (const auto starting_player : std::array<std::uint8_t, 2>{0, 1}) {
                const auto& job = schedule.jobs[index++];
                require(job.root_seed == seed && job.seat_assignment == placement &&
                            job.starting_player == starting_player,
                        "Task7 V3 schedule order changed");
                require(job.environment_contract_id ==
                            "ocgforge.episodic_environment.v4" &&
                            job.teacher_producer_identity ==
                                "ocgforge.policy.teacher_core.v3" &&
                            job.teacher_action_adapter_identity ==
                                "ocgforge.policy.public_action_key.v3" &&
                            job.policy_rng_identity == "ocgforge.no_policy_rng.v1",
                        "Task7 V3 job version binding is incomplete");
            }
        }
    }
    require(index == 16, "Task7 V3 schedule enumeration was incomplete");

    const auto swordsoul = ygo::policy::make_teacher_policy_artifact_v3(
        ygo::teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat = ygo::policy::make_teacher_policy_artifact_v3(
        ygo::teacher::make_salamangreat_profile());
    const auto swordsoul_binding = ygo::policy::make_teacher_policy_binding_v3(
        ygo::teacher::make_swordsoul_tenyi_profile());
    const auto salamangreat_binding = ygo::policy::make_teacher_policy_binding_v3(
        ygo::teacher::make_salamangreat_profile());
    for (const auto& job : schedule.jobs) {
        const bool mirror = job.seat_assignment ==
                            ygo::environment::SeatAssignment::Mirror;
        const auto& seat0_artifact = mirror ? salamangreat : swordsoul;
        const auto& seat1_artifact = mirror ? swordsoul : salamangreat;
        const auto& seat0_binding = mirror ? salamangreat_binding : swordsoul_binding;
        const auto& seat1_binding = mirror ? swordsoul_binding : salamangreat_binding;
        require(job.teacher_policy_artifact_ids[0] == seat0_artifact.policy_artifact_id &&
                    job.teacher_policy_artifact_ids[1] == seat1_artifact.policy_artifact_id &&
                    job.teacher_policy_binding_ids[0] == seat0_binding.teacher_policy_binding_id &&
                    job.teacher_policy_binding_ids[1] == seat1_binding.teacher_policy_binding_id,
                "Task7 V3 Teacher identities do not match production factories");
    }

    const auto bytes = canonical_task7_collection_schedule_bytes_v3(schedule);
    const auto decoded = decode_task7_collection_schedule_v3(bytes);
    require(static_cast<bool>(decoded), "Task7 V3 schedule did not decode");
    require(canonical_task7_collection_schedule_bytes_v3(*decoded.value) == bytes,
            "Task7 V3 schedule was not canonically stable");
    require(task7_collection_schedule_identity_v3(schedule) ==
                task7_collection_schedule_identity_v3(*decoded.value),
            "Task7 V3 schedule identity was not deterministic");
    std::cout << "TASK7_V3_JOB_ID="
              << task7_collection_job_identity_v3(schedule.jobs.front()) << '\n';
    std::cout << "TASK7_V3_SCHEDULE_ID="
              << task7_collection_schedule_identity_v3(schedule) << '\n';
}

void test_fixed_schedule_fails_closed_without_complete_jobs() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kBaseCommit));
    std::size_t calls = 0;
    const auto result = provision_task7_dataset_authority_v3(
        schedule, [&calls](const Task7CollectionJobV3&) {
            ++calls;
            return ygo::policy::TeacherRunnerV4TrajectoryRunResult{};
        });
    require(calls == 16, "Task7 V3 provisioner did not evaluate every fixed job");
    require(!result && result.outcomes.size() == 16,
            "Task7 V3 provisioner issued authority for missing job artifacts");

    auto v1_environment = schedule;
    v1_environment.jobs.front().environment_contract_id =
        "ocgforge.episodic_environment.v2";
    bool rejected = false;
    try {
        (void)canonical_task7_collection_schedule_bytes_v3(v1_environment);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "Task7 V3 schedule accepted a V1 environment contract");
}

void test_job_episode_binding_is_exact() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kBaseCommit));
    const auto& job = schedule.jobs.front();
    const auto config = ygo::environment::CertifiedEnvironmentConfig::canonical_v4();
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV4ContractId);
    spec.root_seed = job.root_seed;
    spec.seat_assignment = job.seat_assignment;
    spec.starting_player = job.starting_player;
    ygo::trajectory::EpisodeEnvelopeV3 envelope;
    envelope.manifest.trusted_trajectory_contract_id =
        std::string(ygo::trajectory::kTrustedTrajectoryV3ContractId);
    envelope.manifest.episodic_environment_contract_id =
        std::string(ygo::environment::kEpisodicEnvironmentV4ContractId);
    envelope.manifest.environment_identity_input =
        ygo::environment::canonical_environment_identity_bytes(config);
    envelope.manifest.environment_semantic_id = ygo::environment::environment_semantic_id(config);
    envelope.manifest.episode_identity_input =
        ygo::environment::canonical_episode_identity_bytes(config, spec);
    envelope.manifest.episode_semantic_id = ygo::environment::episode_semantic_id(config, spec);
    envelope.manifest.policy_provenance = make_task7_v3_policy_provenance(job);
    std::string error;
    require(validate_task7_v3_job_episode_binding(job, envelope, &error),
            "Task7 V3 job/episode identity fixture did not validate: " + error);
    auto wrong_job = job;
    wrong_job.root_seed += 1;
    require(!validate_task7_v3_job_episode_binding(wrong_job, envelope, &error),
            "Task7 V3 accepted an episode under the wrong job seed");

    auto extra_artifact = envelope;
    extra_artifact.manifest.policy_provenance.policy_artifacts.push_back(
        ygo::policy::make_teacher_policy_artifact(
            ygo::teacher::make_swordsoul_tenyi_profile()));
    require(!validate_task7_v3_job_episode_binding(job, extra_artifact, &error),
            "Task7 V3 accepted extra Teacher artifact provenance");

    auto extra_assignment = envelope;
    auto assignment = extra_assignment.manifest.policy_provenance.participant_assignments.front();
    assignment.assignment_epoch = 1;
    assignment.effective_from_decision_index = 1;
    assignment.participant_policy_assignment_id =
        ygo::trajectory::compute_participant_policy_assignment_id(assignment);
    extra_assignment.manifest.policy_provenance.participant_assignments.push_back(assignment);
    require(!validate_task7_v3_job_episode_binding(job, extra_assignment, &error),
            "Task7 V3 accepted extra Teacher assignment provenance");
}

void test_authority_closure_rejects_detached_values() {
    const auto schedule = make_task7_collection_schedule_v3(std::string(kBaseCommit));
    const auto vocabulary = ygo::model::CardVocabularyV1::from_ascending_passcodes({100});
    require(static_cast<bool>(vocabulary), "Task7 V3 detached-authority vocabulary fixture failed");
    Task7V3DatasetAuthority detached{
        schedule, {}, ygo::trajectory::dataset_v3::DatasetManifestV3{}, TrainingDatasetSplitV1{},
        *vocabulary.value};
    std::string error;
    require(!validate_task7_v3_authority(detached, &error),
            "Task7 V3 detached authority passed closure validation");
}

void test_split_v1_and_public_vocabulary_v1() {
    std::vector<std::string> episode_ids;
    bool has_train = false;
    bool has_validation = false;
    bool has_test = false;
    for (std::uint32_t value = 1; episode_ids.size() < 16 && value < 100000; ++value) {
        std::ostringstream stream;
        stream << std::hex << std::setw(64) << std::setfill('0') << value;
        const auto id = stream.str();
        const auto partition = phase6_partition_for_episode(id);
        if (!partition.has_value()) {
            continue;
        }
        if (*partition == Phase6DatasetPartition::Train) has_train = true;
        if (*partition == Phase6DatasetPartition::Validation) has_validation = true;
        if (*partition == Phase6DatasetPartition::Test) has_test = true;
        episode_ids.push_back(id);
    }
    require(has_train && has_validation && has_test && episode_ids.size() == 16,
            "Task7 V3 split fixture did not cover all partitions");

    const auto split = derive_training_dataset_split_v1_from_v3(
        std::string(64, 'a'), episode_ids);
    require(static_cast<bool>(split), "Task7 V3 split derivation failed");
    require(!split.value->train_episode_ids.empty() &&
                !split.value->validation_episode_ids.empty() &&
                !split.value->test_episode_ids.empty(),
            "Task7 V3 split did not fail/produce the required nonempty partitions");
    auto duplicate_episode_ids = episode_ids;
    duplicate_episode_ids[1] = duplicate_episode_ids[0];
    require(!derive_training_dataset_split_v1_from_v3(
                 std::string(64, 'a'), duplicate_episode_ids),
            "Task7 V3 split accepted duplicate episode IDs");

    ygo::observation::PlayerObservation source;
    source.perspective_player = 0;
    source.match_context.perspective_player = 0;
    source.match_context.own_deck.known = true;
    source.match_context.own_deck.main_deck = {200, 100};
    ygo::observation::VisibleGameEvent event;
    event.event_index = 0;
    event.public_passcode = 300;
    source.visible_events.push_back(event);
    const auto observation = ygo::environment::project_public_observation(source);
    const auto vocabulary = derive_card_vocabulary_v1_from_public_observations_v3({observation});
    require(static_cast<bool>(vocabulary), "Task7 V3 public vocabulary derivation failed");
    require(vocabulary.value->ascending_passcodes() ==
                std::vector<std::uint32_t>{100, 200, 300},
            "Task7 V3 vocabulary was not public, unique, and ascending");
    std::cout << "TASK7_V3_SPLIT_ID=" << split.value->split_identity << '\n'
              << "TASK7_V1_VOCABULARY_ID=" << vocabulary.value->identity() << '\n';
}

}  // namespace

int main() {
    try {
        test_exact_schedule_and_identity();
        test_fixed_schedule_fails_closed_without_complete_jobs();
        test_job_episode_binding_is_exact();
        test_authority_closure_rejects_detached_values();
        test_split_v1_and_public_vocabulary_v1();
        test_model_v2_select_unselect_contract();
        test_v3_supervision_materialization();
        test_task7_v3_rejects_unexpected_replay_evidence();
        test_task7_v3_generation_matrix();
        std::cout << "phase6_task7_dataset_authority_provisioning_v3_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "phase6_task7_dataset_authority_provisioning_v3_test: "
                  << error.what() << '\n';
        return 1;
    }
}
