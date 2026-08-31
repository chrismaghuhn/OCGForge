#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ygo/environment/public_decision.hpp"
#include "ygo/environment/public_environment_observation.hpp"
#include "ygo/teacher/public_fact_registry.hpp"
#include "ygo/teacher/strategy_profile.hpp"

namespace ygo::teacher {

inline constexpr std::string_view kTeacherPredicateRegistryContractId =
    "ocgforge.policy.teacher_predicate.v1";

enum class PredicateEvaluationStatus : std::uint8_t {
    True = 0,
    False = 1,
    Unsupported = 2,
    Invalid = 3,
};

struct PredicateDefinitionV1 final {
    std::string predicate_id;
    PredicateScope scope = PredicateScope::Observation;
    std::vector<PredicateAtomKind> argument_kinds;
};

class TeacherPredicateRegistryV1 final {
public:
    static const TeacherPredicateRegistryV1& canonical() noexcept;

    TeacherPredicateRegistryV1(const TeacherPredicateRegistryV1&) = delete;
    TeacherPredicateRegistryV1& operator=(const TeacherPredicateRegistryV1&) = delete;
    TeacherPredicateRegistryV1(TeacherPredicateRegistryV1&&) = delete;
    TeacherPredicateRegistryV1& operator=(TeacherPredicateRegistryV1&&) = delete;

    const std::vector<PredicateDefinitionV1>& definitions() const noexcept;

    bool validate_shape(const PredicateRef& value,
                        std::string* diagnostic = nullptr) const noexcept;
    bool validate_profile_ref(const PredicateRef& value,
                              const StrategyProfileV1& profile,
                              std::string* diagnostic = nullptr) const noexcept;

private:
    TeacherPredicateRegistryV1() = default;
};

PredicateEvaluationStatus combine_predicate_statuses(
    const std::vector<PredicateEvaluationStatus>& statuses) noexcept;

PredicateEvaluationStatus evaluate_observation_predicate(
    const PredicateRef& value, const PublicFactSnapshot& public_facts) noexcept;

PredicateEvaluationStatus evaluate_profile_static_predicate(
    const PredicateRef& value, const StrategyProfileV1& profile) noexcept;

PredicateEvaluationStatus evaluate_candidate_predicate(
    const PredicateRef& value,
    const environment::EnvironmentActionCandidate& candidate,
    const environment::PublicEnvironmentObservation& observation,
    std::uint8_t owning_participant,
    const StrategyProfileV1& profile) noexcept;

}  // namespace ygo::teacher
