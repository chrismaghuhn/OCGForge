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

ygo::environment::DecisionFrame reset_to_frame(
    const ygo::environment::CertifiedEnvironmentConfig& config,
    const std::string& source) {
    auto factory = ygo::environment::EpisodicEnvironment::create(config);
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "environment factory rejected routing test configuration");
    auto environment = std::move(
        std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));
    ygo::environment::EpisodeSpec spec;
    spec.contract_id = config.contract_id;
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 64;
    control.semantic_action_budget = 64;
    control.cancellation.source = source;
    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "routing test reset was rejected");
    const auto& next = std::get<ygo::environment::ResetAccepted>(reset).next;
    const auto* frame = std::get_if<ygo::environment::DecisionFrame>(&next);
    require(frame != nullptr, "routing test reset did not publish a decision frame");
    return *frame;
}

void test_environment_generation_selects_decoder_path() {
    const auto historical = reset_to_frame(
        ygo::environment::CertifiedEnvironmentConfig::canonical_v3(), "v3-routing");
    const auto corrected = reset_to_frame(
        ygo::environment::CertifiedEnvironmentConfig::canonical_v4(), "v4-routing");
    require(historical.contract_id == ygo::environment::kEpisodicEnvironmentV3ContractId &&
                corrected.contract_id == ygo::environment::kEpisodicEnvironmentV4ContractId,
            "routing test published unexpected environment generations");
    require(!historical.request.candidates.empty() &&
                !corrected.request.candidates.empty() &&
                historical.request.candidates.front().public_action_key.rfind(
                    "public_action.v2.", 0) == 0 &&
                corrected.request.candidates.front().public_action_key.rfind(
                    "public_action.v3.", 0) == 0,
            "environment generation did not select its public decoder path");
}

}  // namespace

int main() {
    try {
        test_environment_generation_selects_decoder_path();
        std::cout << "episode driver V4 routing tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
