# OCGForge — Portable Actor/Learner Compute Strategy

Status: post-Phase-5 future architecture note; **not an accepted milestone or
implementation contract**. Phase 6 has not started.

This document preserves a post-Phase-5 compute strategy for using heterogeneous CPU resources — including ephemeral hosted notebook/batch CPUs when available — to generate trustworthy OCGForge trajectories while reserving GPU resources primarily for neural inference and learning.

Provider quotas, accelerator models, concurrent-session counts, pricing, and concrete hardware shapes are operational details. They must be discovered and recorded at execution time rather than encoded into OCGForge gameplay or trajectory semantics.

## Goal

OCGForge should eventually support a workflow where any trusted actor backend can generate interchangeable trajectory shards:

```text
local Windows actors ──────┐
generic Linux actors ──────┼──> validated trajectory shards ──> learner
ephemeral hosted actors ───┘                                 └─> GPU
```

The physical machine, operating system, CPU model, worker count, provider, process IDs, wall-clock scheduling, and completion order must not change authoritative gameplay semantics.

The target property is:

> Any trusted OCGForge actor that passes the relevant environment, portability, and data-trust gates may generate interchangeable training data for the same versioned workload, independent of where that actor executes.

## Provider/backend abstraction

Kaggle is one possible deployment provider, not the architectural abstraction.

A future deployment layer should conceptually look like:

```text
ActorBackend
├─ LocalProcess
├─ GenericLinuxBatch
├─ KaggleNotebook
├─ Colab
├─ CloudVM
└─ future providers
```

The backend owns deployment concerns such as install/build/bootstrap, provider session lifecycle, artifact upload/download, and per-host calibration.

The backend must **not** redefine:

- rules;
- deck identities;
- `PlayerObservation` semantics;
- `DecisionRequest` semantics;
- complete `ActionCandidate` domains;
- semantic action identity;
- reward semantics;
- replay semantics;
- gameplay hashes.

Use only normal provider-supported mechanisms and current quotas. Do not design around quota circumvention, multiple-account aggregation, hidden worker pools, artificial keep-alive behavior, or any other mechanism intended to bypass provider policy.

## Why this fits OCGForge

ocgcore simulation is primarily CPU work. Neural inference and optimization are separate workloads that benefit from GPUs.

A future training system should therefore be able to scale these concerns independently:

```text
CPU actor pool
    ↓
PlayerObservation + complete ActionCandidate domain
    ↓
policy inference
    ↓
semantic action
    ↓
transactional trajectory shards
    ↓
GPU learner
```

Hosted CPU capacity can increase simulation throughput without requiring every actor host to own a GPU.

## Non-goals

This note does **not** authorize:

- a provider-specific rules engine;
- a second environment implementation;
- provider-specific gameplay behavior;
- live distributed training before Phase-6 learner/data contracts exist;
- omitting `PlayerObservation` fields for hosted actors;
- candidate truncation to reduce traffic;
- hidden-state shortcuts;
- silent retry of failed in-flight games;
- dependence on a fixed number of hosted sessions;
- synchronous multi-host GPU training over unreliable public network links;
- selection of PPO, IMPALA, R2D2, BC, self-play, or any other ML algorithm.

## Preferred topology

The default future topology should be asynchronous actor/learner separation rather than treating several hosts as one synchronous machine.

```text
                    policy checkpoint N
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        actor backend A actor backend B local actors
              │            │            │
              └──── validated trajectory shards ────┐
                                                     ▼
                                                GPU learner
                                                     │
                                                     ▼
                                            policy checkpoint N+1
```

Actors should finish bounded immutable work units independently. A provider session ending must not corrupt another actor or require a duel to continue on another host.

This topology is compatible with policy-lag-tolerant or off-policy methods, but that compatibility is **not** an algorithm decision. The environment/data contracts should remain usable by bounded fresh-policy rollout schemes as well.

## Deterministic job identity and partitioning

Logical work assignment must be independent of hardware and worker count.

A future distributed rollout job should derive semantic identity from stable inputs such as:

```text
workload namespace
+ environment identity
+ matchup identity
+ rollout-generation identity
+ master seed
+ job index
```

