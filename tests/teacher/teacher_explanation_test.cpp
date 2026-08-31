#include "ygo/teacher/teacher_decision.hpp"
#include "ygo/teacher/teacher_explanation.hpp"
#include "ygo/teacher/teacher_explanation_codec.hpp"

#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/teacher/public_fact_registry.hpp"

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

TeacherDecisionExplanation explanation(const std::string& selected_key,
                                       const std::uint64_t feature_value = 8000) {
    TeacherDecisionExplanation result;
    result.selected_public_action_key = selected_key;
    result.selected_score_vector.values[static_cast<std::size_t>(
        ScoreDimension::ProfilePreference)] = 11;
    ScoreVector runner_up;
    runner_up.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] = 10;
    result.runner_up_score_vector = runner_up;
    result.confidence_class = ConfidenceClass::Medium;
    result.fallback_level = TeacherFallbackLevel::F1;
    result.active_goal_id = "goal.test";
    result.active_line_id = "line.test";
    result.active_line_node_id = "node.test";
    result.matched_intent_ids = {"intent.a", "intent.b"};
    result.invalidation_reason_ids = {"resource_consumed"};
    PublicFactValue fact;
    fact.fact_id = "public.life_points.self";
    fact.value_kind = PublicFactValueKind::U64;
    fact.u64_value = feature_value;
    result.relevant_public_feature_values = {fact};
    result.explanation_schema_id = std::string(kTeacherDiagnosticContractId);
    return result;
}

TeacherRankingResult selected_result(const std::string& selected_key,
                                     const std::string& other_key) {
    TeacherRankingResult result;
    result.status = TeacherRankingStatus::Selected;
    CandidateEvaluation first;
    first.public_action_key = other_key;
    first.status = CandidateEvaluationStatus::Supported;
    ScoreVector first_score;
    first_score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] = 10;
    first.score = first_score;
    CandidateEvaluation second;
    second.public_action_key = selected_key;
    second.status = CandidateEvaluationStatus::Supported;
    ScoreVector second_score;
    second_score.values[static_cast<std::size_t>(ScoreDimension::ProfilePreference)] = 11;
    second.score = second_score;
    result.evaluations = {first, second};
    result.selected_public_action_key = selected_key;
    result.selected_score_vector = second.score;
    result.fallback_level = TeacherFallbackLevel::F1;
    return result;
}

void test_value_owned_canonical_round_trip() {
    const auto first = candidate(1);
    const auto value = explanation(first.public_action_key);
    static_assert(std::is_same_v<decltype(TeacherRankingResult::explanation),
                                 std::optional<TeacherDecisionExplanation>>);
    require(validate_teacher_decision_explanation(value),
            "valid explanation was rejected");
    const auto bytes = canonical_teacher_decision_explanation_bytes(value);
    require(bytes == canonical_teacher_decision_explanation_bytes(value),
            "explanation bytes are not deterministic");
    const auto decoded = decode_teacher_decision_explanation(bytes);
    require(decoded && *decoded.value == value,
            "explanation did not decode to the original value");
    require(canonical_teacher_decision_explanation_bytes(*decoded.value) == bytes,
            "explanation re-encoding changed canonical bytes");
}

void test_optional_explanation_does_not_change_policy_selection() {
    const auto first = candidate(1);
    const auto second = candidate(2);
    auto without = selected_result(second.public_action_key, first.public_action_key);
    const auto selection_without = teacher_policy_selection_from_result(without);
    require(selection_without && selection_without.value->public_action_key ==
                                      second.public_action_key &&
                !selection_without.value->rng_cursor.has_value(),
            "selection without optional explanation changed");

    auto with = without;
    with.explanation = explanation(second.public_action_key);
    const auto selection_with = teacher_policy_selection_from_result(with);
    require(selection_with && selection_with.value->public_action_key ==
                                  selection_without.value->public_action_key &&
                !selection_with.value->rng_cursor.has_value(),
            "optional explanation changed PolicySelection");

    auto changed_payload = with;
    changed_payload.explanation->active_goal_id = "goal.changed";
    const auto changed_selection = teacher_policy_selection_from_result(changed_payload);
    require(changed_selection && changed_selection.value->public_action_key ==
                                      selection_with.value->public_action_key,
            "derived explanation payload changed gameplay selection");
    require(canonical_teacher_decision_explanation_bytes(
                *changed_payload.explanation) !=
                canonical_teacher_decision_explanation_bytes(*with.explanation),
            "changed explanation payload did not change diagnostic bytes");
}

