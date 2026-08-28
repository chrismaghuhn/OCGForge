#include "ygo/environment/episodic_environment.hpp"
#include "episodic_environment_test_access.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>

#include "ygo/observation/decision_integration.hpp"
#include "ygo/observation/serialization.hpp"
#include "ygo/protocol/continuation.hpp"

namespace {

using namespace ygo::environment;
using ygo::observation::PlayerObservation;
using ygo::protocol::ActionCandidate;
using ygo::protocol::DecisionRequest;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<EpisodicEnvironment> make_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "paired-world test could not create the canonical environment");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

PlayerObservation hidden_observation(const std::uint8_t perspective, const std::uint32_t hidden_code,
                                     const std::uint64_t engine_step_index = 91) {
    PlayerObservation observation;
    observation.perspective_player = perspective;
    observation.engine_step_index = engine_step_index;
    observation.globals.life_points = {8000, 8000};
    observation.globals.terminal = false;
    observation.match_context.perspective_player = perspective;
    const auto hidden_controller = static_cast<std::uint8_t>(1 - perspective);
    const auto locator = std::string("p") + std::to_string(hidden_controller) + ":SPELL_TRAP_ZONE:0";
    observation.zones.push_back({hidden_controller, ygo::observation::SemanticZone::SpellTrapZone, 1, 0, 1, false});
    ygo::observation::ObservedCard hidden;
    hidden.locator = {locator};
    hidden.identity_known = false;
    hidden.controller = hidden_controller;
    hidden.zone = ygo::observation::SemanticZone::SpellTrapZone;
    hidden.sequence = 0;
    hidden.face_down = true;
    observation.entities.push_back(hidden);
    // The code is deliberately used only to attach a private request context
    // in the live pair; it is never copied into the hidden observation.
    (void)hidden_code;
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

DecisionRequest atomic_hidden_request(const std::uint32_t hidden_code) {
    DecisionRequest request;
    request.kind = ygo::protocol::DecisionRequestKind::CardSelection;
    request.decision_id = "private-decision.card." + std::to_string(hidden_code);
    request.engine_step_index = 91;
    request.player = 1;
    request.engine_message_type = 15;
    request.engine_message_name = "MSG_SELECT_CARD";
    request.raw_message_hash = "private-raw." + std::to_string(hidden_code);
    ActionCandidate candidate;
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

PlayerObservation observation_for_request(const DecisionRequest& request, const std::uint32_t hidden_code) {
    auto observation = hidden_observation(request.player, hidden_code, request.engine_step_index);
    ygo::observation::attach_decision_context(observation, request);
    return observation;
}

DecisionRequest continuation_hidden_request(const std::uint32_t hidden_code) {
    ygo::protocol::SelectionContinuation continuation;
    continuation.continuation_kind = ygo::protocol::ContinuationKind::Ordering;
    continuation.raw_message_hash = "private-continuation." + std::to_string(hidden_code);
    continuation.items.push_back({{hidden_code, 0, 8, 0, 0}, 3, 0, 0, 0, 0});
    continuation.min_count = 1;
    continuation.max_count = 1;
    continuation.can_cancel = false;
    return ygo::protocol::make_continuation_request(
        ygo::protocol::DecisionRequestKind::CardSelection, 1, 15, "MSG_SELECT_CARD", 91,
        std::move(continuation));
}

void require_same_candidate(const EnvironmentActionCandidate& left,
                            const EnvironmentActionCandidate& right) {
    const auto same_choice = [](const std::optional<PublicChoice>& first,
                                const std::optional<PublicChoice>& second) {
        if (first.has_value() != second.has_value()) {
            return false;
        }
        return !first.has_value() ||
               (first->kind == second->kind && first->value == second->value &&
                first->response_index == second->response_index);
    };
    const auto same_reference = [](const std::optional<PublicCardReference>& first,
                                   const std::optional<PublicCardReference>& second) {
        if (first.has_value() != second.has_value()) {
            return false;
        }
        return !first.has_value() ||
               (first->kind == second->kind && first->observation_locator == second->observation_locator);
    };
    require(left.action_kind == right.action_kind, "paired public action kinds differ");
    require(left.public_action_key == right.public_action_key, "paired public action keys differ");
    require(same_choice(left.choice, right.choice), "paired public typed choices differ");
    require(same_reference(left.source_reference, right.source_reference),
            "paired public source references differ");
    require(same_reference(left.target_reference, right.target_reference),
            "paired public target references differ");
    require(left.phase == right.phase && left.position == right.position &&
                left.source_index == right.source_index && left.amount == right.amount,
            "paired public scalar descriptors differ");
    require(left.continuation_operation == right.continuation_operation &&
                left.submits_engine_response == right.submits_engine_response,
            "paired public continuation metadata differs");
}

void require_same_continuation(const std::optional<EnvironmentContinuationView>& left,
                               const std::optional<EnvironmentContinuationView>& right) {
    require(left.has_value() == right.has_value(), "paired public continuation presence differs");
    if (!left.has_value()) {
        return;
    }
    require(left->continuation_kind == right->continuation_kind &&
                left->continuation_step == right->continuation_step &&
                left->selected_indices == right->selected_indices &&
                left->remaining_indices == right->remaining_indices &&
                left->assigned_amounts == right->assigned_amounts &&
                left->min_count == right->min_count && left->max_count == right->max_count &&
                left->target_sum == right->target_sum && left->required_amount == right->required_amount &&
                left->available_mask == right->available_mask && left->selected_mask == right->selected_mask &&
                left->continuation_steps == right->continuation_steps && left->exact_sum == right->exact_sum &&
                left->greater_sum == right->greater_sum && left->can_finish == right->can_finish &&
                left->can_cancel == right->can_cancel,
            "paired public continuation views differ");
}

void require_same_public_frame(const DecisionFrame& left, const DecisionFrame& right) {
    require(left.decision_index == right.decision_index && left.acting_player == right.acting_player,
            "paired public frame indices or acting players differ");
    require(left.public_observation.canonical_safe_state_bytes() ==
                right.public_observation.canonical_safe_state_bytes(),
            "paired public safe-state bytes differ");
    require(canonical_public_environment_observation_bytes(left.public_observation) ==
                canonical_public_environment_observation_bytes(right.public_observation),
            "paired public environment observations differ");
    require(left.public_observation_digest == right.public_observation_digest,
            "paired public observation digests differ");
    require(left.public_candidate_domain_digest == right.public_candidate_domain_digest,
            "paired public candidate-domain digests differ");
    require(left.public_semantic_decision_id == right.public_semantic_decision_id,
            "paired public semantic decision IDs differ");
    require(left.request.kind == right.request.kind && left.request.player == right.request.player,
            "paired public requests differ");
    require(left.request.candidates.size() == right.request.candidates.size(),
            "paired public candidate counts differ");
    for (std::size_t index = 0; index < left.request.candidates.size(); ++index) {
        require_same_candidate(left.request.candidates[index], right.request.candidates[index]);
    }
    require_same_continuation(left.request.continuation, right.request.continuation);
}

void test_live_paired_worlds_use_the_facade_projection_boundary() {
    auto environment = make_environment();
    const auto request_a = atomic_hidden_request(14821890);
    const auto request_b = atomic_hidden_request(7654321);
    auto observation_a = observation_for_request(request_a, 14821890);
    auto observation_b = observation_for_request(request_b, 7654321);
    require(request_a.candidates.front().semantic_key != request_b.candidates.front().semantic_key,
            "live paired worlds did not differ internally");
    require(ygo::observation::canonical_serialize(observation_a) !=
                ygo::observation::canonical_serialize(observation_b),
            "live paired worlds did not differ in private observation context");

    const auto frame_a = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, request_a, observation_a, std::string(64, 'a'), 7);
    const auto frame_b = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, request_b, observation_b, std::string(64, 'a'), 7);
    require_same_public_frame(frame_a, frame_b);
    require(frame_a.request.candidates.front().source_reference.has_value() &&
                frame_a.request.candidates.front().source_reference->kind == PublicCardReferenceKind::RedactedSlot,
            "live paired hidden card was not represented as RedactedSlot");

    auto mutated_request = request_a;
    mutated_request.candidates.front().semantic_key = "card.0.3.7654321.0.8.0";
    const auto mutated_frame = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, mutated_request, observation_a, std::string(64, 'a'), 7);
    require_same_public_frame(frame_a, mutated_frame);

    const auto public_bytes = canonical_public_environment_observation_bytes(frame_a.public_observation);
    const std::string public_text(public_bytes.begin(), public_bytes.end());
    require(public_text.find("14821890") == std::string::npos &&
                public_text.find("7654321") == std::string::npos &&
                frame_a.request.candidates.front().public_action_key.find("14821890") == std::string::npos &&
                frame_a.request.candidates.front().public_action_key.find("7654321") == std::string::npos,
            "live paired facade projection leaked a hidden passcode");
}