Policy identities belong in rollout/trajectory provenance; whether they are also part of a higher-level workload identifier should be specified by the eventual trajectory contract rather than inferred from host placement.

Semantic job identity must not derive from:

```text
host name
provider
PID
wall-clock time
worker slot
completion order
```

Hosts may be assigned disjoint numeric job ranges for operational convenience, but the range assignment itself is not gameplay semantics.

Example:

```text
actor backend A: jobs       0.. 99,999
actor backend B: jobs 100,000..199,999
local backend:  jobs 200,000..299,999
```

If a session terminates early, only fully published and validated episodes/shards become eligible training data. Partial or unverifiable work fails closed.

## Trajectory provenance

A future shard must carry enough provenance to determine exactly which environment and which behavior produced each decision.

At minimum the base trajectory contract should eventually bind:

- `trajectory_schema_id`;
- `observation_schema_id`;
- `action_schema_id`;
- OCGForge source/contract identity;
- rules-bundle identity;
- locked deck or matchup identity;
- rollout-generation identity;
- semantic job identity and seed inputs;
- player perspective;
- `PlayerObservation` values;
- complete `DecisionRequest` / `ActionCandidate` domains;
- chosen semantic action keys;
- rewards and terminal results;
- `behavior_policy_id`;
- `opponent_policy_id` where applicable;
- behavior-policy role / seat / deck role;
- opponent-policy role / seat / deck role where applicable;
- policy RNG identity or policy-sampling provenance where stochastic action selection is used;
- league generation / population generation where applicable;
- shard content digest.

This is important for self-play because data from:

```text
v50 vs v48
v50 vs v45
v49 vs v50
v50 vs heuristic-teacher
```

must not be treated as one indistinguishable behavior distribution.

A learner must be able to identify **which policy actually generated each chosen action**.

### Algorithm-specific training metadata

The base trajectory contract should not hard-code one RL algorithm. Additional versioned training metadata may be required by a later algorithm, for example:

- behavior log-probability;
- behavior value estimate;
- recurrent sequence boundaries;
- recurrent burn-in boundaries;
- importance-sampling metadata;
- learner-policy version observed by an actor.

These should be explicit versioned extensions, not silently inferred fields and not requirements imposed on the authoritative environment before an algorithm needs them.

Teacher quality and environment legality remain separate concepts: a legal but strategically poor action is valid behavior data only under the policy/teacher identity that produced it; it must never be reclassified as engine truth.

## Transactional shard publication

A shard should be immutable **after publication**, but immutability alone is insufficient. Publication must be transactional and fail closed.

Conceptual lifecycle:

```text
WRITING
  ↓
VALIDATING
  ↓
VALIDATED
  ↓
PUBLISHED
```

Any validation failure, interrupted write, incomplete manifest, content-hash mismatch, schema mismatch, duplicate semantic job, or provenance mismatch must prevent publication.

A concrete file layout is an implementation detail, but an implementation could use a pattern such as:

```text
shard-000042.data.tmp
shard-000042.manifest.tmp
        ↓
      validate
        ↓
shard-000042.data
shard-000042.manifest
shard-000042.complete
```

The `.complete` spelling is not a contract. The required semantic property is an unambiguous atomic publication state.

A published shard manifest should bind at least:

- schema identities;
- environment/rules/matchup identity;
- job range or explicit semantic job identities;
- episode count;
- decision count;
- behavior/opponent policy provenance;
- shard content digest;
- publication status/version.

## Dataset merge and deduplication

Dataset construction is a validation boundary, not a blind concatenation step.

Conceptually:

```text
published shards
    ↓
verify schema/provenance/content digests
    ↓
canonicalize/sort by SemanticJobIdentity
    ↓
reject duplicate SemanticJobIdentity
    ↓
reject conflicting payloads for same identity
    ↓
produce immutable dataset manifest
```

Duplicate jobs must not be accepted merely because they came from different hosts or providers.

If the same semantic job appears with conflicting authoritative environment history, that is a correctness incident and must fail closed rather than choosing one copy by arrival order.

## Environment reproducibility contract

