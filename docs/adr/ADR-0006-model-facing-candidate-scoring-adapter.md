# ADR-0006: Freeze the framework-neutral model-facing candidate-scoring boundary

## Status

Accepted — Phase 5 Task 1 contract freeze only. This ADR does not claim that a
model adapter, scorer, training system, or any P5 executable gate exists or
passes.

## Context

OCGForge has an accepted public episodic boundary above the rules engine:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
```

The observation is a perspective-safe projection. The candidate vector is the
complete public legal domain for the current request, in the order supplied by
the existing Decision Protocol and public environment. `public_action_key` is
the safe selection/routing identity for one current candidate. Internal
`semantic_key`, response bytes, continuation IDs, and engine state remain
outside that boundary.

The existing authoritative contracts deliberately use variable-length semantic
data. Phase 5 needs a replaceable model-facing representation without turning
one framework's tensor conventions into gameplay semantics, introducing a
global action cap, or making a downstream batch layout part of replay or model
identity.

The Phase 3 trusted trajectory is already accepted as the immutable source of
public frames and selected public keys. It explicitly keeps tensors, candidate
indexes, embeddings, padding, and storage layouts derived. Phase 5 must
consume that contract without changing `ocgforge.trusted_trajectory.v1`.

## Decision

### 1. Owning layer and namespace

Phase 5 is owned by the framework-neutral `ygo::model` layer. It is a
downstream consumer of the public environment and trusted trajectory values:

```text
ocgcore / CoreHost
    -> Decision Protocol
    -> PlayerObservation
    -> PublicEnvironmentObservation + complete public candidates
    -> ygo::model
    -> later scorer or training adapter
```

`ygo::model` does not own gameplay legality, public projection, internal
response construction, trajectory admission, or engine advancement. It may
produce representations and routing metadata only; it never submits an action
or auto-resolves a decision.

### 2. Three explicit representation layers

The Phase 5 representation is split into three layers with separate owners
and compatibility lifecycles:

1. `LogicalModelInputV1` preserves the structured public meaning using exact
   integers, optional values, public categorical tokens, safe references, and
   the complete ordered candidate vector.
2. `EncodedModelInputV1` replaces the logical categorical/card values with
   deterministic integer codes and vocabulary IDs. It retains a parallel
   routing sidecar for the exact public action keys; those keys are not learned
   string features.
3. `ModelBatchLayoutV1` is a lossless physical view with ragged offsets or
   padded rows and masks. It is an execution representation, not a semantic
   input contract.

The normative details and exact contract IDs are in
`docs/p5/P5_MODEL_CONTRACT.md`.

### 3. Exact candidate preservation

For every accepted public request with `N` candidates:

```text
N public candidates
    -> N logical candidates
    -> N encoded candidate rows
    -> N routable model candidate slots
