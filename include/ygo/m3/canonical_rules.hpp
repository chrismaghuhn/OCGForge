#pragma once

#include <cstdint>
#include <string_view>

#include "ocgapi_constants.h"

#ifndef YGO_M3_FORMAT_ID
#error "YGO_M3_FORMAT_ID must be supplied by the canonical rules lock"
#endif
#ifndef YGO_M3_DUEL_MODE_NAME
#error "YGO_M3_DUEL_MODE_NAME must be supplied by the canonical rules lock"
#endif
#ifndef YGO_M3_DUEL_FLAGS_VALUE
#error "YGO_M3_DUEL_FLAGS_VALUE must be supplied by the canonical rules lock"
#endif
#ifndef YGO_M3_RULES_BUNDLE_ID
#error "YGO_M3_RULES_BUNDLE_ID must be supplied by the canonical rules lock"
#endif
#ifndef YGO_M3_CORE_PATCHSET_ID
#error "YGO_M3_CORE_PATCHSET_ID must be supplied by the canonical rules lock"
#endif
#ifndef YGO_M3_CORE_PATCHSET_SHA256
#error "YGO_M3_CORE_PATCHSET_SHA256 must be supplied by the canonical rules lock"
#endif

namespace ygo::m3 {

struct CanonicalRulesConfig {
    std::string_view format_id;
    std::string_view duel_mode_name;
    std::uint64_t duel_flags;
    std::string_view rules_bundle_id;
    std::string_view core_patchset_id;
    std::string_view core_patchset_sha256;
};

inline constexpr std::uint64_t kCanonicalDuelFlags =
    static_cast<std::uint64_t>(YGO_M3_DUEL_FLAGS_VALUE);

static_assert(kCanonicalDuelFlags == static_cast<std::uint64_t>(DUEL_MODE_MR5),
              "canonical rules lock must use the pinned core DUEL_MODE_MR5");

inline constexpr CanonicalRulesConfig kCanonicalRules{
    YGO_M3_FORMAT_ID,
    YGO_M3_DUEL_MODE_NAME,
    kCanonicalDuelFlags,
    YGO_M3_RULES_BUNDLE_ID,
    YGO_M3_CORE_PATCHSET_ID,
    YGO_M3_CORE_PATCHSET_SHA256,
};

inline constexpr const CanonicalRulesConfig& canonical_rules() noexcept {
    return kCanonicalRules;
}

}  // namespace ygo::m3
