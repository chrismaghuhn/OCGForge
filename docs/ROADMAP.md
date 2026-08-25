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

## Next known milestone

### M4 — Performance and throughput baseline

PR #1 explicitly identified M4 performance/throughput work as not included.

M4 should begin with measurement, not optimization.

Recommended M4 questions:

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

M4 should not yet imply vectorized ML readiness.

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

### G. Portable distributed actor / learner compute

Potential scope:

- independently validated CPU actor pools on heterogeneous machines;
- local Windows and generic Linux execution profiles over the same environment semantics;
- provider-specific deployment profiles, including ephemeral hosted CPU sessions when useful;
- immutable, hash-verifiable trajectory shards as the integration boundary;
- per-host concurrency calibration rather than one globally hard-coded worker count;
- GPU resources reserved primarily for batched neural inference and learning.

Provider quotas, accelerator types, session counts, and pricing are operational configuration, not gameplay contracts.

Cross-platform actors must not become training-data sources until deterministic gameplay, complete candidate domains, perspective-safe observations, and trajectory provenance are proven equivalent under the relevant contracts.

See `docs/KAGGLE_ACTOR_FARM_STRATEGY.md` for the preserved architecture note. This is future intent only and does not authorize distributed ML before the episodic, trajectory, model-facing, and data-trust layers exist.

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
