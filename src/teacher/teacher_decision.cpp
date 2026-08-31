#include "ygo/teacher/teacher_decision.hpp"

#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

#include "ygo/environment/public_action_identity.hpp"
#include "ygo/policy/policy.hpp"

namespace ygo::teacher {
namespace {

void set_diagnostic(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
}

bool valid_ranking_status(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_evaluation_status(const std::uint8_t value) noexcept { return value <= 3; }
bool valid_fallback_level(const std::uint8_t value) noexcept { return value <= 4; }

bool canonical_token(const std::string_view value) noexcept {
    if (value.empty()) {
        return false;
    }
    for (const auto character : value) {
        const auto byte = static_cast<unsigned char>(character);
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '.' || byte == '_' || byte == '-')) {
            return false;
        }
    }
    return value.front() != '.' && value.back() != '.' &&
           value.find("..") == std::string_view::npos;
}

bool validate_canonical_id_vector(const std::vector<std::string>& values,
                                  const char* field,
                                  std::string* diagnostic) {
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!canonical_token(values[index])) {
            set_diagnostic(diagnostic,
                           std::string("teacher ") + field +
                               " contains a noncanonical token");
            return false;
        }
        if (index > 0 && !(values[index - 1] < values[index])) {
            set_diagnostic(diagnostic,
                           std::string("teacher ") + field +
                               " is not strictly sorted");
            return false;
        }
    }
    return true;
}

bool validate_candidate_evaluation(const CandidateEvaluation& value,
                                   std::string* diagnostic) {
    if (!ygo::environment::is_public_action_key(value.public_action_key)) {
        set_diagnostic(diagnostic, "candidate evaluation has an invalid public action key");
        return false;
    }
    if (!valid_evaluation_status(static_cast<std::uint8_t>(value.status))) {
        set_diagnostic(diagnostic, "candidate evaluation status is unknown");
        return false;
    }
    if (!validate_canonical_id_vector(value.matched_intent_ids,
                                      "matched intent IDs", diagnostic) ||
        !validate_canonical_id_vector(value.matched_goal_ids,
                                      "matched goal IDs", diagnostic) ||
        !validate_canonical_id_vector(value.matched_line_ids,
                                      "matched line IDs", diagnostic) ||
        !validate_canonical_id_vector(value.reason_ids, "reason IDs", diagnostic)) {
        return false;
    }
    return true;
}

policy::PolicySelection error_selection(const policy::PolicyErrorCode code,
                                        const std::string& message) noexcept {
    return policy::PolicySelection{
        std::nullopt, policy::PolicyError{code, message}};
}

}  // namespace

bool validate_teacher_ranking_result(const TeacherRankingResult& value,
                                     std::string* diagnostic) noexcept {
    try {
        if (!valid_ranking_status(static_cast<std::uint8_t>(value.status))) {
            set_diagnostic(diagnostic, "teacher ranking status is unknown");
            return false;
        }
        if (value.fallback_level.has_value() &&
            !valid_fallback_level(
                static_cast<std::uint8_t>(*value.fallback_level))) {
            set_diagnostic(diagnostic, "teacher fallback level is unknown");
            return false;
        }
        if (value.proposed_state_delta.has_value() &&
            !validate_teacher_state_delta(*value.proposed_state_delta)) {
            set_diagnostic(diagnostic, "teacher state delta is invalid");
            return false;
        }

        for (std::size_t index = 0; index < value.evaluations.size(); ++index) {
            if (!validate_candidate_evaluation(value.evaluations[index], diagnostic)) {
                return false;
            }
            for (std::size_t previous = 0; previous < index; ++previous) {
                if (value.evaluations[previous].public_action_key ==
                    value.evaluations[index].public_action_key) {
                    set_diagnostic(diagnostic,
                                   "teacher evaluation contains duplicate public action keys");
                    return false;
                }
            }
        }

        if (value.status != TeacherRankingStatus::Selected) {
            if (value.selected_public_action_key.has_value() ||
                value.selected_score_vector.has_value() ||
                value.fallback_level.has_value() ||
                value.proposed_state_delta.has_value()) {
                set_diagnostic(diagnostic,
                               "non-selected teacher result carries an actionable result");
                return false;
            }
            return true;
        }

        if (!value.selected_public_action_key.has_value() ||
            !ygo::environment::is_public_action_key(
                *value.selected_public_action_key)) {
            set_diagnostic(diagnostic,
                           "selected teacher result lacks a valid public action key");
            return false;
        }
        if (value.proposed_state_delta.has_value() &&
            value.proposed_state_delta->proposed_for_public_action_key !=
                *value.selected_public_action_key) {
            set_diagnostic(diagnostic,
                           "teacher state delta does not match selected public action key");
            return false;
        }

        const CandidateEvaluation* selected = nullptr;
        for (const auto& evaluation : value.evaluations) {
            if (evaluation.public_action_key == *value.selected_public_action_key) {
                selected = &evaluation;
                break;
            }
        }
        if (selected == nullptr) {
            set_diagnostic(diagnostic,
                           "selected teacher action is absent from evaluation records");
            return false;
        }
        if (selected->status == CandidateEvaluationStatus::Unsupported ||
            selected->status == CandidateEvaluationStatus::Invalid) {
            set_diagnostic(diagnostic,
                           "selected teacher action has a non-actionable evaluation status");
            return false;
        }
        if (value.selected_score_vector.has_value() &&
            (!selected->score.has_value() ||
             *selected->score != *value.selected_score_vector)) {
            set_diagnostic(diagnostic,
                           "selected teacher score does not match its evaluation");
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        set_diagnostic(diagnostic, error.what());
        return false;
    } catch (...) {
        set_diagnostic(diagnostic, "teacher ranking validation threw");
        return false;
    }
}

policy::PolicySelection teacher_policy_selection_from_result(
    const TeacherRankingResult& value) noexcept {
    std::string diagnostic;
    if (!validate_teacher_ranking_result(value, &diagnostic)) {
        return error_selection(policy::PolicyErrorCode::InvalidCandidateDomain,
                               diagnostic.empty() ? "invalid teacher ranking result"
                                                   : diagnostic);
    }
    switch (value.status) {
    case TeacherRankingStatus::Selected:
        return policy::PolicySelection{
            policy::PolicySelectionResult{*value.selected_public_action_key,
                                          std::nullopt},
            std::nullopt};
    case TeacherRankingStatus::InvalidInput:
        return error_selection(policy::PolicyErrorCode::InvalidCandidateDomain,
                               "teacher rejected its public input");
    case TeacherRankingStatus::Blocked:
        return error_selection(policy::PolicyErrorCode::InvalidConfiguration,
                               "teacher decision is blocked");
    case TeacherRankingStatus::Unsupported:
        return error_selection(policy::PolicyErrorCode::InvalidConfiguration,
                               "teacher decision is unsupported");
    }
    return error_selection(policy::PolicyErrorCode::InvalidConfiguration,
                           "teacher ranking status is unknown");
}

}  // namespace ygo::teacher
