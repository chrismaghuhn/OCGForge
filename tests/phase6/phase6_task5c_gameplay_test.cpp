#include "ygo/phase6/task5c_gameplay.hpp"

#include <cstdlib>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/production.hpp"
#include "ygo/trajectory/codec.hpp"

namespace {

using namespace ygo::phase6::task5c;
using ygo::environment::PublicActionKeyInput;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::string key(const std::uint32_t source_index) {
    PublicActionKeyInput input;
    input.action_kind = "card_selection";
    input.source_index = source_index;
    return ygo::environment::public_action_key(input);
}

constexpr std::string_view kNeuralProducer =
    "ocgforge.phase6.task5c.checkpoint_policy.v1";
constexpr std::string_view kNeuralInferenceAdapter =
    "ocgforge.phase6.task5c.checkpoint_inference.v1";

template <typename T>
void require_result(const T& result, const std::string& message) {
    require(static_cast<bool>(result), message +
                                         (result.error.has_value()
                                              ? ": " + result.error->message
                                              : std::string()));
}

ygo::trajectory::ProvenanceRegistration registration(
    const ygo::trajectory::ProvenanceKind kind, const std::string_view identity) {
    ygo::trajectory::ProvenanceRegistration result;
    result.kind = kind;
    result.identity = std::string(identity);
    return result;
}

ygo::trajectory::ProvenanceResolver resolver_for(
    const std::string& checkpoint_identity) {
    using ygo::trajectory::ProvenanceKind;
    std::vector<ygo::trajectory::ProvenanceRegistration> registrations;
    registrations.push_back(registration(
        ProvenanceKind::PolicyRngContract, ygo::trajectory::kNoPolicyRngContractId));
    registrations.push_back(registration(ProvenanceKind::ProducerImplementation,
                                          kNeuralProducer));
    registrations.push_back(registration(ProvenanceKind::InferenceAdapter,
                                          kNeuralInferenceAdapter));
    registrations.push_back(registration(
        ProvenanceKind::InferenceAdapter, ygo::policy::kDirectExecutionInferenceAdapterIdentity));
    registrations.push_back(registration(
        ProvenanceKind::ObservationAdapter, ygo::policy::kPublicObservationAdapterIdentity));
    registrations.push_back(registration(
        ProvenanceKind::ActionAdapter, ygo::policy::kPublicActionKeyAdapterIdentity));
    auto sampling = registration(
        ProvenanceKind::SamplingContract,
        "ocgforge.phase6.bc.inference_tiebreak.v1");
    sampling.sampling_capabilities = ygo::trajectory::SamplingContractCapabilities{true, true};
    registrations.push_back(std::move(sampling));
    registrations.push_back(registration(ProvenanceKind::ModelCheckpointArtifact,
                                          checkpoint_identity));
    registrations.push_back(registration(
        ProvenanceKind::ProducerImplementation, ygo::policy::kTeacherProducerImplementationIdentity));
    auto teacher_sampling = registration(
        ProvenanceKind::SamplingContract,
        ygo::policy::kTeacherDeterministicSamplingContractIdentity);
    teacher_sampling.sampling_capabilities =
        ygo::trajectory::SamplingContractCapabilities{true, true};
    registrations.push_back(std::move(teacher_sampling));
    registrations.push_back(registration(
        ProvenanceKind::ArtifactMetadataArtifact,
        "ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c"));
    registrations.push_back(registration(
        ProvenanceKind::ArtifactMetadataArtifact,
        "ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56"));
    return ygo::trajectory::ProvenanceResolver(std::move(registrations));
}

ygo::trajectory::PolicyArtifact evaluated_artifact(
    const std::string& checkpoint_identity) {
    ygo::trajectory::PolicyArtifact artifact;
    artifact.policy_kind = ygo::trajectory::PolicyKind::NeuralCheckpoint;
    artifact.producer_implementation_identity = std::string(kNeuralProducer);
    artifact.inference_adapter_identity = std::string(kNeuralInferenceAdapter);
    artifact.observation_adapter_identity =
        std::string(ygo::policy::kPublicObservationAdapterIdentity);
    artifact.action_adapter_identity =
        std::string(ygo::policy::kPublicActionKeyAdapterIdentity);
    artifact.sampling_contract_identity = "ocgforge.phase6.bc.inference_tiebreak.v1";
    artifact.policy_rng_contract_identity = ygo::trajectory::kNoPolicyRngContractId;
    artifact.model_checkpoint_identity = checkpoint_identity;
    artifact.search_contract_identity.reset();
    artifact.demonstration_source_identity.reset();
    artifact.artifact_metadata_identity.reset();
    artifact.policy_artifact_id = ygo::trajectory::compute_policy_artifact_id(artifact);
    return artifact;
}

struct EvaluatorFixture final {
    EvaluationContextV1 context;
    FrozenGameplayEvaluatorConfigV1 config;
};

ygo::model::CardVocabularyV1 fixture_vocabulary() {
#ifndef YGO_M0_CARD_DATA_TSV
    throw std::runtime_error("T5C fixture lacks the generated card-data path");
#else
    std::ifstream input(YGO_M0_CARD_DATA_TSV);
    require(static_cast<bool>(input), "T5C fixture could not open generated card data");
    std::vector<std::uint32_t> passcodes;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line.front() == '#') continue;
        const auto delimiter = line.find('|');
        require(delimiter != std::string::npos, "T5C card-data row is malformed");
        passcodes.push_back(static_cast<std::uint32_t>(std::stoul(line.substr(0, delimiter))));
    }
    std::sort(passcodes.begin(), passcodes.end());
    passcodes.erase(std::unique(passcodes.begin(), passcodes.end()), passcodes.end());
    const auto result = ygo::model::CardVocabularyV1::from_ascending_passcodes(std::move(passcodes));
    require(static_cast<bool>(result), "T5C fixture vocabulary was not canonical");
    return *result.value;
