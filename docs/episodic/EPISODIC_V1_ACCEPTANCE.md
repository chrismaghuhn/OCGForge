# Episodic Environment V1 Acceptance Plan

**Contract:** `ocgforge.episodic_environment.v1`  
**ADR:** `docs/adr/ADR-0002-episodic-environment.md`  
**Status:** Proposed acceptance specification; no PASS is claimed by this document.

## Acceptance principles

The episodic milestone is accepted only when the new control surface is proven to be a semantic-preserving refactor/extension of the already-certified OCGForge path.

Priority remains:

```text
correctness
→ determinism
→ information safety
→ complete legal decisions
→ replay/auditability
→ maintainability
→ performance
→ ML scale
```

No gate may be weakened to keep a duel, worker, actor, or framework adapter running.

### Evidence rules

- Every gate produces a machine-readable artifact under `artifacts/episodic/v1/` and a validating test/tool.
- Generated acceptance evidence is derived data; do not hand-edit it to make a gate pass.
- Acceptance artifacts must bind to exact source commit, contract IDs, rules bundle, patchset, locked deck hashes, and test/tool versions.
- Clean-checkout verification is mandatory before a FINAL/PASS claim.
- Performance/timing data may be recorded, but no throughput target is required for semantic V1 acceptance.

## Gate matrix

