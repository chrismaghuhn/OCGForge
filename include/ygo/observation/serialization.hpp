#pragma once

#include <string>

#include "ygo/observation/player_observation.hpp"

#ifdef YGO_M4_PERFORMANCE_AUDIT
#include "ygo/observation/performance_audit.hpp"
#endif

namespace ygo::observation {

std::string canonical_serialize_without_hash(const PlayerObservation& observation);
std::string canonical_serialize(const PlayerObservation& observation);
std::string observation_hash(const PlayerObservation& observation);

#ifdef YGO_M4_PERFORMANCE_AUDIT
std::string canonical_serialize_without_hash(const PlayerObservation& observation,
                                             PerformanceAuditCollector* audit);
std::string observation_hash(const PlayerObservation& observation,
                             PerformanceAuditCollector* audit);
#endif

}  // namespace ygo::observation
