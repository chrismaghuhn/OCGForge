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

Status: **M0 FINAL PASS** recorded in repository history.

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

Status: **M1 FINAL PASS** recorded; consult `docs/protocol/decision_coverage.json` for family-level classification.

### M2 — Perspective-safe player observation

Outcome:

- `ygo.player_observation.v1`;
- player globals, zones, entities, relationships, chain;
- decision context;
- match context;
- visible event history;
- canonical serialization and observation hash;
- explicit hidden-information boundaries.

Status: **M2 FINAL PASS** recorded.

### M2.1 — Xyz material query investigation

Outcome:

- isolated a public-query limitation in the unmodified pinned core;
- documented privacy-safe interim behavior;
- established evidence needed for a narrow fix.

Status: **M2.1 FINAL PASS** recorded; the capability was later resolved by the M3.5 patchset.

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
closure, not a general ML-readiness or Phase 6 claim.

### Episodic V2 — Public environment boundary

Status: **FINAL PASS**.

The accepted Episodic V2 boundary owns reset/step lifecycle, complete ordered
public candidate domains, public observation identity, privacy, replay,
failure-closed behavior, and deterministic semantic execution. See
`docs/contracts/episodic-environment-v2.md` and the historical Episodic
acceptance record.

### Phase 3A / 3B — Trusted trajectory and admission

Status: **FINAL PASS**.

Phase 3A/3B owns trusted trajectory values and provenance plus immutable shard
persistence, restricted evidence, replay admission, receipts, and dataset
identity. The accepted `ocgforge.trusted_trajectory.v1` schema is unchanged by
later model-facing work.

### Phase 4A — Public policy boundary

Status: **FINAL PASS** for the defined fixed certified matchup.

Phase 4A establishes the public-only policy boundary and deterministic domain
preservation. Phase 4B and 4C extend the accepted Teacher and evaluation
sidecar scopes below.

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

With Phase 4A, Phase 4B, and Phase 4C accepted, **Phase 4 is FINAL PASS**.

### Phase 5 — Framework-neutral model-facing boundary

Status: **FINAL PASS**.

Phase 5 is accepted for the framework-neutral path:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
    → LogicalModelInputV1
    → EncodedModelInputV1
    → ModelBatchLayoutV1
```

It also provides admission-backed `ModelSupervisionSampleV1`. The path
preserves exact candidate order and N→N membership, keeps
`public_action_key` as selection/routing identity, treats candidate ordinals
as derived training labels only, and excludes physical layout from
model-input identity. See `docs/p5/P5_MODEL_CONTRACT.md` and
`docs/p5/P5_ACCEPTANCE_EVIDENCE.md`.

Phase 5 does not select PyTorch, JAX, or another framework and does not start
Behavior Cloning, neural training, RL, self-play, or checkpoint training.

Accepted evidence: `H_exec=3c99e86c487361fc4e0f5f12678b4867e59232b7`,
`H_evidence=da3376fc2ab645377f9de2dd9fd6195c1aa8c081`.

## Phase 6 — Behavior Cloning baseline

Status: **active infrastructure; Task 5 tooling final acceptance pending**.

Phase 6 Tasks 1–4B are accepted and merged. Task 5 contract freeze and T5A–T5C
are final and merged. T5D is merged at
`c0156a3451a7f8cc4495d544f7a34cab925e3c5a`; its main acceptance and the Task 5
tooling final pass remain pending post-merge CI review. The accepted Task4B
checkpoint is a bounded technical smoke artifact, not a meaningful BC baseline
or strategic-strength claim.

| Phase-6 task | Status |
| --- | --- |
| Task 1 — BC/data/checkpoint/evaluation contract freeze | **FINAL / MERGED** |
| Task 2 — admitted supervision materialization, split, and model-input inspector | **FINAL / MERGED** |
| Task 3 — framework-neutral BC architecture and reference interface | **FINAL / MERGED** |
| Task 4A — provisional backend infrastructure, codecs, runner, and CUDA preflight | **FINAL / MERGED** |
| Task 4B — one CUDA smoke run, canonical export/reload, and inference evidence | **FINAL / MERGED** |
| Task 5 contract freeze | **FINAL / MERGED** |
| T5A — schemas, codecs, identities, and job manifests | **FINAL / MERGED** |
| T5B — offline evaluator, metrics, and deterministic slicing | **FINAL / MERGED** |
| T5C — frozen gameplay evaluator | **FINAL / MERGED** |
| T5D — public audit, first divergence, distribution shift, and derived report | **MERGED / POST-MERGE ACCEPTANCE PENDING** |
| Task 5 tooling FINAL PASS | **PENDING** |
| Task 6 — PyTorch/JAX backend bake-off | **NOT AUTHORIZED** |
| Task 7 — first accepted BC baseline and checkpoint evidence | **NOT AUTHORIZED** |

The current contract and implementation set is [P6_BC_CONTRACT.md](p6/P6_BC_CONTRACT.md),
[P6_DATASET_AND_SPLIT_CONTRACT.md](p6/P6_DATASET_AND_SPLIT_CONTRACT.md),
[P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md](p6/P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md),
[P6_EVALUATION_PLAN.md](p6/P6_EVALUATION_PLAN.md), and
[P6_IMPLEMENTATION_PLAN.md](p6/P6_IMPLEMENTATION_PLAN.md), with the Task-4A
numeric/provenance supplement in
[P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md](p6/P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md),
the Task-4B acceptance/recovery contract in
[P6_TASK4B_ACCEPTANCE_RECOVERY_V1.md](p6/P6_TASK4B_ACCEPTANCE_RECOVERY_V1.md),
the Task-5 execution contract in
[P6_TASK5_EVALUATION_EXECUTION_CONTRACT.md](p6/P6_TASK5_EVALUATION_EXECUTION_CONTRACT.md),
and the architecture rationale in
[ADR-0007](adr/ADR-0007-phase6-behavior-cloning-boundary.md).

The Task-5 implementation is split into T5A schemas/codecs/identities/job
manifests, T5B offline evaluation, T5C frozen gameplay evaluation, and T5D
public audit, first divergence, distribution shift, and deterministic derived
reporting. P6-G15 is supported by T5D but remains pending post-merge review.
P6-G14 remains **NOT_RUN/BLOCKED_BY_MEANINGFUL_BASELINE** and is not required
for the Task 5 tooling final pass.

The required sequence remains:

```text
Task 5 tooling FINAL PASS
    → Task 6 PyTorch/JAX backend bakeoff
    → Task 7 first meaningful feed-forward BC baseline