Environment reproducibility is an OCGForge correctness property and is distinct from full ML-run reproducibility.

The intended environment contract is conceptually:

```text
same EnvironmentIdentity
+ same ordered semantic action sequence
        ↓
same authoritative environment semantics
```

The deterministic corpus should compare, where the relevant contracts define portability:

- terminal result;
- winner and win reason;
- gameplay semantic hash;
- `DecisionRequest` sequence;
- complete `ActionCandidate` semantic domains;
- `PlayerObservation` hashes;
- zero unsupported required decisions;
- zero automatic decisions;
- zero candidate truncation;
- zero privacy failures.

The neural policy must **not** be part of the environment-equivalence proof. Replaying a fixed ordered semantic action sequence isolates the environment from policy sampling and floating-point implementation details.

Build/provenance hashes may legitimately differ when they intentionally include compiler or platform identity. Do not confuse them with semantic gameplay identity.

## ML-run reproducibility contract

A future ML run has additional sources of variation that are outside authoritative environment semantics, including:

- policy RNG;
- floating-point implementation;
- CPU/GPU kernels;
- CUDA/library versions;
- batch composition;
- asynchronous actor timing;
- optimizer behavior;
- recurrent-state handling;
- distributed learner scheduling.

Therefore:

```text
Environment reproducibility
≠
bit-identical ML training
```

ML-run reproducibility must receive its own versioned requirements later. It may require strong provenance and statistically reproducible evaluation without promising bit-identical GPU training unless that is explicitly proven and worth the cost.

A difference in sampled policy action caused by legitimate neural floating-point/RNG differences is not automatically an OCGForge environment determinism defect when the underlying observation and complete candidate domain are equivalent.

## Cross-platform portability gate

Hosted Linux actors must not be considered interchangeable with the canonical supported environment merely because they compile.

Before Linux/hosted actors become `DATA_TRUSTED`, establish a deterministic cross-platform corpus using identical:

- pinned rules inputs;
- ordered ocgcore patchset;
- locked decks;
- environment identity;
- seeds/jobs;
- ordered semantic action sequence.

The comparison uses the **environment reproducibility contract** above, not neural policy output.

This portability gate is a prerequisite for trusted cross-platform actors. It is **not automatically an M4 final-acceptance gate**. M4 owns parallel simulation correctness and performance characterization on its accepted platform; cross-platform actor portability is a later capability unless a separately admitted milestone explicitly changes that scope.

## Per-host calibration

Correctness validation and performance tuning are separate.

OCGForge should not encode one universal worker count. Each machine class should measure its own useful concurrency range after semantic equivalence has been established for that platform/build.

A calibration may inspect:

```text
1 / 2 / 4 / 8 / 16 / 32 / 64 workers
```

as appropriate for the host.

Record separately:

- maximum semantically validated concurrency;
- recommended production concurrency;
- aggregate games/second or decisions/second;
- scaling efficiency;
- saturation point;
- any understood throughput collapse.

A small hosted CPU may reasonably select a much smaller actor count than a many-core workstation. That difference is performance configuration, not a semantic difference in OCGForge.

Provider hardware characteristics should be discovered and logged with benchmark evidence rather than assumed from stale documentation.

## GPU usage

GPU time should primarily serve workloads that benefit from it:

- batched policy inference;
- representation learning;
- forward/backward optimization;
- checkpoint evaluation where neural inference dominates.

Do not require every CPU actor host to own a GPU.

When actors need a neural policy, prefer batched inference or versioned local policy snapshots over one synchronous cross-internet critical path when possible.

The exact inference topology should be decided only after the accepted
model-facing and trajectory contracts are joined to explicit Phase-6 learner
contracts.

## Ephemeral hosted sessions

Platforms such as Kaggle can be useful when they offer suitable CPU notebook/commit capacity, but OCGForge must treat them as ephemeral resources and must remain within provider terms and current account/session quotas.

Design assumptions:

- sessions may end at provider limits or fail unexpectedly;
- CPU model and performance may differ between runs;
- concurrent-session quotas may change;
- storage may be temporary;
- accelerator availability may vary;
- network connectivity is not a gameplay dependency.

