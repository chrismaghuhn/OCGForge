#include "ygo/phase6/task5c_gameplay.hpp"

#include <cstdlib>
#include <array>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/production.hpp"
#include "episodic_environment_test_access.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/player_observation.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/continuation.hpp"
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
#if !defined(YGO_M3_DECK_A) || !defined(YGO_M3_DECK_B)
    throw std::runtime_error("T5C fixture lacks the locked-deck paths");
#else
    std::vector<std::uint32_t> passcodes;
    for (const auto* path : {YGO_M3_DECK_A, YGO_M3_DECK_B}) {
        std::ifstream input(path);
        require(static_cast<bool>(input), "T5C fixture could not open a locked deck");
        std::string line;
        while (std::getline(input, line)) {
            if (line.empty() || line.front() == '#') continue;
            if (line.front() == '!') break;
            passcodes.push_back(static_cast<std::uint32_t>(std::stoul(line)));
        }
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
    require(vocabulary.identity() == kSmokeCardVocabularyIdentity,
            "T5C fixture vocabulary is not the accepted smoke vocabulary: " +
                vocabulary.identity());
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
    const CheckpointInferenceProviderV1& provider,
    const std::uint8_t participant = 0) {
    auto result = create_checkpoint_bound_policy(
        std::string(kSmokeCheckpointIdentity), participant,
        "participant_policy_assignment.v1." + std::string(64, 'a'),
        "policy_artifact.v1." + std::string(64, 'b'), fixture_vocabulary(), provider);
    require_result(result, "direct checkpoint-policy fixture construction failed");
    return std::move(*result.value);
}

using ResponseMutator = std::function<void(
    InferenceResponseV1&, const std::vector<std::string>&)>;

CheckpointInferenceProviderV1 mutating_response_provider(ResponseMutator mutator) {
    return [mutator = std::move(mutator)](
               const InferenceRequestV1& request,
               const ygo::model::LogicalModelInputV1&,
               const ygo::model::EncodedModelInputV1& input) {
        auto result = make_inference_response(
            request,
            std::vector<std::string>(input.routing_keys.size(), "3f800000"),
            input.routing_keys);
        require(result.value.has_value(), "response mutation fixture construction failed");
        mutator(*result.value, input.routing_keys);
        return result;
    };
}

