#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "ygo/simulation/simulation_contract.hpp"

#ifdef YGO_M4_PERFORMANCE_AUDIT
#include "ygo/observation/performance_audit.hpp"
#endif

namespace ygo::m4::worker {

inline constexpr char kProtocolSchema[] = "ocgforge.m4.worker.v1";
inline constexpr char kProtocolVersion[] = "ocgforge.m4.worker.v1";
#ifdef YGO_M4_PERFORMANCE_AUDIT
inline constexpr char kPerformanceAuditSidecarPrefix[] = "M4_PERFORMANCE_AUDIT ";
inline constexpr char kSerializationLifecycleSidecarPrefix[] = "M4_SERIALIZATION_LIFECYCLE ";
#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
inline constexpr char kSerializationShapeSidecarPrefix[] = "M4_SERIALIZATION_SHAPE ";
#endif
#endif
inline constexpr char kWorkerIdentity[] = "ocgforge.m4.native_worker.v1";

struct WorkerReadyInfo {
    std::uint32_t pid = 0;
    std::string compiler_identity;
    std::string build_type;
};

struct ProtocolParseResult {
    std::optional<ygo::simulation::SimulationJob> job;
    std::optional<std::string> recoverable_job_id;
    std::string failure_code;
    std::string error_message;

    bool ok() const noexcept { return job.has_value(); }
};

std::string serialize_ready(const WorkerReadyInfo& worker,
                            const ygo::simulation::CanonicalSimulationConfig& config);

std::string serialize_result(const ygo::simulation::SimulationResult& result,
                             const WorkerReadyInfo& worker);

#ifdef YGO_M4_PERFORMANCE_AUDIT
std::string serialize_performance_audit(
    const std::string& job_id,
    const ygo::observation::PerformanceAuditSnapshot& audit);
std::string serialize_serialization_lifecycle(
    const std::string& job_id,
    const ygo::observation::PerformanceAuditSnapshot& audit);
#ifdef YGO_M4_SERIALIZATION_SHAPE_AUDIT
std::string serialize_serialization_shape(
    const std::string& job_id,
    const ygo::observation::PerformanceAuditSnapshot& audit);
#endif
#endif

std::string serialize_protocol_error(const ProtocolParseResult& parse_result);

ProtocolParseResult parse_job_request(std::string_view line);

std::optional<std::string> recover_job_id(std::string_view line);

}  // namespace ygo::m4::worker
