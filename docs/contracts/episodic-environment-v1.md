# OCGForge Episodic Environment Contract v1

**Contract ID:** `ocgforge.episodic_environment.v1`  
**Status:** Accepted — normative public semantics under ADR-0002; implementation acceptance still requires all declared gates.  
**Initial scope:** the already-certified fixed OCGForge rules bundle and locked Swordsoul Tenyi / Salamangreat matchup.  
**Owning layer:** `environment` / shared `EpisodeDriver` orchestration above CoreHost, protocol, observation, and trace layers.

This contract defines the smallest authoritative reset/step surface that may later be consumed by evaluation, teachers, learned policies, self-play controllers, and trajectory writers. It does not define tensors, rewards, model architecture, actor transport, arbitrary decks, or a framework adapter.

## 1. Invariants inherited unchanged

V1 MUST preserve all existing accepted invariants:

1. pinned rules/decks remain authoritative inputs;
2. game legality remains owned by the pinned engine plus accepted patchset;
3. the policy boundary remains `PlayerObservation + DecisionRequest + complete ActionCandidate[]`;
4. complete legal candidate domains are never truncated, capped, fabricated, or silently defaulted;
5. unsupported/malformed/incomplete behavior fails closed;
6. hidden state is never exposed through raw CoreHost queries, pointers, locators, caches, opponent-private observations, or inference from internal identity;
7. semantic identity is independent of wall time, PID, machine, worker count, scheduling, pointer values, and unordered iteration;
8. semantic gameplay hashes remain distinct from build/execution provenance;
9. existing decision, observation, response, trace, rules, and deck semantics are not silently reinterpreted.

## 2. Authoritative step definition

One accepted semantic `ActionCandidate` selection equals one authoritative environment step.

This includes intermediate continuation actions such as:

- `PICK`;
- `ASSIGN_AMOUNT`;
- `FINISH`;
- `CANCEL`;
- ordering choices;
- zone selections;
- announcement-mask choices;
- tribute/sum/counter-allocation sub-decisions;
- any equivalent future continuation action supported under the same contract.

A step does not mean one core `process()` call, one phase, one turn, one chain, or one complete combinatorial selection.

An intermediate continuation step is a real authoritative transition because adapter-local continuation state, decision context, and the complete legal domain change even when ocgcore remains paused.

## 3. Owning architecture

The shared internal `EpisodeDriver` is the sole owner of authoritative advancement semantics.

```text
                    EpisodeDriver
                     /          \
                    /            \
      canonical evaluator      episodic facade
       policy/replay loop       reset/step API
```

The driver owns:

- lifecycle state;
- fresh duel/CoreHost construction;
- both `ObservationSession`s;
- automatic core processing;
- message ingestion and decode;
- retry/error handling;
- complete candidate validation;
- perspective-safe observation construction;
- candidate/observation consistency checks;
- continuation transitions;
- exact final response submission;
- terminal/interruption/failure closure;
- semantic trace/audit prefix.

The driver does NOT own:

- policy selection;
- numeric reward policy;
- neural-network tensors;
- recurrent hidden state;
- belief state;
- actor transport/RPC;
- self-play matchmaking;
- policy/checkpoint provenance.

`run_canonical_simulation()` MUST become a client of the shared driver before episodic V1 can claim acceptance.

## 4. Lifecycle

Authoritative lifecycle states are:

```text
EMPTY
AWAITING_ACTION
GAME_TERMINAL
INTERRUPTED
FAILED
```

Allowed transitions:

```text
EMPTY
  reset() -> AWAITING_ACTION | GAME_TERMINAL | INTERRUPTED | FAILED

AWAITING_ACTION
  valid step() -> AWAITING_ACTION | GAME_TERMINAL | INTERRUPTED | FAILED
  invalid step() -> StepRejected; remains AWAITING_ACTION unchanged
  interrupt() -> INTERRUPTED

GAME_TERMINAL / INTERRUPTED / FAILED
  reset() -> a new episode incarnation
```

Rules:

- no automatic reset;
- `step()` outside `AWAITING_ACTION` rejects without mutation;
- `reset()` while `AWAITING_ACTION` rejects; the caller must explicitly interrupt first;
- no action is ignored after closure;
- no further stepping is allowed after semantic execution failure;
- `StepRejected` is a call result, not an episode lifecycle state.

## 5. Version and identity domains

