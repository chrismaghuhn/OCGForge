#include "ygo/environment/episodic_environment.hpp"

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

void test_owned_first_frame() {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "canonical environment factory rejected its certified config");
    auto environment = std::move(std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));

    ygo::environment::EpisodeSpec spec;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "canonical environment reset was not accepted");
    const auto& accepted = std::get<ygo::environment::ResetAccepted>(reset);
    require(std::holds_alternative<ygo::environment::DecisionFrame>(accepted.next),
            "test seed did not produce an actionable first frame");
    const auto& frame = std::get<ygo::environment::DecisionFrame>(accepted.next);
    require(frame.contract_id == ygo::environment::kEpisodicEnvironmentContractId,
            "frame contract identity changed");
    require(frame.acting_player == frame.request.player &&
                frame.acting_player == frame.observation.perspective_player,
            "frame player/observation coupling was not enforced");
    require(frame.submission_token.valid(), "first frame did not receive a valid token");
    require(!frame.request.candidates.empty(), "first frame candidate domain was empty");

    std::vector<std::string> keys;
    keys.reserve(frame.request.candidates.size());
    for (const auto& candidate : frame.request.candidates) {
        keys.push_back(candidate.semantic_key);
    }
    require(keys.size() == frame.request.candidates.size(), "candidate projection lost an entry");
    require(frame.candidate_domain_digest ==
                ygo::environment::candidate_domain_digest(
                    ygo::environment::environment_decision_kind_name(frame.request.kind), keys),
            "candidate digest was not computed over the owned ordered domain");
    require(environment->lifecycle() == ygo::environment::Lifecycle::AwaitingAction,
            "reset did not leave the environment awaiting action");

    ygo::environment::ActionSelection selection;
    selection.contract_id = frame.contract_id;
    selection.episode_semantic_id = frame.episode_semantic_id;
    selection.semantic_decision_id = frame.semantic_decision_id;
    selection.submission_token = frame.submission_token;
    selection.semantic_key = frame.request.candidates.front().semantic_key;
    const auto step = environment->step(selection);
    require(std::holds_alternative<ygo::environment::StepAccepted>(step),
            "a complete public candidate was not accepted by step");
    const auto& step_accepted = std::get<ygo::environment::StepAccepted>(step);
    require(step_accepted.transition.selected_semantic_key == selection.semantic_key,
            "accepted transition changed the selected semantic key");
    require(step_accepted.transition.core_response_submitted,
            "atomic first candidate did not report its response submission");
}

}  // namespace

int main() {
    try {
        test_owned_first_frame();
        std::cout << "episodic_environment_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
