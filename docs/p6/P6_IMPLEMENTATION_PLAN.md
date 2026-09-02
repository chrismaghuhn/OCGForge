# Phase 6 Behavior Cloning Implementation Plan

> **For future agentic workers:** Use this plan task-by-task with an
> independent review checkpoint after each task. Completing one task does not
> authorize the next task.

**Goal:** Build the first accepted Behavior Cloning baseline above the frozen
public, admitted-trajectory, and framework-neutral Phase-5 boundaries without
making a learner the authority for legality, visibility, or candidate
completeness.

**Architecture:** OCGForge owns public model semantics, trusted dataset
membership, episode-level partitioning, checkpoint identity, inference
binding, and evaluation evidence. A later framework owns physical execution
only; every backend must score the exact supplied variable-size candidate
domain and return an existing `public_action_key`.

**Tech Stack:** Existing C++/Python OCGForge public and trajectory contracts,
Python verification tooling, and framework-neutral Phase-5 model values. No
training backend or ML dependency is selected by this plan's Task 1.

---

## 1. Status and sequencing rules

| Task | Scope | Status at this freeze |
| --- | --- | --- |
| Task 1 | BC/data/checkpoint/evaluation contract freeze | FINAL / MERGED |
| Task 2 | admitted supervision materialization, deterministic split, model-input inspector | FINAL / MERGED |
| Task 3 | framework-neutral BC architecture and reference scorer/inference interface | FINAL / MERGED |
| Task 4A | corpus, numeric/config sub-codecs, provisional PyTorch architecture, checkpoint/inference runner, CUDA preflight | CURRENT / AUTHORIZED — zero optimizer steps |
| Task 4B | one CUDA smoke run, canonical export/reload, deterministic inference evidence | NOT AUTHORIZED |
| Task 5 | frozen offline/gameplay evaluation and first-divergence tooling | NOT AUTHORIZED |
| Task 6 | controlled PyTorch/JAX backend bake-off and primary-backend ADR | NOT AUTHORIZED |
| Task 7 | first accepted BC baseline run and canonical checkpoint evidence | NOT AUTHORIZED |

The required order is:

```text
one task
    → one bounded implementation
    → focused verification
    → independent review
    → explicit authorization of the next task
```