The following schema/domain IDs are distinct:

- environment contract: `ocgforge.episodic_environment.v1`;
- environment semantic identity: `ocgforge.environment_identity.v1`;
- episode semantic identity: `ocgforge.episode_identity.v1`;
- semantic decision identity: `ocgforge.semantic_decision_identity.v1`;
- candidate domain digest: `ocgforge.candidate_domain.v1`;
- decision contract: `ocgforge.decision_protocol.v1`;
- action identity schema: `ocgforge.action_identity.v1`;
- seed derivation: `ocgforge.seed_derivation.v1`;
- ScriptStore resolution: `ocgforge.script_resolution.v1`;
- required-script closure schema: `ocgforge.required_script_closure.v1`;
- required-script closure hash domain: `ocgforge.required_script_closure_identity.v1`.

The Decision Protocol, Action Identity, Seed Derivation, and Script
Resolution contracts are the normative owners of their corresponding IDs.
This contract consumes those IDs when constructing the environment semantic
identity; it does not redefine their meanings. The exact closure inputs and
canonical bytes are defined by `docs/contracts/script-resolution-v1.md`.

### 5.1 Canonical identity primitive encoding

Unless an existing accepted contract already supplies canonical bytes, new V1 identity payloads use this primitive encoding:

- unsigned integers: fixed-width big-endian (`u8`, `u32`, `u64` as declared);
- booleans: one byte `0x00` or `0x01`;
- UTF-8 strings: `u32be byte_length || raw_utf8_bytes`;
- byte strings: `u32be byte_length || bytes`;
- vectors: `u32be element_count || encoded elements in declared order`;
- optional values: one presence byte followed by the encoded value when present;
- every hash domain begins with its exact UTF-8 domain string encoded as a length-prefixed string.

Digest algorithm: SHA-256 over the exact canonical payload.

Changing the canonical byte layout requires a new identity schema ID.

### 5.2 Environment semantic identity

`environment_semantic_id` commits to immutable semantic environment inputs, including:

- identity schema ID;
- decision/observation/action identity contract IDs;
- candidate digest schema ID;
- seed-derivation contract ID;
- rules bundle identity;
- ocgcore base commit and accepted patchset ID/hash;
- CardScripts and database identities;
- format ID, duel mode, duel flags;
- ordered locked deck hashes/definitions;
- required script closure identity.

For V1, the consumed identity fields have these exact values:

```text
decision_contract_id      = "ocgforge.decision_protocol.v1"
action_identity_schema_id = "ocgforge.action_identity.v1"
seed_derivation_id        = "ocgforge.seed_derivation.v1"
```

`required_script_closure_identity` is the lowercase SHA-256 identity defined
by `docs/contracts/script-resolution-v1.md`; it binds the pinned/resolved
CardScripts environment, ScriptStore resolution contract, CoreHost bootstrap
names, and deck-derived expected-card seed set. It is not a runtime allowlist.

It excludes compiler, build type, filesystem paths, worker/PID, timing, machine, and run control.

### 5.3 Episode semantic identity

`episode_semantic_id` commits to:

- episode identity schema ID;
- `environment_semantic_id`;
- explicit root seed;
- resolved seed bundle;
- seat assignment;
- starting player;
- ordered resolved seat deck hashes.

Two resets with the same semantic inputs MUST produce the same episode semantic ID even when executed in different processes or at different times.

### 5.4 Semantic decision identity

The environment wraps, but does not redefine, the existing protocol `DecisionRequest.decision_id`.

`semantic_decision_id` commits to:

- decision identity schema ID;
- `episode_semantic_id`;
- environment `decision_index`;
- protocol `DecisionRequest.decision_id`;
- acting player;
- request `engine_step_index`;
- canonical `PlayerObservation.observation_hash`;
- `candidate_domain_digest`.

It is deterministic and replayable.

### 5.5 Submission freshness token

Deterministic semantic identities MUST NOT be used as the sole stale-action freshness mechanism.

Each live environment instance maintains a non-semantic token namespace. Conceptually a token may be represented by:

```text
{ episode_incarnation_counter, frame_generation_counter }
```

Requirements:

- a token issued by one live episode/frame MUST NOT be accepted for another;
- tokens are never reused within the lifetime of the live environment/session namespace;
- reset creates a new episode incarnation;
- every newly published actionable frame receives a new token;
- reconnect/process replacement invalidates the old live token namespace;
- token values are excluded from semantic IDs, gameplay hashes, replay equivalence, model inputs, and persisted semantic trajectories;
- token generation MUST NOT depend on wall time, PID, worker number, scheduling, machine identity, pointer identity, or random UUID semantics.

The token is control-plane freshness only.

### 5.6 Execution provenance

Compiler, build type, platform, worker identity/restart index, timing, machine metadata, and paths belong to a separate versioned execution-provenance surface. They MUST NOT affect environment/episode/decision/gameplay semantic identity.

## 6. Candidate ordering and digest

The protocol layer owns both candidate membership and authoritative ordering.

For the same semantic request, candidate order MUST be deterministic and regression-tested. The `EpisodeDriver` MUST preserve the protocol-provided order exactly and MUST NOT sort, filter, deduplicate, truncate, or fabricate candidates.

`candidate_domain_digest` is SHA-256 over canonical bytes containing:

```text
domain = "ocgforge.candidate_domain.v1"
request kind
candidate count
for each candidate in authoritative protocol order:
    semantic_key
```

The count and every key are encoded with the primitives in section 5.1.

The digest:

- binds audit/replay/model-adapter mapping to the exact current domain;
- does not replace the complete candidate collection;
- is not a model feature by default;
- changes if candidate membership or authoritative order changes.

A future model adapter may reorder candidates internally only if it preserves an exact bijection to every authoritative semantic key and proves no candidate loss.

## 7. Public conceptual types

These are semantic pseudotypes. Production language representation may differ only if the same invariants and versioning are preserved.

### 7.1 `EnvironmentConfig`

```text
EnvironmentConfig {
    contract_id = "ocgforge.episodic_environment.v1"
    environment_semantic_id

    decision_contract_id
    observation_contract_id
    action_identity_schema_id
    candidate_digest_schema_id
    episode_identity_schema_id
    decision_identity_schema_id
    seed_derivation_id

    rules_bundle / core / patchset / CardScripts / database identities
    format_id
    duel_mode
    duel_flags
    locked deck definitions and ordered hashes
    required_script_closure_identity
}
```

V1 exposes no arbitrary deck/rules selectors.

### 7.2 `EpisodeSpec`

```text
EpisodeSpec {
    contract_id
    root_seed: u64
    seat_assignment
    starting_player: u8
}
```

`starting_player` MUST be validated before duel start.

### 7.3 `RunControl`

```text
RunControl {
    engine_process_budget: u64
    semantic_action_budget: u64
    cancellation_reason/source
}
```

Requirements:

- both budgets are positive finite counts in V1;
- RunControl is excluded from episode semantic identity;
- no authoritative wall-clock timeout is part of V1;
- process-budget exhaustion and semantic-action-budget exhaustion produce `Interrupted`;
- administrative cancellation produces `Interrupted`.

`semantic_action_budget` is REQUIRED NOW because legal reversible continuation behavior may consume repeated semantic actions while `engine_step_index` does not advance.

### 7.4 `DecisionFrame`

```text
DecisionFrame {
    contract_id
    episode_semantic_id
    semantic_decision_id
    submission_token                // non-semantic

    decision_index: u64             // policy decisions only
    engine_step_index: u64
    acting_player: u8

    PlayerObservation observation
    DecisionRequest request          // owns the one complete candidate collection
    candidate_domain_digest
}
```

Hard invariant:

```text
acting_player
== request.player
== observation.perspective_player
```

The frame MUST NOT expose opponent-private observations, raw core state, response bytes, pointers, query buffers, recurrent model state, or belief state.

### 7.5 `ActionSelection`

```text
ActionSelection {
    contract_id
    episode_semantic_id
    semantic_decision_id
    submission_token
    semantic_key
}
```

An integer candidate index is never authoritative across a frame boundary. Exact engine-response bytes are never policy input.

### 7.6 `AcceptedActionTransition`

Every accepted `step()` publishes audit metadata:

```text
AcceptedActionTransition {
    episode_semantic_id
    semantic_decision_id
    decision_index
    selected_semantic_key
    core_response_submitted: bool
    final_response_sha256: optional<sha256>   // audit only
}
```

For an intermediate continuation:

```text
core_response_submitted == false
final_response_sha256 == absent
engine_step_index does not advance as a consequence of the continuation action itself
```

