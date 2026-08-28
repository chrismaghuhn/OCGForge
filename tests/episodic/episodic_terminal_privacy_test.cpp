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
            "canonical environment factory rejected the terminal/privacy fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

void test_terminal_views_are_closed_until_true_terminal() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 512;
    control.cancellation.source = "terminal-privacy-test";

    require(!environment->perspective_terminal_view(0).has_value(),
            "EMPTY environment exposed a terminal view for player zero");
    require(!environment->perspective_terminal_view(1).has_value(),
            "EMPTY environment exposed a terminal view for player one");
    require(!environment->perspective_terminal_view(2).has_value(),
            "invalid player exposed a terminal view");

    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(reset), "terminal/privacy reset was rejected");
    require(!environment->perspective_terminal_view(0).has_value() &&
                !environment->perspective_terminal_view(1).has_value(),
            "live environment exposed a terminal view");

    const auto interrupted = environment->interrupt(
        InterruptRequest{std::string(kEpisodicEnvironmentV2ContractId),
                         InterruptionReason::AdministrativeCancel});
    require(std::holds_alternative<InterruptAccepted>(interrupted),
            "terminal/privacy administrative cancellation was rejected");
    require(!environment->perspective_terminal_view(0).has_value() &&
                !environment->perspective_terminal_view(1).has_value(),
            "interrupted environment exposed a terminal view");
}

void test_process_budget_is_not_a_terminal() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 1;
    control.semantic_action_budget = 512;
    control.cancellation.source = "terminal-privacy-budget-test";
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(reset), "budget privacy reset was rejected");
    require(std::holds_alternative<EpisodeInterrupted>(std::get<ResetAccepted>(reset).next),
            "budget privacy fixture did not interrupt");
    require(!environment->perspective_terminal_view(0).has_value() &&
                !environment->perspective_terminal_view(1).has_value(),
            "process-budget interruption exposed a terminal view");
}

void test_true_terminal_materializes_both_perspective_views() {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    RunControl control;
    control.engine_process_budget = 20000;
    control.semantic_action_budget = 20000;
    control.cancellation.source = "terminal-view-test";
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ResetAccepted>(reset), "true-terminal reset was rejected");
    Next next = std::get<ResetAccepted>(reset).next;

    for (std::uint64_t action_count = 0; action_count < 800; ++action_count) {
        const auto* frame = std::get_if<DecisionFrame>(&next);
        require(frame != nullptr, "true-terminal fixture closed before reaching the engine terminal");
        const auto step = environment->step(ActionSelection{
            frame->contract_id, frame->episode_semantic_id, frame->public_semantic_decision_id,
            frame->submission_token, frame->request.candidates.front().public_action_key});
        require(std::holds_alternative<StepAccepted>(step),
                "true-terminal public action was rejected");
        next = std::get<StepAccepted>(step).next;
        if (!std::holds_alternative<EpisodeTerminal>(next)) {
            continue;
        }

        require(environment->lifecycle() == Lifecycle::GameTerminal,
                "true engine terminal did not enter GAME_TERMINAL");
        const auto player_zero = environment->perspective_terminal_view(0);
        const auto player_one = environment->perspective_terminal_view(1);
        require(player_zero.has_value() && player_one.has_value(),
                "true engine terminal did not materialize both perspective views");
        require(player_zero->perspective_player == 0 && player_one->perspective_player == 1,
                "terminal view perspective identity was incorrect");
        require(!player_zero->canonical_safe_state_bytes().empty() &&
                    !player_one->canonical_safe_state_bytes().empty(),
                "terminal view did not contain a canonical public safe state");
        require(public_observation_digest(*player_zero) != public_observation_digest(*player_one),
                "distinct terminal perspectives unexpectedly collapsed to one public view");
        require(!environment->perspective_terminal_view(2).has_value(),
                "invalid terminal perspective returned a view");
        return;
    }
    throw std::runtime_error("true-terminal fixture did not reach an engine terminal within 800 actions");
}

void test_incompatible_public_identity_is_rejected_before_reset() {
    auto config = CertifiedEnvironmentConfig::canonical();
    config.public_action_identity_schema_id = "ocgforge.action_identity.v1";
    const auto factory = EpisodicEnvironment::create(std::move(config));
    require(std::holds_alternative<EnvironmentFactoryRejected>(factory),
            "mixed public/internal action identity set was accepted");
    require(std::get<EnvironmentFactoryRejected>(factory).rejection_code ==
                ResetRejectionCode::InvalidEnvironmentId,
            "mixed public/internal action identity set returned the wrong code");
}

}  // namespace

int main() {
    try {
        test_terminal_views_are_closed_until_true_terminal();
        test_process_budget_is_not_a_terminal();
        test_true_terminal_materializes_both_perspective_views();
        test_incompatible_public_identity_is_rejected_before_reset();
        std::cout << "episodic_terminal_privacy_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
