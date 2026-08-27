#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/episodic_environment_test_access.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

using namespace ygo::environment;
using Next = std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure>;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<EpisodicEnvironment> make_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "canonical environment factory rejected the reset-after-failure fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

RunControl make_control(const std::string& source) {
    RunControl control;
    control.engine_process_budget = 128;
    control.semantic_action_budget = 8;
    control.cancellation.source = source;
    return control;
}

const DecisionFrame& frame_of(const Next& next, const std::string& context) {
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr, context + " did not publish a decision frame");
    return *frame;
}

void require_same_public_frame(const DecisionFrame& expected, const DecisionFrame& actual,
                               const std::string& context) {
    require(expected.contract_id == actual.contract_id, context + " contract drifted");
    require(expected.episode_semantic_id == actual.episode_semantic_id,
            context + " episode identity drifted");
    require(expected.public_semantic_decision_id == actual.public_semantic_decision_id,
            context + " public decision identity drifted");
    require(expected.decision_index == actual.decision_index, context + " decision index drifted");
    require(expected.engine_step_index == actual.engine_step_index,
            context + " engine step index drifted");
    require(expected.acting_player == actual.acting_player, context + " acting player drifted");
    require(expected.public_observation_digest == actual.public_observation_digest,
            context + " public observation digest drifted");
    require(expected.public_candidate_domain_digest == actual.public_candidate_domain_digest,
            context + " public candidate digest drifted");
    require(expected.public_observation.canonical_safe_state_bytes() ==
                actual.public_observation.canonical_safe_state_bytes(),
            context + " public safe state drifted");
    require(expected.request.kind == actual.request.kind && expected.request.player == actual.request.player,
            context + " request identity drifted");
    require(expected.request.candidates.size() == actual.request.candidates.size(),
            context + " candidate cardinality drifted");
    for (std::size_t index = 0; index < expected.request.candidates.size(); ++index) {
        const auto& left = expected.request.candidates[index];
        const auto& right = actual.request.candidates[index];
        require(left.action_kind == right.action_kind && left.public_action_key == right.public_action_key &&
                    left.continuation_operation == right.continuation_operation &&
                    left.submits_engine_response == right.submits_engine_response,
                context + " candidate domain drifted");
    }
    require(expected.public_observation.decision_context.kind ==
                actual.public_observation.decision_context.kind &&
                expected.public_observation.decision_context.player ==
                    actual.public_observation.decision_context.player &&
                expected.public_observation.decision_context.referenced_entities ==
                    actual.public_observation.decision_context.referenced_entities,
            context + " public decision context drifted");
    require(expected.request.continuation.has_value() == actual.request.continuation.has_value(),
            context + " continuation presence drifted");
    if (expected.request.continuation.has_value()) {
        const auto& left = *expected.request.continuation;
        const auto& right = *actual.request.continuation;
        require(left.continuation_kind == right.continuation_kind &&
                    left.continuation_step == right.continuation_step &&
                    left.selected_indices == right.selected_indices &&
                    left.remaining_indices == right.remaining_indices &&
                    left.assigned_amounts == right.assigned_amounts &&
                    left.min_count == right.min_count && left.max_count == right.max_count &&
                    left.target_sum == right.target_sum && left.required_amount == right.required_amount &&
                    left.available_mask == right.available_mask && left.selected_mask == right.selected_mask &&
                    left.continuation_steps == right.continuation_steps &&
                    left.exact_sum == right.exact_sum && left.greater_sum == right.greater_sum &&
                    left.can_finish == right.can_finish && left.can_cancel == right.can_cancel,
                context + " continuation state drifted");
    }
}

ActionSelection selection_for(const DecisionFrame& frame) {
    return {frame.contract_id, frame.episode_semantic_id, frame.public_semantic_decision_id,
            frame.submission_token, frame.request.candidates.front().public_action_key};
}

