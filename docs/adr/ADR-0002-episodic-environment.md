# ADR-0002: Use a shared semantic EpisodeDriver for the episodic environment

## Status

Proposed: option B — extract one shared `EpisodeDriver` from the current canonical simulation path and expose a thin versioned episodic reset/step facade over the existing `PlayerObservation + DecisionRequest + complete ActionCandidate[]` boundary.

This ADR is not architectural authority until accepted. Production implementation may target this proposal on a development branch, but merge/acceptance must preserve the contract and gates referenced below.

## Context

M4 is finalized on `main` and established deterministic parallel simulation, but it deliberately did not define an ML-facing episodic environment API. The current canonical simulation loop already owns the sensitive control semantics: fresh duel construction, core processing, retry/error handling, complete candidate validation, perspective-safe observation construction, candidate/observation consistency, continuation transitions, exact final response submission, trace generation, and terminal detection.

The post-M4 environment must provide explicit reset/step semantics without creating a second game-control implementation, weakening legal-domain completeness, exposing hidden state, or committing the project to Gymnasium, PettingZoo, RLlib, EnvPool, a particular tensor format, or a learning algorithm.

A focused August 2026 architecture comparison against Gymnasium, PettingZoo, OpenSpiel, RLlib, EnvPool, ygo-agent, EDOPro, and WindBot concluded **B — accept the one-semantic-candidate-per-step design with targeted corrections**. The key corrections are:

- deterministic semantic episode/decision identities cannot also serve as live stale-action freshness;
- canonical evaluation and the episodic API must share one advancement owner;
- true duel terminal, external interruption, semantic execution failure, and pre-mutation caller rejection must remain distinct;
- terminal observations must remain perspective-safe;
- complete variable candidate domains remain authoritative and ragged;
- execution budgets, reward policy, build provenance, and submission freshness are non-gameplay concerns.

The research record for this decision is `docs/research/episodic/EPISODIC_CONTRACT_RESEARCH_DECISION_2026-08-26.md`.

## Alternatives considered

### A. Implement a Gym/PettingZoo-style environment directly around `run_canonical_simulation()`

Rejected. A framework API would become the source of reset/status/reward/action-space semantics before OCGForge has frozen its own authoritative contract. Fixed action spaces, masks, auto-reset, generic `info`, reward-bearing step returns, and global/all-player state are useful adapter conventions but are not safe authoritative defaults for OCGForge.

### B. Extract one shared semantic `EpisodeDriver` and build thin clients over it

Accepted by this proposal.

The driver owns the authoritative episode lifecycle and advancement logic. The existing canonical evaluator becomes the first client of the driver. The new episodic environment becomes another client/facade. Policies continue to receive only the existing perspective-safe semantic boundary.

One accepted semantic `ActionCandidate` selection is one environment step. Intermediate continuation actions are full steps because adapter-local authoritative continuation state and the legal domain change even when ocgcore remains paused.

### C. Keep separate canonical and episodic advancement loops

Rejected. This duplicates exactly the logic most likely to drift: process boundaries, candidate validation, continuation handling, observation projection, exact response submission, terminal detection, and trace identity. Any performance or ML convenience gained would be outweighed by replay and correctness risk.

### D. Flatten each complete continuation into one macro action

Rejected as the authoritative contract. It can create combinatorial/factorial domains and encourages caps. A future search or model adapter may derive lossless macro representations, but the environment must preserve the current sequential complete continuation semantics.

### E. Make arbitrary decks/rules/reset options part of V1

Rejected for the first milestone. V1 is intentionally bound to the already-certified fixed environment. General support expansion remains a separate evidence-driven workstream.

## Decision

### 1. Step boundary

One accepted semantic `ActionCandidate` selection equals one authoritative step.

This includes intermediate `PICK`, `ASSIGN_AMOUNT`, `FINISH`, `CANCEL`, ordering, zone, announcement, tribute, sum, counter-allocation, and equivalent continuation operations. An intermediate continuation may return a new `DecisionFrame` with the same `engine_step_index` and no core response submission.

### 2. Sole advancement owner

A shared internal `EpisodeDriver` is the only owner of:

- episode lifecycle;
- fresh `CoreHost` / duel construction;
- observation sessions;
- automatic core processing;
- protocol decode and fail-closed errors;
- complete candidate validation;
- perspective-safe observation construction;
- continuation transitions;
- exact response submission;
- terminal/interruption/failure closure;
- semantic trace/audit prefix.

`run_canonical_simulation()` must become a client of this driver before the public episodic API is accepted.

### 3. Policy boundary

The authoritative policy boundary remains exactly:

```text
PlayerObservation
+
DecisionRequest
+
complete ordered ActionCandidate[]
```

The episodic layer adds lifecycle and identity metadata only. It must not expose raw `CoreHost` state, opponent-private observations, pointers, locators/caches, raw query buffers, exact response bytes, model hidden state, or belief state.

### 4. Identity domains

V1 separates:

- **environment semantic identity** — immutable certified game-contract inputs;
- **episode semantic identity** — environment identity + explicit seed/seat/start-player semantics;
- **semantic decision identity** — deterministic identity of the current policy decision/frame;
- **candidate-domain identity** — versioned digest of the complete authoritative ordered semantic-key domain;
- **submission freshness token** — non-semantic live-incarnation/request freshness used only to reject delayed calls;
- **execution provenance** — compiler/build/platform/worker/timing data, excluded from gameplay identity.

The submission token must never derive semantic meaning from PID, wall time, worker number, thread scheduling, machine identity, or a random UUID. It only needs to be non-reused within the lifetime of the live environment/session namespace; reconnect/restart invalidates the old namespace.

### 5. Episode configuration and run control

`EpisodeSpec` contains only semantic per-episode inputs for V1:

- explicit root seed;
- seat assignment;
- starting player.

`RunControl` is separate and non-semantic. V1 requires:

- engine-process budget;
- semantic-action budget;
- explicit administrative cancellation reason/source.

The semantic-action budget is required because a legal reversible continuation path can consume arbitrarily many policy decisions without advancing ocgcore. Budget exhaustion produces `Interrupted`, never a draw, win, loss, or semantic failure.

### 6. Lifecycle and closure

Authoritative states are:

```text
EMPTY
AWAITING_ACTION
GAME_TERMINAL
INTERRUPTED
FAILED
```

`StepRejected` is a result, not an episode state. Invalid lifecycle, stale incarnation/request, wrong semantic decision, or unknown semantic key must be rejected before authoritative mutation and leave the current frame unchanged.

There is no automatic reset. `step()` after a closed state rejects. `reset()` while `AWAITING_ACTION` rejects unless the current episode is explicitly interrupted first.

`GameTerminal` is only an engine-defined duel outcome. `Interrupted` is a valid semantic prefix stopped by run control. `Failed` is an invalid semantic/integrity execution. Failed mutable duel resources are torn down immediately.

### 7. Reward and terminal views

Numeric reward is not authoritative game semantics. A later versioned `RewardPolicy` maps authoritative outcome + perspective to training reward. `Interrupted` and `Failed` have no implicit outcome or reward.

Terminal outcome is perspective-independent. Any terminal observation is exposed only through an explicit perspective-safe `PlayerObservation` view; no reveal-all or all-player policy surface is added.

### 8. Candidate ordering

The protocol layer owns both candidate membership and authoritative ordering. For the same semantic request, that order must be deterministic and covered by tests. The `EpisodeDriver` preserves it exactly and never sorts/filters/truncates it. A model adapter may derive another order only if it retains an exact lossless mapping back to every current semantic key.

## Consequences

The design introduces more explicit lifecycle/identity types than a convenience-first Gym wrapper, but it preserves OCGForge's correctness hierarchy and makes asynchronous actors, recurrent policies, self-play, replay, and future trajectory collection possible without changing gameplay semantics.

The shared-driver extraction must be treated as an internal semantic-neutral refactor first. No public episodic PASS may be claimed until equivalence with the current canonical path is proven.

One-step-per-semantic-action may increase transport call count for continuations. This must be solved later through colocation, batched inference, async routing, shared memory, or bucketing—not by changing the authoritative step boundary or truncating legal domains.

## Compatibility / migration

- Existing decision, observation, trace, rules, deck, and semantic gameplay-hash contracts are not reinterpreted.
- Existing trace `decision_index` / `engine_advanced` semantics remain historical contracts. The new episodic contract defines its own policy-decision index and uses `core_response_submitted` for new transition metadata.
- The current canonical evaluator must migrate onto the shared driver with exact semantic equivalence.
- Gymnasium, PettingZoo, RLlib, EnvPool, tensorization, trajectories, actor transport, reward shaping, self-play leagues, checkpoint/fork, arbitrary decks, EDOPro deployment, and WindBot Arena integration remain deferred.

## Verification

Normative public semantics are specified in:

- `docs/contracts/episodic-environment-v1.md`

Required acceptance evidence is specified in:

- `docs/episodic/EPISODIC_V1_ACCEPTANCE.md`

The milestone cannot claim PASS unless all BLOCKER gates in that acceptance plan pass from a clean checkout and all existing M0–M4 regression/identity guarantees remain intact.
