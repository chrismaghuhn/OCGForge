#include "ygo/environment/episodic_environment.hpp"

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
            "canonical environment factory rejected the lifecycle fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

RunControl make_control() {
    RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 512;
    control.cancellation.source = "lifecycle-test";
    return control;
}

const DecisionFrame& frame_of(const Next& next) {
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr, "lifecycle fixture did not publish a decision frame");
    return *frame;
}

ActionSelection selection_for(const DecisionFrame& frame, const std::string& key) {
    ActionSelection selection;
    selection.contract_id = frame.contract_id;
    selection.episode_semantic_id = frame.episode_semantic_id;
    selection.public_semantic_decision_id = frame.public_semantic_decision_id;
    selection.submission_token = frame.submission_token;
    selection.public_action_key = key;
    return selection;
}

void test_lifecycle_and_freshness() {
    auto environment = make_environment();
    const auto control = make_control();
    EpisodeSpec spec;
    spec.root_seed = 2;

    ActionSelection before_reset;
    before_reset.contract_id = std::string(kEpisodicEnvironmentV2ContractId);
    const auto empty_step = environment->step(before_reset);
    require(std::holds_alternative<StepRejected>(empty_step), "step in EMPTY was accepted");
    require(std::get<StepRejected>(empty_step).rejection_code == RejectionCode::InvalidLifecycle,
            "step in EMPTY returned the wrong rejection code");
    require(environment->lifecycle() == Lifecycle::Empty, "rejected EMPTY step changed lifecycle");

    const auto first_reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(first_reset), "first lifecycle reset was rejected");
    const auto first_frame = frame_of(std::get<ResetAccepted>(first_reset).next);
    const auto first_frame_copy = first_frame;
    require(environment->lifecycle() == Lifecycle::AwaitingAction, "reset did not enter AWAITING_ACTION");

    const auto reset_while_awaiting = environment->reset(spec, control);
    require(std::holds_alternative<ResetRejected>(reset_while_awaiting),
            "reset while awaiting an action was accepted");
    require(std::get<ResetRejected>(reset_while_awaiting).rejection_code ==
                ResetRejectionCode::ResetWhileAwaitingAction,
            "reset while awaiting returned the wrong rejection code");
    require(environment->lifecycle() == Lifecycle::AwaitingAction,
            "rejected reset changed the live lifecycle");

    const auto first_selection = selection_for(first_frame_copy, first_frame_copy.request.candidates.front().public_action_key);
    const auto interrupted = environment->interrupt(
        InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId), InterruptionReason::AdministrativeCancel});
    require(std::holds_alternative<InterruptAccepted>(interrupted),
            "administrative interrupt at a live boundary was rejected");
    require(std::get<InterruptAccepted>(interrupted).interruption.reason ==
                InterruptionReason::AdministrativeCancel,
            "administrative interrupt returned the wrong reason");
    require(environment->lifecycle() == Lifecycle::Interrupted,
            "administrative interrupt did not close the episode");
    require(!environment->perspective_terminal_view(0).has_value() &&
                !environment->perspective_terminal_view(1).has_value(),
            "interrupted episode exposed a terminal view");

    const auto after_interrupt_step = environment->step(first_selection);
    require(std::holds_alternative<StepRejected>(after_interrupt_step),
            "step after administrative interruption was accepted");
    require(std::get<StepRejected>(after_interrupt_step).rejection_code == RejectionCode::InvalidLifecycle,
            "step after interruption returned the wrong rejection code");

    const auto second_reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(second_reset),
            "reset after interruption was rejected");
    const auto second_frame = frame_of(std::get<ResetAccepted>(second_reset).next);
    require(second_frame.episode_semantic_id == first_frame_copy.episode_semantic_id,
            "identical reset changed the episode semantic identity");
    require(second_frame.public_semantic_decision_id == first_frame_copy.public_semantic_decision_id,
            "identical reset changed the first public decision identity");
    require(second_frame.public_observation_digest == first_frame_copy.public_observation_digest,
            "identical reset changed the first public observation digest");
    require(second_frame.public_candidate_domain_digest == first_frame_copy.public_candidate_domain_digest,
            "identical reset changed the first public domain digest");
    require(second_frame.request.candidates.size() == first_frame_copy.request.candidates.size(),
            "identical reset changed the first public domain size");
    for (std::size_t index = 0; index < second_frame.request.candidates.size(); ++index) {
        require(second_frame.request.candidates[index].public_action_key ==
                    first_frame_copy.request.candidates[index].public_action_key,
                "identical reset changed public candidate order or membership");
    }
    require(second_frame.submission_token.valid() && first_frame_copy.submission_token.valid() &&
                second_frame.submission_token != first_frame_copy.submission_token,
            "identical reset did not issue a fresh submission token");
    require(second_frame.decision_index == second_frame.public_observation.decision_index &&
                first_frame_copy.decision_index == first_frame_copy.public_observation.decision_index,
            "decision/observation index coupling was lost across reset");

    const auto stale = environment->step(first_selection);
    require(std::holds_alternative<StepRejected>(stale),
            "selection from the prior reset incarnation was accepted");
    require(std::get<StepRejected>(stale).rejection_code == RejectionCode::StaleSubmissionToken,
            "prior-incarnation selection returned the wrong rejection code");
    require(std::get<StepRejected>(stale).authoritative_state_unchanged,
            "stale-token rejection did not certify zero mutation");

    const auto third_reset_rejected = environment->reset(spec, control);
    require(std::holds_alternative<ResetRejected>(third_reset_rejected),
            "reset while the second incarnation was awaiting an action was accepted");
    require(std::get<ResetRejected>(third_reset_rejected).rejection_code ==
                ResetRejectionCode::ResetWhileAwaitingAction,
            "second reset while awaiting returned the wrong rejection code");
}

}  // namespace

int main() {
    try {
        test_lifecycle_and_freshness();
        std::cout << "episodic_lifecycle_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