void test_malformed_explanations_fail_closed_without_fallback() {
    const auto key = candidate(1).public_action_key;
    auto value = explanation(key);

    auto bad_schema = value;
    bad_schema.explanation_schema_id = "ocgforge.teacher_explanation.other.v1";
    require(!validate_teacher_decision_explanation(bad_schema),
            "wrong explanation schema was accepted");

    auto unsorted = value;
    unsorted.matched_intent_ids = {"intent.b", "intent.a"};
    require(!validate_teacher_decision_explanation(unsorted),
            "unsorted explanation IDs were accepted");

    auto bad_reason = value;
    bad_reason.invalidation_reason_ids = {"unknown.reason"};
    require(!validate_teacher_decision_explanation(bad_reason),
            "unknown explanation reason was accepted");

    auto bad_fact = value;
    bad_fact.relevant_public_feature_values.front().fact_id = "unknown.fact";
    require(!validate_teacher_decision_explanation(bad_fact),
            "unknown explanation fact was accepted");

    auto malformed_result = selected_result(key, candidate(2).public_action_key);
    malformed_result.explanation = bad_schema;
    require(validate_teacher_ranking_result(malformed_result),
            "optional malformed explanation changed gameplay-result validity");
    const auto malformed_selection = teacher_policy_selection_from_result(malformed_result);
    require(malformed_selection && malformed_selection.value->public_action_key == key &&
                !malformed_selection.value->rng_cursor.has_value(),
            "malformed optional explanation changed PolicySelection");

    auto bytes = canonical_teacher_decision_explanation_bytes(value);
    bytes.push_back(0);
    require(!decode_teacher_decision_explanation(bytes),
            "trailing explanation bytes were accepted");
}

void test_f4_confidence_contract() {
    const auto key = candidate(1).public_action_key;
    const auto non_fallback_confidences = {
        ConfidenceClass::High,
        ConfidenceClass::Medium,
        ConfidenceClass::Low,
    };
    for (const auto confidence : non_fallback_confidences) {
        auto value = explanation(key);
        value.fallback_level = TeacherFallbackLevel::F4;
        value.confidence_class = confidence;
        require(!validate_teacher_decision_explanation(value),
                "F4 explanation accepted a non-FALLBACK confidence");
    }

    auto accepted = explanation(key);
    accepted.runner_up_score_vector.reset();
    accepted.fallback_level = TeacherFallbackLevel::F4;
    accepted.confidence_class = ConfidenceClass::Fallback;
    require(validate_teacher_decision_explanation(accepted),
            "F4/FALLBACK explanation was rejected");

    const auto bytes = canonical_teacher_decision_explanation_bytes(accepted);
    constexpr std::size_t score_byte_count = 9U * sizeof(std::uint64_t);
    const auto confidence_offset =
        4U + kTeacherDiagnosticContractId.size() +
        4U + kTeacherDiagnosticContractId.size() +
        4U + key.size() + score_byte_count + 1U;

    auto malformed_confidence = bytes;
    malformed_confidence[confidence_offset] =
        static_cast<std::uint8_t>(ConfidenceClass::High);
    require(!decode_teacher_decision_explanation(malformed_confidence),
            "F4 explanation with HIGH confidence bytes was decoded");

    auto invalid_confidence = bytes;
    invalid_confidence[confidence_offset] = 4U;
    require(!decode_teacher_decision_explanation(invalid_confidence),
            "F4 explanation with an invalid confidence byte was decoded");
}

}  // namespace

int main() {
    try {
        test_value_owned_canonical_round_trip();
        test_optional_explanation_does_not_change_policy_selection();
        test_malformed_explanations_fail_closed_without_fallback();
        test_f4_confidence_contract();
        std::cout << "teacher_explanation_test: PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "teacher_explanation_test: FAIL: " << error.what() << '\n';
        return 1;
    }
}
