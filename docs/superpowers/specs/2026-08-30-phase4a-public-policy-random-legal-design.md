# Phase 4A Public Policy Substrate and RandomLegal Design

**Status:** Approved design; implementation intentionally not included

**Approval:** The user-approved design input in `C:\Users\chris\Desktop\approv lesen vorhertxt.txt` authorizes this design commit and plan, with the three refinements recorded below.

**Exact base:** `d61824b8436598ed461f83b3b73c291c5420c191`, the merge commit of PR #22.

**Scope:** Issue #16, Phase 4A only.

## Goal

Establish a fair, public-only policy substrate and a production `RandomLegal` baseline that can sample the complete current environment candidate domain uniformly, record exact policy RNG provenance, and run through the existing V2 trajectory and admission path.

Phase 4A does not implement `TeacherCore`, either StrategyProfile, model adapters, search, training, self-play, wrappers, arbitrary-deck support, or cloud infrastructure.

## Authority and research basis

The implementation follows this order of authority:

1. the pinned rules bundle and accepted runtime contracts on `main`;
2. accepted Phase-3A/3B trajectory and provenance invariants;
3. this Phase-4A design and its executable evidence;
4. the seven non-normative research documents merged by PR #22.

The research architecture is preserved as future design guidance:

```text
TeacherCore
├── Generic Tactical Layer
├── Strategic Planner
└── immutable StrategyProfile
    ├── SwordsoulTenyi
    └── Salamangreat
```

Phase 4A provides only the public substrate that this future architecture may consume.

## Design alternatives

### A — Separate public DTOs, one shared safe-state grammar, isolated policy module (recommended)

Move public candidate DTOs into a header that does not expose `DecisionFrame` or submission metadata. Refactor the existing safe-state parser into one typed codec authority. Add `ygo::policy` with a narrow selector interface, a policy-owned SHA-256 counter RNG, and a lifecycle runner that adapts accepted selections to V2 and `TrajectoryRecorder`. Add a production provenance factory in `ygo::trajectory`.

This gives the policy selector a small, deep interface and makes the header dependency itself enforce the privacy seam. It preserves all existing wire bytes and trajectory schemas.

### B — Keep all DTOs in `episodic_environment.hpp`

Expose the same selector signature but include the large episodic header transitively. This is a smaller textual diff, but private lifecycle types remain adjacent to policy-facing types and static dependency review becomes weaker.

### C — Make the policy receive a copied typed state plus copied candidates

Decode the safe state before every selection and pass a value-owned state object. This makes the selector easy to test but expands the policy interface beyond the accepted public observation and risks creating a second state boundary rather than a view of the existing bytes.

Option A is selected because it provides the cleanest seam while preserving the accepted V2 contract exactly.

## Module boundaries

### `ygo::environment`

Add a read-only `PublicSafeStateView` representing exactly the fields encoded by `ocgforge.public_safe_state.v1`:

- globals;
- zones;
- visible/redacted entities and public card properties;
- relationships;
- chain links, sources, targets, and effect descriptions;
- public visible events without `engine_step_index`;
- match knowledge and known static deck lists.

The view will expose const accessors and will not expose or construct a `PlayerObservation`. Its only construction path is the strict safe-state codec. The existing serializer and the decoder will share the same field model and grammar. A typed decode followed by the typed canonical encoder must reproduce the input bytes exactly.

The outer `PublicEnvironmentObservation` codec will call the same typed decoder when validating nested safe-state bytes. No canonical field, ordering rule, digest input, or visibility rule changes.

### Public candidate DTO seam

Create a public-only Environment header containing the existing public decision enums, `EnvironmentActionCandidate`, `EnvironmentContinuationView`, and `EnvironmentDecisionRequest`. `episodic_environment.hpp` includes this header and retains the existing names and meanings.

`ygo::policy` includes the public DTO header and `public_environment_observation.hpp`; it does not include `episodic_environment.hpp`. The policy selector interface contains no `DecisionFrame`, `SubmissionToken`, `engine_step_index`, internal semantic key, raw response, `CoreHost`, or private observation.

### `ygo::policy`

The selector seam is:

```cpp
struct PolicyInput {
    const environment::PublicEnvironmentObservation& observation;
    const std::vector<environment::EnvironmentActionCandidate>& candidates;
};
```

The selector returns:

```cpp
struct PolicySelectionResult {
    std::string public_action_key;
    PolicyRngCursorSpan rng;
};
```

`PolicyRngCursorSpan` carries only policy-owned stream identification and `pre_cursor`/`post_cursor`. It does not carry the sampled vector position. The selector never receives a `DecisionFrame`.

`RandomLegalPolicy` validates its construction configuration, rejects an empty or malformed public domain, samples the entire const vector, and returns exactly the selected candidate's existing `public_action_key`. It never filters, sorts, deduplicates, truncates, repairs, retries, or substitutes a first candidate.

The policy runner is a separate implementation behind the selector seam. It may receive a V2 `DecisionFrame` to copy the already-published public observation and candidate vector, construct a control-plane `ActionSelection`, and submit the selected public key. This lifecycle code is not selection logic.

### `ygo::trajectory`

The existing Phase-3A/3B types and codecs remain authoritative and unchanged. Add only an explicit production provenance resolver factory. The runner converts `PolicyRngCursorSpan` into the existing `PolicyRngDecisionProvenance` by adding the current decision index and acting participant assignment outside the selector.

## Production provenance registrations

The default `ProvenanceResolver` remains unchanged and continues to contain only `ocgforge.no_policy_rng.v1`. A separate production factory registers each identity under its correct category:

