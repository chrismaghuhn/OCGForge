#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ygo::environment {

inline constexpr std::string_view kDecisionContractId = "ocgforge.decision_protocol.v1";
inline constexpr std::string_view kActionIdentitySchemaId = "ocgforge.action_identity.v1";
inline constexpr std::string_view kSeedDerivationId = "ocgforge.seed_derivation.v1";
inline constexpr std::string_view kScriptResolutionContractId = "ocgforge.script_resolution.v1";
inline constexpr std::string_view kRequiredScriptClosureSchemaId = "ocgforge.required_script_closure.v1";
inline constexpr std::string_view kRequiredScriptClosureDomain =
    "ocgforge.required_script_closure_identity.v1";

inline constexpr std::array<std::string_view, 3> kRequiredGlobalScriptNames = {
    "constant.lua", "utility.lua", "proc_normal.lua"};

struct RequiredScriptClosureInput final {
    std::string card_scripts_commit;
    std::string card_scripts_tree_sha256;
    std::string script_resolution_contract_id;
    std::vector<std::string> required_global_script_names;
    std::vector<std::uint32_t> required_script_codes;
};

std::vector<std::uint8_t> canonical_required_script_closure_bytes(
    const RequiredScriptClosureInput& input);

std::string required_script_closure_identity(const RequiredScriptClosureInput& input);

}  // namespace ygo::environment
