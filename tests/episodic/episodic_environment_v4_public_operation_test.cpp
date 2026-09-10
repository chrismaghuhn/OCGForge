#include "episodic_environment_test_access.hpp"
#include "ocgapi_constants.h"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/message_decoder.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {

using ygo::environment::CertifiedEnvironmentConfig;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::EnvironmentDecisionKind;
using ygo::environment::EpisodicEnvironment;
using ygo::environment::PublicCardSelectionOperation;
using ygo::environment::PublicSemanticDecisionIdentityInputV3;
using ygo::observation::PlayerObservation;
using ygo::protocol::ActionCandidate;
using ygo::protocol::ActionKind;
using ygo::protocol::CardSelectionOperation;
using ygo::protocol::DecisionRequest;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void append_u32(std::vector<std::uint8_t>& bytes, const std::uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        bytes.push_back(static_cast<std::uint8_t>((value >> shift) & 0xffu));
    }
}

void append_card(std::vector<std::uint8_t>& payload,
                 const std::uint32_t code,
                 const std::uint8_t controller,
                 const std::uint8_t location,
                 const std::uint32_t sequence,
                 const std::uint32_t position = 0) {
    append_u32(payload, code);
    payload.push_back(controller);
    payload.push_back(location);
    append_u32(payload, sequence);
    append_u32(payload, position);
}