void test_continuation_views_are_paired_through_the_same_boundary() {
    auto environment = make_environment();
    const auto request_a = continuation_hidden_request(14821890);
    const auto request_b = continuation_hidden_request(7654321);
    auto observation_a = observation_for_request(request_a, 14821890);
    auto observation_b = observation_for_request(request_b, 7654321);
    const auto frame_a = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, request_a, observation_a, std::string(64, 'a'), 7);
    const auto frame_b = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, request_b, observation_b, std::string(64, 'a'), 7);
    require_same_public_frame(frame_a, frame_b);
    require(frame_a.request.continuation.has_value(), "continuation pair did not publish a safe continuation view");

    auto mutated_request = request_a;
    const auto bypass = std::find_if(
        mutated_request.candidates.begin(), mutated_request.candidates.end(),
        [](const ActionCandidate& candidate) {
            return candidate.continuation_operation == ygo::protocol::ContinuationOperation::Bypass;
        });
    require(bypass != mutated_request.candidates.end(), "continuation pair did not contain typed bypass metadata");
    bypass->semantic_key = bypass->continuation_id + ".cancel";
    const auto mutated_frame = detail::EpisodicEnvironmentTestAccess::project_frame_for_test(
        *environment, mutated_request, observation_a, std::string(64, 'a'), 7);
    require_same_public_frame(frame_a, mutated_frame);
}

