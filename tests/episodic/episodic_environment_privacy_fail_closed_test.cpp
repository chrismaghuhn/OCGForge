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

void test_hidden_opponent_identity_uses_redacted_public_projection() {
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
    constexpr std::uint64_t kHistoricalPrefix = 220;
    std::uint64_t accepted_actions = 0;
    while (accepted_actions <= kHistoricalPrefix) {
        const auto* frame = decision_frame(next);
        require(frame != nullptr, "V2 privacy fixture closed before the historical boundary");
        require(!frame->request.candidates.empty(), "privacy fixture published an empty domain");
        require(frame->decision_index == frame->public_observation.decision_index,
                "V2 public decision index was not coupled to the public observation");

        ygo::environment::ActionSelection selection;
        selection.contract_id = frame->contract_id;
        selection.episode_semantic_id = frame->episode_semantic_id;
        selection.public_semantic_decision_id = frame->public_semantic_decision_id;
        selection.submission_token = frame->submission_token;
        selection.public_action_key = frame->request.candidates.front().public_action_key;
        auto step = environment->step(selection);
        require(std::holds_alternative<ygo::environment::StepAccepted>(step),
                "V2 privacy fixture selection was rejected before the historical boundary");
        const auto& accepted = std::get<ygo::environment::StepAccepted>(step);
        ++accepted_actions;
        require(!std::holds_alternative<ygo::environment::EpisodeFailure>(accepted.next),
                "V2 hidden-card projection still failed closed");
        if (accepted_actions > kHistoricalPrefix) {
            break;
        }
        const auto* next_frame = decision_frame(accepted.next);
        require(next_frame != nullptr, "V2 privacy fixture closed before the historical boundary");
        require(next_frame->decision_index == frame->decision_index + 1,
                "V2 public decision index did not advance exactly once");
        require(next_frame->submission_token != frame->submission_token,
                "V2 public frame token was reused");
        next = accepted.next;
    }
    require(accepted_actions == kHistoricalPrefix + 1,
            "V2 privacy fixture did not cross the historical hidden-card boundary");
    std::cout << "episodic_environment_privacy_redacted_after_actions=" << accepted_actions << '\n';
}

}  // namespace

int main() {
    try {
        test_hidden_opponent_identity_uses_redacted_public_projection();
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
