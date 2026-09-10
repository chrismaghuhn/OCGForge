#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace ygo::diagnostics {

// This schema is diagnostic-only. It is deliberately not consumed by any
// gameplay, trajectory, replay, admission, or dataset identity path.
inline constexpr char kTask7ForensicDiagnosticsSchemaId[] =
    "ocgforge.phase6.task7.forensic_performance_diagnostics.v1";

struct Task7DiagnosticEvent final {
    std::string phase;
    bool completed = true;
    std::uint64_t duration_us = 0;
    std::uint64_t call_count = 1;
    std::uint64_t max_single_call_us = 0;

    // Public/audit-safe workload counters only.
    std::uint64_t engine_process_count = 0;
    std::uint64_t semantic_action_count = 0;
    std::uint64_t decision_index = 0;
    std::uint64_t engine_step_index = 0;
    std::uint8_t acting_player = 0;
    std::uint64_t candidate_count = 0;
    std::uint64_t candidate_total = 0;
    std::uint64_t candidate_max = 0;
    std::uint64_t observation_count = 0;
    std::uint64_t continuation_decision_count = 0;
    std::uint64_t continuation_action_count = 0;
    std::uint64_t step_accepted_count = 0;
    std::uint64_t step_rejected_count = 0;

    std::uint64_t supported_evaluations = 0;
    std::uint64_t not_applicable_evaluations = 0;
    std::uint64_t unsupported_evaluations = 0;
    std::uint64_t invalid_evaluations = 0;
    std::uint64_t f0_count = 0;
    std::uint64_t f1_count = 0;
    std::uint64_t f2_count = 0;
    std::uint64_t f3_count = 0;
    std::uint64_t f4_count = 0;

    std::string decision_family;
    std::string closure_kind;
    std::string failure_code;
    std::string failure_stage;
};

using Task7DiagnosticObserver = std::function<void(const Task7DiagnosticEvent&)>;

}  // namespace ygo::diagnostics
