#include "ygo/environment/episodic_environment.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>

namespace {

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const ygo::environment::DecisionFrame* decision_frame(
    const std::variant<ygo::environment::DecisionFrame, ygo::environment::EpisodeTerminal,
                       ygo::environment::EpisodeInterrupted, ygo::environment::EpisodeFailure>& next) {
    return std::get_if<ygo::environment::DecisionFrame>(&next);
}

std::string describe_failure(const ygo::environment::EpisodeFailure& failure) {
    return std::string(ygo::environment::failure_code_name(failure.failure_code)) + "/" +
           std::string(ygo::environment::failure_stage_name(failure.failure_stage));
}

void test_unsafe_opponent_identity_fails_closed() {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "canonical environment factory rejected its certified config");
    auto environment = std::move(std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));

    ygo::environment::EpisodeSpec spec;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 512;
    auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "privacy fixture reset was rejected");
    auto next = std::move(std::get<ygo::environment::ResetAccepted>(reset).next);
    std::uint64_t accepted_actions = 0;
    for (;;) {
        const auto* frame = decision_frame(next);
        require(frame != nullptr, "privacy fixture closed before reaching its hidden-identity boundary");
        require(!frame->request.candidates.empty(), "privacy fixture published an empty domain");

        ygo::environment::ActionSelection selection;
        selection.contract_id = frame->contract_id;
        selection.episode_semantic_id = frame->episode_semantic_id;
        selection.semantic_decision_id = frame->semantic_decision_id;
        selection.submission_token = frame->submission_token;
        selection.semantic_key = frame->request.candidates.front().semantic_key;
        auto step = environment->step(selection);
        require(std::holds_alternative<ygo::environment::StepAccepted>(step),
                "privacy fixture selection was rejected before the unsafe boundary");
        const auto& accepted = std::get<ygo::environment::StepAccepted>(step);
        ++accepted_actions;
        if (const auto* failure = std::get_if<ygo::environment::EpisodeFailure>(&accepted.next)) {
            require(failure->failure_code == ygo::environment::FailureCode::PrivacyInvariant,
                    "unsafe candidate domain did not fail with PRIVACY_INVARIANT");
            require(failure->failure_stage == ygo::environment::FailureStage::Projection,
                    "unsafe candidate domain failed at an unexpected stage");
            require(environment->lifecycle() == ygo::environment::Lifecycle::Failed,
                    "privacy failure did not close the environment");
            std::cout << "episodic_environment_privacy_fail_closed_after_actions=" << accepted_actions << '\n';
            return;
        }
        const auto* next_frame = decision_frame(accepted.next);
        require(next_frame != nullptr, "privacy fixture closed without a typed privacy failure");
        require(next_frame->decision_index == frame->decision_index + 1,
                "public decision index did not advance exactly once");
        require(next_frame->submission_token != frame->submission_token,
                "public frame token was reused");
        next = accepted.next;
        require(accepted_actions < 256, "privacy fixture did not reach its boundary in time");
    }
}

}  // namespace

int main() {
    try {
        test_unsafe_opponent_identity_fails_closed();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