```

The transformation never filters, sorts, deduplicates, truncates, fabricates,
defaults, or auto-resolves a candidate. Candidate order is copied exactly.
The candidate vector position may be exposed as a local derived coordinate for
labels or physical rows, but it is never a replay identity or a substitute for
`public_action_key`.

### 4. Public-only state and candidate inputs

The model layer accepts exactly the already-public observation and its complete
ordered public candidate vector. Request kind/player values may be copied only
from the observation's already-public decision context or used as an equality
check against that context when available. `EnvironmentContinuationView` and
any other request-only state are not independent Phase 5 model inputs. A
`PublicSafeState` is decoded only by the existing
`decode_canonical_public_safe_state` path. The model layer never accepts or
constructs a `PlayerObservation`, queries `CoreHost`, reads an internal
`semantic_key`, consumes response bytes or a `SubmissionToken`, or accesses a
private/physical locator.

An already-public card passcode may be mapped through the owned vocabulary.
Database/catalog lookup is forbidden from filling an unknown or redacted card,
revealing a hidden deck entry, or reconstructing any omitted property.

Public locator references use a frame-local deterministic ordinal over exact
public locator tokens. The ordinal means token equality only. Candidate, chain,
and relationship references may additionally carry a proven current-entity
ordinal; accumulated visible-event references never do, even when an old token
matches the locator of a current entity in the same slot.

### 5. Vocabulary ownership and versioning

`ygo::model` owns the semantic mapping from an explicit immutable list of
public passcodes to dense vocabulary IDs. The mapping is identified by
`ocgforge.model_card_vocabulary.v1` and its canonical passcode-list digest.
Reserved IDs are fixed:

```text
0 = PAD                         (physical padding only)
1 = PUBLIC_UNKNOWN_OR_REDACTED (a real public row with no known identity)
2.. = known public passcodes in strictly ascending vocabulary order
```

Changing the reserved meanings, passcode list, ordering rule, or unknown-card
semantics requires a new vocabulary version and a new model-input identity.
The vocabulary does not become a global gameplay card identity and does not
assign IDs to hidden cards.

### 6. Model-input identity excludes physical layout

The model-input identity binds the canonical logical representation, the
canonical deterministic encoded representation, and the exact vocabulary
identity. It also binds the exact ordered routing-key sidecar because a model
output must remain resolvable to the current public candidate domain.

`ModelBatchLayoutV1` is explicitly excluded from that identity. In particular,
the following do not change a model-input identity:

- padding values or padding width;
- bucket capacity;
- batch composition or sample order;
- ragged offsets and physical masks;
- tensor shape, storage dtype, device, backend, or framework;
- physical concatenation or allocation order.

Those values may have a separate layout/debug identity, but that identity is
not a model-input identity, public action identity, trajectory identity,
environment identity, or replay key.

### 7. Trusted trajectory and supervision

`ocgforge.trusted_trajectory.v1` remains unchanged. A later Phase 5
materializer may derive `ModelSupervisionSampleV1` from one accepted
`DecisionRecord` by locating its exact selected `public_action_key` in the
complete ordered frame domain and recording the corresponding zero-based
candidate ordinal as a training label. The ordinal is derived and local. It
is never used as replay identity, public action identity, or a replacement for
the selected public key.

### 8. Failure behavior

Any malformed contract, decoder failure, unknown vocabulary value, unsafe
reference, missing/duplicate public key, count/order mismatch, integer
overflow, insufficient bucket capacity, or unproven representation rejects
the complete model input or batch operation. The layer fails closed; it does
not repair the input by reducing the domain or choosing a candidate.

## Alternatives considered

### Make a framework tensor API authoritative

Rejected. PyTorch, JAX, NumPy, RL frameworks, tensor dtypes, device choices,
and network-specific shapes are replaceable implementation choices. Making one
of them normative would couple public semantics to a downstream consumer and
make equivalent adapters disagree.

### Use a fixed global candidate width

Rejected. The public legal domain is variable and complete. A width cap would
turn a legal domain into a heuristic subset and would fail at the exact G28
maximum-domain witness. Ragged storage and lossless padding are the correct
physical representations.

### Feed `public_action_key` as a learned string feature

Rejected. The key is a selection/routing identity, not semantic card or action
content. It remains in an exact sidecar for output resolution and identity
binding, while candidate feature payloads contain only the separately encoded
public descriptors.

### Re-enter `PlayerObservation`, `CoreHost`, or internal protocol values

Rejected. That would bypass the public privacy boundary and could expose hidden
identity, response material, stale-action control data, or engine metadata.
The model layer must consume the existing public-safe decoder and public V2
candidate values only.

### Include padding and bucket layout in model-input identity

Rejected. The same logical/encoded decision must have one identity regardless
of how it is batched. Layout is an execution detail and can change without
changing semantic model input.

### Change the trusted trajectory schema to store model rows

Rejected. Tensors, padding, candidate ordinals, and training labels are
derived artifacts. Changing the trusted trajectory would conflate collection
truth with one future learner representation and would violate its accepted
compatibility boundary.

## Consequences

- Phase 5 has a stable owner and a framework-neutral semantic seam.
- Every complete public candidate remains available to a later scorer and can
  be resolved through its exact current `public_action_key`.
- The same public decision can be encoded into different ragged/padded batches
  without changing its model-input identity.
- Vocabulary updates, logical-field changes, and encoded-code changes are
  explicit version migrations rather than silent reinterpretations.
- A model adapter cannot hide incompleteness behind a smaller tensor or a
  database-derived hidden-card guess.
- No neural network, loss, optimizer, float normalization, RL, self-play,
  checkpoint training, or framework dependency is introduced by this freeze.

## Compatibility / migration

This ADR adds a downstream model-facing contract. It does not change:

- `PublicEnvironmentObservation` or `ocgforge.public_safe_state.v1`;
- `EnvironmentActionCandidate`, public candidate ordering, or
  `public_action_key` semantics;
- `PlayerObservation`, `CoreHost`, the Decision Protocol, or EngineTrace;
- `ocgforge.trusted_trajectory.v1` or any trajectory identity;
- rules, decks, Teacher behavior, CMake, or dependencies.

An incompatible change to logical fields, encoded category codes, vocabulary
reserved IDs, canonical identity inputs, or routing metadata requires a new
versioned contract/identity. A physical layout change may version
`ocgforge.model_batch_layout.v1` independently and must not invalidate the
semantic model-input identity solely because padding or batching changed.

## Verification

The later executable evidence is specified in
`docs/p5/P5_ACCEPTANCE_PLAN.md`. It must prove the public-only boundary,
decoder ownership, exact N-to-N/order/key preservation, deterministic
vocabulary and canonical bytes, paired-hidden-world equality, lossless ragged
and padded views, boundary domains `N=24`, `N=25`, and `N=129`, the real G28
maximum-domain witness, derived trajectory labels, framework neutrality, and
Phase-3/4 regression from a clean checkout.

This Phase 5 Task 1 commit is a documentation-only freeze. It does not report
those P5 gates as passed.
