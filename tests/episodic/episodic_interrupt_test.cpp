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
            "canonical environment factory rejected the interrupt fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

RunControl make_control() {
    RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 512;
    control.cancellation.source = "interrupt-test";
    return control;
}

const DecisionFrame& frame_of(const Next& next) {
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr, "interrupt fixture did not publish a decision frame");
    return *frame;
}

void test_interrupt_is_explicit_and_non_gameplay() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    const auto reset = environment->reset(spec, make_control());
    require(std::holds_alternative<ResetAccepted>(reset), "interrupt reset was rejected");
    const auto frame = frame_of(std::get<ResetAccepted>(reset).next);

    const auto unsupported = environment->interrupt(
        InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId), InterruptionReason::EngineProcessBudget});
    require(std::holds_alternative<InterruptRejected>(unsupported),
            "non-administrative interruption reason was accepted by the public facade");
    require(std::get<InterruptRejected>(unsupported).rejection_code ==
                RejectionCode::UnsupportedInterruptionReason,
            "unsupported interruption returned the wrong rejection code");
    require(environment->lifecycle() == Lifecycle::AwaitingAction,
            "unsupported interruption changed lifecycle");

    const auto wrong_contract = environment->interrupt(
        InterruptRequest{"ocgforge.episodic_environment.v1", InterruptionReason::AdministrativeCancel});
    require(std::holds_alternative<InterruptRejected>(wrong_contract),
            "incompatible interruption contract was accepted");
    require(std::get<InterruptRejected>(wrong_contract).rejection_code == RejectionCode::IncompatibleContract,
            "incompatible interruption returned the wrong rejection code");

    const auto accepted = environment->interrupt(
        InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId), InterruptionReason::AdministrativeCancel});
    require(std::holds_alternative<InterruptAccepted>(accepted),
            "administrative cancellation was not accepted at the live boundary");
    const auto& interruption = std::get<InterruptAccepted>(accepted).interruption;
    require(interruption.reason == InterruptionReason::AdministrativeCancel,
            "administrative cancellation has the wrong typed reason");
    require(interruption.semantic_action_count == 0,
            "administrative cancellation fabricated a semantic action");
    require(environment->lifecycle() == Lifecycle::Interrupted,
            "administrative cancellation did not enter INTERRUPTED");
    require(!environment->perspective_terminal_view(0).has_value() &&
                !environment->perspective_terminal_view(1).has_value(),
            "administrative cancellation exposed a terminal view");

    ActionSelection stale;
    stale.contract_id = frame.contract_id;
    stale.episode_semantic_id = frame.episode_semantic_id;
    stale.public_semantic_decision_id = frame.public_semantic_decision_id;
    stale.submission_token = frame.submission_token;
    stale.public_action_key = frame.request.candidates.front().public_action_key;
    const auto after_interrupt = environment->step(stale);
    require(std::holds_alternative<StepRejected>(after_interrupt),
            "step after interruption was accepted");
    require(std::get<StepRejected>(after_interrupt).rejection_code == RejectionCode::InvalidLifecycle,
            "step after interruption returned the wrong rejection code");
}

}  // namespace

int main() {
    try {
        test_interrupt_is_explicit_and_non_gameplay();
        std::cout << "episodic_interrupt_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
