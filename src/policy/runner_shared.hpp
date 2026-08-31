#pragma once

#include "ygo/environment/episodic_environment.hpp"
#include "ygo/policy/policy.hpp"
#include "ygo/trajectory/recorder.hpp"
#include "ygo/trajectory/types.hpp"

namespace ygo::policy::detail {

// Shared runner attribution preserves the existing RandomLegal cursor fields
// while giving deterministic Teacher selections the canonical NONE shape.
trajectory::PolicyRngDecisionProvenance make_policy_rng_attribution(
    const environment::DecisionFrame& frame,
    const PolicyExecutionBinding& binding,
    const PolicySelectionResult& selection) noexcept;

std::optional<trajectory::TerminalViews> terminal_views_for_environment(
    const environment::EpisodicEnvironment& environment);

trajectory::RestrictedReplayEvidence restricted_replay_evidence_for_interruption(
    const environment::EpisodeInterrupted& interruption);

}  // namespace ygo::policy::detail
