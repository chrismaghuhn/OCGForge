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
            "canonical environment factory rejected the rejection fixture");
    return std::move(std::get<std::unique_ptr<EpisodicEnvironment>>(factory));
}

RunControl make_control() {
    RunControl control;
    control.engine_process_budget = 512;
    control.semantic_action_budget = 512;
    control.cancellation.source = "rejection-test";
    return control;
}

const DecisionFrame& frame_of(const Next& next) {
    const auto* frame = std::get_if<DecisionFrame>(&next);
    require(frame != nullptr, "rejection fixture did not publish a decision frame");
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

void assert_rejection_then_valid(const std::string& label, const ActionSelection& invalid,
                                 const RejectionCode expected) {
    auto environment = make_environment();
    EpisodeSpec spec;
    spec.root_seed = 2;
    const auto reset = environment->reset(spec, make_control());
    require(std::holds_alternative<ResetAccepted>(reset), label + ": reset rejected");
    const auto frame = frame_of(std::get<ResetAccepted>(reset).next);
    const auto valid = selection_for(frame, frame.request.candidates.front().public_action_key);

    const auto rejected = environment->step(invalid);
    require(std::holds_alternative<StepRejected>(rejected), label + ": mutation was accepted");
    const auto& value = std::get<StepRejected>(rejected);
    require(value.rejection_code == expected, label + ": wrong rejection precedence");
    require(value.authoritative_state_unchanged, label + ": rejection did not certify zero mutation");
    require(value.current_episode_semantic_id == frame.episode_semantic_id,
            label + ": current episode identity changed after rejection");
    require(value.current_public_semantic_decision_id == frame.public_semantic_decision_id,
            label + ": current decision identity changed after rejection");
    require(value.current_public_candidate_domain_digest == frame.public_candidate_domain_digest,
            label + ": current domain digest changed after rejection");

    const auto accepted = environment->step(valid);
    require(std::holds_alternative<StepAccepted>(accepted), label + ": valid selection did not remain usable");
}

void test_fixed_rejection_precedence() {
    EpisodeSpec spec;
    spec.root_seed = 2;

    {
        auto environment = make_environment();
        const auto reset = environment->reset(spec, make_control());
        require(std::holds_alternative<ResetAccepted>(reset), "contract rejection setup failed");
        const auto frame = frame_of(std::get<ResetAccepted>(reset).next);
        auto invalid = selection_for(frame, frame.request.candidates.front().public_action_key);
        invalid.contract_id = "ocgforge.episodic_environment.v1";
        const auto rejected = environment->step(invalid);
        require(std::holds_alternative<StepRejected>(rejected), "incompatible contract was accepted");
        require(std::get<StepRejected>(rejected).rejection_code == RejectionCode::IncompatibleContract,
                "incompatible contract returned the wrong code");
    }

    {
        auto environment = make_environment();
        const auto reset = environment->reset(spec, make_control());
        require(std::holds_alternative<ResetAccepted>(reset), "episode rejection setup failed");
        const auto frame = frame_of(std::get<ResetAccepted>(reset).next);
        auto invalid = selection_for(frame, frame.request.candidates.front().public_action_key);
        invalid.episode_semantic_id = std::string(64, 'e');
        assert_rejection_then_valid("wrong episode", invalid, RejectionCode::WrongEpisode);
    }

    {
        auto environment = make_environment();
        const auto reset = environment->reset(spec, make_control());
        require(std::holds_alternative<ResetAccepted>(reset), "token rejection setup failed");
        const auto frame = frame_of(std::get<ResetAccepted>(reset).next);
        auto invalid = selection_for(frame, frame.request.candidates.front().public_action_key);
        ++invalid.submission_token.frame_generation;
        assert_rejection_then_valid("stale token", invalid, RejectionCode::StaleSubmissionToken);
    }

    {
        auto environment = make_environment();
        const auto reset = environment->reset(spec, make_control());
        require(std::holds_alternative<ResetAccepted>(reset), "decision rejection setup failed");
        const auto frame = frame_of(std::get<ResetAccepted>(reset).next);
        auto invalid = selection_for(frame, frame.request.candidates.front().public_action_key);
        invalid.public_semantic_decision_id = std::string(64, 'd');
        assert_rejection_then_valid("wrong public decision", invalid,
                                    RejectionCode::WrongPublicSemanticDecision);
    }

    {
        auto environment = make_environment();
        const auto reset = environment->reset(spec, make_control());
        require(std::holds_alternative<ResetAccepted>(reset), "unknown-key rejection setup failed");
        const auto frame = frame_of(std::get<ResetAccepted>(reset).next);
        auto invalid = selection_for(frame, "public_action.v1.00");
        assert_rejection_then_valid("unknown public key", invalid, RejectionCode::UnknownPublicActionKey);
    }
}

}  // namespace

int main() {
    try {
        test_fixed_rejection_precedence();
        std::cout << "episodic_rejection_tests=passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
