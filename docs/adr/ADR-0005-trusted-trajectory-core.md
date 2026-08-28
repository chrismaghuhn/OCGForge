# ADR-0005: Define a trusted trajectory core above EpisodicEnvironment V2

## Status

Accepted. This ADR is the Phase-3A architectural authority. It does not
authorize Teacher, model adapters, or ML work; Phase 3B persistence and
admission are separately versioned and evidenced by their own contracts.

## Context

`EpisodicEnvironment V2` now provides the first accepted public policy
boundary: a perspective-safe `PublicEnvironmentObservation`, one complete
ordered public candidate domain, and one selected `public_action_key` per
accepted step. The lower `EpisodeDriver` retains CoreHost ownership, internal
semantic keys, continuations, exact response bytes, and EngineTrace v2.

The next layer must preserve those distinctions when it records behavior. A
generic `(s, a, r, s')` row, a fixed global action vocabulary, a trace export,
or a framework-owned episode would either duplicate V2 semantics or invite
hidden-information and identity leaks.

PR #17 records research provenance for this decision. Its names and schemas
are not authority. The accepted V2 contracts, ADR-0002 through ADR-0004, and
the implementation at the merged Phase-2 checkpoint take precedence.

The priority order remains:

```text
correctness
-> determinism
-> information safety
-> complete legal decision representation
-> replayability / auditability
-> maintainability
-> performance
-> ML scale
```

## Decision

### 1. Owning layer

Future `ygo::trajectory` owns pure, immutable trusted-trajectory values above
`EpisodicEnvironment V2`:

```text
ocgcore
  -> Decision Protocol
  -> EpisodeDriver
  -> EpisodicEnvironment V2
  -> ygo::trajectory
  -> Phase 3B persistence and admission
  -> Phase 4 evaluation / Teacher
  -> Phase 5 model adapter
  -> later ML work
```

The layer may consume only immutable V2 public values, reset inputs, and
closure values. It must not query CoreHost, inspect raw engine state, access
an internal `ActionCandidate.semantic_key`, read raw response bytes,
reconstruct a candidate domain, alter candidate order, advance the engine,
mutate continuation state, consume an opponent-private observation, or turn a
restricted diagnostic into learner data.

### 2. Canonical logical episode

The canonical logical shape is fixed:

```text
EpisodeEnvelope
├── EpisodeManifest
│   └── PolicyProvenanceEnvelope
├── DecisionRecord[]
└── EpisodeClosure
```

One `DecisionRecord` represents one accepted public semantic action at one
actionable V2 frame. It owns that frame, the selected public action, accepted
transition classification, successor reference, and policy-decision
provenance. It is not an algorithm-specific `(s, a, r, s')` row.

`EpisodeClosure` owns terminal views and an optional unacted interruption
boundary. Neither value is a fabricated final action.

### 3. Identity separation

The trajectory core keeps these meanings distinct:

```text
environment_semantic_id
episode_semantic_id
public_semantic_decision_id
public_gameplay_trajectory_id
trajectory_record_id
```

The existing environment, episode, and public-decision IDs retain their
accepted owners. The new public gameplay ID binds global public gameplay
values, including both participants' accepted frames and terminal views. The
new record ID additionally binds exact policy/collection provenance. Neither
global identity is a participant learner or policy feature. They do not bind
physical storage, compression, host, build, provider, wall clock, worker, PID,
path, or completion order.

### 4. Public gameplay versus collection provenance

The canonical public gameplay projection contains public V2 frames, complete
ordered public domains, selected public keys, accepted transition classes, and
closure. The raw `EpisodeEnvelope` and global gameplay identity are
collection/replay values, never direct learner or policy input. A participant
learner projection contains only its own public records and terminal view.

