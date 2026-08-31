#include "ygo/teacher/candidate_evaluator.hpp"
#include "ygo/teacher/deterministic_resolver.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <limits>
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
using ygo::teacher::ScoreDimension;
using ygo::teacher::ScoreVector;
using ygo::teacher::TeacherRankingResult;
using ygo::teacher::TeacherRankingStatus;

static_assert(ygo::teacher::kTeacherScoreDimensionCount == 9);

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

CandidateEvaluation evaluation(const EnvironmentActionCandidate& value,
                               const CandidateEvaluationStatus status,
                               const std::optional<ScoreVector>& score) {
    CandidateEvaluation result;
    result.public_action_key = value.public_action_key;
    result.status = status;
    result.score = score;
    return result;
}

TeacherRankingResult resolve_scores(const std::vector<EnvironmentActionCandidate>& candidates,
                                    const ScoreVector& first_score,
                                    const ScoreVector& second_score) {
    const auto first_key = candidates[0].public_action_key;
    const CandidateEvaluator evaluator = [first_key, first_score, second_score](const auto& value) {
        return evaluation(value, CandidateEvaluationStatus::Supported,
                          value.public_action_key == first_key ? std::optional<ScoreVector>{first_score}
                                                               : std::optional<ScoreVector>{second_score});
    };
    return ygo::teacher::resolve_teacher_ranking(candidates, evaluator);
}

void require_selected(const TeacherRankingResult& result, const std::string& expected_key) {
    require(result.status == TeacherRankingStatus::Selected,
            "resolver did not return SELECTED");
    require(result.selected_public_action_key.has_value() &&
                *result.selected_public_action_key == expected_key,
            "resolver selected the wrong public action key");
    require(result.selected_score_vector.has_value(),
            "selected result omitted its total score vector");
    require(!result.fallback_level.has_value(),
            "ordinary Task-4 resolution introduced fallback provenance");
}

void test_each_score_dimension_is_higher_better() {
    const std::vector<EnvironmentActionCandidate> candidates = {candidate(0), candidate(1)};

    for (std::size_t dimension = 0; dimension < 9; ++dimension) {
        ScoreVector first_score;
        ScoreVector second_score;
        second_score.values[dimension] = 1;
        const auto result = resolve_scores(candidates, first_score, second_score);
        require_selected(result, candidates[1].public_action_key);
    }
}

void test_score_dimensions_are_compared_in_frozen_order() {
    const std::vector<EnvironmentActionCandidate> candidates = {candidate(0), candidate(1)};

    for (std::size_t dimension = 0; dimension + 1 < 9; ++dimension) {
        ScoreVector first_score;
        ScoreVector second_score;
        first_score.values[dimension] = 1;
        second_score.values[dimension + 1] = 1;
        const auto result = resolve_scores(candidates, first_score, second_score);
        require_selected(result, candidates[0].public_action_key);
    }
}

void test_equal_scores_use_public_key_tiebreak_only() {
    const auto first = candidate(0);
    const auto second = candidate(1);
    const auto expected_key = std::min(first.public_action_key, second.public_action_key);
    const ScoreVector equal_score;

    const auto forward = std::vector<EnvironmentActionCandidate>{first, second};
    const auto forward_result = resolve_scores(forward, equal_score, equal_score);
    require_selected(forward_result, expected_key);
    require(forward_result.evaluations.size() == 2 &&
                forward_result.evaluations[0].public_action_key == first.public_action_key &&
                forward_result.evaluations[1].public_action_key == second.public_action_key,
            "forward tie evidence order changed");

    const auto reversed = std::vector<EnvironmentActionCandidate>{second, first};
    const auto reversed_result = resolve_scores(reversed, equal_score, equal_score);
    require_selected(reversed_result, expected_key);
    require(reversed_result.evaluations.size() == 2 &&
                reversed_result.evaluations[0].public_action_key == second.public_action_key &&
                reversed_result.evaluations[1].public_action_key == first.public_action_key,
            "reversed tie evidence order changed");

    const auto selection = ygo::teacher::teacher_policy_selection_from_result(reversed_result);
    require(static_cast<bool>(selection), "valid tie result did not map to PolicySelection");
    require(selection.value->public_action_key == expected_key,
            "PolicySelection adapter changed the tie winner");
    require(!selection.value->rng_cursor.has_value(),
            "deterministic resolver introduced RNG provenance");
}