void require_policy_failure(
    CheckpointBoundPolicyV1& policy,
    const ygo::environment::DecisionFrame& frame,
    const GameplayFailureStage expected_stage,
    const std::string_view expected_code,
    const std::string_view label) {
    require(!policy.select(frame), std::string(label) + " was accepted");
    require(policy.last_failure().has_value(),
            std::string(label) + " did not expose a typed failure");
    require(policy.last_failure()->stage == expected_stage &&
                policy.last_failure()->code == expected_code,
            std::string(label) + " had the wrong failure stage/code: actual=" +
                std::string(gameplay_failure_stage_name(policy.last_failure()->stage)) +
                "/" + policy.last_failure()->code + " expected=" +
                std::string(gameplay_failure_stage_name(expected_stage)) + "/" +
                std::string(expected_code));
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

void test_smoke_context_and_vocabulary_are_checkpoint_bound() {
    const auto accepted_context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd");
    auto alternate_job = accepted_context.jobs.front();
    alternate_job.evaluated_policy_checkpoint_identity =
        "phase6_checkpoint.v1." + std::string(64, 'e');
    std::string job_error;
    require(!validate_evaluation_job(alternate_job, &job_error),
            "implementation acceptance accepted a job for an alternate checkpoint");

    bool rejected = false;
    try {
        (void)make_implementation_acceptance_context(
            "434066289a14d0dae67222e0486f4df8538950bd",
            "phase6_checkpoint.v1." + std::string(64, 'e'));
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, "implementation acceptance minted an alternate checkpoint context");

    auto fixture = make_fixture();
    const auto wrong_vocabulary_result =
        ygo::model::CardVocabularyV1::from_ascending_passcodes({123456});
    require(static_cast<bool>(wrong_vocabulary_result),
            "wrong vocabulary fixture construction failed");
    fixture.config.card_vocabulary = *wrong_vocabulary_result.value;
    auto created = create_frozen_gameplay_evaluator(std::move(fixture.config));
    require(!created && created.error.has_value(),
            "canonical but wrong CardVocabulary was accepted for the smoke checkpoint");
}

ygo::observation::PlayerObservation paired_private_observation(
    const std::uint8_t perspective, const std::uint64_t engine_step_index) {
    ygo::observation::PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = perspective;
    observation.engine_step_index = engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.globals.terminal = false;
    observation.match_context.perspective_player = perspective;
    const auto hidden_controller = static_cast<std::uint8_t>(1 - perspective);
    observation.zones.push_back({hidden_controller,
                                 ygo::observation::SemanticZone::SpellTrapZone,
                                 1, 0, 1, false});
    ygo::observation::ObservedCard hidden;
    hidden.locator = {"p" + std::to_string(hidden_controller) +
                      ":SPELL_TRAP_ZONE:0"};
    hidden.identity_known = false;
    hidden.controller = hidden_controller;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(std::move(hidden));
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

ygo::protocol::DecisionRequest paired_private_request(const std::uint32_t hidden_code) {
    ygo::protocol::DecisionRequest request;
    request.kind = ygo::protocol::DecisionRequestKind::CardSelection;
    request.decision_id = "private-decision.card." + std::to_string(hidden_code);
    request.engine_step_index = 91;
    request.player = 1;
    request.engine_message_type = 15;
    request.engine_message_name = "MSG_SELECT_CARD";
    request.raw_message_hash = "private-raw." + std::to_string(hidden_code);
    ygo::protocol::ActionCandidate candidate;
    candidate.action_kind = ygo::protocol::ActionKind::CardSelection;
    candidate.semantic_key = "card.0.3." + std::to_string(hidden_code) + ".0.8.0";
    candidate.source_card = hidden_code;
    candidate.source_controller = 0;
    candidate.source_location = 8;
    candidate.source_sequence = 0;
    candidate.source_index = 3;
    candidate.exact_response_bytes = {3, 0, 0, 0};
    request.candidates.push_back(std::move(candidate));
    return request;
}

void test_real_paired_hidden_worlds_have_equal_checkpoint_inputs_and_selection() {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "T5C paired-hidden environment construction failed");
    auto environment = std::move(
        std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
    const auto request_a = paired_private_request(14821890);
    const auto request_b = paired_private_request(7654321);
    auto observation_a = paired_private_observation(1, 91);
    auto observation_b = paired_private_observation(1, 91);
    ygo::observation::attach_decision_context(observation_a, request_a);
    ygo::observation::attach_decision_context(observation_b, request_b);
    require(request_a.candidates.front().semantic_key !=
                request_b.candidates.front().semantic_key &&
                request_a.raw_message_hash != request_b.raw_message_hash,
            "paired hidden fixtures did not differ privately");
    require(ygo::observation::canonical_serialize(observation_a) !=
                ygo::observation::canonical_serialize(observation_b),
            "paired hidden fixtures did not retain distinct private source data");
    const auto frame_a = ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, request_a, observation_a, std::string(64, 'a'), 7);
    const auto frame_b = ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, request_b, observation_b, std::string(64, 'a'), 7);
    require(ygo::environment::canonical_public_environment_observation_bytes(
                frame_a.public_observation) ==
                ygo::environment::canonical_public_environment_observation_bytes(
                    frame_b.public_observation) &&
                frame_a.public_observation_digest == frame_b.public_observation_digest &&
                frame_a.public_semantic_decision_id == frame_b.public_semantic_decision_id &&
                frame_a.public_candidate_domain_digest == frame_b.public_candidate_domain_digest &&
                frame_a.request.kind == frame_b.request.kind &&
                frame_a.request.player == frame_b.request.player &&
                frame_a.request.candidates.size() == frame_b.request.candidates.size(),
            "paired hidden fixtures changed the accepted public frame or domain");
    require(frame_a.request.candidates.front().public_action_key ==
                frame_b.request.candidates.front().public_action_key &&
                frame_a.request.candidates.front().public_action_key.find("14821890") ==
                    std::string::npos &&
                frame_a.request.candidates.front().public_action_key.find("7654321") ==
                    std::string::npos,
            "paired hidden fixture leaked a private value through the public key");
    for (std::size_t index = 0; index < frame_a.request.candidates.size(); ++index) {
        require(frame_a.request.candidates[index].public_action_key ==
                    frame_b.request.candidates[index].public_action_key,
                "paired hidden fixtures changed the complete public candidate vector");
    }

    struct Capture final {
        std::vector<std::vector<std::uint8_t>> logical;
        std::vector<std::vector<std::uint8_t>> encoded;
        std::vector<InferenceRequestV1> requests;
        std::vector<std::vector<std::string>> scores;
        std::vector<std::string> selections;
    } capture;
    const CheckpointInferenceProviderV1 provider =
        [&capture](const InferenceRequestV1& request,
                   const ygo::model::LogicalModelInputV1& logical,
                   const ygo::model::EncodedModelInputV1& encoded) {
            capture.logical.push_back(ygo::model::canonical_logical_model_input_bytes(logical));
            capture.encoded.push_back(ygo::model::canonical_encoded_model_input_bytes(encoded));
            capture.requests.push_back(request);
            const std::vector<std::string> scores(encoded.routing_keys.size(), "3f800000");
            capture.scores.push_back(scores);
            auto result = make_inference_response(request, scores, encoded.routing_keys);
            require(result.value.has_value(), "paired hidden response construction failed");
            capture.selections.push_back(result.value->selected_public_action_key);
            return result;
        };
    auto policy_a = make_direct_policy(provider, 1);
    auto policy_b = make_direct_policy(provider, 1);
    const auto selected_a = policy_a.select(frame_a);
    const auto selected_b = policy_b.select(frame_b);
    require(selected_a && selected_b && capture.logical.size() == 2 &&
                capture.encoded.size() == 2 && capture.requests.size() == 2 &&
                capture.scores.size() == 2 && capture.selections.size() == 2,
            "paired hidden policy execution did not produce two complete responses");
    require(capture.logical[0] == capture.logical[1] &&
                capture.encoded[0] == capture.encoded[1] &&
                capture.requests[0].request_identity == capture.requests[1].request_identity &&
                capture.requests[0].checkpoint_identity == capture.requests[1].checkpoint_identity &&
                capture.requests[0].model_input_identity == capture.requests[1].model_input_identity &&
                capture.requests[0].ordered_candidate_domain_identity ==
                    capture.requests[1].ordered_candidate_domain_identity &&
                capture.requests[0].public_semantic_decision_id ==
                    capture.requests[1].public_semantic_decision_id &&
                capture.requests[0].perspective_player == capture.requests[1].perspective_player &&
                capture.requests[0].decision_index == capture.requests[1].decision_index &&
                capture.scores[0] == capture.scores[1] &&
                capture.selections[0] == capture.selections[1] &&
                selected_a.value->public_action_key == selected_b.value->public_action_key,
            "paired hidden worlds changed checkpoint model input, scores, or selection");
}

