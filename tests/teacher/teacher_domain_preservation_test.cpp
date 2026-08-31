#include "ygo/teacher/candidate_evaluator.hpp"
#include "ygo/teacher/deterministic_resolver.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/policy.hpp"

namespace {

using ygo::environment::EnvironmentActionCandidate;
using ygo::environment::EnvironmentActionKind;
using ygo::environment::PublicActionKeyInput;
using ygo::environment::PublicChoice;
using ygo::environment::PublicChoiceKind;
using ygo::teacher::CandidateEvaluation;
using ygo::teacher::CandidateEvaluationStatus;
using ygo::teacher::CandidateEvaluator;
using ygo::teacher::TeacherRankingStatus;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnvironmentActionCandidate candidate(const std::uint64_t choice) {
    EnvironmentActionCandidate value;
    value.action_kind = EnvironmentActionKind::YesNo;
    value.choice = PublicChoice{PublicChoiceKind::YesNo, choice, std::nullopt};

    PublicActionKeyInput key_input;
    key_input.action_kind = "yes_no";
    key_input.choice = value.choice;
    value.public_action_key = ygo::environment::public_action_key(key_input);
    return value;
}

CandidateEvaluation evaluation(const EnvironmentActionCandidate& value) {
    CandidateEvaluation result;
    result.public_action_key = value.public_action_key;
    result.status = CandidateEvaluationStatus::Supported;
    return result;
}

void test_preserves_order_and_invokes_each_candidate_once() {
    const std::vector<EnvironmentActionCandidate> candidates = {
        candidate(1), candidate(0)};
    std::vector<std::string> invoked_keys;
    std::vector<CandidateEvaluation> evaluations;

    const CandidateEvaluator evaluator = [&](const auto& value) {
        invoked_keys.push_back(value.public_action_key);
        auto result = evaluation(value);
        if (value.public_action_key == candidates[1].public_action_key) {
            ygo::teacher::ScoreVector score;
            score.values[8] = -1;
            result.score = score;
        }
        return result;
    };

    require(ygo::teacher::evaluate_candidate_domain(candidates, evaluator, evaluations),
            "valid candidate domain was rejected");
    require(evaluations.size() == candidates.size(),
            "candidate domain did not produce exactly N evaluations");
    require(invoked_keys.size() == candidates.size(),
            "spy evaluator was not invoked exactly once per candidate");
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        require(invoked_keys[index] == candidates[index].public_action_key,
                "evaluator invocation order changed");
        require(evaluations[index].public_action_key == candidates[index].public_action_key,
                "evaluation key changed supplied order or identity");
    }
    require(evaluations[1].score.has_value() && evaluations[1].score->values[8] == -1,
            "strategically bad candidate was removed from evaluation evidence");
}

void test_rejects_invalid_domains_without_selecting() {
    const auto valid_candidates = std::vector<EnvironmentActionCandidate>{candidate(0),
                                                                            candidate(1)};
    std::size_t invocation_count = 0;
    const CandidateEvaluator evaluator = [&](const auto& value) {
        ++invocation_count;
        return evaluation(value);
    };

    std::vector<CandidateEvaluation> evaluations = {evaluation(valid_candidates[0])};
    require(!ygo::teacher::evaluate_candidate_domain({}, evaluator, evaluations),
            "empty candidate domain was accepted");
    require(evaluations.empty(), "empty domain left stale evaluation evidence");
    require(invocation_count == 0, "empty domain invoked the evaluator");

    auto malformed = valid_candidates;
    malformed[0].public_action_key = "not-a-public-action-key";
    require(!ygo::teacher::evaluate_candidate_domain(malformed, evaluator, evaluations),
            "malformed public action key was accepted");
    require(invocation_count == 0, "malformed domain invoked the evaluator");

    auto duplicate = valid_candidates;
    duplicate[1].public_action_key = duplicate[0].public_action_key;
    require(!ygo::teacher::evaluate_candidate_domain(duplicate, evaluator, evaluations),
            "duplicate supplied public action key was accepted");
    require(invocation_count == 0, "duplicate domain invoked the evaluator");
}

void test_rejects_evaluator_key_substitution() {
    const auto candidates = std::vector<EnvironmentActionCandidate>{candidate(0),
                                                                      candidate(1)};
    std::vector<CandidateEvaluation> evaluations;
    const CandidateEvaluator evaluator = [&](const auto& value) {
        auto result = evaluation(value);
        if (value.public_action_key == candidates[1].public_action_key) {
            result.public_action_key = candidates[0].public_action_key;
        }
        return result;
    };

    require(!ygo::teacher::evaluate_candidate_domain(candidates, evaluator, evaluations),
            "evaluator public-key substitution was accepted");
    require(evaluations.empty(), "key substitution left trusted evaluation evidence");

    const auto result = ygo::teacher::resolve_teacher_ranking(candidates, evaluator);
    require(result.status == TeacherRankingStatus::InvalidInput,
            "key substitution did not fail the resolver closed");
    require(!ygo::teacher::teacher_policy_selection_from_result(result),
            "key substitution produced a gameplay selection");
}

}  // namespace

int main() {
    try {
        test_preserves_order_and_invokes_each_candidate_once();
        test_rejects_invalid_domains_without_selecting();
        test_rejects_evaluator_key_substitution();
        std::cout << "teacher_domain_preservation_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_domain_preservation_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
