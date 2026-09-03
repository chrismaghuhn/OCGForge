# Phase 6 Task 4B CUDA Smoke Design

## Status

`PROPOSED — pending independent specification review` on branch
`chris/phase6-task4b-cuda-smoke`, based on
`1727f09eb0fdc4e4e25e3f9ced9748feb4058234`. The design is not yet an
implementation-plan authorization. It authorizes no CUDA training run and no
optimizer step.

## Goal

Add one auditable Task-4B execution path for the provisional PyTorch backend.
The path must consume only the independently admitted Task-4 corpus, execute
one deterministic CUDA behavior-cloning smoke of at most 500 successful
optimizer steps, export an OCGForge canonical checkpoint, and derive positive
smoke evidence from the accepted completion path.

The smoke is infrastructure evidence. It makes no convergence, gameplay,
production-readiness, backend-selection, or Task-5 claim.

## Provenance stages

Task 4B has two immutable provenance stages:

1. `H_exec` is the exact committed source head containing this specification,
   the implementation plan, the runner, the CLI, and focused zero-step tests.
   The branch is pushed and left clean for independent review. No optimizer
   step is executed before this checkpoint.
2. After explicit approval of that exact head, the clean `H_exec` checkout is
   run once. The runner derives the real CUDA preflight, training count,
   memory telemetry, checkpoint, completion receipt, and smoke evidence. Only
   generated evidence is committed afterward.

The `training_code_commit` in `TrainingRunManifestV1` is derived inside the
runner from the exact `H_exec` checkout. The evidence records that source head
explicitly. The later evidence commit changes no source used by the run; its
evidence source head remains `H_exec`.

The runner enforces the clean source-head precondition itself before it builds
or executes the corpus probe, performs CUDA preflight, creates an optimizer,
or performs any other training work. It executes the equivalent of:

```text
git -C <source_root> rev-parse HEAD
git -C <source_root> status --porcelain --untracked-files=all
```

It accepts the first command only when the result is exactly 40 lowercase
hexadecimal characters, and accepts the second only when the result is empty.
The accepted HEAD is the sole source for `training_code_commit` and
`H_exec`. Ignored build/cache artifacts are allowed because they do not appear
in this porcelain status. Any non-clean result fails closed with
`actual_optimizer_steps=0`, before corpus creation and before CUDA preflight.

## Hard boundary

The implementation preserves these fixed values:

```text
PROVISIONAL_BACKEND=PYTORCH
PRIMARY_BACKEND_SELECTED=NO
device=cuda:0
CPU_FALLBACK=FORBIDDEN
maximum_optimizer_steps=500
shuffle=false
train sample order=unsigned UTF-8 ascending bc_sample_identity
step i sample=train_samples[i % train_sample_count]
seed=1729
dtype=float32
RL=FORBIDDEN
SELF_PLAY=FORBIDDEN
TASK5=UNAUTHORIZED
```

Rules, decks, Teacher identities, Phase-5 contracts, BC semantics, dataset
split semantics, candidate ordering, numeric projection, checkpoint identity,
and the frozen Request/TrainingRun V1 contracts remain unchanged. The
existing completion path remains the authority for canonical export, strict
fresh reload, and deterministic frozen inference; no optional reload-device
parameter is added.

## Components and ownership

### `tools/phase6/task4b_runner.py`

This module owns the complete Task-4B run and returns a typed result. It is the
only production caller that supplies execution-derived values to the existing
low-level smoke-evidence builder. Its private run state owns:

- the successful optimizer-step counter;
- the measured CUDA memory values;
- the sorted train-sample sequence;
- the live preflight and its attestation;
- the finalized run manifest;
- the actual canonical checkpoint and completion receipt.

The step counter increments only after `optimizer.step()` returns. A failure
before or during a step leaves the count at the number of prior successful
steps. Any failure raises a structured error carrying that count and produces
no positive smoke evidence. The runner never retries and never invokes
Teacher, RandomLegal, a heuristic, a first candidate, or CPU training.

