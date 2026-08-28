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
            "canonical environment factory rejected the budget fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

ActionSelection selection_for(const DecisionFrame& frame) {
    ActionSelection selection;
    selection.contract_id = frame.contract_id;
    selection.episode_semantic_id = frame.episode_semantic_id;
    selection.public_semantic_decision_id = frame.public_semantic_decision_id;
    selection.submission_token = frame.submission_token;
    selection.public_action_key = frame.request.candidates.front().public_action_key;
    return selection;
}

const DecisionFrame& frame_of(const Next& next) {
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr, "budget fixture did not publish a decision frame");
    return *frame;
}

void test_semantic_action_budget() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 1;
    control.cancellation.source = "semantic-budget-test";

    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(reset), "semantic budget reset was rejected");
    const auto& frame = frame_of(std::get<ResetAccepted>(reset).next);
    const auto result = environment->step(selection_for(frame));
    require(std::holds_alternative<StepAccepted>(result),
            "the action at the semantic budget boundary was rejected");
    const auto& accepted = std::get<StepAccepted>(result);
    require(accepted.transition.decision_index == frame.decision_index,
            "accepted transition changed the source decision index");
    require(std::holds_alternative<EpisodeInterrupted>(accepted.next),
            "semantic budget did not close as an interruption");
    const auto& interruption = std::get<EpisodeInterrupted>(accepted.next);
    require(interruption.reason == InterruptionReason::SemanticActionBudget,
            "semantic budget returned the wrong interruption reason");
    require(interruption.semantic_action_count == 1,
            "semantic budget counted the accepted action incorrectly");
    require(!interruption.last_public_semantic_decision_id.has_value() ||
                *interruption.last_public_semantic_decision_id == frame.public_semantic_decision_id,
            "semantic budget lost the last valid public decision identity");
    require(environment->lifecycle() == Lifecycle::Interrupted,
            "semantic budget did not close the public lifecycle");
    require(!environment->perspective_terminal_view(0).has_value(),
            "semantic budget fabricated a terminal view");
}

void test_engine_process_budget() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 1;
    control.semantic_action_budget = 512;
    control.cancellation.source = "process-budget-test";

    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(reset), "process budget reset was rejected");
    const auto& next = std::get<ResetAccepted>(reset).next;
    require(std::holds_alternative<EpisodeInterrupted>(next),
            "process budget did not close before exceeding its limit");
    const auto& interruption = std::get<EpisodeInterrupted>(next);
    require(interruption.reason == InterruptionReason::EngineProcessBudget,
            "process budget returned the wrong interruption reason");
    require(interruption.run_control_evidence.engine_process_count <= 1,
            "engine process count exceeded the configured budget");
    require(!environment->perspective_terminal_view(0).has_value(),
            "process budget fabricated a terminal view");
    require(environment->lifecycle() == Lifecycle::Interrupted,
            "process budget did not close the public lifecycle");
}

void test_zero_budget_is_rejected_before_construction() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 0;
    control.semantic_action_budget = 1;
    const auto result = environment->reset(spec, control);
    require(std::holds_alternative<ResetRejected>(result), "zero process budget was accepted");
    require(std::get<ResetRejected>(result).rejection_code == ResetRejectionCode::InvalidRunControl,
            "zero process budget returned the wrong rejection code");
    require(environment->lifecycle() == Lifecycle::Empty,
            "invalid run control changed lifecycle before construction");
}

}  // namespace

int main() {
    try {
        test_semantic_action_budget();
        test_engine_process_budget();
        test_zero_budget_is_rejected_before_construction();
        std::cout << "episodic_budget_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