#endif
}

EvaluatorFixture make_fixture(
    const CheckpointInferenceProviderV1& provider =
        [](const InferenceRequestV1& request,
           const ygo::model::LogicalModelInputV1&, const ygo::model::EncodedModelInputV1& input) {
            return make_inference_response(
                request,
                std::vector<std::string>(input.candidate_count(), "3f800000"),
                input.routing_keys);
        }) {
    const auto context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd");
    const auto artifact = evaluated_artifact(context.checkpoint_identity);
    const auto vocabulary = fixture_vocabulary();
    ygo::environment::RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 1;
    control.cancellation.reason = "ADMINISTRATIVE_CANCEL";
    control.cancellation.source = "phase6-task5c-test";
    FrozenGameplayEvaluatorConfigV1 config{
        context,
        ygo::environment::CertifiedEnvironmentConfig::canonical(),
        artifact,
        vocabulary,
        provider,
        resolver_for(context.checkpoint_identity),
        control};
    return {context, std::move(config)};
}

CheckpointBoundPolicyV1 make_direct_policy(
    const CheckpointInferenceProviderV1& provider) {
    auto result = create_checkpoint_bound_policy(
        std::string(kSmokeCheckpointIdentity), 0,
        "participant_policy_assignment.v1." + std::string(64, 'a'),
        "policy_artifact.v1." + std::string(64, 'b'), fixture_vocabulary(), provider);
    require_result(result, "direct checkpoint-policy fixture construction failed");
    return std::move(*result.value);
}

ygo::environment::DecisionFrame first_frame(const std::uint64_t seed,
                                            const std::uint64_t action_budget = 2) {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "T5C paired-world environment construction failed");
    auto environment = std::move(
        std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
    ygo::environment::EpisodeSpec spec;
    spec.root_seed = seed;
    ygo::environment::RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = action_budget;
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "T5C paired-world reset failed");
    const auto* frame = std::get_if<ygo::environment::DecisionFrame>(
        &std::get<ygo::environment::ResetAccepted>(reset).next);
    require(frame != nullptr, "T5C paired-world reset did not publish a frame");
    return *frame;
}

