# OCGForge Roadmap

This roadmap separates **completed repository checkpoints** from **future intent**.

Only completed milestones with acceptance evidence should be described as passed.

## Roadmap principles

Future work should preserve this ordering:

```text
semantic correctness
→ deterministic identity
→ information safety
→ legal-decision completeness
→ replay/audit evidence
→ performance
→ ML scale
```

A later milestone may add capability, but it should not weaken earlier guarantees.

## Completed checkpoints

### M0 — Deterministic pinned-core foundation

Outcome:

- reproducible pinned rules inputs;
- C++ core host/lifecycle boundary;
- deterministic controlled-duel probe;
- basic typed protocol;
- trace and diagnostic foundation;
- initial privacy boundary.

Status: foundation established in repository history.

### M1 — Decision protocol and continuation foundation

Outcome:

- typed `DecisionRequest`;
- stable semantic action keys;
- complex-selection continuations;
- fail-closed unsupported behavior;
- no global candidate truncation;
- exact final engine response construction;
- continuation immobility and stale-action tests.

Important nuance:

M1 did not convert every globally known interactive message family into engine-verified support.

Status: implemented foundation; consult `docs/protocol/decision_coverage.json` for family-level classification.

### M2 — Perspective-safe player observation

Outcome:

- `ygo.player_observation.v1`;
- player globals, zones, entities, relationships, chain;
- decision context;
- match context;
- visible event history;
- canonical serialization and observation hash;
- explicit hidden-information boundaries.

Status: implemented foundation.

### M2.1 — Xyz material query investigation

Outcome:

- isolated a public-query limitation in the unmodified pinned core;
- documented privacy-safe interim behavior;
- established evidence needed for a narrow fix.

Status: investigation completed; capability later resolved by M3.5 patchset.

### M3 — Locked fixed-deck conformance

Locked matchup:

- Swordsoul Tenyi ML v1;
- Salamangreat ML v1.

Outcome recorded by repository acceptance:

- exact deck manifests;
- card/script/database audit;
- canonical rules mode;
- required mechanics inventory;
- focused engine fixtures;
- full-game matrix;
- determinism and semantic-action re-execution;
- privacy regression;
- machine-readable acceptance evidence.

Status: **M3 FINAL PASS recorded**.

### M3.5 — Narrow ocgcore public API hardening

Outcome:

- corrected existing individual Xyz-material `overlay_seq` query path;
- added validated pre-duel starting-player selection;
- retained immutable pinned base checkout;
- made ordered repository patches canonical bundle inputs;
- reran/recorded M3 acceptance over both starting-player partitions.

Status: **M3.5 FINAL PASS recorded**.

### M4 — Parallel-simulation foundation and baseline checkpoint

Outcome:

- persistent native workers and deterministic job/result protocol;
- one-worker and multi-worker benchmark report generation;
- semantic, privacy, failure-isolation, and benchmark-integrity contracts;
- coordinator timing documented as end-to-end dispatch/result latency;
- no observation or engine optimization included in this checkpoint.

Status: **M4 FINAL PASS**. Fresh Release evidence is committed and independently
verifiable from a clean checkout. Semantic equivalence is validated through 64
workers; 16 workers is the recommended production concurrency for the measured
host. M4 remains narrowly the parallel-simulation foundation and baseline
closure, not a general ML-readiness or M5 claim.

## Post-M4 work remains separate

The following are candidate directions only and are not started by M4 finalization.

Historical M4 performance characterization is recorded under `docs/m4`.
M4.3.5 is explicitly **REJECTED — NO MATERIAL BENEFIT** and its reserve-backed
implementation is not part of the finalized production path.

Future work must continue to preserve the existing contracts. Questions that
remain outside this milestone include:

- What is reset/start cost?
- What is engine-process cost per semantic decision?
- What is observation-build and canonical-serialization cost?
- What is trace cost?
- What is full-game decisions/second and games/second?
- What fraction of time is ocgcore, adapter protocol, observation projection, serialization, Python orchestration, and process startup?
- What is the warm vs. cold dependency/cache behavior?
- Which optimizations preserve byte/semantic equivalence?

Recommended M4 acceptance properties:

- a versioned benchmark workload;
- pinned machine/toolchain metadata;
- warm and cold measurements separated;
- no candidate truncation;
- no observation field removal at the authoritative layer;
- no hidden-state shortcuts;
- no determinism regression;
- before/after semantic equivalence tests;
- performance targets stated only after a baseline exists.

M4 does not imply vectorized ML readiness.

## Post-M4 candidate workstreams

These are **candidate directions**, not accepted numbered milestones.

### A. Episodic environment API

Potential scope:

- explicit reset configuration;
- decision/observation coupling;
- step semantics;
- termination/truncation semantics;
- rewards as a separate policy layer;
- stable error model.

Do not let a convenience API redefine engine legality or observation privacy.

### B. Checkpoint / fork / replay architecture

Potential scope:

- authoritative checkpoint boundary;
- deterministic restore;
- forked simulation;
- contract-versioned persistence;
- replay verification independent from ephemeral process state.

This requires an ADR before implementation because persistence semantics become long-lived compatibility commitments.

### C. General support expansion

Potential scope:

- add additional decks through the same census/conformance process;
- expand engine-verified decision families;
- expand observation/event coverage;
- detect new public API gaps.

Support should grow by evidence closure, not by assuming that a parser implies card compatibility.

### D. Model-facing adapter

Potential scope:

- versioned vocabulary;
- tensorization;
- masks;
- padding/bucketing;
- embeddings/card catalog IDs.

This layer should consume semantic OCGForge contracts and remain replaceable.

The authoritative environment should not be redesigned around one network architecture.

### E. Vectorized / batched simulation

Potential scope:

- multiple independent environments;
- process/thread architecture;
- deterministic per-environment seeds/streams;
- bounded memory;
- throughput metrics.

Parallelism must not introduce nondeterministic authoritative ordering.

### F. Trajectory and training interfaces

Potential scope:

- versioned trajectory schema;
- observation/action/reward/termination records;
- provenance and bundle IDs;
- policy/model version metadata.

Do not begin algorithm selection by assuming PPO, BC, self-play, or another method is correct before environment and data semantics are stable.

## Milestone admission rule

Before assigning a future milestone a `PASS`, define:

- scope;
- explicit non-goals;
- acceptance gates;
- authoritative inputs;
- deterministic identities;
- privacy requirements;
- failure behavior;
- required evidence;
- migration/versioning impact.

Then execute the gates.

A roadmap checkbox is not evidence.
