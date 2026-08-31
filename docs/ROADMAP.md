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

### M4 — Parallel-simulation foundation and final acceptance

Outcome:

- persistent native workers and deterministic job/result protocol;
- one-worker and multi-worker benchmark report generation;
- semantic, privacy, failure-isolation, and benchmark-integrity contracts;
- coordinator timing documented as end-to-end dispatch/result latency;
- accepted semantic-equivalent internal optimizations only: M4.3.1 deferred
  decision-observation finalization, M4.3.2 one serialize/hash operation per
  observation epoch, and M4.3.6 Direct Canonical Writer;
- no observation schema, privacy, event-history, trace, rules, or deck contract
  change; M4.3.5 remains **REJECTED — NO MATERIAL BENEFIT**.

Status: **M4 FINAL PASS**. Fresh Release evidence is committed and independently
verifiable from a clean checkout. Semantic equivalence is validated through 64
workers; 16 workers is the recommended production concurrency for the measured
host. M4 remains narrowly the parallel-simulation foundation and baseline
closure, not a general ML-readiness or M5 claim.

### Phase 4B — Teacher public-only profiles and trusted path

Status: **FINAL PASS** for the fixed certified Swordsoul-versus-Salamangreat
matchup.

~~~text
H_exec =
cd00c3d34cc41c50ac1e7730a26a0e532cd21902

H_evidence =
32a1adedc50681fd3f5bf2d4b59f8fa3cd7a3030

schema =
ocgforge.phase4b_acceptance.v1

P4B-G00..G18 = PASS
~~~

Phase 4B established the public-only Teacher profiles, deterministic
fallback/diagnostic semantics, participant-safe state lifecycle, trusted
trajectory path, replay/admission compatibility, and fixed-matchup evidence.
Its accepted Teacher v1 identities remain immutable. This is not an arbitrary
deck, general battle-proof, or ML claim.

### Phase 4C — Public battle proof + frozen Teacher evaluation

Status: **FINAL PASS** for the fixed certified Swordsoul-versus-Salamangreat
matchup.

~~~text
H_exec =
9fe935531b63aaaf9535201dd4daf3f25e0f1a93

H_evidence =
043061a6bd81701d6344bd97dab098fd36acd7be

schema =
ocgforge.phase4c_acceptance.v1

P4C-G00..G14 = PASS

Task 1  — Battle Facts / Provable Lethal contract freeze
FINAL PASS
Task 2  — Battle snapshot implementation
FINAL PASS
Task 2A — Battle/Lethal prerequisite decision
FINAL PASS
Task 3  — fail-closed ProvableLethal evaluator
FINAL PASS
Task 4  — Teacher Battle/Lethal integration decision
FINAL PASS
Task 5  — frozen Teacher-v1 + sidecar evaluation harness
FINAL PASS
Task 6  — Phase-4C final acceptance/evidence
FINAL PASS
~~~

Accepted integration decision:

`TEACHER_V1_PLUS_EVALUATION_SIDECAR`

Teacher v1 remains the authoritative gameplay policy. BattleSnapshot and
ProvableLethal are deterministic, public-only, post-hoc evaluation/audit
sidecars and do not change action selection, strategy state, trusted trajectory
bytes, replay, admission, or dataset identity.

The accepted positive-lethal capability remains deliberately fail-closed:

`BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT`

This is not a proven-non-lethal result and does not authorize optimistic damage,
direct-attack, response-absence, or terminal-outcome inference. Phase 4C does
not claim general provable lethal, complete battle resolution, arbitrary-deck
battle intelligence, Teacher v2, or ML.

With Phase 4A, Phase 4B, and Phase 4C accepted, **Phase 4 is FINAL PASS**. Future
model-facing or ML work remains a separate milestone and requires its own
contracts and acceptance gates.

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

Phase 3A is the accepted contract prerequisite for this work:
`ocgforge.trusted_trajectory.v1` and
`ocgforge.policy_provenance.v1`. It fixes logical episode, public action,
closure, identity, privacy, and producer-attribution semantics. The single
Phase-3B implementation PR now owns the physical shard, restricted-evidence,
semantic-replay admission, receipt, and dataset-manifest layers above V2.
See ADR-0005, `docs/trajectory/PHASE3A_ACCEPTANCE.md`, and the
[Phase-3B acceptance matrix](trajectory/PHASE3B_ACCEPTANCE.md). The Phase-3B
implementation remains subject to its exact-head gates and independent
review; it does not begin Teacher, model, or ML work.

Potential scope:

- versioned trajectory, observation, and action schema identities;
- observation/action/reward/termination records;
- rules/environment/bundle provenance;
- behavior-policy and opponent-policy provenance where applicable;
- transactional shard publication and hash-verifiable dataset manifests;
- algorithm-specific metadata only through explicit versioned extensions.

Do not begin algorithm selection by assuming PPO, BC, self-play, or another method is correct before environment and data semantics are stable.

### G. Portable distributed actor / learner compute

Potential scope:

- independently validated CPU actor backends on heterogeneous machines;
- local Windows and generic Linux execution profiles over the same environment semantics;
- provider-specific deployment profiles, including ephemeral hosted CPU sessions when useful;
- transactional, immutable, hash-verifiable trajectory shards as the integration boundary;
- semantic job identity plus fail-closed duplicate/conflict rejection during dataset merge;
- per-host concurrency calibration rather than one globally hard-coded worker count;
- explicit separation of environment reproducibility from ML-run reproducibility;
- GPU resources reserved primarily for batched neural inference and learning.

Provider quotas, accelerator types, concrete hardware shapes, session counts, and pricing are operational configuration, not gameplay contracts. Provider backends must use normal supported mechanisms and current quotas rather than relying on circumvention behavior.

Cross-platform actors must not become training-data sources until deterministic gameplay, complete candidate domains, perspective-safe observations, and trajectory provenance are proven equivalent under the relevant contracts. This portability gate is post-M4 future work unless separately admitted; it does not silently expand M4's scope.

See `docs/PORTABLE_ACTOR_LEARNER_COMPUTE_STRATEGY.md` for the preserved architecture note. This is future intent only and does not authorize distributed ML before the episodic, trajectory, model-facing, and data-trust layers exist.

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
