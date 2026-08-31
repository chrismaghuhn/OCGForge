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
    const bool is_yes_no = choice <= 1;
    value.action_kind = is_yes_no ? EnvironmentActionKind::YesNo : EnvironmentActionKind::Chain;
    value.choice = PublicChoice{is_yes_no ? PublicChoiceKind::YesNo
                                          : PublicChoiceKind::EffectChoice,
                               choice, std::nullopt};

    PublicActionKeyInput key_input;
    key_input.action_kind = is_yes_no ? "yes_no" : "chain";
    key_input.choice = value.choice;
    value.public_action_key = ygo::environment::public_action_key(key_input);
    return value;
}

CandidateEvaluation evaluation(const EnvironmentActionCandidate& value) {
    CandidateEvaluation result;
    result.public_action_key = value.public_action_key;
    result.status = CandidateEvaluationStatus::Supported;
    result.score = ygo::teacher::ScoreVector{};
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
                                                                      candidate(1),
                                                                      candidate(2)};
    std::vector<CandidateEvaluation> evaluations;
    std::vector<std::string> invoked_keys;
    const CandidateEvaluator evaluator = [&](const auto& value) {
        invoked_keys.push_back(value.public_action_key);
        auto result = evaluation(value);
        if (value.public_action_key == candidates[1].public_action_key) {
            result.public_action_key = candidates[0].public_action_key;
        }
        return result;
    };

    require(ygo::teacher::evaluate_candidate_domain(candidates, evaluator, evaluations),
            "evaluator public-key substitution did not materialize a complete result");
    require(invoked_keys.size() == candidates.size(),
            "key substitution stopped before evaluating every candidate");
    require(evaluations.size() == candidates.size(),
            "key substitution did not preserve N evaluation records");
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        require(invoked_keys[index] == candidates[index].public_action_key,
                "key substitution changed evaluator invocation order");
        require(evaluations[index].public_action_key == candidates[index].public_action_key,
                "key substitution did not restore the authoritative candidate key");
    }
    require(evaluations[1].status == CandidateEvaluationStatus::Invalid &&
                !evaluations[1].score.has_value() && evaluations[1].matched_intent_ids.empty() &&
                evaluations[1].matched_goal_ids.empty() &&
                evaluations[1].matched_line_ids.empty() && evaluations[1].reason_ids.empty(),
            "key substitution retained evaluator metadata instead of INVALID evidence");

    invoked_keys.clear();
    const auto result = ygo::teacher::resolve_teacher_ranking(candidates, evaluator);
    require(result.status == TeacherRankingStatus::InvalidInput,
            "key substitution did not fail the resolver closed");
    require(invoked_keys.size() == candidates.size(),
            "resolver key substitution stopped before evaluating every candidate");
    require(result.evaluations.size() == candidates.size(),
            "resolver dropped candidates after key substitution");
    require(!ygo::teacher::teacher_policy_selection_from_result(result),
            "key substitution produced a gameplay selection");
}

void test_preserves_evidence_after_evaluator_exception() {
    const auto candidates = std::vector<EnvironmentActionCandidate>{candidate(0),
                                                                      candidate(1),
                                                                      candidate(2)};
    std::vector<std::string> invoked_keys;
    const CandidateEvaluator evaluator = [&](const auto& value) {
        invoked_keys.push_back(value.public_action_key);
        if (value.public_action_key == candidates[1].public_action_key) {
            throw std::runtime_error("synthetic evaluator failure");
        }
        return evaluation(value);
    };

    const auto result = ygo::teacher::resolve_teacher_ranking(candidates, evaluator);
    require(result.status == TeacherRankingStatus::InvalidInput,
            "evaluator exception did not fail the resolver closed");
    require(invoked_keys.size() == candidates.size(),
            "evaluator exception stopped before remaining candidates were evaluated");
    require(result.evaluations.size() == candidates.size(),
            "evaluator exception did not preserve N evaluation records");
    for (std::size_t index = 0; index < candidates.size(); ++index) {
        require(invoked_keys[index] == candidates[index].public_action_key,
                "evaluator exception changed invocation order");
        require(result.evaluations[index].public_action_key == candidates[index].public_action_key,
                "evaluator exception changed an authoritative candidate key");
    }
    require(result.evaluations[1].status == CandidateEvaluationStatus::Invalid &&
                !result.evaluations[1].score.has_value() &&
                result.evaluations[1].matched_intent_ids.empty() &&
                result.evaluations[1].matched_goal_ids.empty() &&
                result.evaluations[1].matched_line_ids.empty() &&
                result.evaluations[1].reason_ids.empty(),
            "evaluator exception did not produce empty INVALID evidence");
    require(!ygo::teacher::teacher_policy_selection_from_result(result),
            "evaluator exception produced a gameplay selection");
}

void test_preserves_evidence_after_malformed_evaluation() {
    const auto candidates = std::vector<EnvironmentActionCandidate>{candidate(0),
                                                                      candidate(1),
                                                                      candidate(2)};
    std::vector<std::string> invoked_keys;
    const CandidateEvaluator evaluator = [&](const auto& value) {
        invoked_keys.push_back(value.public_action_key);
        auto result = evaluation(value);
        if (value.public_action_key == candidates[1].public_action_key) {
            result.matched_intent_ids = {"intent.b", "intent.a"};
        }
        return result;
    };

    const auto result = ygo::teacher::resolve_teacher_ranking(candidates, evaluator);
    require(result.status == TeacherRankingStatus::InvalidInput,
            "malformed evaluator metadata did not fail the resolver closed");
    require(invoked_keys.size() == candidates.size() &&
                result.evaluations.size() == candidates.size(),
            "malformed evaluator metadata changed the N-record shape");
    require(result.evaluations[1].public_action_key == candidates[1].public_action_key &&
                result.evaluations[1].status == CandidateEvaluationStatus::Invalid &&
                !result.evaluations[1].score.has_value() &&
                result.evaluations[1].matched_intent_ids.empty() &&
                result.evaluations[1].matched_goal_ids.empty() &&
                result.evaluations[1].matched_line_ids.empty() &&
                result.evaluations[1].reason_ids.empty(),
            "malformed evaluator metadata was not discarded fail-closed");
    require(!ygo::teacher::teacher_policy_selection_from_result(result),
            "malformed evaluator metadata produced a gameplay selection");
}

}  // namespace

int main() {
    try {
        test_preserves_order_and_invokes_each_candidate_once();
        test_rejects_invalid_domains_without_selecting();
        test_rejects_evaluator_key_substitution();
        test_preserves_evidence_after_evaluator_exception();
        test_preserves_evidence_after_malformed_evaluation();
        std::cout << "teacher_domain_preservation_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_domain_preservation_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