std::vector<std::uint8_t> frame(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> result;
    append_u32(result, static_cast<std::uint32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}

std::vector<std::uint8_t> unselect_payload() {
    std::vector<std::uint8_t> payload = {MSG_SELECT_UNSELECT_CARD, 0, 1, 0};
    append_u32(payload, 0);
    append_u32(payload, 2);
    append_u32(payload, 1);
    append_card(payload, 801, 0, LOCATION_HAND, 0);
    append_u32(payload, 1);
    append_card(payload, 802, 0, LOCATION_HAND, 1);
    return payload;
}

DecisionRequest decode(const bool corrected) {
    const auto decoded = corrected
                             ? ygo::protocol::decode_messages_v4(frame(unselect_payload()))
                             : ygo::protocol::decode_messages(frame(unselect_payload()));
    require(decoded.decisions.size() == 1, "unselect fixture did not produce one decision");
    return decoded.decisions.front();
}

PlayerObservation observation_for(const DecisionRequest& request) {
    PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.engine_step_index = request.engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.match_context.perspective_player = 0;
    observation.match_context.duel_flags = 190464;
    for (const auto [code, sequence] : {std::pair<std::uint32_t, std::uint32_t>{801, 0},
                                        std::pair<std::uint32_t, std::uint32_t>{802, 1}}) {
        ygo::observation::ObservedCard card;
        card.locator = {"p0:HAND:" + std::to_string(sequence)};
        card.identity_known = true;
        card.passcode = code;
        card.owner = 0;
        card.controller = 0;
        card.zone = ygo::observation::SemanticZone::Hand;
        card.sequence = sequence;
        card.face_up = true;
        observation.entities.push_back(std::move(card));
    }
    ygo::observation::attach_decision_context(observation, request);
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

std::unique_ptr<EpisodicEnvironment> make_environment(
    const CertifiedEnvironmentConfig& config) {
    auto factory = EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "certified environment factory rejected the expected configuration");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

bool equal_choice(const std::optional<ygo::environment::PublicChoice>& left,
                  const std::optional<ygo::environment::PublicChoice>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (left->kind == right->kind && left->value == right->value &&
            left->response_index == right->response_index);
}

bool equal_reference(
    const std::optional<ygo::environment::PublicCardReference>& left,
    const std::optional<ygo::environment::PublicCardReference>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    return !left.has_value() ||
           (left->kind == right->kind &&
            left->observation_locator == right->observation_locator);
}

void require_public_candidate_equal_except_generation(
    const ygo::environment::EnvironmentActionCandidate& historical,
    const ygo::environment::EnvironmentActionCandidate& corrected) {
    require(historical.action_kind == corrected.action_kind &&
                equal_choice(historical.choice, corrected.choice) &&
                equal_reference(historical.source_reference, corrected.source_reference) &&
                equal_reference(historical.target_reference, corrected.target_reference) &&
                historical.phase == corrected.phase &&
                historical.position == corrected.position &&
                historical.source_index == corrected.source_index &&
                historical.amount == corrected.amount &&
                historical.continuation_operation == corrected.continuation_operation &&
                historical.submits_engine_response == corrected.submits_engine_response,
            "V3/V4 public candidate descriptor changed outside generation");
}

void test_v4_public_projection() {
    auto environment_v3 = make_environment(CertifiedEnvironmentConfig::canonical_v3());
    auto environment_v4 = make_environment(CertifiedEnvironmentConfig::canonical_v4());
    const auto historical_request = decode(false);
    const auto corrected_request = decode(true);
    require(historical_request.candidates.size() == corrected_request.candidates.size(),
            "V3/V4 decoder changed candidate membership");
    for (std::size_t index = 0; index < historical_request.candidates.size(); ++index) {
        require(historical_request.candidates[index].semantic_key ==
                    corrected_request.candidates[index].semantic_key &&
                    historical_request.candidates[index].source_index ==
                        corrected_request.candidates[index].source_index &&
                    historical_request.candidates[index].exact_response_bytes ==
                        corrected_request.candidates[index].exact_response_bytes,
                "V3/V4 decoder changed semantic key, source index, or response bytes");
    }

    const auto historical_frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment_v3, historical_request, observation_for(historical_request),
            std::string(64, 'a'), 0);
    const auto corrected_frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment_v4, corrected_request, observation_for(corrected_request),
            std::string(64, 'a'), 0);

    require(corrected_frame.contract_id == ygo::environment::kEpisodicEnvironmentV4ContractId,
            "V4 frame has the wrong contract ID");
    require(corrected_frame.request.kind == EnvironmentDecisionKind::UnselectCard &&
                corrected_frame.request.candidates.size() == 3,
            "V4 projection changed request family or cardinality");
    require(corrected_frame.request.candidates[0].card_selection_operation ==
                PublicCardSelectionOperation::Select &&
                corrected_frame.request.candidates[1].card_selection_operation ==
                    PublicCardSelectionOperation::Unselect &&
                corrected_frame.request.candidates[2].action_kind == EnvironmentActionKind::Finish &&
                corrected_frame.request.candidates[2].card_selection_operation ==
                    PublicCardSelectionOperation::None,
            "V4 projection did not map first Select, second Unselect, and Finish None");
    require(corrected_frame.request.candidates[0].public_action_key.rfind(
                "public_action.v3.", 0) == 0 &&
                corrected_frame.request.candidates[1].public_action_key.rfind(
                    "public_action.v3.", 0) == 0,
            "V4 projection did not use public_action.v3");

    require(historical_frame.public_observation.canonical_safe_state_bytes() ==
                corrected_frame.public_observation.canonical_safe_state_bytes() &&
                historical_frame.public_observation_digest ==
                    corrected_frame.public_observation_digest,
            "V3/V4 public observation changed outside the corrected operation");
    require(historical_frame.request.candidates.size() ==
                corrected_frame.request.candidates.size(),
            "V3/V4 public candidate order cardinality changed");
    for (std::size_t index = 0; index < historical_frame.request.candidates.size(); ++index) {
        require_public_candidate_equal_except_generation(
            historical_frame.request.candidates[index], corrected_frame.request.candidates[index]);
    }

    std::vector<std::string> keys;
    for (const auto& candidate : corrected_frame.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    require(corrected_frame.public_candidate_domain_digest ==
                ygo::environment::public_candidate_domain_digest_v3("unselect_card", keys),
            "V4 frame did not use the V3 candidate-domain identity");
    PublicSemanticDecisionIdentityInputV3 identity;
    identity.episode_semantic_id = corrected_frame.episode_semantic_id;
    identity.decision_index = corrected_frame.decision_index;
    identity.acting_player = corrected_frame.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = corrected_frame.public_observation_digest;
    identity.public_candidate_domain_digest = corrected_frame.public_candidate_domain_digest;
    identity.public_action_keys = keys;
    require(corrected_frame.public_semantic_decision_id ==
                ygo::environment::public_semantic_decision_id_v3(identity),
            "V4 frame did not use the V3 public decision identity");
    require(historical_frame.public_semantic_decision_id !=
                corrected_frame.public_semantic_decision_id,
            "V3/V4 public decision identities unexpectedly matched");
}

void test_v4_reset_and_version_rejection() {
    auto environment = make_environment(CertifiedEnvironmentConfig::canonical_v4());
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = ygo::environment::kEpisodicEnvironmentV4ContractId;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    control.cancellation.source = "v4-operation-test";
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "V4 reset was rejected");
    const auto& next = std::get<ygo::environment::ResetAccepted>(reset).next;
    const auto* frame = std::get_if<ygo::environment::DecisionFrame>(&next);
    require(frame != nullptr && frame->contract_id ==
                ygo::environment::kEpisodicEnvironmentV4ContractId,
            "V4 reset did not publish a V4 decision frame");
    require(!frame->request.candidates.empty() &&
                frame->request.candidates.front().public_action_key.rfind(
                    "public_action.v3.", 0) == 0,
            "V4 reset did not publish V3 public action keys");

    auto mixed = CertifiedEnvironmentConfig::canonical_v4();
    mixed.public_action_identity_schema_id =
        std::string(ygo::environment::kPublicActionIdentityV2SchemaId);
    mixed.environment_semantic_id = ygo::environment::environment_semantic_id(mixed);
    const auto mixed_factory = EpisodicEnvironment::create(std::move(mixed));
    require(std::holds_alternative<ygo::environment::EnvironmentFactoryRejected>(mixed_factory),
            "V4 factory accepted mixed V2 public identity fields");
}

}  // namespace

int main() {
    try {
        test_v4_public_projection();
        test_v4_reset_and_version_rejection();
        std::cout << "episodic environment V4 public operation tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