Policy artifact, participant assignment, and policy-RNG provenance are
canonical collection provenance. They identify the producer but never become
policy input, public-gameplay identity input, or a requirement to reproduce
floating-point inference for environment replay. A policy may retain
policy-local recurrent/strategic state only when derived from prior permitted
perspective-safe public inputs and policy-local state.

Derived rewards, tensors, candidate indexes, recurrent windows, replay
priorities, exports, and storage layouts are outside the core contract.

### 5. Failure and rejection

Rejected `step()` calls create no `DecisionRecord`. A collector that observes
a policy-generated rejection must mark the episode as quarantined under the
default trusted collection profile; a later retry does not erase that fact.
`TERMINAL`, `INTERRUPTED`, and `FAILED` remain separate closure kinds. Only a
true terminal has a winner and win reason. An `INTERRUPTED` admission replay
also requires exact restricted reason/run-control/count/engine-step evidence,
which Phase 3B must bind to the candidate before receipt issuance. Failed
episodes are never normal learner samples.

### 6. Framework neutrality

RLDS, Minari, RLlib, Reverb, TorchRL, PyTorch, Arrow, Parquet, Hugging Face,
Weights & Biases, and Kaggle do not own the canonical contract. Future code
may produce derived adapters or exports for them.

## Alternatives considered

### Use EngineTrace v2 as the trajectory

Rejected. EngineTrace v2 correctly retains internal semantic keys,
continuation IDs, response hashes, and audit data. Those values are not a
learner-visible public trajectory boundary.

### Store framework `(s, a, r, s')` rows

Rejected. That shape duplicates successor observations, forces reward timing
into environment truth, and cannot represent complete ragged legal domains
without framework-specific conventions.

### Use one ID for gameplay, policy provenance, and storage

Rejected. The same public gameplay can arise from different policy artifacts,
while the same logical record can be repacked into different shards. One hash
would conflate those facts.

### Record only one episode-global behavior policy

Rejected. Alternating self-play requires each accepted decision to resolve to
its acting participant assignment.

## Consequences

Phase 3B can add codecs, shards, replay admission, and immutable receipts
without changing the meaning of an episode or action. A recorder must fail
closed when it cannot prove a public frame is complete, safe, uniquely
selected, or correctly attributed.

The contract deliberately carries more explicit provenance than a simple
dataset row. It also prohibits that provenance from becoming a gameplay or
learner feature.

## Compatibility / migration

The decision introduces these proposed Phase-3A domains and fixed contract
identifier:

```text
ocgforge.trusted_trajectory.v1
ocgforge.policy_provenance.v1
ocgforge.policy_artifact_identity.v1
ocgforge.participant_policy_assignment_identity.v1
ocgforge.policy_rng_initialization_identity.v1
ocgforge.policy_rng_stream_identity.v1
ocgforge.policy_rng_decision_provenance.v1
ocgforge.no_policy_rng.v1
ocgforge.public_gameplay_trajectory_identity.v1
ocgforge.trajectory_record_identity.v1
ocgforge.restricted_replay_evidence.v1
```

`ocgforge.no_policy_rng.v1` is a fixed exact versioned contract identifier,
not a content-addressed artifact.

An incompatible field, visibility, ordering, codec, or digest-input change
requires a new versioned contract or identity domain. No reader may treat an
unknown version as v1, and no compatibility alias may change an old byte
meaning.

This ADR does not modify the rules bundle, Decision Protocol, V2 candidate
order, PlayerObservation v1, PublicEnvironmentObservation v1, public action
identity, EpisodicEnvironment V2, or EngineTrace v2.

## Verification

The normative wire/identity rules are in:

- `docs/contracts/trusted-trajectory-v1.md`;
- `docs/contracts/policy-provenance-v1.md`.

Phase-3A review gates and required future executable Phase-3B evidence are in:

- `docs/trajectory/PHASE3A_ACCEPTANCE.md`.

This ADR does not claim a trusted trajectory runtime, persistence format,
admission receipt, dataset, Teacher, or ML system exists.
