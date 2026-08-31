#include "ygo/teacher/fallback_resolver.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"

namespace {

using namespace ygo::environment;
using namespace ygo::teacher;

void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

EnvironmentActionCandidate candidate(const std::uint64_t value) {
    EnvironmentActionCandidate result;
    result.action_kind = EnvironmentActionKind::Option;
    result.choice = PublicChoice{PublicChoiceKind::OptionValue, value,
                                 static_cast<std::uint32_t>(value)};
    PublicActionKeyInput key;
    key.action_kind = "option";
    key.choice = result.choice;
    result.public_action_key = public_action_key(key);
    return result;
}

std::vector<EnvironmentActionCandidate> domain() {
    return {candidate(1), candidate(2)};
}

std::vector<TeacherFallbackCandidateValue> supported_stage(
    const std::vector<EnvironmentActionCandidate>& candidates,
    const std::int64_t first_score,
    const std::int64_t second_score) {
    std::vector<TeacherFallbackCandidateValue> result(2);
    result[0].public_action_key = candidates[0].public_action_key;
    result[1].public_action_key = candidates[1].public_action_key;
    result[0].status = CandidateEvaluationStatus::Supported;
    result[1].status = CandidateEvaluationStatus::Supported;
    ScoreVector first;
    first.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] = first_score;
    ScoreVector second;
    second.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] = second_score;
    result[0].score = first;
    result[1].score = second;
    result[0].contributions.push_back(
        {ScoreDimension::ProfilePreference, static_cast<std::int32_t>(first_score)});
    result[1].contributions.push_back(
        {ScoreDimension::ProfilePreference, static_cast<std::int32_t>(second_score)});
    return result;
}

std::vector<TeacherFallbackCandidateValue> unsupported_stage(
    const std::vector<EnvironmentActionCandidate>& candidates) {
    std::vector<TeacherFallbackCandidateValue> result(2);
    for (std::size_t index = 0; index < result.size(); ++index) {
        result[index].public_action_key = candidates[index].public_action_key;
        result[index].status = CandidateEvaluationStatus::Unsupported;
    }
    return result;
}

void test_f0_to_f3_preserve_one_domain_vector() {
    const auto candidates = domain();
    for (std::size_t stage = 0; stage < 4; ++stage) {
        TeacherFallbackStageSet stages;
        stages.stage_evaluations[stage] = supported_stage(candidates, 1, 2);
        const auto result = resolve_teacher_fallback(candidates, stages);
        require(result.status == TeacherRankingStatus::Selected,
                "total fallback stage did not select");
        require(result.fallback_level ==
                    std::optional<TeacherFallbackLevel>(static_cast<TeacherFallbackLevel>(stage)),
                "fallback level changed");
        require(result.evaluations.size() == candidates.size(),
                "fallback changed the N-record shape");
        require(result.evaluations[0].public_action_key == candidates[0].public_action_key &&
                    result.evaluations[1].public_action_key == candidates[1].public_action_key,
                "fallback changed supplied evaluation order");
        require(result.explanation.has_value(),
                "selected fallback result did not emit derived explanation data");
        require(result.selected_public_action_key ==
                    std::optional<std::string>(candidates[1].public_action_key),
                "fallback selected the wrong scored candidate");
    }
}

void test_unsupported_stages_fall_through_without_domain_mutation() {
    const auto candidates = domain();
    TeacherFallbackStageSet stages;
    stages.stage_evaluations[0] = unsupported_stage(candidates);
    stages.stage_evaluations[1] = supported_stage(domain(), 4, 3);
    const auto result = resolve_teacher_fallback(candidates, stages);
    require(result.status == TeacherRankingStatus::Selected &&
                result.fallback_level == TeacherFallbackLevel::F1 &&
                result.selected_public_action_key ==
                    std::optional<std::string>(candidates[0].public_action_key),
            "unsupported F0 did not fall through to F1");
    require(result.evaluations.size() == 2 &&
                result.evaluations[0].public_action_key == candidates[0].public_action_key &&
                result.evaluations[1].public_action_key == candidates[1].public_action_key,
            "fallthrough changed the complete domain evidence");

    TeacherFallbackStageSet simultaneous;
    simultaneous.stage_evaluations[0] = supported_stage(candidates, 1, 2);
    simultaneous.stage_evaluations[1] = supported_stage(candidates, 9, 10);
    const auto first_stage_wins = resolve_teacher_fallback(candidates, simultaneous);
    require(first_stage_wins.status == TeacherRankingStatus::Selected &&
                first_stage_wins.fallback_level == TeacherFallbackLevel::F0 &&
                first_stage_wins.selected_public_action_key ==
                    std::optional<std::string>(candidates[1].public_action_key),
            "a later total fallback stage overrode F0");
}