void test_response_codec_rejects_wrong_selection_and_noncanonical_json() {
    InferenceRequestV1 request;
    request.checkpoint_identity =
        "phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327";
    request.model_input_identity = "model_input.v1." + std::string(64, '1');
    request.ordered_candidate_domain_identity = std::string(64, '2');
    request.public_semantic_decision_id = std::string(64, '3');
    request.perspective_player = 0;
    request.decision_index = 7;
    request.request_identity = inference_request_identity(request);
    require(request.request_identity ==
                "phase6_inference_request.v1.9735774b99d66dc0b802572a0e522ae1838e3a495240278085daec07480046b0",
            "T5C request identity diverged from the accepted Task4 inference codec");
    const auto response = make_inference_response(
        request, {"3f800000", "3f800000"}, {key(0), key(1)});
    require(response.value.has_value(), "response fixture was not constructed");

    auto wrong_selection = *response.value;
    wrong_selection.selected_candidate_ordinal = 1;
    wrong_selection.selected_public_action_key = key(1);
    require(!validate_inference_response(request, wrong_selection),
            "response with a changed selection envelope was accepted");

    const auto context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd");
    auto replay = ReplayAdmissionSummaryV1{
        std::string(kReplayAdmissionSummarySchemaId), context.evaluation_identity,
        evaluation_job_identity(context.jobs.front()), std::nullopt, std::nullopt,
        ReplayAdmissionStatus::NotRun, ReplayAdmissionStatus::Quarantined,
        GameplayFailureStage::Inference, std::string("INFERENCE_FAILURE"), false};
    const auto encoded = encode_replay_admission_summary_json(replay);
    require(decode_replay_admission_summary_json(encoded).failure_code ==
                replay.failure_code,
            "replay/admission summary did not round-trip canonically");
    auto noncanonical = encoded;
    noncanonical.insert(noncanonical.find('{') + 1, " ");
    bool rejected = false;
    try {
        (void)decode_replay_admission_summary_json(noncanonical);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "noncanonical T5C JSON was accepted");

    GameplayJobResultV1 result;
    result.evaluation_identity = context.evaluation_identity;
    result.evaluation_job_identity = evaluation_job_identity(context.jobs.front());
    result.checkpoint_identity = context.checkpoint_identity;
    result.status = GameplayJobStatus::Failed;
    result.failure_stage = GameplayFailureStage::Inference;
    result.failure_code = "INFERENCE_FAILURE";
    result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
    const auto result_json = encode_gameplay_job_result_json(result);
    require(decode_gameplay_job_result_json(result_json).failure_code ==
                result.failure_code,
            "gameplay job result did not round-trip canonically");
    auto duplicate = result_json;
    const auto duplicate_position = duplicate.find(",\"schema_id\":");
    require(duplicate_position != std::string::npos,
            "gameplay result fixture did not contain its schema field");
    duplicate.insert(duplicate_position + 1, "\"schema_id\":\"duplicate\",");
    rejected = false;
    try {
        (void)decode_gameplay_job_result_json(duplicate);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "duplicate JSON object keys were accepted");

    auto unknown = result_json;
    const auto object_end = unknown.rfind('}');
    require(object_end != std::string::npos, "gameplay result JSON has no object terminator");
    unknown.insert(object_end, ",\"unknown\":0");
    rejected = false;
    try {
        (void)decode_gameplay_job_result_json(unknown);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "unknown gameplay-result JSON fields were accepted");
}

