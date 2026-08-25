# OCGForge — Portable Actor/Learner Compute Strategy

Status: future architecture note; **not an accepted milestone or implementation contract**.

This document preserves a post-M4 compute strategy for using heterogeneous CPU resources — including ephemeral hosted notebook CPUs such as Kaggle when available — to generate trustworthy OCGForge trajectories while reserving GPU resources primarily for model inference and learning.

Platform quotas, accelerator models, concurrent-session counts, and pricing are operational details and are intentionally **not** part of this document. They must be discovered from the provider at execution time rather than encoded into OCGForge semantics.

## Goal

OCGForge should eventually support a workflow where any trusted worker host can generate interchangeable trajectory shards:

```text
local Windows CPU actors ─┐
Linux CPU actors          ├──> validated trajectory shards ──> learner
hosted CPU sessions       ┘                                 └─> GPU
```

The physical machine, operating system, CPU model, worker count, process IDs, wall-clock scheduling, and provider must not change authoritative gameplay semantics.

The target property is:

> Any trusted OCGForge worker that passes the relevant environment and trajectory gates may generate interchangeable training data for the same versioned workload, regardless of whether it runs locally or on an ephemeral hosted CPU session.

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
trajectory shards
    ↓
GPU learner
```

Hosted CPU capacity can then increase simulation throughput without requiring every actor host to own a GPU.

## Non-goals

This note does **not** authorize:

- a Kaggle-specific rules engine;
- a second environment implementation;
- provider-specific gameplay behavior;
- live distributed training before trajectory contracts exist;
- omitting PlayerObservation fields for hosted workers;
- candidate truncation to reduce traffic;
- hidden-state shortcuts;
- silent retry of failed in-flight games;
- dependence on a fixed number of hosted sessions;
- synchronous multi-host GPU training over unreliable public network links.

## Preferred topology

The default future topology should be asynchronous actor/learner separation rather than treating several hosts as one synchronous machine.

```text
                    policy checkpoint N
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
        actor pool A  actor pool B  local actors
              │            │            │
              └──── validated trajectory shards ────┐
                                                     ▼
                                                GPU learner
                                                     │
                                                     ▼
                                            policy checkpoint N+1
```

Actors should be allowed to finish immutable work units independently. A provider session ending must not corrupt another actor or require a duel to continue on another host.

## Deterministic job partitioning

Logical work assignment must be independent of hardware and worker count.

A future distributed trajectory job should derive semantic identity from stable inputs such as:

```text
(master_seed, job_index, rules identity, matchup identity, policy identity)
```

not from:

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
actor pool A: jobs       0.. 99,999
actor pool B: jobs 100,000..199,999
local pool:   jobs 200,000..299,999
```

If a session terminates early, only fully validated completed episodes become eligible training data. Partial or unverifiable episodes fail closed.

## Trajectory shard trust boundary

A future shard should be immutable after publication and carry enough provenance to be independently validated.

At minimum the trajectory contract should eventually bind:

- trajectory schema version;
- OCGForge source/contract identity;
- rules-bundle identity;
- locked deck or matchup identity;
- job IDs and seeds;
- player perspective;
- `PlayerObservation` values;
- complete `DecisionRequest` / `ActionCandidate` domains;
- chosen semantic action keys;
- rewards and terminal results;
- policy/checkpoint identity;
- shard content digest.

A learner must reject shards that fail schema, provenance, semantic-action, privacy, or integrity validation.

Teacher quality and environment legality remain separate concepts: a legal but strategically poor action is valid trajectory data only under the policy/teacher identity that produced it; it must never be reclassified as engine truth.

## Cross-platform acceptance gate

Hosted Linux actors must not be considered interchangeable with the canonical local environment merely because they compile.

Before Linux/hosted actors become `DATA_TRUSTED`, establish a deterministic cross-platform corpus using identical:

- pinned rules inputs;
- ordered ocgcore patchset;
- locked decks;
- seeds/jobs;
- semantic action sequence.

Expected equivalence should include, where the contracts define cross-platform portability:

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

Build/provenance hashes may legitimately differ when they intentionally include compiler or platform identity. Do not confuse them with semantic gameplay identity.

## Per-host calibration

