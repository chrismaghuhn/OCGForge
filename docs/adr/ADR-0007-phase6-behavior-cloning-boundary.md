# ADR-0007: Freeze the Phase-6 Behavior Cloning boundary

## Status

Accepted — Phase 6 Task 1 contract freeze. This ADR records the architectural
boundary and its rationale. It does not claim that a learner, training run,
checkpoint, or Phase-6 acceptance gate exists.

## Context

OCGForge has accepted deterministic, perspective-safe, and replayable layers
through Phase 5. The accepted model-facing path is:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
    → LogicalModelInputV1
    → EncodedModelInputV1
    → ModelBatchLayoutV1
```

The accepted trajectory/admission path separately supplies trusted
membership:

```text
accepted trusted trajectory
+ VerifiedAdmissionReceipt
    → ModelSupervisionSampleV1
```

Phase 6 is the first learned-policy milestone. It therefore needs long-lived
boundaries for dataset trust, episode-level partitioning, candidate scoring,
training provenance, checkpoint identity, inference freshness, deterministic
selection, and evaluation. Those boundaries must not turn a learner into a
second legality engine or make one ML framework part of OCGForge semantics.

The first curriculum is deliberately limited to the accepted fixed
Swordsoul Tenyi versus Salamangreat matchup and accepted Teacher v1
provenance. The accepted positive-lethal capability remains
`BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT`.

## Decision

### 1. OCGForge owns semantics; a framework owns execution

The Phase-6 semantic owner is OCGForge. A future PyTorch, JAX, or other
implementation may own tensors, devices, compilation, distribution, optimizer
state, and physical serialization, but those details are never authoritative
for legality, public action identity, dataset membership, sample identity,
checkpoint identity, or evaluation acceptance.

The accepted Phase-5 references are bound as historical inputs:

```text
H_EXEC     = 3c99e86c487361fc4e0f5f12678b4867e59232b7
H_EVIDENCE = da3376fc2ab645377f9de2dd9fd6195c1aa8c081
```

An incompatible Phase-5 input, vocabulary, candidate-order, or public-key
meaning requires a lower-layer versioned migration before Phase 6 can consume
it. Task 1 does not reinterpret Phase 5.

### 2. The learner is a variable-domain candidate scorer

The minimum learner shape is:

```text
EncodedModelInputV1
    → state_encoder
    → candidate_encoder(state, candidate row[i])
    → candidate_scoring_function
    → exactly N finite scores
    → one supplied public_action_key
```

The Environment remains the sole authority for the complete legal candidate
domain, candidate order, public-action-key validation, continuation lifecycle,
response construction, and engine advancement. The learner may use shared
state context, but it must score each supplied candidate and cannot replace the
domain with a fixed global action vocabulary, family-specific action head,
truncated top-K set, reconstructed candidate, or fabricated default.

At every continuation step the Environment publishes the complete current
domain and the learner scores that domain as a new `N`-candidate request.
Candidate ordinal is a local label coordinate only; `public_action_key` stays
the routing/selection identity.

The initial objective is one Teacher-selected candidate among the exact `N`
supplied candidates. Physical padding has a row mask of zero and contributes no
semantic loss. A physical width smaller than `N` fails closed. The Phase-5
capacity witnesses `N=24`, `N=25`, and `N=129` remain mandatory future tests.

### 3. Dataset membership is admitted, then partitioned by episode

The only trusted BC membership is an immutable accepted `DatasetManifest`
whose members resolve to verified whole-shard admission receipts and clean
trusted trajectory records. JSONL, NumPy, Arrow, Parquet, pickle, mmap,
tensor caches, and framework dataset libraries may be derived projections but
cannot grant membership by being loadable.

The initial positive-label corpus accepts exactly the two existing Teacher v1
PolicyArtifact/profile/binding identities for the fixed matchup. RandomLegal
trajectories are not Teacher labels. Teacher selection is a demonstration,
not proof of strategic optimality; an unselected candidate is not thereby a
negative or illegal action.

The split boundary is established before row assignment. The canonical unit is
the exact V2 `episode_semantic_id`/duel identity. A deterministic frozen
80/10/10 SHA-256 partition assigns every perspective, decision, continuation,
shard, encoded representation, and physical cache row from that episode to
one and only one partition. No decision-record or sample-row random split is
valid.

### 4. Provenance and checkpoint identity are separate layers

Every future `TrainingRunManifestV1` records source dataset/split, Phase-5
contracts and vocabulary, architecture/config, behavior and opponent policy
sources, immutable code commit, backend/version, optimizer/schedule, batch and
RNG configuration, precision, device/distributed provenance, initial checkpoint,
and final exported checkpoint.

Training-run identity, semantic checkpoint identity, framework-native training
state, and execution-environment identity remain distinct. Native optimizer,
sharding, gradient, and worker state is not canonical inference state.

The canonical export boundary is:

```text
framework-native state
    → versioned canonical inference weight representation
    → exact weight-content identity
    → OCGForge checkpoint manifest/identity