void test_failure_accounting_preserves_replay_and_admission_failures() {
    const auto context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd");
    const auto job_id = evaluation_job_identity(context.jobs.front());
    ReplayAdmissionSummaryV1 replay;
    replay.evaluation_identity = context.evaluation_identity;
    replay.evaluation_job_identity = job_id;
    replay.trajectory_record_id = "trajectory_record.v1." + std::string(64, 'a');
    replay.public_gameplay_trajectory_id =
        "public_gameplay_trajectory.v1." + std::string(64, 'b');
    replay.replay_status = ReplayAdmissionStatus::Passed;
    replay.admission_status = ReplayAdmissionStatus::Failed;
    replay.failure_stage = GameplayFailureStage::Admission;
    replay.failure_code = "ADMISSION_FAILURE";
    GameplayJobResultV1 result;
    result.evaluation_identity = context.evaluation_identity;
    result.evaluation_job_identity = job_id;
    result.checkpoint_identity = context.checkpoint_identity;
    result.status = GameplayJobStatus::Failed;
    result.started = true;
    result.terminal_observed = true;
    result.terminal_outcome = TerminalOutcomeV1{true, std::uint8_t{0}, std::uint8_t{1}};
    result.trajectory_record_id = replay.trajectory_record_id;
    result.public_gameplay_trajectory_id = replay.public_gameplay_trajectory_id;
    result.failure_stage = GameplayFailureStage::Admission;
    result.failure_code = "ADMISSION_FAILURE";
    result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
    require(validate_replay_admission_summary(replay) &&
                validate_gameplay_job_result(result),
            "admission-failure evidence was not independently valid");

    GameplaySummaryV1 summary;
    summary.evaluation_identity = context.evaluation_identity;
    summary.evaluation_corpus_identity = context.evaluation_corpus_identity;
    summary.evaluation_job_manifest_identity = context.evaluation_job_manifest_identity;
    summary.checkpoint_identity = context.checkpoint_identity;
    summary.gameplay_job_result_identities = {gameplay_job_result_identity(result)};
    summary.scheduled_job_count = 1;
    summary.started_job_count = 1;
    summary.failed_job_count = 1;
    summary.admission_failure_count = 1;
    require(validate_gameplay_summary(summary),
            "summary dropped an explicit admission failure from accounting");

    auto replay_failure = replay;
    replay_failure.admission_status = ReplayAdmissionStatus::NotRun;
    replay_failure.replay_status = ReplayAdmissionStatus::Failed;
    replay_failure.failure_stage = GameplayFailureStage::Replay;
    replay_failure.failure_code = "REPLAY_FAILURE";
    require(validate_replay_admission_summary(replay_failure),
            "replay failure was not independently valid");

    auto quarantined = replay_failure;
    quarantined.replay_status = ReplayAdmissionStatus::NotRun;
    quarantined.admission_status = ReplayAdmissionStatus::Quarantined;
    quarantined.failure_stage = GameplayFailureStage::Environment;
    quarantined.failure_code = "STEP_REJECTED";
    require(validate_replay_admission_summary(quarantined),
            "quarantine result was not independently valid");
}

void test_score_bits_are_exact_and_finite() {
    InferenceRequestV1 request;
    request.checkpoint_identity = std::string(kSmokeCheckpointIdentity);
    request.model_input_identity = "model_input.v1." + std::string(64, '1');
    request.ordered_candidate_domain_identity = std::string(64, '2');
    request.perspective_player = 0;
    request.request_identity = inference_request_identity(request);
    const auto positive = make_inference_response(
        request, {"3f800000", "40000000"}, {key(0), key(1)});
    require(positive.value.has_value() && positive.value->selected_candidate_ordinal == 1,
            "binary32 score ordering was not decoded from exact bits");
    const auto negative_zero = make_inference_response(
        request, {"80000000", "00000000"}, {key(0), key(1)});
    require(negative_zero.value.has_value(), "signed-zero score bits were rejected");
    const auto nan = make_inference_response(
        request, {"7fc00000", "00000000"}, {key(0), key(1)});
    require(!nan.value.has_value(), "NaN score bits were accepted");
}

void test_checkpoint_policy_rejects_wrong_checkpoint_and_candidate_domain_response() {
    const auto frame = first_frame(2);
    const CheckpointInferenceProviderV1 wrong_checkpoint =
        [](const InferenceRequestV1& request, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1& input) {
            auto response = make_inference_response(
                request, std::vector<std::string>(input.routing_keys.size(), "3f800000"),
                input.routing_keys);
            require(response.value.has_value(), "wrong-checkpoint response fixture failed");
            response.value->checkpoint_identity =
                "phase6_checkpoint.v1." + std::string(64, 'e');
            response.value->response_identity = inference_response_identity(*response.value);
            return response;
        };
    auto policy = make_direct_policy(wrong_checkpoint);
    require(!policy.select(frame), "wrong-checkpoint response was accepted by policy");

    const CheckpointInferenceProviderV1 wrong_domain =
        [](const InferenceRequestV1& request, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1& input) {
            auto response = make_inference_response(
                request, std::vector<std::string>(input.routing_keys.size(), "3f800000"),
                input.routing_keys);
            require(response.value.has_value(), "wrong-domain response fixture failed");
            response.value->ordered_candidate_domain_identity = std::string(64, 'e');
            response.value->response_identity = inference_response_identity(*response.value);
            return response;
        };
    auto second_policy = make_direct_policy(wrong_domain);
    require(!second_policy.select(frame), "wrong-domain response was accepted by policy");
}