Correctness validation and performance tuning are separate.

OCGForge should not encode one universal worker count. Each machine class should measure its own useful concurrency range after semantic equivalence has been established.

A calibration may inspect:

```text
1 / 2 / 4 / 8 / 16 / 32 / 64 workers
```

as appropriate for the host.

Record separately:

- maximum semantically validated concurrency;
- recommended production concurrency;
- aggregate games/second or decisions/second;
- saturation point;
- any understood throughput collapse.

A host with four CPU cores may reasonably select a much smaller actor count than a many-core workstation. That difference is configuration, not a semantic difference in OCGForge.

## GPU usage

GPU time should primarily serve workloads that benefit from it:

- batched policy inference;
- representation learning;
- forward/backward optimization;
- checkpoint evaluation where neural inference dominates.

Do not require every CPU actor host to own a GPU.

When actors need a neural policy, prefer batched inference or versioned local policy snapshots over one synchronous cross-internet critical path when possible.

The exact inference topology should be decided only after model-facing and trajectory contracts exist.

## Ephemeral hosted sessions

Platforms such as Kaggle can be useful when they offer no-cost or low-cost CPU notebook/commit capacity, but OCGForge must treat them as ephemeral resources.

Design assumptions:

- sessions may end without warning or at provider limits;
- CPU model and performance may differ between runs;
- concurrent-session quotas may change;
- storage may be temporary;
- accelerator availability may vary;
- network connectivity is not a gameplay dependency.

Therefore provider-specific quotas must remain deployment configuration and documentation, never a versioned gameplay or trajectory contract.

## M4 dependency

This strategy depends on M4 proving the parallel environment foundation first.

M4 should establish that increasing concurrency does not change authoritative semantics, privacy, or complete legal decisions and should empirically characterize the useful worker range on the measured host.

M4 does **not** need to prove that 64 workers are always optimal.

A valid closure can distinguish:

```text
maximum semantically validated concurrency
recommended concurrency for this machine
```

Later hosted machines repeat performance calibration while reusing the same semantic contracts.

## Later prerequisites before real distributed training

Do not implement the actor farm merely because M4 passes. The following layers should exist first:

1. stable episodic environment semantics;
2. versioned trajectory contract;
3. model-facing adapter;
4. `DATA_TRUSTED` validation gates;
5. checkpoint/policy provenance;
6. cross-platform environment equivalence;
7. deterministic shard/job assignment;
8. evaluation harness independent from training reward.

Only then should deployment tooling automate hosted actor pools.

## Suggested readiness statuses

Future work may benefit from explicit gates such as:

### `ENVIRONMENT_READY`

- episodic reset/step contract is stable;
- complete legal candidates are preserved;
- observations are perspective-safe;
- deterministic replay/equivalence gates pass.

### `CROSS_PLATFORM_READY`

- Windows and Linux execute the deterministic corpus equivalently;
- semantic gameplay, candidate, and observation identities match as required;
- platform provenance remains separate.

### `DATA_TRUSTED`

- trajectory schema is versioned;
- every chosen action maps to the complete legal domain;
- rules/deck/policy provenance is present;
- shards are hash-verifiable;
- hidden information is absent;
- partial/corrupted episodes are rejected.

### `DISTRIBUTED_ACTORS_READY`

- independent actor hosts can generate interchangeable trusted shards;
- provider/session failure does not create silent retries or duplicated semantic jobs;
- hardware-specific worker calibration is recorded;
- learner input validation is fail-closed.

## Future implementation principle

When this work is eventually implemented, there should still be one OCGForge environment implementation:

```text
same OCGForge
├─ local Windows profile
├─ generic Linux profile
└─ hosted/Kaggle deployment profile
```

The provider profile may install/build/calibrate the environment, but it must not redefine rules, observations, legal candidates, hashes, rewards, or trajectory semantics.

## Decision preserved by this note

The intended long-term compute strategy is:

> Scale Yu-Gi-Oh! simulation horizontally across independently validated CPU actor pools, use immutable versioned trajectory shards as the integration boundary, and reserve GPU resources primarily for batched neural inference and learning.

This is a candidate architecture direction, not authorization to begin distributed ML before the environment/data contracts are ready.