```

The checkpoint manifest binds schema, architecture/config, Phase-5 input
contracts, CardVocabulary, dataset/split, BC training contract, optional parent,
and canonical weight-content identity. Hardware and framework provenance may be
attached without automatically changing semantic checkpoint identity. Mutable
aliases such as `latest`, `best`, `prod`, or `run-17` are locators only.

### 5. Inference is bound to the current public decision

An inference request binds the immutable checkpoint identity, Phase-5
`model_input_identity`, exact ordered candidate-domain identity, and current
public decision identity where available. The response must echo those
bindings, return exactly `N` finite scores in source order, and name the exact
supplied selected public key.

Stale, duplicated, late, wrong-domain, wrong-input, wrong-checkpoint,
wrong-length, non-finite, or invalid responses are rejected. No response may
be applied to another frame. Equal finite scores use the explicit
bytewise-ascending existing `public_action_key` tie rule; backend-dependent
unordered behavior cannot choose a candidate.

Timeout, crash, transport failure, or any other neural-policy failure never
invokes Teacher, RandomLegal, a heuristic, candidate zero, first candidate, or
a retry under another policy. The affected run is failed/quarantined under the
future runner contract and cannot count as a neural-policy win.

### 6. Evaluation is a hierarchy, not one accuracy number

Phase 6 freezes three separate evidence layers:

1. offline imitation metrics over admitted Teacher states, sliced by request
   family, domain size, phase, deck/participant role, starting player,
   continuation status, and rare/critical decisions;
2. frozen gameplay through normal `PolicySelection` → EpisodicEnvironment V2
   → trajectory recorder → replay/admission, using fixed jobs/seeds/opponents
   and reporting uncertainty plus failure/quarantine separately; and
3. first-divergence evidence between Teacher and BC on deterministic shared
   initial jobs, containing only public-safe observation/model-input identity,
   complete candidate keys, scores, selected keys, and decision context.

Validation on Teacher states is explicitly insufficient. The evaluator also
compares the distribution and performance of states induced by the BC policy's
own earlier decisions. Offline Teacher agreement is not online parity or proof
of optimal gameplay.

### 7. Privacy and inspection are gates

The model-input inspector and all normal learner/evaluation diagnostics consume
only accepted public fields. They must not query CoreHost or private
PlayerObservation state. Paired hidden worlds with equal public observation and
complete public domain must produce equal logical/encoded model input, model
input identity, deterministic scores under the declared numeric contract, and
selected public action key. No hidden passcode, private locator, internal key,
or hidden-derived value may enter the path.

### 8. Backend bake-off is deferred

Task 1 selects neither PyTorch nor JAX. A later Backend Bake-Off compares both
only after a real BC workload exists, using identical data/split, Phase-5
inputs, architecture, objective/budget, evaluation corpus, and canonical
export semantics. The primary backend decision belongs before Phase 7 and in a
separate accepted decision record.

Accelerate, Safetensors, Hugging Face Datasets, Trackio, and `huggingface_hub`
remain optional future tooling roles. None becomes authority for dataset
membership, checkpoint identity, evaluation acceptance, or gameplay semantics.

## Alternatives considered

### Make a fixed global action vocabulary the learner authority

Rejected. The accepted public environment exposes a variable complete domain.
A fixed vocabulary would make a downstream implementation responsible for
legality and would fail at large or continuation-specific domains.

### Split individual decision records or sample rows randomly

Rejected. The same duel would leak across partitions through both perspectives,
continuations, or derived rows. Episode/duel identity is the required split
boundary.

### Treat any loadable file or framework dataset as trusted membership

Rejected. Physical formats are rebuildable projections and can silently drop,
reorder, redact, or relabel data. Admission receipts and DatasetManifest own
membership.

### Use separate summon, target, chain, or option heads as action authorities

Rejected. Those heads could make legal candidates unreachable or create a
second legality boundary. One generic scorer handles every current domain.

### Choose PyTorch or JAX before a real workload

Rejected. The choice needs evidence about exact-domain ergonomics, correctness,
throughput, multi-device behavior, export, and debugging under the same
contracts. Task 1 freezes the comparison, not its winner.

### Treat a framework-native checkpoint as canonical

Rejected. Optimizer, sharding, device, and worker state are training
execution artifacts. Only the explicit canonical export and OCGForge manifest
can establish an immutable inference checkpoint identity.

### Continue with Teacher/RandomLegal after neural failure

Rejected. A fallback-assisted result cannot be attributed to the neural
policy, hides reliability defects, and violates the fail-closed environment
boundary.

## Consequences

- Future BC implementations can be swapped without changing public legality,
  observation privacy, trajectory identity, or candidate routing.
- Rebuilt caches and different batch layouts can preserve the same semantic
  dataset/sample/model-input/checkpoint identities when their canonical inputs
  are equal.
- Large candidate domains remain representable and testable without a hidden
  action cap.
- Training, inference, and evaluation failures are explicit data rather than
  plausible fallback gameplay.
- Teacher demonstrations are useful behavior labels without being mistaken for
  optimality or negative strategic supervision.
- Phase 6 remains limited to the fixed matchup until separately admitted
  evidence expands the scope.
- Task 1 creates no neural code, dependency, training run, checkpoint, or
  acceptance evidence.

## Compatibility and migration

This ADR does not change:

```text
PublicEnvironmentObservation
EnvironmentActionCandidate or public_action_key
EpisodicEnvironment V2
trusted trajectory/admission/receipt contracts
Phase-5 model input or CardVocabulary contracts
rules bundle, fixed deck identities, Teacher v1 identities, or lethal semantics
```

An incompatible change to dataset membership authority, split grouping/hash,
label meaning, scorer cardinality, numerical/tie semantics, checkpoint
identity inputs, inference bindings, or evaluation metrics requires a new
versioned Phase-6 contract and migration evidence. A physical framework or
serialization change alone may remain compatible only when it reproduces the
same canonical semantic values and validates through the unchanged OCGForge
boundary.

Project Ignis / EDOPro Bot Adapter V1 remains deferred until:

```text
first useful trained checkpoint
+ canonical checkpoint identity
+ frozen inference contract
+ successful frozen evaluation
```

Human games from that future adapter are evaluation/debug evidence first; a
separate admission contract is required before they can become trusted
training data.

## Verification

Task 1 verification is documentation/reference-only. It checks the requested
rules/deck identity and Teacher profile binding scripts, the repository Python
suite, Markdown references when a reusable repository checker exists, and
`git diff --check`. It does not run a build, CTest, native MSVC, heavy replay,
neural training, GPU workload, checkpoint generation, or future P6 gates.

The future implementation/evaluation gates are frozen in
[P6_EVALUATION_PLAN.md](../p6/P6_EVALUATION_PLAN.md#8-frozen-future-acceptance-matrix)
and the independently reviewable sequence is frozen in
[P6_IMPLEMENTATION_PLAN.md](../p6/P6_IMPLEMENTATION_PLAN.md).