For an atomic or terminal continuation action, exactly one final core response is submitted.

This new field name does not reinterpret existing versioned trace `engine_advanced` semantics.

### 7.7 `StepResult`

```text
StepResult =
    StepAccepted {
        AcceptedActionTransition transition
        NextBoundary next
    }
  | StepRejected

NextBoundary =
    AwaitingAction(DecisionFrame)
  | EpisodeTerminal
  | EpisodeInterrupted
  | EpisodeFailure
```

`reset()` returns a boundary directly (or a reset-call rejection); it does not fabricate an accepted action.

### 7.8 `StepRejected`

```text
StepRejected {
    contract_id
    rejection_code
    submitted episode/decision/token/key safe identifiers
    current_episode_semantic_id
    current_semantic_decision_id
    current_candidate_domain_digest
    authoritative_state_unchanged = true
}
```

Required rejection classes include:

- invalid lifecycle;
- wrong episode semantic ID;
- stale/wrong submission token;
- wrong semantic decision ID;
- unknown semantic key.

A rejection MUST occur before continuation mutation, response submission, core processing, decision-index increment, semantic trace append, or any other authoritative mutation.

### 7.9 `EpisodeTerminal`

```text
EpisodeTerminal {
    contract_id
    episode_semantic_id
    winner
    win_reason
    semantic_action_count
    last_decision_index: optional<u64>
    final_engine_step_index
    semantic_gameplay_hash
    final_audit_prefix_hash
}
```

Only a true engine-defined duel ending produces this type.

### 7.10 `EpisodeInterrupted`

```text
EpisodeInterrupted {
    contract_id
    episode_semantic_id
    reason
    semantic_action_count
    last_semantic_decision_id: optional
    last_decision_index: optional<u64>
    final_engine_step_index
    last_valid_audit_prefix_hash
    run_control_evidence
}
```

Required V1 reasons:

- `ENGINE_PROCESS_BUDGET`;
- `SEMANTIC_ACTION_BUDGET`;
- `ADMINISTRATIVE_CANCEL`.

It has no winner, win reason, draw meaning, or implicit reward.

### 7.11 `EpisodeFailure`

```text
EpisodeFailure {
    contract_id
    episode_semantic_id: optional
    failure_code
    failure_stage
    semantic_action_count
    last_semantic_decision_id: optional
    last_valid_audit_prefix_hash: optional
    mutation_may_have_occurred: bool
    restricted_diagnostic_reference: optional
}
```

Failures include retry, core error, unsupported/malformed protocol, incomplete/duplicate candidates, privacy failure, candidate/observation inconsistency, invalid authoritative state, and response/protocol inconsistency.

After failure:

- no further `step()` is permitted;
- mutable duel/CoreHost/ObservationSession/continuation resources are torn down immediately;
- only immutable value diagnostics/evidence remain;
- a new `reset()` constructs a fresh duel and token namespace.

### 7.12 Perspective-safe terminal view

Terminal outcome is authoritative and perspective-independent. A terminal observation, when requested, is a separate per-player surface:

```text
PerspectiveTerminalView(player) -> optional<PlayerObservation>
```

It MUST use the unchanged privacy contract. No reveal-all/global/all-player terminal state is policy-visible.

## 8. Decision and engine indices

Environment `decision_index` is a 0-based sequence of published semantic policy decisions only.

- first actionable frame after reset: `decision_index = 0`;
- `StepRejected`: index unchanged;
- every accepted semantic action leading to another actionable frame increments the next frame index by one;
- intermediate continuations count;
- terminal/audit records do not fabricate a new policy decision index.

`engine_step_index` remains the existing core-processing progress/request identity. During continuations it may remain constant across multiple environment decisions.

Existing trace index semantics remain unchanged unless separately versioned.

## 9. Reset semantics

`reset(spec, control)` MUST:

1. reject if the environment is still `AWAITING_ACTION`;
2. validate immutable environment identity and explicit EpisodeSpec/RunControl before duel mutation;
3. create a new non-semantic episode incarnation/token namespace;
4. derive the exact existing canonical seed bundle deterministically and record root + resolved bundle;
5. construct a fresh CoreHost/duel;
6. load the exact locked decks and required scripts;
7. set validated starting player before duel start;
8. create fresh observation sessions for both perspectives;
9. initialize semantic trace/audit state and separate execution provenance;
10. advance using the shared driver until the first actionable decision, true game terminal, interruption, or failure.