```text
ProducerImplementation → ocgforge.policy.random_legal.v1
InferenceAdapter       → ocgforge.policy.direct_execution.v1
ObservationAdapter     → ocgforge.policy.public_observation.v1
ActionAdapter          → ocgforge.policy.public_action_key.v1
SamplingContract       → ocgforge.policy.uniform_below_u64.v1
PolicyRngContract      → ocgforge.policy_rng.sha256_counter.v1
```

The sampling registration declares `complete=true` and `deterministic=false`. The `RandomLegal` artifact declares the SHA-256 counter contract, has no model/search/demonstration identity, and is rejected when paired with `ocgforge.no_policy_rng.v1` or deterministic sampling. No production registration uses an `ocgforge.test.*` identity.

## `ocgforge.policy_rng.sha256_counter.v1`

This is a policy-owned RNG contract, separate from engine seed derivation and public gameplay identity.

### Initialization material

The canonical initialization material contains, in order:

```text
string RNG contract identity
string RNG contract identity as schema/domain separator
u64be explicit policy RNG root seed
string episode semantic ID
string participant policy assignment ID
string policy RNG stream ID
```

The stream ID is a canonical policy stream token. The episode ID, participant assignment ID, and stream ID are explicit semantic inputs. The policy RNG root seed is supplied separately and is never implicitly copied from `EpisodeSpec::root_seed`. No host, process, PID, thread, provider, wall-clock, scheduling, pointer, or hidden-state value is permitted.

The existing trajectory `PolicyRngInitializationIdentity` stores the contract identity, stream ID, canonical initialization material, and its content identity. The existing `PolicyRngStreamIdentity` then binds that initialization to the immutable policy artifact and participant assignment.

### Raw words

For cursor `c`:

```text
block_index = c / 4
lane        = c % 4
```

The canonical block bytes are:

```text
string RNG block domain `ocgforge.policy_rng.sha256_counter.block.v1`
string policy_rng_initialization_identity
u64be block_index
```

The SHA-256 digest is interpreted as four consecutive big-endian `u64` words. The selected lane is one consumed raw word. Cursor zero is the first word. A cursor at exhaustion fails closed; no increment may wrap.

### Bounded sampling

`uniform_below_u64(n)` uses unsigned rejection sampling:

```text
n == 0 → structured failure
n == 1 → result 0, cursor unchanged
n > 1  → threshold = (-n) % n
         consume raw words until raw >= threshold
         result = raw % n
```

Every rejected raw word advances the cursor exactly once. The candidate vector position exists only inside the selector call and is never returned, persisted, hashed, or used as action identity.

## Lifecycle and trajectory flow

The production runner uses the existing path:

```text
EpisodicEnvironment V2
    ↓
policy selector receives public observation + const candidate vector
    ↓
exact selected public_action_key
    ↓
V2 ActionSelection using the current control-plane token
    ↓
TrajectoryRecorder
    ↓
CandidateTrajectoryShard + restricted RNG initialization evidence
    ↓
semantic V2 replay admission
    ↓
AdmissionReceipt
    ↓
DatasetManifest
```

An accepted selection produces a `CURSOR` provenance record with the exact decision index, acting assignment, stream identity, initialization identity, and pre/post cursor. The runner adds those collection fields after selection.

A policy-origin `StepRejected` is not recoverable. The runner records no `DecisionRecord` for the rejected submission, marks the recorder disposition as quarantined, terminates collection without retrying or choosing another candidate, and does not admit the episode as clean trusted data.

An RNG or policy-state failure returns a structured policy failure and never submits a guessed action. The runner does not rewind RNG state to continue a trusted episode.

## P4A-G00 public-fact audit

The Phase-4 public-fact matrix will enumerate each requirement in the seven research documents and map it to the exact public source and executable evidence. The audit will distinguish direct availability, safe derivation, and `BLOCKED`.

Directly represented facts include public life points, turn/phase globals, zone counts, visible or legitimately known card properties, public chain source/targets/effect descriptions, supported visible events, match-knowledge flags, known deck lists, and all safe candidate descriptor fields.

The following future Teacher capabilities remain blocked when their facts are not present in the accepted public boundary:

- hidden opponent hand or deck order;
- hidden top-deck identity for Foxy;
- hidden physical identity across randomization boundaries;
- complete once-per-turn/effect-use or temporary-restriction facts not reconstructible from public observation and accepted public history;
- omitted/deferred event families;
- candidate semantics that are not represented by safe public metadata.

The matrix will not weaken these research requirements and will not introduce a private side channel.

## Evidence strategy

Phase 4A tests will prove:

- strict typed safe-state decode and all malformed/truncated/trailing/noncanonical negatives;
- byte-identical safe-state round-trip and unchanged public observation digest;
- policy header/dependency isolation;
- exact candidate count/order/domain-digest preservation;
- empty-domain fail-closed behavior and no first-candidate fallback;
- initialization and SHA-256-counter golden vectors, cursor lanes, stream separation, and independent-process reproduction;
- unbiased rejection sampling and exact cursor traces;
- production typed provenance registration and RandomLegal compatibility rejection;
- accepted public key membership and policy-origin rejection quarantine;
- policy reset/isolation and paired-world privacy;
- trusted recorder, shard, restricted evidence, replay admission, receipt, and dataset integration.

The existing Phase-3A/3B canonical trajectory codecs, rules inputs, deck identities, and gameplay semantics are not rewritten. Generated acceptance evidence is produced by its generator and is never hand-edited.

## Explicit stop conditions

Implementation stops and reports a blocker if typed decode changes canonical bytes, the policy sees a different candidate domain, a required public fact requires private state, paired worlds diverge, the RNG depends on process or scheduling state, semantic replay diverges, or the existing Phase-3A/3B schema proves insufficient.
