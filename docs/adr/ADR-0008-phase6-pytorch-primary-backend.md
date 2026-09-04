# ADR-0008: Select PyTorch as the Phase-6 Task7 execution backend

## Status

Proposed / pending independent Task6 review.

This branch-local decision record is not an independent acceptance, does not
claim `FINAL PASS`, and does not authorize Task7. The accompanying readiness
record is authoritative for the bounded Task6 audit result.

## Context

OCGForge has accepted the public environment, trusted trajectory/admission,
framework-neutral model-input, Phase-6 contract, Task4A/Task4B, and Task5
tooling boundaries for their defined scopes. The accepted Phase-6 sequence is:

```text
Task5 tooling FINAL PASS
    → Task6 backend decision and Task7 readiness
    → Task7 first meaningful feed-forward BC baseline
```

The accepted Task4B run exercised PyTorch on a real, bounded CUDA smoke. Its
historical records deliberately remained provisional and its original
post-smoke result remains `SMOKE_PASS=true` with `TASK4B_PASS=false`. The
separate recovery record establishes `TASK4B_FINAL_PASS=true` without changing
those historical fields. The checkpoint is a technical smoke artifact, not a
meaningful BC baseline, convergence claim, strategic-strength claim, playable
policy, or backend-superiority claim.

Task6 is intentionally a backend/provenance/readiness decision. It must not
move legality, public observation, candidate completeness, action identity,
model-input identity, dataset membership, checkpoint semantic identity,
replay, admission, or evaluation authority into PyTorch.

## Decision

The proposed Phase-6 primary learner/inference execution backend for Task7 is
PyTorch:

```text
PHASE6_PRIMARY_BACKEND=PYTORCH
TASK7_FRAMEWORK=PYTORCH
TASK7_FRAMEWORK_VERSION=2.12.1+cu126

JAX_STATUS=DEFERRED_CANDIDATE
JAX_REJECTED=NO
JAX_PHASE6_REQUIREMENT=NO
JAX_IMPLEMENTATION_AUTHORIZED=NO

PYTORCH_DOES_NOT_BECOME_SEMANTIC_AUTHORITY=YES
NO_BACKEND_BAKEOFF_WITHOUT_MEASURED_REASON=YES
```

The proposed documentation-only decision label is:

```text
ocgforge.phase6.backend.pytorch.primary.v1
```

This label is not present in Task4 artifacts or production code. It is not a
gameplay identity, model-input identity, candidate-domain identity, replay
identity, rules/deck identity, or checkpoint semantic identity.

### Scope

PyTorch owns only physical learner and inference execution: tensors, the
provisional feed-forward candidate scorer, device execution, optimizer state
when separately authorized, and the adapter that produces validated numeric
scores. Task6 does not select Task7 hyperparameters, train a meaningful
baseline, create or replace a checkpoint, implement JAX, benchmark backends,
regenerate Teacher data, or alter the Task5 evaluation semantics.

Task7 remains unauthorized and unstarted. A future Task7 execution-contract
slice must separately freeze its training budget, optimizer, batch semantics,
checkpoint-selection rule, and exact source-head provenance.

### Why PyTorch now

PyTorch is selected because it is the only backend exercised by the accepted
Task4A/Task4B implementation and recovery evidence. Reusing that exercised
physical path is the shortest review and implementation path to a first
meaningful BC baseline. The choice also avoids introducing a second learner,
runtime, export/reload path, and deterministic-execution surface before a real
accepted workload demonstrates that those costs are justified.

This is an evidence-and-risk decision, not a claim that PyTorch is universally
better than JAX.

### Why JAX is deferred rather than rejected

JAX is a valid future candidate and has no negative acceptance result here. It
is deferred because no accepted Task7-or-later workload currently supplies a
measured reason to duplicate the learner and verification surface. JAX may be
reconsidered after an accepted workload demonstrates one of these triggers:

- a measured PyTorch bottleneck on the accepted workload;
- a multi-device, compilation, or deployment requirement with measured value;
- backend pressure from a separately authorized world-model/planning workload;
- a specific JAX capability with measured expected benefit.