```

Task 6 requires Task 5 tooling FINAL PASS and explicit authorization. Task 7
requires an accepted Task 6 backend decision and explicit authorization. Later
tasks must not weaken determinism, privacy, candidate completeness, replay, or
admission semantics. Phase 7 is **NOT STARTED**.

### Phase 7 — Not started

Status: **NOT STARTED**. Phase 7 has no authorized implementation or
acceptance scope in this roadmap.

## Historical M4 measurement notes

The following preserves M4-era measurement guidance. M4 and Phase 5 are
accepted separately; these notes do not reopen either milestone or imply
learner implementation.

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

M4 does not imply vectorized simulation or framework-specific ML readiness.

## Deferred Phase 6 and later workstreams

These are deferred directions, not authorization to begin later Phase-6 tasks
or accepted numbered-milestone evidence. The active status and sequencing
section above owns the current Phase-6 boundary; frozen contracts own semantic
meaning.

### A. Episodic environment API — completed

Episodic V2 is accepted. Its public reset/step API, complete candidate
domains, identity, privacy, replay, and failure-closed lifecycle are no longer
future work. Any incompatible successor requires a new versioned contract.

Do not let a future convenience API redefine engine legality or observation
privacy.

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

### D. Model-facing adapter — Phase 5 completed

Phase 5 has completed the framework-neutral semantic model-facing adapter:

- `LogicalModelInputV1`;
- `EncodedModelInputV1` and immutable vocabulary mapping;
- `ModelBatchLayoutV1` ragged/padded views and masks;
- canonical model-input identity;
- admission-backed supervision samples.

This layer consumes semantic OCGForge contracts and remains replaceable. A
framework-specific tensor or learner adapter belongs to Phase 6 or later.

The authoritative environment should not be redesigned around one network architecture.

### E. Further execution scale

Potential later scope:

- multiple independent environments;
- process/thread architecture;
- deterministic per-environment seeds/streams;
- bounded memory;
- throughput metrics.

Parallelism must not introduce nondeterministic authoritative ordering.

### F. Trajectory and training interfaces — trajectory completed

Phase 3A/3B trajectory and admission interfaces are accepted:
`ocgforge.trusted_trajectory.v1` and
`ocgforge.policy_provenance.v1`. It fixes logical episode, public action,
closure, identity, privacy, and producer-attribution semantics. The single
Phase-3B implementation PR now owns the physical shard, restricted-evidence,
semantic-replay admission, receipt, and dataset-manifest layers above V2.
See ADR-0005, `docs/trajectory/PHASE3A_ACCEPTANCE.md`, and the
[Phase-3B acceptance matrix](trajectory/PHASE3B_ACCEPTANCE.md). New learner
or dataset-consumer behavior must remain above these accepted interfaces.

Future work in this area is limited to explicit Phase-6 data consumers or
versioned algorithm-specific extensions. The accepted v1 trajectory and
admission semantics remain unchanged.

Phase 6 Task 1 has explicitly selected the Behavior Cloning baseline as its
first learned-policy objective. That selection does not authorize RL,
self-play, search, or any later algorithm; each needs its own scope and
contract.

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

Cross-platform actors must not become training-data sources until deterministic gameplay, complete candidate domains, perspective-safe observations, and trajectory provenance are proven equivalent under the relevant contracts. This portability gate remains future work after Phase 5 unless separately admitted; it does not silently expand M4 or Phase 5's scope.

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