No task may silently broaden the fixed Swordsoul Tenyi versus Salamangreat
curriculum, alter Phase-5 semantics, add a fallback, or treat a derived file
as authority. The Phase-6 gate matrix in
[P6_EVALUATION_PLAN.md](P6_EVALUATION_PLAN.md#8-frozen-future-acceptance-matrix)
remains future evidence; this plan does not mark any gate `PASS`.

## 2. Task 1 — Contract freeze

**Status:** FINAL / MERGED — the accepted Phase-6 contract freeze.

**Authorized files:** exactly these nine Markdown files:

```text
docs/p6/P6_BC_CONTRACT.md
docs/p6/P6_DATASET_AND_SPLIT_CONTRACT.md
docs/p6/P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md
docs/p6/P6_EVALUATION_PLAN.md
docs/p6/P6_IMPLEMENTATION_PLAN.md
docs/adr/ADR-0007-phase6-behavior-cloning-boundary.md
docs/ROADMAP.md
docs/adr/README.md
docs/README.md
```

**Implementation boundary:** document the framework-neutral candidate scorer,
admitted DatasetManifest membership, exact episode-level split,
training/checkpoint provenance, canonical export boundary, deterministic
inference binding, offline/gameplay/first-divergence evaluation, privacy gate,
capacity gate, backend bake-off, and deferred deployment trigger.

**Must not do:** add production code, tests, CMake, dependency manifests,
generated evidence, neural networks, optimizer code, training, GPU usage,
checkpoint files, Project Ignis/EDOPro, RL, self-play, human-data ingestion,
broader decks, or any positive-lethal change.

**Review checkpoint:** verify exact base/head references, exact accepted
Teacher identities, no backend selection, no silent fallback, all future gates
remain `NOT_RUN`, all links resolve, and `git diff --check` is clean. Commit
once with the requested message and stop for independent review; Task 2 is not
automatically authorized.

## 3. Task 2 — Admitted supervision materialization, split, and inspector

**Status:** FINAL / MERGED — the accepted Task-2 implementation.

**Purpose:** Build the first data consumer above the accepted Phase-3B
admission and Phase-5 representation without changing either source contract.

**Inputs:** immutable `DatasetManifest` members, verified whole-shard
`AdmissionReceipt` values, admitted trusted trajectory records, accepted
public frames/candidate vectors, and the exact Phase-5 model-input path.

**Required behavior:**

1. accept membership only from a validated DatasetManifest and its admitted
   receipt commitments;
2. derive one `ModelSupervisionSampleV1`/BC sample from one accepted decision
   record and exact selected `public_action_key`;
3. include continuation decisions and both perspectives without special-case
   truncation or candidate reconstruction;
4. compute the frozen 80/10/10 SHA-256 partition from
   `episode_semantic_id` before assigning any sample rows;
5. produce an auditable `TrainingDatasetSplitV1` identity and reject duplicate
   or conflicting membership;
6. preserve redaction/presence masks, source order, candidate count, and the
   selected label; and
7. provide the public-only model-input inspector with the fields listed in
   `P6_EVALUATION_PLAN.md`.

**Required negative cases:** arbitrary parsed files, missing/invalid receipts,
quarantined/failed trajectories, RandomLegal labels, duplicate selected keys,
missing labels, cross-episode split leakage, changed redaction, reordered or
dropped candidates, and physical widths below `N` must fail closed.

**Review checkpoint:** independently verify dataset identity against accepted
Phase-3B codecs, recompute split membership in a second process, inspect the
public-only output, and run the G01–G03, G07, G11, and inspector evidence. No
backend dependency is authorized by this task.

## 4. Task 3 — Framework-neutral BC architecture and reference interface

**Status:** FINAL / MERGED — the accepted Task-3 implementation.

**Purpose:** Specify and, where useful, implement a backend-neutral reference
interface for state encoding, candidate encoding, exact-domain scoring, and
deterministic key resolution.

**Required behavior:**

1. consume only `EncodedModelInputV1` and its exact routing sidecar;
2. expose a generic `state_encoder`, `candidate_encoder`, and
   `candidate_scoring_function` seam;
3. return exactly `N` scores for exactly `N` supplied candidates;
4. preserve source order and candidate/key pairing;
5. apply the explicit finite-score tie rule without unordered iteration;
6. reject `N=24`, `N=25`, or `N=129` when physical capacity is smaller than
   the supplied domain rather than truncating; and
7. keep continuation and engine advancement in the Environment.

The task must not freeze a neural architecture, action-family authority,
tensor framework, fixed action vocabulary, optimizer, or batch width. Separate
heads for summon/target/chain/option decisions are not legal authorities.

**Review checkpoint:** verify semantic equivalence of the reference interface
against Phase-5 model identities, exact N→N output, padding exclusion, paired
hidden-world equality, and stale/wrong-response binding. No training is
authorized by this task.

## 5. Task 4A — First backend infrastructure, codecs, runner, and CUDA preflight

**Status:** CURRENT / AUTHORIZED — zero-step Task-4A implementation.

Task 4A defines and validates the Task-4 numeric/configuration/checkpoint
sub-codecs, the rebuildable admitted smoke-corpus projection, the provisional
PyTorch architecture, the fail-closed inference runner, and the CUDA preflight.
It MUST execute zero optimizer steps. Its exact identities and field order are
owned by [P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md](P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md).

The Task-4B execution configuration is frozen before that later run: train
samples are ordered by ascending `bc_sample_identity`, `shuffle=false`, step
`i` selects `train_samples[i % train_sample_count]`, the Adam execution flags
are all explicitly false (`foreach`, `fused`, `amsgrad`, `maximize`,
`capturable`, `differentiable`, and `decoupled_weight_decay`), deterministic
algorithms are strict, and float32 matmul precision is `highest`.

The run manifest keeps the exact frozen Task-1 V1 top-level field set. Task-4B
step counts, CUDA-preflight attestation, deterministic execution identity, and
GPU-memory measurements are emitted in a separate smoke-evidence sidecar.

Task 4A does not issue accepted training-run or trained-checkpoint evidence.
Task 4B requires a separate authorization after independent review.

## 6. Task 4B — One CUDA smoke run, canonical export, and fail-closed runner evidence

**Status:** NOT AUTHORIZED. Requires separate authorization after Task 4A
review.

**Purpose:** Exercise one provisional implementation backend on a small real BC
workload, export a canonical inference checkpoint, and prove the runner's
failure behavior. This is an implementation experiment, not the primary
backend decision.

**Required behavior:**

1. use the exact DatasetManifest, split identity, Phase-5 inputs, architecture
   specification, and BC objective;
2. record a complete `TrainingRunManifestV1`, including framework/version,
   optimizer/schedule, batch and RNG configuration, precision, device/
   distributed provenance, source policy identities, code commit, and final
   checkpoint identity;
3. keep framework-native training state separate from canonical inference
   weights;
4. export only through the versioned canonical weight boundary and validate
   the immutable checkpoint manifest;
5. bind every inference request/response to checkpoint, model-input, ordered
   candidate-domain, and current public decision identities;
6. reject stale, duplicate, late, wrong-domain, wrong-checkpoint,
   wrong-length, non-finite, and invalid responses; and
7. never call Teacher, RandomLegal, heuristic, first-candidate, or another
   policy after a neural failure.
8. record `GPU_MEMORY_BEFORE`, `GPU_MEMORY_PEAK`, and `GPU_MEMORY_AFTER` as
   execution provenance for the one CUDA smoke; these values must not enter
   checkpoint semantic identity.

The provisional backend is PyTorch only for the one explicitly authorized CUDA
smoke. Task 4B does not choose PyTorch over JAX, JAX over PyTorch, or either as
the primary Phase-6 backend.

**Review checkpoint:** validate checkpoint/content/provenance identities, exact
inference binding, no fallback, capacity witnesses, and public-only diagnostics
before any result is used in evaluation.

## 7. Task 5 — Frozen offline, gameplay, and first-divergence evaluation

**Status:** NOT AUTHORIZED. Requires separate authorization after Task 4 review.

**Purpose:** Evaluate a frozen checkpoint through the normal public environment
and trusted trajectory/admission paths.

**Required behavior:**

1. report exact-domain BC loss, Teacher top-1 agreement, optional declared
   top-K agreement, coverage, and rejection counts;
2. slice offline results by decision family, domain size, phase/context,
   participant/deck role, starting player, continuation status, and rare/
   critical decisions;
3. run fixed immutable jobs/seeds/opponents across the accepted fixed matchup,
   mirrored seats, and starting-player partitions;
4. report wins/losses/draws/interrupted/failed/fallback/quarantine and
   replay/admission results separately, with the frozen uncertainty method;
5. produce public-safe first-divergence records with complete candidate keys
   and model scores; and
6. compare Teacher-state validation with BC-induced states and retain separate
   distribution identities.

**Review checkpoint:** confirm no neural shortcut, no hidden debug data, no
fallback-assisted win, no aggregate-only claim, and reproducible first
divergence. Run the P6-G08 through P6-G18 evidence applicable to the task.

## 8. Task 6 — Controlled PyTorch/JAX backend bake-off

**Status:** NOT AUTHORIZED. Requires a separate authorization after a real BC
workload and Task-5 evaluation evidence exist.

**Purpose:** Compare PyTorch and JAX only after both can be evaluated under one
accepted semantic workload. This task produces evidence and a later primary
backend decision; it does not retroactively alter the Phase-6 contracts.

Both implementations MUST use the same:

```text
DatasetManifest and dataset semantic identity
TrainingDatasetSplitV1
Phase-5 logical/encoded inputs and CardVocabulary identity
model architecture specification
exact candidate-scoring objective and training budget
evaluation corpus and fixed jobs
canonical checkpoint export semantics
```

Compare, at minimum:

```text
contract correctness
implementation complexity
single-GPU throughput
multi-device path
variable candidate-domain ergonomics
debuggability
checkpoint export fidelity
deployment compatibility
tooling ecosystem
```

Record hardware and framework details as provenance, not semantic identity.
The primary backend decision belongs in its own accepted ADR before Phase 7;
Task 1 intentionally selects neither backend.

**Review checkpoint:** independently reproduce key contract gates for both
implementations, compare canonical exports, audit the same evaluation corpus,
and reject any winner that relies on truncation, fixed action authority,
hidden-state shortcuts, or silent fallback.

## 9. Task 7 — First accepted BC baseline

**Status:** NOT AUTHORIZED. Requires the backend decision and explicit
authorization after Tasks 2–6 are reviewed.

**Purpose:** Run the first accepted BC baseline only after the data, model,
runner, evaluation, and backend evidence is complete.

**Required behavior:**

1. use only the fixed accepted matchup and eligible Teacher v1 source
   identities;
2. record the complete training-run manifest and immutable split identity;
3. export and validate one canonical checkpoint manifest/content identity;
4. run frozen offline and gameplay evaluation through the normal environment;
5. retain first-divergence and BC-induced distribution-shift evidence;
6. prove paired-hidden-world privacy, exact capacity, stale-response rejection,
   no fallback, and Phase-5 regression; and
7. publish only evidence whose commands actually ran at the declared source
   head.

This task may claim a BC baseline only for the frozen curriculum and evidence
scope. It may not claim arbitrary-deck support, competitive general
Yu-Gi-Oh!, optimal play, RL readiness, self-play, or Project Ignis/EDOPro
deployment.

**Review checkpoint:** independent review of the entire P6 gate matrix,
training/checkpoint provenance, evaluation identities, privacy evidence, and
clean-worktree/source-head binding before any Phase-6 final status change.

## 10. Cross-task non-goals

No task in this plan authorizes, without its own explicit scope and review,

```text
arbitrary-deck support or multi-archetype training
RL, PPO, R2D2, IMPALA, APPO, self-play, league training, MCTS, or search
Project Ignis/EDOPro deployment before the frozen-checkpoint trigger
human replay ingestion or human-demonstration admission
positive-lethal or battle-contract expansion
replacement of Environment legality, privacy, candidate completeness, or replay
```

The accepted Phase-5 references remain
`H_EXEC=3c99e86c487361fc4e0f5f12678b4867e59232b7` and
`H_EVIDENCE=da3376fc2ab645377f9de2dd9fd6195c1aa8c081`. No future task may
reinterpret them as training or neural evidence.
