#include "episodic_environment_test_access.hpp"
#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"
#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/serialization.hpp"

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
using ygo::environment::PublicSemanticDecisionIdentityInput;
using ygo::observation::PlayerObservation;
using ygo::protocol::ActionCandidate;
using ygo::protocol::ActionKind;
using ygo::protocol::CardSelectionOperation;
using ygo::protocol::DecisionRequest;
using ygo::protocol::DecisionRequestKind;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

ActionCandidate card_candidate(const std::string& semantic_key,
                               const std::uint32_t code,
                               const std::uint32_t source_index,
                               const CardSelectionOperation operation) {
    ActionCandidate candidate;
    candidate.action_kind = ActionKind::CardSelection;
    candidate.semantic_key = semantic_key;
    candidate.source_card = code;
    candidate.source_controller = 0;
    candidate.source_location = 0;
    candidate.source_sequence = source_index;
    candidate.source_index = source_index;
    candidate.card_selection_operation = operation;
    candidate.exact_response_bytes = {1, static_cast<std::uint8_t>(source_index)};
    return candidate;
}

DecisionRequest unselect_request() {
    DecisionRequest request;
    request.kind = DecisionRequestKind::UnselectCard;
    request.decision_id = "v3-unselect-decision";
    request.engine_step_index = 7;
    request.player = 0;
    request.engine_message_type = 15;
    request.engine_message_name = "MSG_SELECT_UNSELECT_CARD";
    request.raw_message_hash = "v3-unselect-raw";
    request.candidates.push_back(card_candidate(
        "unselect.selected.0.801.0.0.0", 801, 0,
        CardSelectionOperation::Unselect));
    request.candidates.push_back(card_candidate(
        "unselect.unselected.1.802.0.0.1", 802, 1,
        CardSelectionOperation::Select));
    ActionCandidate cancel;
    cancel.action_kind = ActionKind::Cancel;
    cancel.semantic_key = "unselect.cancel";
    cancel.card_selection_operation = CardSelectionOperation::None;
    cancel.exact_response_bytes = {0};
    request.candidates.push_back(std::move(cancel));
    return request;
}

PlayerObservation observation_for(const DecisionRequest& request) {
    PlayerObservation observation;
    observation.schema_version = "ygo.player_observation.v1";
    observation.perspective_player = 0;
    observation.engine_step_index = request.engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.match_context.perspective_player = 0;
    observation.match_context.duel_flags = 190464;
    ygo::observation::ObservedCard first;
    first.locator = {"p0:MONSTER_ZONE:0"};
    first.identity_known = true;
    first.passcode = 801;
    first.owner = 0;
    first.controller = 0;
    first.zone = ygo::observation::SemanticZone::MonsterZone;
    first.sequence = 0;
    first.face_up = true;
    observation.entities.push_back(std::move(first));
    ygo::observation::ObservedCard second;
    second.locator = {"p0:MONSTER_ZONE:1"};
    second.identity_known = true;
    second.passcode = 802;
    second.owner = 0;
    second.controller = 0;
    second.zone = ygo::observation::SemanticZone::MonsterZone;
    second.sequence = 1;
    second.face_up = true;
    observation.entities.push_back(std::move(second));
    ygo::observation::attach_decision_context(observation, request);
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

std::unique_ptr<EpisodicEnvironment> make_v3_environment() {
    auto factory = EpisodicEnvironment::create(
        CertifiedEnvironmentConfig::canonical_v3());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "canonical V3 environment was rejected");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

void require_projection_rejected(const DecisionRequest& request,
                                 const std::string& message) {
    auto environment = make_v3_environment();
    bool rejected = false;
    try {
        (void)ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, request, observation_for(request), std::string(64, 'a'), 0);
    } catch (const std::exception&) {
        rejected = true;
    }
    require(rejected, message);
}