| Gate | Purpose | Exact PASS condition | Severity if failed | Evidence artifact |
| --- | --- | --- | --- | --- |
| **G01 Reset determinism** | Prove reset is a deterministic semantic operation. | Same `EnvironmentConfig + EpisodeSpec` across at least 3 independent processes produces identical environment/episode semantic IDs, reset boundary kind, first semantic decision ID, acting player, observation canonical bytes/hash, protocol request ID, authoritative ordered semantic keys, candidate digest, and engine-step index. Only allowlisted execution provenance may differ. | BLOCKER | `reset_determinism.json` |
| **G02 Different-seed identity** | Prevent semantic seed aliasing. | At least two explicit root seeds are each repeatable and produce distinct episode semantic IDs/resolved seed bundles. Outcome need not differ. | BLOCKER | `seed_identity.json` |
| **G03 Repeated-identical-reset freshness** | Prevent stale calls across semantically identical reset incarnations. | Two identical resets reproduce equal semantic episode/frame identities but different live submission tokens. An `ActionSelection` retained from incarnation A is rejected in B before mutation. | BLOCKER | `repeated_reset_freshness.json` |
| **G04 Reset interleaving isolation** | Detect duel/script/observation state leakage between episodes. | In one persistent process, `A → B → C → A → D → A` produces every A frame/action/outcome/gameplay identity exactly equal to an independent fresh-process A reference. | BLOCKER | `reset_interleaving_isolation.json` |
| **G05 Reset soak isolation** | Catch slow state accumulation/global interpreter leakage. | A declared long-lived reset sequence (minimum 500 completed/closed episodes across mixed specs) has zero semantic drift, stale continuation/session state, cross-episode knowledge leakage, or resource-lifecycle failure; repeated reference specs match clean-process identities. | BLOCKER | `reset_soak.json` |
| **G06 Decision/observation/player coupling** | Enforce the sole perspective-safe policy frame. | Every published frame satisfies `acting_player == request.player == observation.perspective_player`. A test-only mismatch fails before publication. | BLOCKER | `player_coupling.json` |
| **G07 Complete candidate domain** | Prevent loss/fabrication/defaulting of legal actions. | For every certified request family and accepted full-game request, the shared driver exposes exactly the existing protocol candidate count, membership, order, action kinds, continuation linkage, and response-submission classification; candidate truncation/automatic/fabricated counts are zero. | BLOCKER | `candidate_completeness.json` |
| **G08 Candidate-order determinism** | Make ordered candidate identity an explicit contract rather than container luck. | Same semantic request across independent processes/build modes used by acceptance and worker counts yields identical authoritative semantic-key order. Deliberate unordered-container perturbation fixtures cannot change published order. | BLOCKER | `candidate_order_determinism.json` |
| **G09 Candidate-domain digest** | Bind frame/replay/adapter mapping to the exact ordered domain. | Independent implementation of `ocgforge.candidate_domain.v1` recomputes every published digest exactly. Mutating request kind, count, one key, or order changes/rejects the digest. Duplicate/empty invalid domains fail closed before frame publication. | BLOCKER | `candidate_digest.json` |
| **G10 Stale prior-decision rejection** | Reject delayed actions when the same semantic key appears in a later frame. | Submit frame N's complete action selection at N+1 where the key text is also legal. Result is `StepRejected`, frame/state unchanged, no response/process/action-trace append. | BLOCKER | `stale_decision_rejection.json` |
| **G11 Unknown-action rejection** | Reject actions outside the current complete domain. | Current IDs/token plus an absent semantic key returns `StepRejected(UNKNOWN_SEMANTIC_KEY)` with exact zero authoritative mutation. | BLOCKER | `unknown_action_rejection.json` |
| **G12 Lifecycle rejection** | Prevent implicit reset/ignored calls/silent abandonment. | `step()` in EMPTY/closed states rejects; `reset()` while AWAITING_ACTION rejects; no automatic reset or ignored action occurs. Explicit interrupt is required to abandon a live episode. | BLOCKER | `lifecycle_rejection.json` |
| **G13 Zero mutation on rejected call** | Certify `StepRejected` as non-transition. | Before/after snapshot of current frame bytes, continuation state hash, response/process counters, decision/action counts, trace length/prefix hash, observation sessions, and gameplay prefix is identical for every rejection class. Only rejection diagnostics may change. | BLOCKER | `rejection_zero_mutation.json` |
| **G14 Continuation immobility** | Preserve adapter-local intermediate continuation semantics. | For every certified continuation kind with an intermediate action: one semantic action is accepted, next policy decision index increments, `core_response_submitted=false`, no response/process call occurs due to the intermediate action, engine-step index remains unchanged, and next continuation/domain matches the existing oracle. | BLOCKER | `continuation_immobility.json` |
| **G15 Final continuation response equivalence** | Preserve exact final core response. | Certified continuation action sequences through old canonical path and shared driver submit exactly one final response with identical bytes/hash; no intermediate response; subsequent core progression and semantic trace are equivalent. | BLOCKER | `continuation_response_equivalence.json` |
| **G16 Atomic response equivalence** | Prevent response-builder drift for non-continuation requests. | Same certified atomic semantic key produces identical existing response bytes/hash, exactly one response submission, and semantically identical subsequent request/terminal behavior. | BLOCKER | `atomic_response_equivalence.json` |
| **G17 Semantic-action budget** | Bound legal non-progress/reversible continuation behavior without changing game semantics. | A controlled legal reversible/no-progress continuation path with budget N accepts exactly N semantic actions, then closes at the next actionable boundary as `Interrupted(SEMANTIC_ACTION_BUDGET)`. No fabricated terminal/outcome/failure, no extra action/response, valid replayable prefix, and episode semantic ID is independent of the budget value. | BLOCKER | `semantic_action_budget.json` |
| **G18 Engine-process budget** | Bound automatic/core progress without relabeling gameplay. | Process-budget exhaustion closes as `Interrupted(ENGINE_PROCESS_BUDGET)` with no winner/reward/failure fabrication. Accepted semantic prefix and submitted-response state are audit-replayable; process count never exceeds the configured budget. | BLOCKER | `engine_process_budget.json` |
| **G19 Canonical-simulator equivalence** | Prove one shared game-control implementation. | Current canonical policy/replay jobs executed through the new driver exactly match the pre-refactor canonical baseline for ordered semantic actions, acting players, candidate domains/digests, observations/hashes, response hashes, continuation identities, terminal result, error counters relevant to semantics, and semantic gameplay hash. | BLOCKER | `canonical_equivalence.json` |
| **G20 Semantic replay equivalence** | Prove semantic actions reconstruct every accepted boundary. | Record accepted semantic keys then replay independently with same semantic inputs. Ordered decision IDs, players, indices, request IDs/kinds, candidate domains/digests, observations, continuation identities, final response hashes, closure, outcome, and semantic gameplay hash match exactly. Submission tokens/provenance are excluded. | BLOCKER | `semantic_replay.json` |
| **G21 Worker-count / process determinism** | Preserve M4 scheduling independence. | Same logical episode corpus across independent processes and at least 1 and 16 workers yields identical per-job semantic identities/frames/domains/observations/responses/outcomes/gameplay hashes regardless of completion order. Only provenance/timing differ. | BLOCKER | `worker_determinism.json` |
| **G22 Paired-world privacy** | Prevent hidden-world differences from changing the current player's policy boundary. | Paired worlds that differ only in information hidden from the acting player produce identical policy-visible observation bytes/hash, candidate semantic fields/domain digest, semantic decision identity, and boundary wherever rules entitle the same visible information. | BLOCKER | `paired_world_privacy.json` |
| **G23 Terminal-view privacy** | Ensure episode end does not reveal hidden information. | Per-perspective terminal views obey unchanged `PlayerObservation` privacy/redaction under paired hidden worlds. No reveal-all/global/all-player policy result exists. | BLOCKER | `terminal_view_privacy.json` |
| **G24 True-terminal correctness** | Distinguish engine outcome from interruption/failure. | `EpisodeTerminal` appears only on actual engine terminal and carries exact winner/win_reason/gameplay hash. Process budget, semantic-action budget, cancellation, actor error, retry, or unsupported behavior never set a gameplay outcome. | BLOCKER | `terminal_correctness.json` |
| **G25 Administrative interruption** | Make voluntary stop explicit/auditable. | Interrupt at a live decision boundary (including after an intermediate continuation) produces `Interrupted(ADMINISTRATIVE_CANCEL)`, invalidates the current submission token, preserves the last valid semantic prefix, submits no fabricated action/response, and requires reset. | BLOCKER | `administrative_interruption.json` |
| **G26 Fail-closed execution** | Prevent integrity errors from becoming normal samples. | Inject retry, malformed/unsupported protocol, incomplete/duplicate candidates, observation/candidate mismatch, privacy violation, response inconsistency, and core error. Each yields typed `EpisodeFailure`, no normal outcome/fallback candidate, no further step, and correct failure stage/code. | BLOCKER | `fail_closed.json` |
| **G27 Reset after failure** | Ensure failed state cannot poison later episodes. | Force failure then reset same/different specs in the same process. New CoreHost/duel, sessions, continuation state, incarnation/token namespace are fresh; resulting semantic frames equal clean-process references. Failed mutable resources are not retained. | BLOCKER | `reset_after_failure.json` |
| **G28 Maximum-candidate witness** | Turn the per-domain candidate maximum into reproducible evidence. | Accepted deterministic workload emits one canonical tie-broken witness whose candidate count equals `candidate_domain_max` and whose episode/frame/request/continuation/raw-message/domain/observation identities reproduce exactly under replay. Historical aggregate `candidate_max=1344` is not a single-domain maximum. No candidate cap and witness is not policy input. | MAJOR | `max_candidate_witness.json` |
| **G29 Reward independence** | Prove later training reward cannot alter gameplay semantics. | Two deterministic test reward policies over the same terminal episodes produce identical environment/episode/frame/action/outcome/gameplay identities; only adapter reward and `reward_policy_id` differ. Interrupted/Failed receive no implicit reward. | BLOCKER | `reward_independence.json` |
| **G30 Contract-version rejection** | Prevent silent reinterpretation of incompatible callers/artifacts. | Unknown/incompatible episodic contract or identity schema IDs are rejected before authoritative mutation/persistence; supported v1 IDs reproduce golden canonical payload/hash fixtures. | BLOCKER | `contract_versioning.json` |
| **G31 M0–M4 regression and identity continuity** | Preserve all accepted foundations. | Full declared existing native/Python/protocol/observation/M3/M3.5/M4 suites pass with unchanged canonical rules bundle, patchset, locked deck hashes, PlayerObservation v1 semantics, decision legality, existing trace contracts, and M4 semantic compatibility expectations. No generated acceptance evidence is hand-edited. | BLOCKER | `regression_m0_m4.json` |
| **G32 Clean-checkout acceptance evidence** | Make the milestone independently verifiable. | From a fresh checkout at the exact acceptance source commit, declared build/tests/evidence generation all succeed; every evidence artifact hash matches its manifest; a second deterministic evidence render is byte-identical; `git diff --check` passes; repository is clean except explicitly generated ignored output. | BLOCKER to FINAL claim | `episodic_acceptance_manifest.json` |