Any such comparison must use the same accepted semantic inputs, candidate
domain, objective, budget, evaluation corpus, and canonical export semantics.
No unmeasured bake-off is authorized by this ADR.

## Verified Task4 provenance

The accepted Task4B execution report, smoke evidence, manifest, completion
receipt, checkpoint, original acceptance records, and recovery record agree on
the following observed provenance:

| Field | Verified value |
| --- | --- |
| historical backend identity | `ocgforge.phase6.backend.pytorch.provisional.v1` |
| framework version (`torch.__version__`) | `2.12.1+cu126` |
| CUDA build (`torch.version.cuda`) | `12.6` |
| device | `cuda:0` |
| device count | `1` |
| GPU | `NVIDIA GeForce RTX 4060 Ti` |
| compute capability | `8.9` |
| distributed execution | `single_process`, world size `1` |
| deterministic execution | strict algorithms, `warn_only=false`, matmul precision `highest` |
| Task4B checkpoint | `phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327` |
| smoke evidence | `phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe` |

The accepted Task4B evidence also records `actual_optimizer_steps=500` and
complete GPU-memory telemetry. Those are historical execution provenance,
not Task6 training and not semantic checkpoint identity. The contract records
no independently measured driver/runtime version; Task6 does not invent one.

The ten original Task4B files retain their contract hashes, and the original
`TASK4B_PASS=false` value remains immutable. The additive recovery status is
not substituted for the historical status.

## Framework-neutral authorities preserved

The following authorities remain outside PyTorch and unchanged:

| Authority | Preserved owner |
| --- | --- |
| game legality and engine semantics | pinned rules bundle and ordered ocgcore patchset |
| public visibility and complete legal domain | `EpisodicEnvironment V2` / `PublicEnvironmentObservation` |
| candidate order and routing | Environment-owned ordered candidates and existing `public_action_key` |
| logical/encoded model input | accepted Phase-5 `ygo::model` contracts and `CardVocabulary` |
| model-input identity | OCGForge canonical Phase-5 identity |
| labels and dataset membership | admitted trajectory, receipts, `DatasetManifest`, and split contracts |
| checkpoint semantic identity | OCGForge checkpoint manifest and canonical weight-content identity |
| evaluation meaning | accepted Task5 contracts and typed evidence |
| replay and admission | normal trajectory recorder, semantic replay, and admission path |

The learner receives the exact supplied candidate vector and returns scores in
that source order. It does not reconstruct legality, choose a global action
class, submit engine response bytes, or advance the engine.

## Task4 historical/provisional identity preservation

Task4A/Task4B remain historical provisional evidence. This ADR does not edit
their code, contracts, manifests, checkpoint, smoke evidence, completion
receipt, execution report, verification report, acceptance report, or recovery
record. In particular, it does not replace:

```text
ocgforge.phase6.backend.pytorch.provisional.v1
```

with the proposed primary label. A future Task7 run must receive a new
training-run/checkpoint identity when its semantic inputs or canonical weights
change; it must not overwrite the Task4 smoke artifact.

## Task7 readiness audit

The detailed matrix is in
[`P6_TASK6_PYTORCH_READINESS.md`](../p6/P6_TASK6_PYTORCH_READINESS.md). The
bounded conclusions are:

| Audit item | Result | Interpretation |
| --- | --- | --- |
| Task4 model path reusable for Task7 | `NO` | The current model consumes the Task4A numeric projection, which the Task4A contract accepts only for the lossy smoke. A non-smoke Task7 projection/materialization slice is still required. |
| Task4 inference path reusable for Task7 | `YES` | Request/response binding, exact score cardinality, deterministic selection, and no-fallback handling are reusable at the adapter boundary once Task7 supplies an accepted input projection. |
| Task4 canonical export reusable for Task7 | `YES` | The export is canonical, framework-neutral, and produces a new checkpoint identity from new semantic weight content. |
| Task4 canonical reload reusable for Task7 | `YES` | Strict architecture, Phase-5, vocabulary, dataset/split, export, and content validation plus fresh reload are already implemented. |
| Task5 evaluator reusable for Task7 | `NO` | The accepted T5C C++ implementation currently validates only the eight-job `IMPLEMENTATION_ACCEPTANCE` profile and the Task4 smoke checkpoint; it has no current meaningful-profile execution context. |