Reset isolation is strict. Duel/script/continuation/knowledge/RNG cursor state from a prior episode MUST NOT survive. Immutable semantic-transparent caches may survive only if equivalence tests prove they cannot carry duel-local state.

## 10. Step semantics

`step(selection)` MUST:

1. require `AWAITING_ACTION`;
2. validate episode semantic ID, submission token, semantic decision ID, current candidate-domain identity, and semantic-key membership before mutation;
3. return `StepRejected` with zero authoritative mutation on any mismatch;
4. commit exactly one semantic action to the audit prefix;
5. increment semantic action count exactly once;
6. apply continuation or atomic action using existing protocol semantics;
7. for intermediate continuation: submit no core response and call no core process as a consequence of the continuation action itself;
8. for terminal continuation/atomic action: submit exactly one existing canonical final response;
9. advance with the shared driver to the next boundary;
10. enforce process budget before each core process call;
11. if a new actionable frame would be published after `semantic_action_count` reaches the semantic-action budget, close as `Interrupted(SEMANTIC_ACTION_BUDGET)` instead of publishing another actionable frame;
12. if the accepted action reaches true engine terminal before another decision, return `GameTerminal` even when the accepted-action count equals the budget;
13. preserve exact candidate, observation, response, continuation, and gameplay identities required by replay/equivalence.

## 11. Explicit interruption

`interrupt(ADMINISTRATIVE_CANCEL)` is valid only for a live non-closed episode. It closes the current valid semantic prefix as `Interrupted` and invalidates the current submission token. It never creates a winner/reward and never silently discards the prefix.

## 12. Reward boundary

The authoritative environment exposes outcome/status, not numeric reward.

A later adapter may define:

```text
RewardPolicy(reward_policy_id, GameTerminal outcome, perspective) -> number
```

Changing reward policy MUST NOT change environment/episode/decision IDs, observations, candidates, accepted actions, outcomes, or gameplay hash.

`Interrupted` and `Failed` have no implicit reward.

## 13. Replay/equivalence requirement

For the same `EnvironmentConfig`, `EpisodeSpec`, and ordered accepted semantic keys, independent execution MUST reproduce:

- environment and episode semantic identity;
- reset boundary kind;
- ordered semantic decision IDs;
- acting-player sequence;
- policy decision and engine-step indices;
- protocol request identities/kinds;
- complete authoritative candidate order and candidate-domain digests;
- observation schemas/bytes/hashes;
- continuation identities/state hashes;
- exact final response hashes;
- closure kind/reason;
- winner/win reason when GameTerminal;
- semantic gameplay hash.

Build/runtime provenance may differ without gameplay divergence.

Submission tokens are explicitly excluded from replay equivalence.

## 14. Resource lifecycle

Each reset owns one fresh duel. Mutable duel state never crosses episode boundaries.

On `GAME_TERMINAL`, `INTERRUPTED`, or `FAILED`, the driver invalidates current action freshness immediately. Mutable authoritative resources are released as soon as required safe value evidence/terminal views have been materialized. `FAILED` requires immediate teardown and cannot retain a live CoreHost for inspection through the public API.

## 15. Explicit non-goals

V1 does not implement or define:

- trajectory/shard storage;
- behavior/opponent policy provenance;
- tensorization, embeddings, masks, padding, bucketing, segmented softmax;
- PyTorch/JAX/TensorFlow/ONNX integration;
- BC, PPO, IMPALA/V-trace, R2D2, AlphaZero, CFR, MCTS, or other algorithms;
- self-play league/matchmaking;
- actor/learner transport or GPU inference;
- checkpoint/fork/restore;
- replay-format redesign;
- arbitrary decks/rules/formats;
- new card/mechanics coverage;
- reward shaping;
- EDOPro deployment;
- WindBot Arena integration;
- performance optimization not required for semantic equivalence.

## 16. Acceptance

This contract is not considered implemented merely because types or methods exist. Acceptance requires the complete gate matrix in `docs/episodic/EPISODIC_V1_ACCEPTANCE.md`, including canonical-driver equivalence, repeated-reset freshness, reset isolation, continuation immobility, zero-mutation rejections, paired-world privacy, maximum-domain witness, M0–M4 regression, and clean-checkout evidence.