void test_v3_projection() {
    const auto request = unselect_request();
    auto environment = make_v3_environment();
    const auto frame =
        ygo::environment::detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
            *environment, request, observation_for(request), std::string(64, 'a'), 0);

    require(frame.contract_id == ygo::environment::kEpisodicEnvironmentV3ContractId,
            "V3 frame has the wrong contract ID");
    require(frame.request.kind == EnvironmentDecisionKind::UnselectCard &&
                frame.request.candidates.size() == 3,
            "V3 projection changed request kind or candidate cardinality");
    require(frame.request.candidates[0].action_kind == EnvironmentActionKind::CardSelection &&
                frame.request.candidates[0].card_selection_operation ==
                    PublicCardSelectionOperation::Unselect,
            "selected-list candidate did not project as public Unselect");
    require(frame.request.candidates[1].card_selection_operation ==
                PublicCardSelectionOperation::Select,
            "unselected-list candidate did not project as public Select");
    require(frame.request.candidates[2].card_selection_operation ==
                PublicCardSelectionOperation::None,
            "Cancel did not project with public operation None");
    require(frame.request.candidates[0].public_action_key.rfind(
                "public_action.v2.", 0) == 0 &&
                frame.request.candidates[1].public_action_key.rfind(
                    "public_action.v2.", 0) == 0,
            "V3 candidate projection did not use public action identity V2");

    std::vector<std::string> keys;
    for (const auto& candidate : frame.request.candidates) {
        keys.push_back(candidate.public_action_key);
    }
    require(frame.public_candidate_domain_digest ==
                ygo::environment::public_candidate_domain_digest_v2("unselect_card", keys),
            "V3 frame did not use the V2 public candidate-domain identity");
    PublicSemanticDecisionIdentityInput identity;
    identity.episode_semantic_id = frame.episode_semantic_id;
    identity.decision_index = frame.decision_index;
    identity.acting_player = frame.acting_player;
    identity.request_kind = "unselect_card";
    identity.public_observation_digest = frame.public_observation_digest;
    identity.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    require(frame.public_semantic_decision_id ==
                ygo::environment::public_semantic_decision_id_v2(identity),
            "V3 frame did not use the V2 public decision identity");
}

void test_v3_projection_rejects_inconsistent_operations() {
    auto missing = unselect_request();
    missing.candidates[0].card_selection_operation = CardSelectionOperation::None;
    require_projection_rejected(missing,
                                 "V3 projection accepted missing UNSELECT operation");

    auto non_unselect = unselect_request();
    non_unselect.kind = DecisionRequestKind::CardSelection;
    non_unselect.engine_message_name = "MSG_SELECT_CARD";
    non_unselect.candidates[0].card_selection_operation = CardSelectionOperation::Select;
    require_projection_rejected(non_unselect,
                                 "non-UNSELECT card selection accepted an operation");

    auto non_card = unselect_request();
    non_card.candidates[0].action_kind = ActionKind::Cancel;
    non_card.candidates[0].card_selection_operation = CardSelectionOperation::Unselect;
    require_projection_rejected(non_card,
                                 "non-card action accepted a selection operation");
}

void test_v3_live_contract_ids() {
    auto environment = make_v3_environment();
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = ygo::environment::kEpisodicEnvironmentV3ContractId;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    control.cancellation.source = "v3-contract-id-test";

    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "V3 reset was rejected");
    const auto& next = std::get<ygo::environment::ResetAccepted>(reset).next;
    const auto* frame = std::get_if<ygo::environment::DecisionFrame>(&next);
    require(frame != nullptr &&
                frame->contract_id == ygo::environment::kEpisodicEnvironmentV3ContractId,
            "V3 reset did not publish a V3 decision frame");

    const auto wrong_contract = environment->step(ygo::environment::ActionSelection{
        std::string(ygo::environment::kEpisodicEnvironmentV2ContractId),
        frame->episode_semantic_id, frame->public_semantic_decision_id,
        frame->submission_token, frame->request.candidates.front().public_action_key});
    require(std::holds_alternative<ygo::environment::StepRejected>(wrong_contract) &&
                std::get<ygo::environment::StepRejected>(wrong_contract).contract_id ==
                    ygo::environment::kEpisodicEnvironmentV3ContractId,
            "V3 rejected step did not retain the active V3 contract ID");

    const auto interrupted = environment->interrupt(ygo::environment::InterruptRequest{
        std::string(ygo::environment::kEpisodicEnvironmentV3ContractId),
        ygo::environment::InterruptionReason::AdministrativeCancel});
    require(std::holds_alternative<ygo::environment::InterruptAccepted>(interrupted) &&
                std::get<ygo::environment::InterruptAccepted>(interrupted).interruption.contract_id ==
                    ygo::environment::kEpisodicEnvironmentV3ContractId,
            "V3 interruption did not retain the active V3 contract ID");
}

}  // namespace

int main() {
    try {
        test_v3_projection();
        test_v3_projection_rejects_inconsistent_operations();
        test_v3_live_contract_ids();
        std::cout << "episodic_environment_v3_public_operation_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