void test_checkpoint_policy_preserves_failure_stage_identity() {
    const auto frame = first_frame(2);
    auto public_frame_failure = frame;
    public_frame_failure.public_observation_digest = std::string(64, 'e');
    auto policy = make_direct_policy(
        [](const InferenceRequestV1&, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1&) {
            return InferenceResponseCreateResult{std::nullopt, std::string("unused")};
        });
    require(!policy.select(public_frame_failure) && policy.last_failure().has_value() &&
                policy.last_failure()->stage == GameplayFailureStage::PublicFrameValidation &&
                policy.last_failure()->code == "PUBLIC_FRAME_INVALID",
            "invalid public frame did not preserve its failure stage");

    auto model_frame_failure = frame;
    model_frame_failure.request.candidates.front().source_reference =
        ygo::environment::PublicCardReference{
            static_cast<ygo::environment::PublicCardReferenceKind>(99), "invalid"};
    auto model_policy = make_direct_policy(
        [](const InferenceRequestV1&, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1&) {
            return InferenceResponseCreateResult{std::nullopt, std::string("unused")};
        });
    require(!model_policy.select(model_frame_failure) && model_policy.last_failure().has_value() &&
                model_policy.last_failure()->stage == GameplayFailureStage::ModelInputValidation &&
                model_policy.last_failure()->code == "MODEL_INPUT_INVALID",
            "model-input failure did not preserve its failure stage");

    auto inference_policy = make_direct_policy(
        [](const InferenceRequestV1&, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1&) {
            return InferenceResponseCreateResult{std::nullopt, std::string("provider failure")};
        });
    require(!inference_policy.select(frame) && inference_policy.last_failure().has_value() &&
                inference_policy.last_failure()->stage == GameplayFailureStage::Inference &&
                inference_policy.last_failure()->code == "INFERENCE_FAILURE",
            "provider failure did not preserve the inference stage");

    const CheckpointInferenceProviderV1 selection_provider =
        [](const InferenceRequestV1& request, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1& input) {
            auto result = make_inference_response(
                request, std::vector<std::string>(input.routing_keys.size(), "3f800000"),
                input.routing_keys);
            require(result.value.has_value() && input.routing_keys.size() > 1,
                    "selection-stage fixture lacks a complete domain");
            result.value->selected_candidate_ordinal =
                (result.value->selected_candidate_ordinal + 1) %
                static_cast<std::uint32_t>(input.routing_keys.size());
            result.value->selected_public_action_key =
                input.routing_keys[result.value->selected_candidate_ordinal];
            result.value->response_identity = inference_response_identity(*result.value);
            return result;
        };
    auto selection_policy = make_direct_policy(selection_provider);
    require(!selection_policy.select(frame) && selection_policy.last_failure().has_value() &&
                selection_policy.last_failure()->stage == GameplayFailureStage::Selection &&
                selection_policy.last_failure()->code == "SELECTION_INVALID",
            "invalid selection envelope did not preserve the selection stage");

    const auto context = make_implementation_acceptance_context(
        "434066289a14d0dae67222e0486f4df8538950bd");
    const auto job_id = evaluation_job_identity(context.jobs.front());
    const std::array<std::pair<GameplayFailureStage, std::string>, 4> stages = {
        std::make_pair(GameplayFailureStage::PublicFrameValidation, "PUBLIC_FRAME_INVALID"),
        std::make_pair(GameplayFailureStage::ModelInputValidation, "MODEL_INPUT_INVALID"),
        std::make_pair(GameplayFailureStage::Inference, "INFERENCE_RESPONSE_INVALID"),
        std::make_pair(GameplayFailureStage::Selection, "SELECTION_INVALID")};
    for (const auto& [stage, code] : stages) {
        ReplayAdmissionSummaryV1 replay;
        replay.evaluation_identity = context.evaluation_identity;
        replay.evaluation_job_identity = job_id;
        replay.failure_stage = stage;
        replay.failure_code = code;
        GameplayJobResultV1 result;
        result.evaluation_identity = context.evaluation_identity;
        result.evaluation_job_identity = job_id;
        result.checkpoint_identity = context.checkpoint_identity;
        result.status = GameplayJobStatus::Failed;
        result.failure_stage = stage;
        result.failure_code = code;
        result.replay_admission_summary_identity = replay_admission_summary_identity(replay);
        const auto round_trip = decode_gameplay_job_result_json(
            encode_gameplay_job_result_json(result));
        require(round_trip.failure_stage == stage && round_trip.failure_code == code,
                "canonical job-result transport changed a typed failure stage");
    }
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
            response.value->selected_candidate_ordinal =
                std::numeric_limits<std::uint32_t>::max();
            response.value->selected_public_action_key = input.routing_keys.front();
            response.value->response_identity = inference_response_identity(*response.value);
            return response;
        };
    auto policy = make_direct_policy(wrong_checkpoint);
    require_policy_failure(policy, frame, GameplayFailureStage::Inference,
                           "INFERENCE_RESPONSE_INVALID", "wrong-checkpoint response");

    const CheckpointInferenceProviderV1 wrong_domain =
        [](const InferenceRequestV1& request, const ygo::model::LogicalModelInputV1&,
           const ygo::model::EncodedModelInputV1& input) {
            auto response = make_inference_response(
                request, std::vector<std::string>(input.routing_keys.size(), "3f800000"),
                input.routing_keys);
            require(response.value.has_value(), "wrong-domain response fixture failed");
            response.value->ordered_candidate_domain_identity = std::string(64, 'e');
            response.value->selected_candidate_ordinal =
                std::numeric_limits<std::uint32_t>::max();
            response.value->selected_public_action_key = input.routing_keys.front();
            response.value->response_identity = inference_response_identity(*response.value);
            return response;
        };
    auto second_policy = make_direct_policy(wrong_domain);
    require_policy_failure(second_policy, frame, GameplayFailureStage::Inference,
                           "INFERENCE_RESPONSE_INVALID", "wrong-domain response");
}