void test_checkpoint_policy_keeps_public_requests_equal_across_paired_public_worlds() {
    const auto frame_a = first_frame(2);
    const auto frame_b = first_frame(2);
    std::vector<std::string> request_ids;
    const CheckpointInferenceProviderV1 provider =
        [&request_ids](const InferenceRequestV1& request,
                       const ygo::model::LogicalModelInputV1&,
                       const ygo::model::EncodedModelInputV1& input) {
            request_ids.push_back(request.request_identity);
            return make_inference_response(
                request,
                std::vector<std::string>(input.routing_keys.size(), "3f800000"),
                input.routing_keys);
        };
    auto policy_a = make_direct_policy(provider);
    auto policy_b = make_direct_policy(provider);
    const auto selected_a = policy_a.select(frame_a);
    const auto selected_b = policy_b.select(frame_b);
    require(selected_a && selected_b && request_ids.size() == 2 &&
                request_ids[0] == request_ids[1] &&
                selected_a.value->public_action_key == selected_b.value->public_action_key,
            "paired public worlds changed the checkpoint-bound request or selection");
}

void test_stale_response_is_rejected_without_a_second_selection() {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "T5C stale-response environment construction failed");
    auto environment = std::move(
        std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
    ygo::environment::EpisodeSpec spec;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 3;
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "T5C stale-response reset failed");
    const auto* initial_frame = std::get_if<ygo::environment::DecisionFrame>(
        &std::get<ygo::environment::ResetAccepted>(reset).next);
    require(initial_frame != nullptr, "T5C stale-response reset did not publish a frame");
    const auto frame_a = *initial_frame;
    std::optional<InferenceResponseV1> first_response;
    const CheckpointInferenceProviderV1 provider =
        [&first_response](const InferenceRequestV1& request,
                          const ygo::model::LogicalModelInputV1&,
                          const ygo::model::EncodedModelInputV1& input) {
            if (!first_response.has_value()) {
                auto result = make_inference_response(
                    request,
                    std::vector<std::string>(input.routing_keys.size(), "3f800000"),
                    input.routing_keys);
                require(result.value.has_value(), "stale-response first fixture failed");
                first_response = *result.value;
                return result;
            }
            return InferenceResponseCreateResult{first_response, std::nullopt};
        };
    auto policy = make_direct_policy(provider);
    const auto first = policy.select(frame_a);
    require(static_cast<bool>(first), "stale-response first selection failed");
    ygo::environment::ActionSelection action;
    action.contract_id = frame_a.contract_id;
    action.episode_semantic_id = frame_a.episode_semantic_id;
    action.public_semantic_decision_id = frame_a.public_semantic_decision_id;
    action.submission_token = frame_a.submission_token;
    action.public_action_key = first.value->public_action_key;
    const auto stepped = environment->step(action);
    require(std::holds_alternative<ygo::environment::StepAccepted>(stepped),
            "T5C stale-response first step failed");
    require(policy.commit(std::get<ygo::environment::StepAccepted>(stepped).transition),
            "T5C stale-response first commit failed");
    const auto* next_frame = std::get_if<ygo::environment::DecisionFrame>(
        &std::get<ygo::environment::StepAccepted>(stepped).next);
    require(next_frame != nullptr, "T5C stale-response fixture did not reach a second frame");
    const auto frame_b = *next_frame;
    require(!policy.select(frame_b),
            "a response bound to an earlier request was accepted");
}