## Required canonical-equivalence dimensions

G19/G20 MUST compare, at minimum:

```text
episode_semantic_id
ordered accepted semantic keys
semantic_action_count
acting-player sequence
policy decision_index sequence
engine_step_index sequence
protocol DecisionRequest.decision_id sequence
request kind sequence
complete authoritative candidate semantic-key sequence per frame
candidate_domain_digest per frame
PlayerObservation canonical bytes/hash per frame
continuation IDs/state hashes
core_response_submitted classification
final engine-response hashes
closure kind/reason
winner/win_reason when GameTerminal
semantic_gameplay_hash
```

Submission tokens, wall time, PID, worker slot, completion order, compiler/path provenance, and performance counters are not gameplay-equivalence fields.

## Reset-isolation corpus

G04/G05 MUST include more than repeated identical games. The persistent process corpus must interleave different:

- root seeds;
- starting players;
- normal/mirror seat assignment;
- episode lengths;
- continuation-heavy and non-continuation-heavy games;
- at least one intentionally failed episode before a successful reset.

The purpose is to detect hidden duel/script/Lua/session/knowledge/RNG state that survives reset.

## Candidate-order ownership gate

G08 is required before `candidate_domain_digest` can be treated as a replay-stable identity. Protocol candidate ordering is therefore part of V1 acceptance. The environment itself may not repair nondeterministic protocol ordering by sorting; nondeterminism must be fixed in the owning protocol layer with semantic-equivalence evidence.