void test_checkpoint_policy_preserves_response_failure_precedence() {
    const auto frame = first_frame(2);
    const auto bad_ordinal = std::numeric_limits<std::uint32_t>::max();

    auto wrong_score_count = make_direct_policy(mutating_response_provider(
        [bad_ordinal](InferenceResponseV1& response, const std::vector<std::string>& keys) {
            require(!keys.empty(), "wrong-score-count fixture lacks a candidate domain");
            response.score_count = static_cast<std::uint32_t>(response.score_f32_bits.size() - 1);
            response.selected_candidate_ordinal = bad_ordinal;
            response.selected_public_action_key = keys.front();
            response.response_identity = inference_response_identity(response);
        }));
    require_policy_failure(wrong_score_count, frame, GameplayFailureStage::Inference,
                           "INFERENCE_RESPONSE_INVALID", "wrong-score-count response");

    auto current_bad_ordinal = make_direct_policy(mutating_response_provider(
        [bad_ordinal](InferenceResponseV1& response, const std::vector<std::string>& keys) {
            require(!keys.empty(), "bad-ordinal fixture lacks a candidate domain");
            response.selected_candidate_ordinal = bad_ordinal;
            response.selected_public_action_key = keys.front();
            response.response_identity = inference_response_identity(response);
        }));
    require_policy_failure(current_bad_ordinal, frame, GameplayFailureStage::Selection,
                           "SELECTION_INVALID", "current bad-ordinal response");

    auto current_key_ordinal_mismatch = make_direct_policy(mutating_response_provider(
        [](InferenceResponseV1& response, const std::vector<std::string>& keys) {
            require(keys.size() > 1, "key/ordinal mismatch fixture lacks two candidates");
            response.selected_candidate_ordinal = 0;
            response.selected_public_action_key = keys[1];
            response.response_identity = inference_response_identity(response);
        }));
    require_policy_failure(current_key_ordinal_mismatch, frame,
                           GameplayFailureStage::Selection, "SELECTION_INVALID",
                           "current key/ordinal mismatch response");

    auto current_tiebreak_mismatch = make_direct_policy(mutating_response_provider(
        [](InferenceResponseV1& response, const std::vector<std::string>& keys) {
            require(keys.size() > 1, "tie-break fixture lacks two candidates");
            const auto accepted = response.selected_candidate_ordinal;
            const auto alternate = accepted == 0 ? std::uint32_t{1} : std::uint32_t{0};
            response.selected_candidate_ordinal = alternate;
            response.selected_public_action_key = keys[alternate];
            response.response_identity = inference_response_identity(response);
        }));
    require_policy_failure(current_tiebreak_mismatch, frame,
                           GameplayFailureStage::Selection, "SELECTION_INVALID",
                           "current tie-break mismatch response");
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
    const auto frame_a = first_frame(2);
    const auto frame_b = first_frame(3);
    require(frame_a.acting_player == 0 && frame_b.acting_player == 0 &&
                frame_a.public_semantic_decision_id != frame_b.public_semantic_decision_id,
            "T5C stale-response fixtures did not provide two distinct participant-0 requests");
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
            auto stale = *first_response;
            stale.selected_candidate_ordinal = std::numeric_limits<std::uint32_t>::max();
            stale.selected_public_action_key = first_response->selected_public_action_key;
            stale.response_identity = inference_response_identity(stale);
            return InferenceResponseCreateResult{std::optional<InferenceResponseV1>(stale),
                                                 std::nullopt};
        };
    auto policy = make_direct_policy(provider);
    const auto first = policy.select(frame_a);
    require(static_cast<bool>(first), "stale-response first selection failed");
    ygo::environment::AcceptedActionTransition transition;
    transition.episode_semantic_id = frame_a.episode_semantic_id;
    transition.public_semantic_decision_id = frame_a.public_semantic_decision_id;
    transition.decision_index = frame_a.decision_index;
    transition.selected_public_action_key = first.value->public_action_key;
    require(policy.commit(transition), "T5C stale-response first commit failed");
    require_policy_failure(policy, frame_b, GameplayFailureStage::Inference,
                           "INFERENCE_RESPONSE_INVALID",
                           "a stale response with a bad ordinal");
}

void test_opponent_policy_failure_is_not_neural_inference() {
    const auto classification = detail::classify_policy_selection_failure(false, std::nullopt);
    require(classification.stage == GameplayFailureStage::Environment &&
                classification.code == "OPPONENT_POLICY_FAILURE",
            "opponent Teacher failure was classified as checkpoint inference");
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
        test_smoke_context_and_vocabulary_are_checkpoint_bound();
        test_real_paired_hidden_worlds_have_equal_checkpoint_inputs_and_selection();
        test_checkpoint_policy_preserves_failure_stage_identity();
        test_failure_accounting_preserves_replay_and_admission_failures();
        test_score_bits_are_exact_and_finite();
        test_checkpoint_policy_rejects_wrong_checkpoint_and_candidate_domain_response();
        test_checkpoint_policy_preserves_response_failure_precedence();
        test_checkpoint_policy_keeps_public_requests_equal_across_paired_public_worlds();
        test_stale_response_is_rejected_without_a_second_selection();
        test_opponent_policy_failure_is_not_neural_inference();
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