### `tools/phase6/task4b_cuda_smoke.py`

The CLI is a thin launcher. It accepts only the source-root/build configuration
needed to build the accepted `phase6_task4_corpus_probe` target and an output
directory. It does not accept an arbitrary probe executable path, optimizer-
step counts, memory values, checkpoint identities, completion flags, dataset
identities, split identities, or loss values. The runner derives all of those
values.

### Existing Task-4A modules

The runner uses, without changing their semantic meaning:

- `task4_cuda.require_task4_cuda()` for the attested `cuda:0` preflight;
- `task4_codec` for canonical identities and artifact validation;
- `task4_model.Phase6TorchCandidateScorer` for exact-domain scoring and loss;
- `task4_inference.export_canonical_checkpoint()`;
- `task4_inference.issue_task4b_completion_receipt()`;
- `task4_cuda.finalize_training_run_manifest_from_cuda_preflight()`;
- `task4_cuda.smoke_evidence_from_cuda_preflight()`.

## Corpus and training data flow

The runner creates a private temporary directory and invokes the accepted C++
corpus probe exactly once. Before that invocation, the runner builds the
`phase6_task4_corpus_probe` target from the clean `H_exec` source tree, resolves
the resulting target path within the requested build directory, and computes
its SHA-256. It records:

```text
CORPUS_PROBE_SHA256 = SHA-256 of the exact executed binary
CORPUS_PROBE_SOURCE_COMMIT = H_exec
```

The runner executes exactly that hashed binary once. The binary hash and source
commit are build/execution provenance only; they never enter DatasetManifest,
sample, checkpoint, or gameplay identity. A missing, ambiguous, out-of-build,
or unbuildable target fails closed before corpus creation. The probe produces
the derived corpus artifact and the independent authority sidecar. The runner
then:

1. decodes both artifacts strictly;
2. admits the corpus through `admit_corpus_artifact()` and the decoded authority;
3. verifies that admission succeeds before constructing the optimizer;
4. selects only samples whose admitted partition is `train`;
5. sorts those samples by `bc_sample_identity.encode("utf-8")`;
6. preserves each sample's candidate rows and routing keys in source order; and
7. never materializes validation or test samples into the training loop.

The source dataset identity, split identity, CardVocabulary identity, and
sample count are read from the admitted artifacts. They are not CLI inputs.
The runner may copy the exact generated corpus and authority bytes to the
evidence directory after a successful run; those files are derived outputs,
never training authority supplied by a caller.

## CUDA and optimizer sequencing

The runner calls the accepted preflight before creating an optimizer or
performing an update. It requires CUDA availability, `cuda:0`, the exact
`NVIDIA GeForce RTX 4060 Ti`, zero preflight optimizer steps, and
`cpu_fallback=False`.

It then applies and verifies the frozen execution settings:

```python
torch.use_deterministic_algorithms(True, warn_only=False)
torch.set_float32_matmul_precision("highest")
torch.manual_seed(1729)
torch.cuda.manual_seed_all(1729)
```

The model is created on the preflight device. The optimizer is created with
the exact frozen Adam values and explicit execution flags:

```python
torch.optim.Adam(
    model.parameters(),
    lr=0.001,
    betas=(0.9, 0.999),
    eps=1e-8,
    weight_decay=0.0,
    foreach=False,
    fused=False,
    amsgrad=False,
    maximize=False,
    capturable=False,
    differentiable=False,
    decoupled_weight_decay=False,
)
```

Before the measured section, the runner verifies that all model parameters are
on `cuda:0`. It resets CUDA peak-memory statistics immediately before the
measured section and records allocated bytes before, peak allocated bytes
after synchronization, and allocated bytes after the final gradient clear.
These values are execution provenance only and never enter checkpoint
semantic identity.

For every step, the runner creates the state tensor, exact candidate tensor,
Boolean real-candidate mask, and label tensor on `cuda:0`. It verifies the
logits and loss are on `cuda:0`, finite, and exact-domain. Padding is never a
semantic candidate. The update order is:

```text
zero_grad
forward exact N candidates
exact-domain cross entropy over Teacher candidate_ordinal
backward
optimizer.step()
increment actual_optimizer_steps
```

The loop terminates after 500 successful updates. Initial and final loss are
diagnostic values measured on the first sorted train sample; no convergence
claim is made.

## Export, completion, and evidence

After the final successful step, the runner clears gradients with no parameter
mutation, exports only through `export_canonical_checkpoint()`, and validates
the resulting canonical artifact. It builds a request from an admitted train
sample and invokes `issue_task4b_completion_receipt()` exactly once. That
accepted path performs the required canonical export binding, two independent
fresh checkpoint reloads, deterministic repeated frozen inference, identical
scores, identical selected ordinal, identical selected public key, and
identical response identity.

The runner finalizes `TrainingRunManifestV1` from the live preflight and actual
checkpoint identity. It then constructs `Task4BSmokeEvidenceV1` through the
attested completion path using the internally counted steps and directly
measured memory values. A positive evidence artifact is emitted only when all
required receipt and evidence validations succeed.

The generated evidence set contains the exact corpus and authority artifacts,
canonical checkpoint bytes, canonical run-manifest bytes, canonical smoke-
evidence bytes, and a derived JSON/Markdown report. No generated file is
hand-edited. The report includes the source `H_exec`, actual PyTorch/CUDA
facts, probe hash/source commit, dataset counts, losses, identities, and the
explicit non-claims.

The runner always writes a machine-readable `Task4BExecutionReportV1`,
including when the single attempt fails. It writes a complete temporary JSON
file in the destination directory, flushes and fsyncs it, and publishes it
with an atomic same-directory replace. The report contains:

```text
schema_id
H_exec
corpus_probe_sha256
corpus_probe_source_commit
cuda_preflight_identity or null
source_dataset_identity or null
dataset_split_identity or null
card_vocabulary_identity or null
train/validation/test sample counts or null
actual_optimizer_steps
GPU_MEMORY_BEFORE/PEAK/AFTER or null
error_code or null
TASK4B_PASS
checkpoint_identity or null
smoke_evidence_identity or null
```

On any failure, `TASK4B_PASS=false`, the checkpoint and smoke-evidence
identities are null, and the report preserves the internally counted steps and
all provenance values reached before the failure. This report is execution
audit evidence, not a positive `Task4BSmokeEvidenceV1`.

## Failure behavior

Any preflight, admission, device-placement, determinism-setting, optimizer,
forward, loss, export, reload, or inference failure is fail-closed. The CLI
reports `TASK4B_PASS=NO`, atomically publishes the execution report, and
returns the internally known successful-step count and structured blocker. It
does not retry, change configuration, use CPU, invoke the Teacher, select a
candidate, or fabricate positive evidence.

If clean-`H_exec` or probe-build verification fails, the count is exactly zero
and CUDA preflight is not reached. If CUDA preflight fails, the count is
exactly zero and the optimizer is never created. If a step fails after `N`
successful calls, the count is exactly `N` and positive completion evidence is
impossible.

## Focused zero-step verification

The new focused Python tests prove, without a real optimizer run:

- incrementing occurs only after a successful `optimizer.step()`;
- a failed optimizer step is not counted;
- CLI step-count and GPU-memory overrides are rejected;
- validation and test samples never enter the training sequence;
- candidate row/key order and Teacher ordinal pairing are preserved; and
- preflight failure occurs before optimizer construction and leaves the count
  at zero.

The existing Task-4A tests remain part of the focused verification. The actual
CUDA smoke is not executed by any test; it is run once only after independent
review of `H_exec`.

## Explicit non-goals

This design does not authorize:

```text
Task 5 or any offline/gameplay evaluation
hyperparameter search or long training
convergence or gameplay-strength claims
PyTorch primary-backend selection or JAX comparison
RL, self-play, search, human-data ingestion, or broader decks
changes to rules, decks, Teacher, Phase-5, or frozen Phase-6 semantics
```