## Interruption precedence

For an accepted semantic action:

1. true engine terminal reached while completing that action wins over future budget interruption;
2. process budget is checked before each core process call;
3. semantic-action budget is checked before publishing another actionable frame after the accepted action;
4. explicit administrative cancellation closes the current valid prefix at the next defined driver check/boundary and never fabricates a game outcome.

If more than one external run-control condition is simultaneously true, a versioned deterministic precedence must be implemented and fixture-tested. V1 recommended precedence is:

```text
ADMINISTRATIVE_CANCEL
→ ENGINE_PROCESS_BUDGET
→ SEMANTIC_ACTION_BUDGET
```

This precedence is operational status metadata only and is excluded from episode semantic identity.

## Required final evidence summary

A human-readable final summary may state `EPISODIC V1 FINAL PASS` only when it is derived from the machine-readable manifest and includes:

- accepted source commit;
- ADR/contract IDs;
- exact rules/patchset/deck identities;
- gate PASS table G01–G32;
- semantic equivalence result;
- privacy result;
- maximum-candidate witness identity/count;
- reset-isolation/soak result;
- M0–M4 regression result;
- clean-checkout evidence identity;
- explicit statement that trajectory, tensorization, ML algorithms, self-play league, arbitrary decks, checkpoint/fork, EDOPro, and WindBot Arena are not included.
