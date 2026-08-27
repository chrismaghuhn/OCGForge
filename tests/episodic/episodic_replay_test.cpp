#include "ygo/environment/episodic_environment.hpp"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

namespace {

using namespace ygo::environment;
using Next = std::variant<DecisionFrame, EpisodeTerminal, EpisodeInterrupted, EpisodeFailure>;

struct FrameRecord final {
    std::uint64_t decision_index = 0;
    std::uint64_t engine_step_index = 0;
    std::uint8_t acting_player = 0;
    std::string public_semantic_decision_id;
    std::string public_observation_digest;
    std::string public_candidate_domain_digest;
    std::vector<std::string> public_action_keys;
    std::vector<std::uint8_t> safe_state_bytes;
};

struct ReplayRecord final {
    std::string environment_semantic_id;
    std::string episode_semantic_id;
    std::vector<FrameRecord> frames;
    std::vector<std::string> accepted_public_action_keys;
    std::vector<bool> core_response_submitted;
};

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::unique_ptr<EpisodicEnvironment> make_environment() {
    auto factory = EpisodicEnvironment::create(CertifiedEnvironmentConfig::canonical());
    require(std::holds_alternative<std::unique_ptr<EpisodicEnvironment>>(factory),
            "canonical environment factory rejected the replay fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

RunControl make_control() {
    RunControl control;
    control.engine_process_budget = 4096;
    control.semantic_action_budget = 4096;
    control.cancellation.source = "replay-test";
    return control;
}

const DecisionFrame& frame_of(const Next& next) {
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr, "replay run closed before its requested prefix");
    return *frame;
}

FrameRecord record_frame(const DecisionFrame& frame) {
    FrameRecord result;
    result.decision_index = frame.decision_index;
    result.engine_step_index = frame.engine_step_index;
    result.acting_player = frame.acting_player;
    result.public_semantic_decision_id = frame.public_semantic_decision_id;
    result.public_observation_digest = frame.public_observation_digest;
    result.public_candidate_domain_digest = frame.public_candidate_domain_digest;
    result.safe_state_bytes = frame.public_observation.canonical_safe_state_bytes();
    for (const auto& candidate : frame.request.candidates) {
        result.public_action_keys.push_back(candidate.public_action_key);
    }
    return result;
}

ActionSelection selection_for(const DecisionFrame& frame, const std::string& public_key) {
    return ActionSelection{frame.contract_id, frame.episode_semantic_id,
                           frame.public_semantic_decision_id, frame.submission_token, public_key};
}

ReplayRecord run_and_record(EpisodicEnvironment& environment, const EpisodeSpec& spec,
                            const std::size_t action_limit) {
    const auto reset = environment.reset(spec, make_control());
    require(std::holds_alternative<ResetAccepted>(reset), "replay recording reset was rejected");
    const auto& accepted = std::get<ResetAccepted>(reset);
    const auto& initial_frame = frame_of(accepted.next);
    ReplayRecord result;
    result.environment_semantic_id = environment.config().environment_semantic_id;
    result.episode_semantic_id = initial_frame.episode_semantic_id;
    Next next = accepted.next;
    for (std::size_t index = 0; index < action_limit; ++index) {
        const auto& frame = frame_of(next);
        result.frames.push_back(record_frame(frame));
        const auto public_key = frame.request.candidates.front().public_action_key;
        result.accepted_public_action_keys.push_back(public_key);
        const auto step = environment.step(selection_for(frame, public_key));
        require(std::holds_alternative<StepAccepted>(step),
                "public action was rejected while recording replay");
        const auto& step_accepted = std::get<StepAccepted>(step);
        result.core_response_submitted.push_back(step_accepted.transition.core_response_submitted);
        require(step_accepted.transition.selected_public_action_key == public_key,
                "recording changed the selected public action key");
        next = step_accepted.next;
    }
    return result;
}

void compare_frame(const FrameRecord& expected, const DecisionFrame& actual,
                   const std::size_t index) {
    const auto prefix = "replay frame " + std::to_string(index) + ": ";
    require(actual.decision_index == expected.decision_index, prefix + "decision index drifted");
    require(actual.engine_step_index == expected.engine_step_index, prefix + "engine step index drifted");
    require(actual.acting_player == expected.acting_player, prefix + "acting player drifted");
    require(actual.public_semantic_decision_id == expected.public_semantic_decision_id,
            prefix + "public decision identity drifted");
    require(actual.public_observation_digest == expected.public_observation_digest,
            prefix + "public observation digest drifted");
    require(actual.public_candidate_domain_digest == expected.public_candidate_domain_digest,
            prefix + "public candidate digest drifted");
    require(actual.public_observation.canonical_safe_state_bytes() == expected.safe_state_bytes,
            prefix + "public safe state drifted");
    require(actual.request.candidates.size() == expected.public_action_keys.size(),
            prefix + "public domain cardinality drifted");
    for (std::size_t candidate = 0; candidate < expected.public_action_keys.size(); ++candidate) {
        require(actual.request.candidates[candidate].public_action_key ==
                    expected.public_action_keys[candidate],
                prefix + "public domain order or membership drifted");
    }
}

void replay_public_keys_only(const ReplayRecord& record, EpisodicEnvironment& environment,
                             const EpisodeSpec& spec) {
    const auto reset = environment.reset(spec, make_control());
    require(std::holds_alternative<ResetAccepted>(reset), "public replay reset was rejected");
    Next next = std::get<ResetAccepted>(reset).next;
    for (std::size_t index = 0; index < record.accepted_public_action_keys.size(); ++index) {
        const auto& frame = frame_of(next);
        compare_frame(record.frames[index], frame, index);
        const auto step = environment.step(
            selection_for(frame, record.accepted_public_action_keys[index]));
        require(std::holds_alternative<StepAccepted>(step),
                "recorded public action was not accepted during replay");
        const auto& accepted = std::get<StepAccepted>(step);
        require(accepted.transition.selected_public_action_key ==
                    record.accepted_public_action_keys[index],
                "replay transition changed the public action key");
        require(accepted.transition.core_response_submitted ==
                    record.core_response_submitted[index],
                "replay response-submission classification drifted");
        next = accepted.next;
    }
}

void test_public_replay() {
    EpisodeSpec spec;
    spec.root_seed = 2;
    auto recording_environment = make_environment();
    const auto record = run_and_record(*recording_environment, spec, 16);

    auto replay_environment = make_environment();
    require(replay_environment->config().environment_semantic_id == record.environment_semantic_id,
            "public replay used a different certified environment identity");
    replay_public_keys_only(record, *replay_environment, spec);
    require(replay_environment->lifecycle() == Lifecycle::AwaitingAction,
            "replay did not preserve the live prefix boundary");
}

}  // namespace

int main() {
    try {
        test_public_replay();
        std::cout << "episodic_replay_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
