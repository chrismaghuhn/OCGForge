#include "ygo/teacher/strategy_profile.hpp"

#include <string>
#include <stdexcept>
#include <utility>

#include "ygo/trace/sha256.hpp"
#include "ygo/trajectory/codec.hpp"
#include "ygo/trajectory/policy_provenance.hpp"

namespace ygo::teacher {
namespace {

void require_condition(const bool condition, const char* message) {
    if (!condition) {
        throw std::invalid_argument(message);
    }
}

void set_diagnostic(std::string* diagnostic, const std::string& message) {
    if (diagnostic != nullptr) {
        *diagnostic = message;
    }
}

void validate_binding_content(const TeacherPolicyBindingV1& value) {
    require_condition(
        trajectory::is_canonical_provenance_identity(
            trajectory::ProvenanceKind::ProducerImplementation,
            value.teacher_core_artifact_identity),
        "teacher core artifact identity is not a canonical ProducerImplementation identity");
    require_condition(
        trajectory::is_canonical_identity(value.strategy_profile_id,
                                          kStrategyProfileIdentityPrefix),
        "teacher policy binding has an invalid strategy profile identity");
    require_condition(value.score_contract_identity == kTeacherScoreContractId,
                      "teacher policy binding has an invalid score contract");
    require_condition(value.fallback_contract_identity == kTeacherFallbackContractId,
                      "teacher policy binding has an invalid fallback contract");
    require_condition(value.tie_break_contract_identity == kTeacherTieBreakContractId,
                      "teacher policy binding has an invalid tie-break contract");
    if (value.diagnostic_contract_identity.has_value()) {
        require_condition(*value.diagnostic_contract_identity == kTeacherDiagnosticContractId,
                          "teacher policy binding has an invalid diagnostic contract");
    }
}

}  // namespace

std::vector<std::uint8_t> canonical_teacher_policy_binding_content_bytes(
    const TeacherPolicyBindingV1& value) {
    validate_binding_content(value);
    trajectory::ByteWriter writer;
    writer.string(kTeacherPolicyBindingSchemaId);
    writer.string(kTeacherPolicyBindingSchemaId);
    writer.string(value.teacher_core_artifact_identity);
    writer.string(value.strategy_profile_id);
    writer.string(value.score_contract_identity);
    writer.string(value.fallback_contract_identity);
    writer.string(value.tie_break_contract_identity);
    writer.u8(value.diagnostic_contract_identity.has_value() ? 1 : 0);
    if (value.diagnostic_contract_identity.has_value()) {
        writer.string(*value.diagnostic_contract_identity);
    }
    return std::move(writer).take();
}

std::string teacher_policy_binding_id(const TeacherPolicyBindingV1& value) {
    return std::string(kTeacherPolicyBindingIdentityPrefix) +
           trace::sha256_bytes(canonical_teacher_policy_binding_content_bytes(value));
}

bool validate_teacher_policy_binding(const TeacherPolicyBindingV1& value,
                                     std::string* diagnostic) noexcept {
    try {
        validate_binding_content(value);
        if (!trajectory::is_canonical_identity(value.teacher_policy_binding_id,
                                               kTeacherPolicyBindingIdentityPrefix)) {
            set_diagnostic(diagnostic, "teacher policy binding ID is not canonical");
            return false;
        }
        if (value.teacher_policy_binding_id != teacher_policy_binding_id(value)) {
            set_diagnostic(diagnostic, "teacher policy binding ID does not match content");
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        set_diagnostic(diagnostic, error.what());
        return false;
    } catch (...) {
        set_diagnostic(diagnostic, "teacher policy binding validation threw");
        return false;
    }
}

bool validate_teacher_policy_binding(
    const TeacherPolicyBindingV1& value,
    const StrategyProfileV1& profile,
    std::string* diagnostic) noexcept {
    if (!validate_teacher_policy_binding(value, diagnostic)) {
        return false;
    }
    if (!validate_strategy_profile(profile, diagnostic)) {
        return false;
    }
    if (value.strategy_profile_id != profile.profile_id) {
        set_diagnostic(diagnostic, "teacher policy binding references a different profile");
        return false;
    }
    return true;
}

}  // namespace ygo::teacher