void test_wrong_checkpoint_is_rejected_before_gameplay() {
    auto fixture = make_fixture();
    fixture.config.evaluated_policy_artifact.model_checkpoint_identity =
        "phase6_checkpoint.v1." + std::string(64, 'e');
    fixture.config.evaluated_policy_artifact.policy_artifact_id =
        ygo::trajectory::compute_policy_artifact_id(
            fixture.config.evaluated_policy_artifact);
    auto created = create_frozen_gameplay_evaluator(std::move(fixture.config));
    require(!created && created.error.has_value(),
            "wrong checkpoint binding constructed a gameplay evaluator");
}

void test_frozen_evaluator_runs_all_jobs_without_fallback_and_admits_interruptions() {
    auto fixture = make_fixture();
    auto created = create_frozen_gameplay_evaluator(std::move(fixture.config));
    require_result(created, "frozen evaluator construction failed");
    const auto result = created.value->run();
    require(result.job_results.size() == 8 && result.replay_admission_summaries.size() == 8,
            "frozen evaluator did not preserve all scheduled jobs");
    require(result.summary.scheduled_job_count == 8 &&
                result.summary.started_job_count == 8 &&
                result.summary.interrupted_job_count == 8 &&
                result.summary.fallback_assisted_job_count == 0,
            "frozen evaluator did not account for bounded interrupted jobs: scheduled=" +
                std::to_string(result.summary.scheduled_job_count) +
                " started=" + std::to_string(result.summary.started_job_count) +
                " interrupted=" + std::to_string(result.summary.interrupted_job_count) +
                " failed=" + std::to_string(result.summary.failed_job_count) +
                " quarantined=" + std::to_string(result.summary.quarantined_job_count));
    for (std::size_t index = 0; index < result.job_results.size(); ++index) {
        require(result.job_results[index].evaluation_job_identity ==
                    evaluation_job_identity(
                        make_implementation_acceptance_context(
                            "434066289a14d0dae67222e0486f4df8538950bd")
                            .jobs[index]),
                "frozen evaluator changed manifest job order");
        require(result.job_results[index].status == GameplayJobStatus::Interrupted,
                "bounded job was not reported as interrupted");
        require(result.replay_admission_summaries[index].replay_status ==
                    ReplayAdmissionStatus::Passed &&
                    result.replay_admission_summaries[index].admission_status ==
                        ReplayAdmissionStatus::Passed,
                "bounded interruption did not use replay/admission authority");
    }
    const auto job_ids = [&fixture] {
        std::vector<std::string> ids;
        ids.reserve(fixture.context.jobs.size());
        for (const auto& value : fixture.context.jobs) ids.push_back(evaluation_job_identity(value));
        return ids;
    }();
    const auto stream = encode_gameplay_job_results_jsonl(result.job_results, job_ids);
    require(decode_gameplay_job_results_jsonl(stream, job_ids).size() == 8,
            "gameplay result JSONL did not strict round-trip");
    bool malformed_rejected = false;
    try {
        (void)decode_gameplay_job_results_jsonl(stream + "\n", job_ids);
    } catch (const std::exception&) {
        malformed_rejected = true;
    }
    require(malformed_rejected, "blank gameplay JSONL records were accepted");
    auto reordered_results = result.job_results;
    std::swap(reordered_results[0], reordered_results[1]);
    bool rejected = false;
    try {
        (void)encode_gameplay_job_results_jsonl(reordered_results, job_ids);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "gameplay result JSONL reordered the frozen job schedule");
}

void test_inference_failure_quarantines_without_policy_fallback() {
    const CheckpointInferenceProviderV1 failing_provider =
        [](const InferenceRequestV1&, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1&) {
            return InferenceResponseCreateResult{
                std::nullopt, std::string("TEST_INFERENCE_FAILURE")};
        };
    auto fixture = make_fixture(failing_provider);
    auto created = create_frozen_gameplay_evaluator(std::move(fixture.config));
    require_result(created, "failing-provider evaluator construction failed");
    const auto result = created.value->run();
    require(result.summary.failed_job_count == 4 &&
                result.summary.interrupted_job_count == 4 &&
                result.summary.fallback_assisted_job_count == 0 &&
                result.summary.inference_failure_count == 4,
            "inference failures were not converted to a fallback: failed=" +
                std::to_string(result.summary.failed_job_count) +
                " quarantined=" + std::to_string(result.summary.quarantined_job_count) +
                " inference=" + std::to_string(result.summary.inference_failure_count) +
                " first_stage=" + (result.job_results[0].failure_stage.has_value()
                                         ? std::string(gameplay_failure_stage_name(*result.job_results[0].failure_stage))
                                         : std::string("none")) +
                " first_code=" + result.job_results[0].failure_code.value_or("none"));
    for (std::size_t index = 0; index < result.job_results.size(); ++index) {
        const auto& value = result.job_results[index];
        if (index % 2 == 0) {
            require(value.status == GameplayJobStatus::Failed &&
                        value.failure_stage == GameplayFailureStage::Inference,
                    "inference failure did not remain a typed failure");
        } else {
            require(value.status == GameplayJobStatus::Interrupted,
                    "a job that did not reach the evaluated seat was not interrupted");
        }
    }
}

