#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ygo::environment {
struct CertifiedEnvironmentConfig;
}

namespace ygo::teacher {

inline constexpr std::string_view kStrategyProfileSchemaId =
    "ocgforge.strategy_profile.v1";
inline constexpr std::string_view kStrategyProfileIdentityPrefix =
    "ocgforge.strategy_profile.v1.";
inline constexpr std::string_view kTeacherPolicyBindingSchemaId =
    "ocgforge.teacher_policy_binding.v1";
inline constexpr std::string_view kTeacherPolicyBindingIdentityPrefix =
    "ocgforge.teacher_policy_binding.v1.";
inline constexpr std::string_view kCertifiedMatchupId =
    "ocgforge.matchup.swordsoul_salamangreat.v1";
inline constexpr std::string_view kTeacherScoreContractId =
    "ocgforge.policy.teacher_score.v1";
inline constexpr std::string_view kTeacherFallbackContractId =
    "ocgforge.policy.teacher_fallback.v1";
inline constexpr std::string_view kTeacherTieBreakContractId =
    "ocgforge.policy.public_key_tiebreak.v1";
inline constexpr std::string_view kTeacherDiagnosticContractId =
    "ocgforge.teacher_decision_explanation.v1";

enum class PredicateScope : std::uint8_t {
    Observation = 0,
    Candidate = 1,
    AcceptedPublicHistory = 2,
    ProfileStatic = 3,
};

enum class PredicateAtomKind : std::uint8_t {
    Token = 0,
    U64 = 1,
    I32 = 2,
    Passcode = 3,
    Boolean = 4,
};

struct PredicateAtom final {
    PredicateAtomKind kind = PredicateAtomKind::Token;
    std::string token;
    std::uint64_t u64 = 0;
    std::int32_t i32 = 0;
    std::uint32_t passcode = 0;
    bool boolean = false;
};

struct PredicateRef final {
    PredicateScope scope = PredicateScope::Observation;
    std::string predicate_id;
    std::vector<PredicateAtom> arguments;
};

struct CardRoleEntry final {
    std::uint32_t passcode = 0;
    std::vector<std::string> role_ids;
};

struct ResourceDefinition final {
    std::string resource_id;
    std::string public_fact_id;
    std::uint32_t max_value = 0;
    std::int32_t preservation_priority = 0;
    std::int32_t conversion_priority = 0;
};

struct CandidateIntentDefinition final {
    std::string intent_id;
    std::vector<PredicateRef> public_predicates;
};

struct GoalDefinition final {
    std::string goal_id;
    std::int32_t priority = 0;
    std::vector<PredicateRef> preconditions;
    std::vector<PredicateRef> completion_predicates;
    std::vector<PredicateRef> stop_predicates;
};

struct ResourceRequirement final {
    std::string resource_id;
    std::uint32_t minimum = 0;
};

struct LineNode final {
    std::string node_id;
    std::vector<std::string> candidate_intent_ids;
    std::vector<PredicateRef> completion_predicates;
    std::vector<std::string> preserve_resource_ids;
    std::vector<PredicateRef> stop_predicates;
};

struct NodeDependency final {
    std::string predecessor_node_id;
    std::string successor_node_id;
};

struct LineDefinition final {
    std::string line_id;
    std::string goal_id;
    std::vector<PredicateRef> applicability_predicates;
    std::vector<ResourceRequirement> required_resources;
    std::vector<std::string> optional_resources;
    std::vector<LineNode> nodes;
    std::vector<NodeDependency> dependencies;
    std::vector<std::string> recovery_edge_ids;
};

enum class RecoverySourceKind : std::uint8_t {
    Goal = 0,
    Line = 1,
    Node = 2,
};

enum class ConfidenceClass : std::uint8_t {
    High = 0,
    Medium = 1,
    Low = 2,
    Fallback = 3,
};

struct RecoveryEdge final {
    std::string recovery_edge_id;
    RecoverySourceKind source_kind = RecoverySourceKind::Goal;
    std::string source_id;
    std::vector<std::string> invalidation_reason_ids;
    std::vector<PredicateRef> preconditions;
    std::vector<std::string> candidate_intent_ids;
    std::string target_goal_id;
    std::optional<std::string> target_line_id;
    std::vector<std::string> preserve_resource_ids;
    ConfidenceClass confidence_cap = ConfidenceClass::High;
};

struct InteractionRule final {
    std::string interaction_id;
    std::vector<PredicateRef> trigger_predicates;
    std::vector<std::string> candidate_intent_ids;
    std::int32_t timing_priority = 0;
    std::vector<std::string> preserve_resource_ids;
};

enum class ScoreDimension : std::uint8_t {
    SurvivalOrGuaranteedLethalClass = 0,
    ActiveGoalLineOrValidatedRecoveryProgress = 1,
    ImmediateTacticalNecessity = 2,
    InteractionTiming = 3,
    PublicTargetValue = 4,
    ResourcePreservationAndCost = 5,
    EngineFollowUpValue = 6,
    BattleAndMainPhase2Value = 7,
    ProfilePreference = 8,
};

enum class PreferenceSubjectKind : std::uint8_t {
    Global = 0,
    Goal = 1,
    Line = 2,
    Intent = 3,
    Resource = 4,
    Interaction = 5,
};

struct PreferenceEntry final {
    ScoreDimension dimension = ScoreDimension::ProfilePreference;
    PreferenceSubjectKind subject_kind = PreferenceSubjectKind::Global;
    std::string subject_id;
    std::int32_t value = 0;
};

struct StrategyProfileV1 final {
    std::string matchup_id;
    std::string rules_bundle_id;
    std::string format_id;
    std::string duel_mode;
    std::uint64_t duel_flags = 0;
    std::uint8_t own_deck_role = 0;
    std::string own_deck_id;
    std::string own_deck_sha256;
    std::uint8_t opponent_deck_role = 1;
    std::string opponent_deck_id;
    std::string opponent_deck_sha256;
    std::vector<CardRoleEntry> card_roles;
    std::vector<ResourceDefinition> resources;
    std::vector<CandidateIntentDefinition> candidate_intents;
    std::vector<GoalDefinition> goals;
    std::vector<LineDefinition> lines;
    std::vector<RecoveryEdge> recovery_edges;
    std::vector<InteractionRule> interactions;
    std::vector<PreferenceEntry> preferences;
    std::string profile_id;
};

struct TeacherPolicyBindingV1 final {
    std::string teacher_core_artifact_identity;
    std::string strategy_profile_id;
    std::string score_contract_identity;
    std::string fallback_contract_identity;
    std::string tie_break_contract_identity;
    std::optional<std::string> diagnostic_contract_identity;
    std::string teacher_policy_binding_id;
};

std::vector<std::uint8_t> canonical_predicate_ref_bytes(const PredicateRef& value);
std::vector<std::uint8_t> canonical_strategy_profile_content_bytes(
    const StrategyProfileV1& value);
std::string strategy_profile_id(const StrategyProfileV1& value);

bool validate_strategy_profile(const StrategyProfileV1& value,
                                std::string* diagnostic = nullptr) noexcept;
bool validate_strategy_profile_binding(
    const StrategyProfileV1& value,
    const environment::CertifiedEnvironmentConfig& config,
    std::string* diagnostic = nullptr) noexcept;

std::vector<std::uint8_t> canonical_teacher_policy_binding_content_bytes(
    const TeacherPolicyBindingV1& value);
std::string teacher_policy_binding_id(const TeacherPolicyBindingV1& value);

bool validate_teacher_policy_binding(const TeacherPolicyBindingV1& value,
                                     std::string* diagnostic = nullptr) noexcept;
bool validate_teacher_policy_binding(
    const TeacherPolicyBindingV1& value,
    const StrategyProfileV1& profile,
    std::string* diagnostic = nullptr) noexcept;

}  // namespace ygo::teacher