These two `NO` results are implementation gaps, not semantic authority
changes. The exact minimal follow-ups are a separately authorized non-smoke
Task7 input projection/materialization path and a separately authorized T5C
meaningful fixed-matchup context/job path that accepts the exact Task7
checkpoint. Task6 does not implement either follow-up.

Therefore:

```text
TASK7_READINESS=BLOCKED
TASK6_READINESS=BLOCKED_BY_IMPLEMENTATION_GAP
TASK7_AUTHORIZED=NO
TASK7_STARTED=NO
```

The block is deliberate: Task4 smoke evidence and the generic Task5 codec
surface are not promoted into proof that the current end-to-end production
path can run a meaningful Task7 checkpoint.

## Known limitations

- The Task4A state/candidate numeric projection is explicitly lossy and
  smoke-only; its identity is not a replacement for Phase-5 model-input
  identity.
- The Task4B checkpoint is technical smoke evidence, not a meaningful BC
  baseline and does not prove convergence, strategic quality, or playability.
- The current T5C C++ evaluator is intentionally bound to the small
  implementation/acceptance corpus and smoke checkpoint. The Task5 contract
  defines a later meaningful profile, but current T5C code does not expose
  that profile as a Task7 execution path.
- Task7 hyperparameters, stopping rule, checkpoint selection, meaningful job
  population, and acceptance claims remain unfrozen and unauthorized.
- Framework/device provenance may be recorded for diagnosis; it remains
  outside gameplay, replay, model-input, candidate, dataset, and checkpoint
  semantic identities.

## Privacy implications

This decision adds no data path. The future PyTorch adapter must consume only
the accepted public observation, exact ordered public candidate descriptors,
and Phase-5 model representations. It must not receive `CoreHost`, raw
`FullState`, private opponent identity, private semantic keys, response bytes,
raw engine locators, pointers, process IDs, filesystem paths, wall time, or
device scheduling facts as authoritative model input or gameplay identity.

Paired hidden worlds with equal public observation and complete public domain
must continue to produce equal logical/encoded model input, model-input
identity, deterministic scores, and selected public action key.

## Determinism implications

The backend choice does not alter deterministic semantics. Candidate rows and
keys remain in source order; the result remains exactly `N` finite scores for
`N` supplied candidates; padding remains non-semantic; equal finite scores use
the existing bytewise public-key tie rule; and unordered iteration cannot
choose an action. Framework version, CUDA, GPU, process topology, and worker
order remain execution provenance only.

## Replay implications

PyTorch does not submit engine response bytes and does not advance the engine.
The normal `PolicySelection → EpisodicEnvironment V2 → trajectory recorder →
semantic replay → admission` path remains mandatory. A timeout, crash, stale or
wrong response, invalid domain, or other neural failure remains a typed
failure/quarantine with no Teacher, RandomLegal, heuristic, first-candidate,
or retry fallback. A fallback-assisted result cannot become a trusted win.

## Revisit, rollback, and reconsideration conditions

Independent review may reject or defer this proposed decision if the verified
Task4 provenance, Task7 readiness audit, or semantic-boundary claims do not
hold. If accepted later, changing the primary backend requires a new reviewed
decision record; it must not rewrite Task4 history or reuse a semantic identity
for a different meaning.

JAX or another backend may be reconsidered only after a real accepted workload
provides a measured trigger and an equivalence review over the unchanged
OCGForge authorities. A rollback of the execution preference changes only
future execution policy/documentation; it never changes rules, decks,
trajectory/admission, public action identity, model-input identity, or existing
checkpoint identities.

## Verification

Task6 performed no CUDA smoke, optimizer step, meaningful training, backend
bake-off, new checkpoint, Teacher regeneration, or generated-evidence update.
The exact commands and outcomes are recorded in
[`P6_TASK6_PYTORCH_READINESS.md`](../p6/P6_TASK6_PYTORCH_READINESS.md).

The Task6 branch must stop for independent review. It must not create a PR,
merge, authorize Task7, or claim independent Task6 acceptance.