PlayerObservation terminal_observation(const std::uint8_t perspective, const std::uint32_t hidden_code) {
    auto observation = hidden_observation(perspective, hidden_code, 120);
    observation.globals.terminal = true;
    observation.globals.winner = 0;
    observation.globals.win_reason = 1;
    observation.observation_hash = ygo::observation::observation_hash(observation);
    return observation;
}

void test_terminal_views_are_paired_through_the_public_getter() {
    auto environment_a = make_environment();
    auto environment_b = make_environment();
    detail::EpisodicEnvironmentTestAccess::install_terminal_views_for_test(
        *environment_a, terminal_observation(0, 14821890), terminal_observation(1, 14821890), 7);
    detail::EpisodicEnvironmentTestAccess::install_terminal_views_for_test(
        *environment_b, terminal_observation(0, 7654321), terminal_observation(1, 7654321), 7);

    for (const auto player : {std::uint8_t{0}, std::uint8_t{1}}) {
        const auto view_a = environment_a->perspective_terminal_view(player);
        const auto view_b = environment_b->perspective_terminal_view(player);
        require(view_a.has_value() && view_b.has_value(), "paired terminal view was not published");
        require(canonical_public_environment_observation_bytes(*view_a) ==
                    canonical_public_environment_observation_bytes(*view_b),
                "paired terminal public views differ");
        require(public_observation_digest(*view_a) == public_observation_digest(*view_b),
                "paired terminal public digests differ");
    }
}

}  // namespace

int main() {
    try {
        test_live_paired_worlds_use_the_facade_projection_boundary();
        test_continuation_views_are_paired_through_the_same_boundary();
        test_terminal_views_are_paired_through_the_public_getter();
        std::cout << "episodic_paired_world_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
