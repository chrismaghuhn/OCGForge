#pragma once

#include <string>

#include "ygo/observation/player_observation.hpp"
#include "ygo/protocol/continuation.hpp"

#ifdef YGO_M4_PERFORMANCE_AUDIT
#include "ygo/observation/performance_audit.hpp"
#endif

namespace ygo::observation {

// Attach only the identity and continuation reference for a decision. The
// complete legal candidate list remains owned by DecisionRequest and is not
// copied into PlayerObservation.
void attach_decision_context(PlayerObservation& observation,
                             const ygo::protocol::DecisionRequest& request);

#ifdef YGO_M4_PERFORMANCE_AUDIT
void attach_decision_context(PlayerObservation& observation,
                             const ygo::protocol::DecisionRequest& request,
                             PerformanceAuditCollector* audit);
#endif

// Verify that every card-bearing source/target reference in a request can be
// resolved against the current perspective-safe observation. A false result
// is fail-closed: callers must not manufacture an identity to make a request
// appear consistent.
bool candidate_observation_consistent(const PlayerObservation& observation,
                                      const ygo::protocol::ActionCandidate& candidate);

#ifdef YGO_M4_PERFORMANCE_AUDIT
bool candidate_observation_consistent(const PlayerObservation& observation,
                                      const ygo::protocol::ActionCandidate& candidate,
                                      PerformanceAuditCollector* audit);
#endif

}  // namespace ygo::observation
