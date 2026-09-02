#pragma once

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ygo/phase6/bc_candidate_scorer.hpp"

namespace ygo::phase6::task4 {

inline constexpr std::uint32_t kStateNumericRowWidth = 8;
inline constexpr std::uint32_t kCandidateNumericRowWidth = 28;
inline constexpr std::string_view kNumericProjectionContractId =
    "ocgforge.phase6.task4.numeric_projection.v1";

using StateNumericRow = std::array<float, kStateNumericRowWidth>;
using CandidateNumericRow = std::array<float, kCandidateNumericRowWidth>;

std::vector<StateNumericRow> project_state_numeric_rows(
    const Phase6BcStateInputV1& state);

CandidateNumericRow project_candidate_numeric_row(
    const Phase6BcCandidateInputV1& candidate);

}  // namespace ygo::phase6::task4