void test_incomplete_or_invalid_stage_fails_closed() {
    const auto candidates = domain();
    TeacherFallbackStageSet incomplete;
    auto missing_score = unsupported_stage(candidates);
    missing_score[0].status = CandidateEvaluationStatus::NotApplicable;
    incomplete.stage_evaluations[0] = missing_score;
    const auto incomplete_result = resolve_teacher_fallback(candidates, incomplete);
    require(incomplete_result.status == TeacherRankingStatus::Blocked &&
                !incomplete_result.selected_public_action_key.has_value(),
            "incomplete stage did not fail closed");

    TeacherFallbackStageSet malformed;
    auto wrong_key = supported_stage(candidates, 1, 2);
    wrong_key.pop_back();
    malformed.stage_evaluations[0] = wrong_key;
    const auto malformed_result = resolve_teacher_fallback(candidates, malformed);
    require(malformed_result.status == TeacherRankingStatus::InvalidInput &&
                !malformed_result.selected_public_action_key.has_value(),
            "malformed stage evidence did not fail closed");
    require(malformed_result.evaluations.size() == candidates.size(),
            "malformed stage erased supplied domain evidence");

    TeacherFallbackStageSet wrong_domain;
    auto foreign_stage = supported_stage(candidates, 1, 2);
    foreign_stage[1].public_action_key = candidate(3).public_action_key;
    wrong_domain.stage_evaluations[0] = foreign_stage;
    const auto foreign_result = resolve_teacher_fallback(candidates, wrong_domain);
    require(foreign_result.status == TeacherRankingStatus::InvalidInput,
            "stage evidence from another candidate domain was accepted");

    TeacherFallbackStageSet invalid;
    auto invalid_stage = supported_stage(candidates, 1, 2);
    invalid_stage[0].status = CandidateEvaluationStatus::Invalid;
    invalid_stage[0].score.reset();
    invalid_stage[0].contributions.clear();
    invalid.stage_evaluations[0] = invalid_stage;
    const auto invalid_result = resolve_teacher_fallback(candidates, invalid);
    require(invalid_result.status == TeacherRankingStatus::InvalidInput,
            "INVALID stage evidence was allowed to fall through");

    TeacherFallbackStageSet arithmetic_failure;
    auto out_of_range = supported_stage(domain(), 1, 2);
    out_of_range[0].contributions[0].value = 1'000'001;
    arithmetic_failure.stage_evaluations[0] = out_of_range;
    const auto arithmetic_result = resolve_teacher_fallback(candidates, arithmetic_failure);
    require(arithmetic_result.status == TeacherRankingStatus::InvalidInput,
            "unchecked score contribution did not fail closed");

    TeacherFallbackStageSet score_mismatch;
    auto mismatched_score = supported_stage(domain(), 1, 2);
    mismatched_score[0].score->values[static_cast<std::size_t>(
        ScoreDimension::ProfilePreference)] = 2;
    score_mismatch.stage_evaluations[0] = mismatched_score;
    const auto mismatch_result = resolve_teacher_fallback(candidates, score_mismatch);
    require(mismatch_result.status == TeacherRankingStatus::InvalidInput,
            "unproven stage score did not fail closed");
}

void test_f4_uses_only_public_key_order() {
    const auto candidates = domain();
    TeacherFallbackStageSet stages;
    const auto result = resolve_teacher_fallback(candidates, stages);
    require(result.status == TeacherRankingStatus::Selected &&
                result.fallback_level == TeacherFallbackLevel::F4,
            "empty stage set did not select explicit F4");
    const auto expected = candidates[0].public_action_key < candidates[1].public_action_key
                              ? candidates[0].public_action_key
                              : candidates[1].public_action_key;
    require(result.selected_public_action_key == std::optional<std::string>(expected),
            "F4 did not use bytewise public-key ordering");
    require(result.evaluations.size() == candidates.size() &&
                result.evaluations[0].public_action_key == candidates[0].public_action_key &&
                result.evaluations[1].public_action_key == candidates[1].public_action_key,
            "F4 did not preserve the supplied N-record evidence vector");
    require(result.explanation.has_value() &&
                result.explanation->confidence_class == ConfidenceClass::Fallback,
            "F4 did not emit fallback-confidence explanation data");

    TeacherFallbackStageSet unsupported_stages;
    for (auto& stage : unsupported_stages.stage_evaluations) {
        stage = unsupported_stage(candidates);
    }
    const auto after_unsupported = resolve_teacher_fallback(candidates, unsupported_stages);
    require(after_unsupported.status == TeacherRankingStatus::Selected &&
                after_unsupported.fallback_level == TeacherFallbackLevel::F4,
            "all unsupported stages did not fall through to F4");

    auto duplicate = candidates;
    duplicate[1].public_action_key = duplicate[0].public_action_key;
    const auto duplicate_result = resolve_teacher_fallback(duplicate, stages);
    require(duplicate_result.status == TeacherRankingStatus::InvalidInput,
            "duplicate public keys were accepted by F4");
}

}  // namespace

int main() {
    try {
        test_f0_to_f3_preserve_one_domain_vector();
        test_unsupported_stages_fall_through_without_domain_mutation();
        test_incomplete_or_invalid_stage_fails_closed();
        test_f4_uses_only_public_key_order();
        std::cout << "teacher_fallback_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_fallback_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
