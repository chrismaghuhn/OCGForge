#include "ygo/environment/episodic_environment.hpp"
#include "ygo/environment/public_action_identity.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using Next = std::variant<ygo::environment::DecisionFrame, ygo::environment::EpisodeTerminal,
                          ygo::environment::EpisodeInterrupted, ygo::environment::EpisodeFailure>;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const ygo::environment::DecisionFrame* frame_of(const Next& next) {
    return std::get_if<ygo::environment::DecisionFrame>(&next);
}

std::string describe_next(const Next& next) {
    if (const auto* terminal = std::get_if<ygo::environment::EpisodeTerminal>(&next)) {
        return "terminal actions=" + std::to_string(terminal->semantic_action_count);
    }
    if (const auto* interrupted = std::get_if<ygo::environment::EpisodeInterrupted>(&next)) {
        return "interrupted actions=" + std::to_string(interrupted->semantic_action_count);
    }
    if (const auto* failure = std::get_if<ygo::environment::EpisodeFailure>(&next)) {
        return "failure code=" + std::string(ygo::environment::failure_code_name(failure->failure_code)) +
               " stage=" + std::string(ygo::environment::failure_stage_name(failure->failure_stage)) +
               " actions=" + std::to_string(failure->semantic_action_count);
    }
    return "decision";
}

void test_hidden_opponent_card_uses_complete_redacted_public_projection() {
    auto factory = ygo::environment::EpisodicEnvironment::create(
        ygo::environment::CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory),
            "canonical V2 environment factory rejected its certified config");
    auto environment = std::move(std::get<std::unique_ptr<ygo::environment::EpisodicEnvironment>>(factory));

    ygo::environment::EpisodeSpec spec;
    spec.contract_id = std::string(ygo::environment::kEpisodicEnvironmentV2ContractId);
    spec.root_seed = 2;
    ygo::environment::RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 512;
    control.cancellation.source = "v2-regression";

    const auto reset = environment->reset(spec, control);
    require(std::holds_alternative<ygo::environment::ResetAccepted>(reset),
            "V2 privacy regression reset was rejected: " +
                (std::holds_alternative<ygo::environment::ResetRejected>(reset)
                     ? std::string(ygo::environment::reset_rejection_code_name(
                           std::get<ygo::environment::ResetRejected>(reset).rejection_code))
                     : "unexpected reset result"));
    Next next = std::get<ygo::environment::ResetAccepted>(reset).next;

    constexpr std::uint64_t kExpectedPrefix = 220;
    std::uint64_t accepted_actions = 0;
    for (;;) {
        const auto* frame = frame_of(next);
        require(frame != nullptr, "V2 privacy regression closed before the hidden-card frame: " +
                                     describe_next(next));
        require(frame->contract_id == ygo::environment::kEpisodicEnvironmentV2ContractId,
                "V2 frame exposed the wrong contract identity");
        require(frame->decision_index == frame->public_observation.decision_index,
                "public decision index is not coupled to public observation index");
        require(!frame->request.candidates.empty(), "V2 public candidate domain is empty");

        std::vector<std::string> public_keys;
        public_keys.reserve(frame->request.candidates.size());
        const ygo::environment::EnvironmentActionCandidate* selected = nullptr;
        for (const auto& candidate : frame->request.candidates) {
            require(ygo::environment::is_public_action_key(candidate.public_action_key),
                    "V2 frame contains a malformed public action key");
            require(candidate.public_action_key.find("14821890") == std::string::npos,
                    "V2 public action key leaked the hidden passcode");
            public_keys.push_back(candidate.public_action_key);
            if (accepted_actions == kExpectedPrefix) {
                const auto inspect = [](const std::optional<ygo::environment::PublicCardReference>& reference) {
                    return reference.has_value() &&
                           reference->kind == ygo::environment::PublicCardReferenceKind::RedactedSlot &&
                           reference->observation_locator.rfind("p0:", 0) == 0;
                };
                if (candidate.source_index == 3 &&
                    (inspect(candidate.source_reference) || inspect(candidate.target_reference))) {
                    selected = &candidate;
                }
            }
        }
        require(frame->public_candidate_domain_digest ==
                    ygo::environment::public_candidate_domain_digest(
                        ygo::environment::environment_decision_kind_name(frame->request.kind), public_keys),
                "V2 public candidate digest was not computed from the ordered public domain");

        if (accepted_actions == kExpectedPrefix) {
            require(selected != nullptr,
                    "the historical hidden-opponent-card candidate was not represented as RedactedSlot");
        } else {
            selected = &frame->request.candidates.front();
        }

        ygo::environment::ActionSelection selection;
        selection.contract_id = frame->contract_id;
        selection.episode_semantic_id = frame->episode_semantic_id;
        selection.public_semantic_decision_id = frame->public_semantic_decision_id;
        selection.submission_token = frame->submission_token;
        selection.public_action_key = selected->public_action_key;
        const auto step = environment->step(selection);
        require(std::holds_alternative<ygo::environment::StepAccepted>(step),
                std::holds_alternative<ygo::environment::StepRejected>(step)
                    ? "V2 public action selection was rejected before the hidden-card frame: " +
                          std::string(ygo::environment::rejection_code_name(
                              std::get<ygo::environment::StepRejected>(step).rejection_code))
                    : "V2 public action selection returned an unexpected result");
        const auto& accepted = std::get<ygo::environment::StepAccepted>(step);
        ++accepted_actions;
        if (accepted_actions > kExpectedPrefix) {
            require(!std::holds_alternative<ygo::environment::EpisodeFailure>(accepted.next),
                    "V2 hidden-opponent-card projection still failed closed after RedactedSlot publication");
            return;
        }
        next = accepted.next;
    }
}

}  // namespace

int main() {
    try {
        test_hidden_opponent_card_uses_complete_redacted_public_projection();
        std::cout << "episodic_environment_v2_public_projection_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
