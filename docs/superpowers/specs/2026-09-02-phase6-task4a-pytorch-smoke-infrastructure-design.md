# Phase 6 Task 4A PyTorch Smoke Infrastructure Design

## Status

Approved Task-4A design. This document defines the implementation boundary for
the first provisional PyTorch integration. It does not authorize the Task-4B
CUDA optimizer run, a quality claim, Task 5 evaluation, or a primary-backend
decision.

## Goal

Provide the trusted admitted-corpus bridge, versioned numeric/configuration
sub-codecs, a small PyTorch candidate-scoring model, canonical weight/checkpoint
validation, request/response binding, and CUDA preflight while executing zero
optimizer steps.

## Authority and data flow

The C++ probe is the only producer of the smoke corpus:

    fixed TeacherRunner job set
        -> admitted EpisodeEnvelope
        -> verified AdmissionReceipt
        -> DatasetManifest
        -> Phase-6 Task-2 materializer
        -> Task-4 numeric projection
        -> derived phase6_task4_smoke_corpus.v2

The physical corpus is rebuildable and is not DatasetManifest authority. It
carries the source dataset semantic identity, split identity, derivation
contract, every source BC sample identity, every model-input identity, and a
derived-artifact content digest. The probe also emits a separate authority
sidecar directly from the trusted manifest, Task-2 split, and admitted sample
path. The Python admission path requires that sidecar (or equivalent
independent authority) plus the exact expected corpus artifact identity; it
never manufactures positive authority from decoded corpus bytes.

The fixed corpus job set is declared in code and does not search for favorable
partition membership. If its train partition is empty, corpus creation fails
closed.

## Numeric and feature boundary

The accepted Task-4A numeric contract is
ocgforge.phase6.inference_numeric.v1 with the exact representation
ocgforge.phase6.numeric.f32_ieee754_be.v1:

- every score and canonical inference weight is IEEE-754 binary32;
- canonical bytes are the raw four-byte binary32 bits in big-endian order;
- NaN and either infinity are invalid;
- no implicit rounding or tolerance is used for action selection;
- equal finite scores use unsigned-byte lexicographic public-action-key order;
- response identity remains the Task-1 selection-envelope identity and excludes
  score bytes.

State and candidate feature projection is
ocgforge.phase6.task4.numeric_projection.v1. It emits numeric rows in fixed
field order. State rows use explicit field tags and accepted numeric
presence/namespace values; candidate rows use action/choice/reference/optional
codes and state-vs-candidate-only locator namespace ordinals. Public locator
strings are not emitted to the network. Routing keys and public action keys are
control-plane sidecars only.

## Provisional PyTorch architecture

The model is intentionally small and not a final architecture:

    state numeric rows[variable count, 8]
        -> Linear(8, 16) + ReLU
        -> mean/max pooling -> shared state context[32]

    candidate numeric row[28] + shared state context[32]
        -> Linear(60, 16) + ReLU
        -> Linear(48, 1) -> one scalar score

The forward path scores every real candidate row in source order. Candidate
ordinal is never an input. There are no action-family heads and no legality,
continuation, or engine-advancement authority in the model.

Architecture configuration, parameter order, and projection identities are
canonical scalar identities. PyTorch is used only as the provisional physical
execution backend.

## Training-run and checkpoint codecs

Task 4A defines and validates, but does not execute, the required
TrainingRunManifestV1 sub-identities for architecture, optimizer, schedule,
batch, gradient accumulation, RNG/initialization, precision, and execution
provenance. A training-run identity is a canonical length-prefixed SHA-256
identity over those accepted scalar/sub-identities.

Canonical weight export has explicit tensor name/order, shape, dtype, byte
order, numeric codec, and exact parameter bytes. The export contains no
optimizer, gradient, sharding, worker, device, or cache state. The physical
checkpoint artifact is OCGForge-owned and binds its manifest to the canonical
weight-content identity; no torch.save object is authoritative.

GPU model, device index, the PyTorch CUDA build, and the version reported by
`torch.version.cuda` are training provenance only and never checkpoint
semantic identity; no separately measured driver/runtime version is claimed.

## Inference runner

The runner creates canonical InferenceRequestV1 values from checkpoint, the
validated numeric model-input unit, ordered-domain, numeric-input, and
public-decision identities. The PyTorch model receives only numeric
state/candidate tensors. The runner retains routing keys outside the model,
validates exactly N finite scores, resolves the deterministic selection, and
computes the selection-envelope response identity.

A request is single-use. Stale, duplicate, late, wrong-domain, wrong-input,
wrong-checkpoint, wrong-length, non-finite, malformed, invalid-selection, or
undersized-capacity responses return structured failure and never invoke a
Teacher, RandomLegal, heuristic, or candidate fallback.

## CUDA preflight and sequencing

Task 4A provides a preflight that requires CUDA, one device, cuda:0, and the
expected NVIDIA GeForce RTX 4060 Ti. It verifies the selected device contract
without starting optimization. If unavailable or mismatched, it returns
CUDA_UNAVAILABLE/CUDA_DEVICE_MISMATCH and zero optimizer steps.

Task 4A's tests may exercise model/codec behavior on CPU and may use injected
device probes. They do not claim the smoke gate. Task 4B alone may run exactly
one CUDA smoke of at most 500 optimizer steps, then export/reload and emit
acceptance evidence.

## Verification boundary

Task 4A proves codec round trips, digest/mutation rejection, config identity,
numeric finite rules, model exact N-to-N forward behavior, capacity rejection,
request/response binding, fresh checkpoint reload, no fallback, public/private
surface checks, and CUDA preflight. It reports no optimizer run and no
gameplay/evaluation result.