void close_with_interrupt(EpisodicEnvironment& environment) {
    const auto result = environment.interrupt(
        InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId),
                         InterruptionReason::AdministrativeCancel});
    require(std::holds_alternative<InterruptAccepted>(result),
            "recovered environment could not be closed administratively");
}

void test_reset_after_failure() {
    auto environment = make_environment();
    const auto control = make_control("reset-after-failure-test");
    EpisodeSpec first_spec;
    first_spec.root_seed = 2;

    detail::EpisodicEnvironmentTestAccess::force_next_reset_failure(*environment);
    const auto failed_reset = environment->reset(first_spec, control);
    require(std::holds_alternative<ResetAccepted>(failed_reset),
            "forced failure reset did not return an accepted closure");
    const auto& failed_next = std::get<ResetAccepted>(failed_reset).next;
    require(std::holds_alternative<EpisodeFailure>(failed_next),
            "forced failure did not map to EpisodeFailure");
    const auto& failure = std::get<EpisodeFailure>(failed_next);
    require(failure.failure_code == FailureCode::UnsupportedProtocol &&
                failure.failure_stage == FailureStage::Advance &&
                !failure.mutation_may_have_occurred && failure.semantic_action_count == 0,
            "forced failure returned the wrong typed closure evidence");
    require(environment->lifecycle() == Lifecycle::Failed,
            "forced failure did not close the public lifecycle");

    auto fresh = make_environment();
    const auto fresh_reset = fresh->reset(first_spec, control);
    require(std::holds_alternative<ResetAccepted>(fresh_reset),
            "fresh reference reset was rejected");
    const auto& fresh_frame = frame_of(std::get<ResetAccepted>(fresh_reset).next, "fresh reference");

    const auto recovered_reset = environment->reset(first_spec, control);
    require(std::holds_alternative<ResetAccepted>(recovered_reset),
            "reset after failure was rejected");
    const auto& recovered_frame = frame_of(std::get<ResetAccepted>(recovered_reset).next,
                                           "recovered reset");
    require_same_public_frame(fresh_frame, recovered_frame, "reset after failure");
    require(recovered_frame.submission_token != fresh_frame.submission_token,
            "reset after failure reused the reference submission token");
    const auto recovered_selection = selection_for(recovered_frame);
    close_with_interrupt(*environment);

    EpisodeSpec second_spec;
    second_spec.root_seed = 1;
    second_spec.seat_assignment = SeatAssignment::Mirror;
    second_spec.starting_player = 1;
    const auto second_reset = environment->reset(second_spec, control);
    require(std::holds_alternative<ResetAccepted>(second_reset),
            "different-spec reset after failure was rejected");
    const auto& second_frame = frame_of(std::get<ResetAccepted>(second_reset).next,
                                        "different-spec reset");
    require(second_frame.episode_semantic_id != recovered_frame.episode_semantic_id,
            "different-spec reset reused the failed episode identity");
    const auto stale = environment->step(recovered_selection);
    require(std::holds_alternative<StepRejected>(stale) &&
                std::get<StepRejected>(stale).rejection_code == RejectionCode::WrongEpisode &&
                std::get<StepRejected>(stale).authoritative_state_unchanged,
            "selection from the recovered episode did not reject before mutation");
    close_with_interrupt(*environment);

    const auto final_reset = environment->reset(first_spec, control);
    require(std::holds_alternative<ResetAccepted>(final_reset),
            "same-spec reset after a later episode was rejected");
    const auto& final_frame = frame_of(std::get<ResetAccepted>(final_reset).next, "final reset");
    require_same_public_frame(fresh_frame, final_frame, "final reset after failure");
    const auto final_stale = environment->step(recovered_selection);
    require(std::holds_alternative<StepRejected>(final_stale) &&
                std::get<StepRejected>(final_stale).rejection_code == RejectionCode::StaleSubmissionToken &&
                std::get<StepRejected>(final_stale).authoritative_state_unchanged,
            "same-spec reset after failure did not reject the old token before mutation");
}

}  // namespace

int main() {
    try {
        test_reset_after_failure();
        std::cout << "episodic_reset_after_failure_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