void test_terminal_result_and_summary_codecs_are_strict() {
    const auto context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd");
    const auto job_id = evaluation_job_identity(context.jobs.front());
    ReplayAdmissionSummaryV1 replay;
    replay.evaluation_identity = context.evaluation_identity;
    replay.evaluation_job_identity = job_id;
    replay.trajectory_record_id = "trajectory_record.v1." + std::string(64, 'a');
    replay.public_gameplay_trajectory_id =
        "public_gameplay_trajectory.v1." + std::string(64, 'b');
    replay.replay_status = ReplayAdmissionStatus::Passed;
    replay.admission_status = ReplayAdmissionStatus::Passed;
    GameplayJobResultV1 result;
    result.evaluation_identity = context.evaluation_identity;
    result.evaluation_job_identity = job_id;
    result.checkpoint_identity = context.checkpoint_identity;
    result.status = GameplayJobStatus::TrustedWin;
    result.terminal_observed = true;
    result.terminal_outcome = TerminalOutcomeV1{true, std::uint8_t{0}, std::uint8_t{1}};
    result.trajectory_record_id = replay.trajectory_record_id;
    result.public_gameplay_trajectory_id = replay.public_gameplay_trajectory_id;
    result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
    require(validate_gameplay_job_result(result), "trusted terminal result was rejected");

    GameplaySummaryV1 summary;
    summary.evaluation_identity = context.evaluation_identity;
    summary.evaluation_corpus_identity = context.evaluation_corpus_identity;
    summary.evaluation_job_manifest_identity = context.evaluation_job_manifest_identity;
    summary.checkpoint_identity = context.checkpoint_identity;
    summary.gameplay_job_result_identities = {gameplay_job_result_identity(result)};
    summary.scheduled_job_count = 1;
    summary.started_job_count = 1;
    summary.completed_terminal_job_count = 1;
    summary.trusted_win_count = 1;
    summary.wilson_numerator = 1;
    summary.wilson_denominator = 1;
    summary.wilson_interval_status = "AVAILABLE";
    require(validate_gameplay_summary(summary), "terminal summary was rejected");
    require(decode_gameplay_summary_json(encode_gameplay_summary_json(summary)).wilson_numerator == 1,
            "gameplay summary did not round-trip");
}