Therefore provider quotas, pricing, hardware shape, accelerator availability, and session limits belong to deployment discovery/metadata, never to a versioned gameplay or trajectory contract.

## Accepted foundation and scope boundary

This strategy depends on the accepted M4 parallel environment foundation and
Phase 5 model-facing boundary. It remains future deployment architecture.

M4 should establish on its accepted platform that increasing concurrency does not change authoritative semantics, privacy, or complete legal decisions and should empirically characterize the useful worker range on the measured host.

M4 does **not** need to prove that 64 workers are always optimal.

A valid M4 closure can distinguish:

```text
maximum semantically validated concurrency
recommended concurrency for this machine
```

Cross-platform Linux/hosted equivalence is intentionally kept as a later
portability gate rather than silently expanding M4 or Phase 5 into a
deployment/ML portability milestone.

## Later prerequisites before real distributed training

Do not implement portable distributed actors merely because the preceding
milestones pass. The following layers should exist first:

1. stable accepted episodic environment semantics;
2. accepted versioned trajectory contract;
3. accepted transactional shard writer/validator;
4. accepted framework-neutral model-facing adapter;
5. `DATA_TRUSTED` validation gates;
6. checkpoint/behavior/opponent policy provenance;
7. cross-platform environment equivalence for every trusted platform class;
8. deterministic semantic job assignment and dataset deduplication;
9. evaluation harness independent from training reward.

Only then should deployment tooling automate hosted actor backends.

## Suggested readiness statuses

Future work may benefit from explicit gates such as:

### `ENVIRONMENT_READY`

- episodic reset/step contract is stable;
- complete legal candidates are preserved;
- observations are perspective-safe;
- deterministic replay/equivalence gates pass.

### `CROSS_PLATFORM_READY`

- Windows and Linux execute the deterministic action corpus equivalently;
- semantic gameplay, candidate, and observation identities match as required;
- neural policy output is not used to define environment equivalence;
- platform/build provenance remains separate.

### `DATA_TRUSTED`

- trajectory/observation/action schemas are versioned;
- every chosen action maps to the complete legal domain;
- rules/deck/environment provenance is present;
- behavior/opponent policy provenance is present where applicable;
- policy RNG provenance is present when stochastic behavior is used;
- shards use transactional fail-closed publication;
- shards and dataset manifests are hash-verifiable;
- semantic job duplicates/conflicts are rejected;
- hidden information is absent;
- partial/corrupted episodes are rejected.

### `DISTRIBUTED_ACTORS_READY`

- independent actor backends can generate interchangeable trusted shards;
- provider/session failure does not create silent retries or duplicated semantic jobs;
- every platform class used for data generation is `CROSS_PLATFORM_READY`;
- hardware-specific worker calibration is recorded;
- learner/dataset input validation is fail-closed.

## Future implementation principle

When this work is eventually implemented, there should still be one OCGForge environment implementation:

```text
same OCGForge
├─ local Windows profile
├─ generic Linux profile
└─ provider deployment profiles
   ├─ Kaggle
   ├─ Colab
   └─ cloud/other
```

Provider profiles may install/build/calibrate the environment and transport published shards, but they must not redefine rules, observations, legal candidates, hashes, rewards, or trajectory semantics.

## Algorithm neutrality

The asynchronous shard architecture is a compute/data boundary, not an RL recommendation.

It may later fit methods that tolerate policy lag or reuse trajectories particularly well. It must also remain possible to run a bounded fresh-policy collection cycle when an on-policy method is scientifically justified.

Algorithm selection should happen only after the environment, trajectory, provenance, and evaluation contracts are stable enough for a controlled comparison.

## Decision preserved by this note

The intended long-term compute strategy is:

> Scale Yu-Gi-Oh! simulation horizontally across independently validated CPU actor backends, publish immutable versioned trajectory shards transactionally, preserve exact behavior/opponent provenance, validate and deduplicate them fail-closed, and reserve GPU resources primarily for batched neural inference and learning.

This is a candidate architecture direction, not authorization to begin distributed ML before the environment/data contracts are ready.