void test_non_total_evaluations_never_select() {
    const auto candidates = std::vector<EnvironmentActionCandidate>{candidate(0), candidate(1)};

    const auto run = [&](const CandidateEvaluationStatus second_status,
                         const std::optional<ScoreVector>& second_score) {
        const CandidateEvaluator evaluator = [&, second_status, second_score](const auto& value) {
            if (value.public_action_key == candidates[1].public_action_key) {
                return evaluation(value, second_status, second_score);
            }
            ScoreVector score;
            score.values[0] = 1;
            return evaluation(value, CandidateEvaluationStatus::Supported, score);
        };
        return ygo::teacher::resolve_teacher_ranking(candidates, evaluator);
    };

    const auto unsupported = run(CandidateEvaluationStatus::Unsupported, std::nullopt);
    require(unsupported.status != TeacherRankingStatus::Selected,
            "unsupported evaluation was treated as a rankable low score");
    require(unsupported.evaluations.size() == candidates.size(),
            "unsupported candidate disappeared from evidence");
    require(!ygo::teacher::teacher_policy_selection_from_result(unsupported),
            "unsupported evaluation produced a gameplay selection");

    const auto invalid = run(CandidateEvaluationStatus::Invalid, std::nullopt);
    require(invalid.status != TeacherRankingStatus::Selected,
            "invalid evaluation was treated as a rankable low score");
    require(invalid.evaluations.size() == candidates.size(),
            "invalid candidate disappeared from evidence");
    require(!ygo::teacher::teacher_policy_selection_from_result(invalid),
            "invalid evaluation produced a gameplay selection");

    const auto missing_score = run(CandidateEvaluationStatus::Supported, std::nullopt);
    require(missing_score.status != TeacherRankingStatus::Selected,
            "missing required score was accepted as a total rank");
    require(!ygo::teacher::teacher_policy_selection_from_result(missing_score),
            "missing required score produced a gameplay selection");

    const auto not_applicable_without_score =
        run(CandidateEvaluationStatus::NotApplicable, std::nullopt);
    require(not_applicable_without_score.status != TeacherRankingStatus::Selected,
            "NOT_APPLICABLE without a total score was selected");

    ScoreVector not_applicable_score;
    not_applicable_score.values[0] = 2;
    const auto not_applicable_with_score =
        run(CandidateEvaluationStatus::NotApplicable, not_applicable_score);
    require_selected(not_applicable_with_score, candidates[1].public_action_key);
}

void test_checked_score_contributions() {
    ScoreVector score;
    score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] =
        std::numeric_limits<std::int64_t>::max();
    require(!ygo::teacher::add_score_contribution(
                score, ScoreDimension::ProfilePreference, 1),
            "positive int64 overflow was accepted");

    score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] =
        std::numeric_limits<std::int64_t>::min();
    require(!ygo::teacher::add_score_contribution(
                score, ScoreDimension::ProfilePreference, -1),
            "negative int64 underflow was accepted");

    score = {};
    require(!ygo::teacher::add_score_contribution(
                score, ScoreDimension::ProfilePreference, 1'000'001),
            "out-of-range positive contribution was accepted");
    require(!ygo::teacher::add_score_contribution(
                score, ScoreDimension::ProfilePreference, -1'000'001),
            "out-of-range negative contribution was accepted");
    require(!ygo::teacher::add_score_contribution(
                score, static_cast<ScoreDimension>(9), 1),
            "unknown score dimension was accepted");

    require(ygo::teacher::add_score_contribution(
                score, ScoreDimension::ProfilePreference, 1'000'000),
            "maximum valid positive contribution was rejected");
    require(ygo::teacher::add_score_contribution(
                score, ScoreDimension::ImmediateTacticalNecessity, -1'000'000),
            "maximum valid negative contribution was rejected");
    require(score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] ==
                1'000'000 &&
                score.values[static_cast<std::size_t>(ScoreDimension::ImmediateTacticalNecessity)] ==
                    -1'000'000,
            "valid contribution did not accumulate exactly");
}

}  // namespace

int main() {
    try {
        test_each_score_dimension_is_higher_better();
        test_score_dimensions_are_compared_in_frozen_order();
        test_equal_scores_use_public_key_tiebreak_only();
        test_non_total_evaluations_never_select();
        test_checked_score_contributions();
        std::cout << "teacher_ranking_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_ranking_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