void test_frozen_context_has_the_exact_eight_job_schedule() {
    const auto context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd",
        "phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327");
    std::string error;
    require(validate_evaluation_context(context, &error),
            "frozen T5C context was rejected: " + error);
    require(context.jobs.size() == 8, "frozen T5C context did not contain eight jobs");
    require(context.jobs.front().deterministic_seed == 1 &&
                context.jobs.front().starting_player == 0,
            "frozen T5C context changed the first schedule coordinate");
    require(context.jobs.back().deterministic_seed == 2 &&
                context.jobs.back().starting_player == 1,
            "frozen T5C context changed the last schedule coordinate");
    const std::vector<std::string> expected_ids = {
        "phase6_evaluation_job.v1.686ed61ff5875de11e61c1431425bac2f6ff023cb853d31b4e1b5ff0f51d4cf8",
        "phase6_evaluation_job.v1.61403d67697dcb34ad842c38982a3853f5b7299e334984be3eb195810601a945",
        "phase6_evaluation_job.v1.067df496c93ee0747bc0f2cca50a69b8d33eb39249f3bf6f26836e3ba0835490",
        "phase6_evaluation_job.v1.8e441ac420915dffa0a9dd3967694598c4f582401f652eab8a8376acb351af2f",
        "phase6_evaluation_job.v1.e56eb71abaecb34750367e87f8ab66ef8449a12e806a1a21efdb5d25dd4f1331",
        "phase6_evaluation_job.v1.e239d41d7cfff9a32f46830870c7240c5cc5792eb9e4c8043018299bf176e91c",
        "phase6_evaluation_job.v1.064c3e4cf1b9d7262e6aadce8c50cf271be9988d8e806403af0458d28077c648",
        "phase6_evaluation_job.v1.d23ca499cfb2a8455f5a78e660b8ef30efea3e0ccde4d035de0cc4c17be46700"};
    for (std::size_t index = 0; index < expected_ids.size(); ++index) {
        require(evaluation_job_identity(context.jobs[index]) == expected_ids[index],
                "T5C C++ job mirror diverged from the accepted T5A identity codec");
    }
    require(context.evaluation_identity ==
                "phase6_evaluation.v1.fe35e023849390b8f70bca0a8e6acaedf5176dd86d689567c85b018510a6d10d" &&
                context.evaluation_corpus_identity ==
                    "phase6_evaluation_corpus.v1.4c7c3bfb84ff64c834608de313879696574758183b84caa42f05e00fee1d1701" &&
                context.evaluation_job_manifest_identity ==
                    "phase6_evaluation_job_manifest.v1.a6935c8fc352c70300c02adb4492a961aa17f4ae44a525d709af076467d6992a",
            "T5C aggregate identities diverged from the accepted T5A codecs");
}

void test_inference_response_identity_is_bound_to_request() {
    InferenceRequestV1 request;
    request.checkpoint_identity =
        "phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327";
    request.model_input_identity = "model_input.v1." + std::string(64, '1');
    request.ordered_candidate_domain_identity = std::string(64, '2');
    request.public_semantic_decision_id = std::string(64, '3');
    request.perspective_player = 0;
    request.decision_index = 7;
    request.request_identity = inference_request_identity(request);

    const auto response = make_inference_response(
        request, {"3f800000", "3f800000"},
        {key(0), key(1)});
    require(response.value.has_value(), "valid T5C inference response was not constructed");
    require(validate_inference_response(request, *response.value, nullptr),
            "valid T5C inference response failed validation");

    auto stale = *response.value;
    stale.request_identity = "phase6_inference_request.v1." + std::string(64, '4');
    require(!validate_inference_response(request, stale, nullptr),
            "stale T5C inference response was accepted");
}

}  // namespace

void determinism_probe() {
    auto fixture = make_fixture();
    auto created = create_frozen_gameplay_evaluator(std::move(fixture.config));
    require_result(created, "fresh-process determinism evaluator construction failed");
    const auto result = created.value->run();
    std::vector<std::string> job_ids;
    for (const auto& job : fixture.context.jobs) job_ids.push_back(evaluation_job_identity(job));
    std::cout << encode_gameplay_job_results_jsonl(result.job_results, job_ids);
    std::cout << encode_gameplay_summary_json(result.summary);
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--determinism") {
            determinism_probe();
            return EXIT_SUCCESS;
        }
        test_frozen_context_has_the_exact_eight_job_schedule();
        test_inference_response_identity_is_bound_to_request();
        test_response_codec_rejects_wrong_selection_and_noncanonical_json();
        test_failure_accounting_preserves_replay_and_admission_failures();
        test_score_bits_are_exact_and_finite();
        test_checkpoint_policy_rejects_wrong_checkpoint_and_candidate_domain_response();
        test_checkpoint_policy_keeps_public_requests_equal_across_paired_public_worlds();
        test_stale_response_is_rejected_without_a_second_selection();
        test_wrong_checkpoint_is_rejected_before_gameplay();
        test_frozen_evaluator_runs_all_jobs_without_fallback_and_admits_interruptions();
        test_inference_failure_quarantines_without_policy_fallback();
        test_terminal_result_and_summary_codecs_are_strict();
        std::cout << "phase6_task5c_gameplay_tests=passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
