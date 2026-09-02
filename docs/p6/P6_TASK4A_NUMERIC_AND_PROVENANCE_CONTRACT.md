# OCGForge Phase 6 Task 4A Numeric, Provenance, and CUDA Preflight Contract

## Status and scope

**Status:** CURRENT / AUTHORIZED — Phase-6 Task 4A prerequisite contract.

This document supplements the Phase-6 Task-1 checkpoint/inference contract with
the exact numeric and canonicalization boundaries required before a training
run or checkpoint can be accepted. It defines the provisional Task-4
architecture/configuration identities, derived smoke-corpus artifact, and CUDA
preflight. It executes zero optimizer steps and does not authorize Task 4B,
Task 5, a primary-backend decision, or a gameplay evaluation.

## 1. Contract identities

The following identities are fixed for Task 4A:

| Surface | Identity |
| --- | --- |
| inference numeric comparison | `ocgforge.phase6.inference_numeric.v1` |
| canonical numeric scalar | `ocgforge.phase6.numeric.f32_ieee754_be.v1` |
| Task-4 numeric projection | `ocgforge.phase6.task4.numeric_projection.v1` |
| provisional BC architecture config | `ocgforge.phase6.bc_architecture_config.v1` |
| provisional architecture identity prefix | `phase6_architecture_config.v1.` |
| canonical weight export | `ocgforge.phase6.canonical_weight_export.v1` |
| canonical weight-content identity prefix | `phase6_weight_content.v1.` |
| Task-4 smoke corpus | `ocgforge.phase6.task4.smoke_corpus.v2` |
| smoke-corpus derivation | `ocgforge.phase6.task4.numeric_projection.v1` |
| smoke-corpus artifact identity prefix | `phase6_corpus_artifact.v2.` |
| smoke-corpus authority sidecar | `ocgforge.phase6.task4.corpus_authority.v1` |
| smoke-corpus authority identity prefix | `phase6_corpus_authority.v1.` |
| training-run manifest | `ocgforge.phase6.training_run.v1` |
| training-run identity prefix | `phase6_training_run.v1.` |
| architecture config sub-identity | `ocgforge.phase6.bc_architecture_config.v1` |
| optimizer config sub-identity | `ocgforge.phase6.optimizer_config.v1` |
| schedule config sub-identity | `ocgforge.phase6.schedule_config.v1` |
| batch config sub-identity | `ocgforge.phase6.batch_config.v1` |
| gradient accumulation sub-identity | `ocgforge.phase6.gradient_accumulation.v1` |
| RNG/initialization sub-identity | `ocgforge.phase6.rng_initialization.v1` |
| precision sub-identity | `ocgforge.phase6.precision.v1` |
| deterministic execution sub-identity | `ocgforge.phase6.determinism_config.v1` |
| execution provenance sub-identity | `ocgforge.phase6.execution_provenance.v1` |
| CUDA preflight | `ocgforge.phase6.cuda_preflight.v1` |

These are OCGForge semantic/provenance names, not PyTorch package names.
PyTorch remains the provisional Task-4 execution backend and is not selected as
the primary Phase-6 backend.

## 2. Canonical primitive and numeric rules

Task-4A canonical bytes use the accepted primitive encoding:

```text
u8/u16/u32/u64: unsigned big-endian
string: u32be byte length || UTF-8 bytes
vector: u32be count || entries in declared order
optional: presence:u8 || value when present
```

The canonical numeric codec is
`ocgforge.phase6.numeric.f32_ieee754_be.v1`:

- each scalar is exactly one IEEE-754 binary32 value;
- canonical bytes are the four raw binary32 bits in big-endian order;
- signed zero preserves its raw bits;
- NaN, positive infinity, and negative infinity are invalid;
- no implicit decimal normalization, platform-native byte order, or tolerance
  is part of canonical bytes;
- score vectors are in exact supplied candidate order and have exactly N
  entries;
- Task-4A action selection compares the decoded finite binary32 values exactly;
- equal numeric scores use bytewise-ascending existing
  `public_action_key` values;
- the response identity remains the Task-1 selection envelope and excludes
  score bytes;
- any future numeric tolerance that could change the selected key is an
  ambiguity failure, not an accepted backend difference.

The state/candidate numeric projection is intentionally a provisional,
lossy execution representation: normalizing wide integer fields into
binary32 values can collapse distinct large source values. This is accepted
for the Task-4A smoke only. It never replaces the exact Phase-5 model-input
identity, source sample identity, candidate identity, or public action key.
A future non-smoke baseline may introduce an exact limb-based projection only
under a new projection identity.

Cross-backend score comparison is diagnostic only until another backend uses
this same numeric contract. A score mismatch does not authorize a different
candidate or a fallback policy.

## 3. Provisional architecture configuration

Task 4A uses one deliberately small, provisional architecture. It is not a
final Phase-6 architecture and is not an action-family authority:

```text
state numeric rows [variable count, 8]
    -> Linear(8, 16) + ReLU
    -> mean/max pooling
    -> shared state representation [32]

candidate numeric row [28] + shared state representation [32]
    -> Linear(60, 16) + ReLU
    -> Linear(48, 1)
    -> one scalar candidate score
```

The architecture configuration canonical fields, in this order, are:

```text
identity domain:string = ocgforge.phase6.bc_architecture_config.v1
identity schema:string = ocgforge.phase6.bc_architecture_config.v1
architecture name:string = provisional_mean_max_candidate_scorer
numeric contract identity:string
projection contract identity:string
state row width:u32 = 8
state hidden width:u32 = 16
candidate row width:u32 = 28
candidate hidden width:u32 = 16
state pooling identity:string = mean_max.v1
activation identity:string = relu.v1
parameter order:vector<string>
```

The parameter order is exactly:

```text
state_encoder.input.weight
state_encoder.input.bias
candidate_encoder.input.weight
candidate_encoder.input.bias
score_head.weight
score_head.bias
```

The architecture identity is:

```text
phase6_architecture_config.v1.<lowercase SHA-256 of those canonical bytes>
```

The framework name, device, CUDA version, optimizer, worker count, and physical
module path are not architecture-config inputs.

## 4. Numeric feature projection

The Task-4 projection consumes the Task-3 state-only and namespaced candidate
surfaces and emits no strings to the PyTorch network.

State rows have width 8 and use explicit field tags, presence values, accepted
numeric state values, and state-locator ordinals. Candidate rows have width 28
and use action/choice/reference/optional values plus the explicit
`State`/`CandidateOnly` locator namespace ordinals. Values wider than
binary32 are represented by explicitly ordered high/low components before
binary32 conversion. All rows are emitted in source order.

The projection MUST NOT emit:

```text
public_action_key
routing key
raw locator string
candidate ordinal
internal semantic key
response bytes
CoreHost or PlayerObservation value
hidden/private card identity
```

The projection is a derived execution representation. Its identity binds its
version and architecture config but does not replace DatasetManifest,
trajectory, sample, public-action, or model-input identity.

Before inference, the projection is carried by one validated
`Task4NumericModelInputV1` unit. That unit binds the source `model_input_identity`,
the Phase-5 `public_candidate_domain_digest` when present (otherwise the
versioned Task-4 fallback domain identity), public decision context, exact
state/candidate numeric rows, and the ordered routing sidecar. Its own derived
identity is an integrity check over those fields; it is not a second dataset
authority. Requests and inference may not combine an identity from one unit
with rows from another.

## 5. Derived smoke-corpus artifact

The C++ corpus probe is the only Task-4A corpus producer. Its fixed job set is
declared in code and is never changed by searching for a favorable split. Each
job uses the accepted Teacher-v1 source identities, the canonical fixed
matchup, a fixed root seed, starting-player value, and semantic-action budget
of one. If the fixed set produces no train sample, the probe fails closed.

The binary artifact body has this exact field order:

```text
schema identity:string = ocgforge.phase6.task4.smoke_corpus.v2
source dataset identity:string
split identity:string
derivation contract identity:string
CardVocabulary identity:string
episode identity vector:string in unsigned UTF-8 order
sample count:u32
sample:
  bc sample identity:string
  trajectory record identity:string
  episode identity:string
  public semantic decision identity:string
  model-input identity:string
  Phase-5 public candidate-domain digest:optional string
  perspective player:u8
  decision index:u64
  selected public action key:string       // control sidecar only
  partition token:string
  candidate ordinal:u32
  ordered candidate-domain identity:string
  state row count:u32
  state row[8]:f32 canonical values
  candidate count:u32
  candidate row[28]:f32 canonical values in source order
  routing-key count:u32
  routing key:string in source order       // control sidecar only
```

All strings use the canonical length prefix. The derived artifact content
identity is:

```text
phase6_corpus_artifact.v2.<lowercase SHA-256 of the complete artifact body>
```

The digest detects mutation of numeric rows or provenance fields but is not
DatasetManifest authority. `decode_corpus_artifact` performs only strict
schema/canonical/content validation. `admit_corpus_artifact` additionally
requires an independently supplied expected artifact identity, source
DatasetManifest identity, split episode vectors/identity, vocabulary identity,
and the admitted Task-2 source-sample identity set. It recomputes every
sample identity, the Phase-5-or-fallback ordered-domain identity, every
episode partition, and exact source-sample/model-input membership. A newly
hashed self-consistent artifact is not admitted without that authority
context.

The C++ probe emits the authority sidecar as a separate artifact, built
directly from the validated `DatasetManifest`, Task-2 split, and admitted
`Phase6BcSampleV1` values before any physical corpus decoding. A positive
admission result must consume that sidecar (or equivalent trusted authority
values) and the exact expected corpus artifact identity; the decoded corpus
can never be used to manufacture its own positive authority.

## 6. Configuration sub-identities

Every Task-4A configuration value is canonicalized before it can be referenced
by a training-run manifest:

- optimizer config uses Adam, learning rate `0.001`, beta1 `0.9`, beta2
  `0.999`, epsilon `1e-8`, and weight decay `0.0`, each as canonical
  binary32 values;
- schedule config is the explicit no-schedule identity;
- batch config declares the real-sample loss normalization
  `mean_per_real_sample`, no semantic padding rows, ascending
  `bc_sample_identity` order, `shuffle=false`, and the deterministic
  `step_i_modulo_train_sample_count` schedule;
- optimizer config declares Adam with `foreach=false`, `fused=false`,
  `amsgrad=false`, `maximize=false`, `capturable=false`, and
  `differentiable=false`;
- gradient-accumulation config declares accumulation count `1`;
- RNG/initialization config declares the versioned PyTorch CPU/CUDA manual-seed
  contract and seed `1729`;
- precision config declares binary32 model/input/score execution;
- deterministic-execution config declares
  `torch.use_deterministic_algorithms(True, warn_only=False)` and
  `torch.set_float32_matmul_precision("highest")`;
- execution provenance declares backend identity, exact framework version,
  device type/index, GPU name, PyTorch CUDA build and the version reported by
  `torch.version.cuda`, capability, and distributed strategy/world size through
  structured canonical provenance bytes. The preflight does not claim an
  independently measured driver/runtime version. It produces this value and
  its identity; a training manifest records that exact identity rather than an
  arbitrary prefix string.

Each sub-identity uses its own domain/schema, fixed field order, canonical
primitive values, and lowercase SHA-256 digest. Execution provenance is
training provenance only and never enters checkpoint semantic identity.

## 7. TrainingRunManifestV1

Task 4A validates the complete field ownership and codec before Task 4B can
issue an accepted run identity. The canonical top-level field order is:

```text
schema_id
training_contract_identity
source_dataset_identity
dataset_split_identity
phase5_logical_model_input_contract_identity
phase5_encoded_model_input_contract_identity
phase5_batch_layout_contract_identity
card_vocabulary_identity
model_architecture_config_identity
behavior_policy_source_identities: sorted unique vector
opponent_policy_source_identities: sorted unique vector
training_code_commit
framework_backend_identity
framework_version
optimizer_configuration_identity
learning_rate_schedule_identity
batch_configuration_identity
gradient_accumulation_configuration_identity
training_rng_contract_identity
training_seed_or_initialization_identity
initial_checkpoint_identity:optional
precision_mode_identity
deterministic_execution_configuration_identity
device_and_distributed_provenance_identity
cuda_preflight_identity:optional; required when actual_optimizer_steps > 0
maximum_optimizer_steps:u32 = 500
actual_optimizer_steps:u32
final_exported_checkpoint_identity:optional for Task 4A
```

The identity is issued only when every required scalar/sub-identity is present,
the actual step count is in `0..500`, and every referenced codec validates:

```text
phase6_training_run.v1.<lowercase SHA-256 of canonical manifest bytes>
```

A Task-4A zero-step manifest is infrastructure/provenance test data, not
training acceptance evidence.

The generic/default manifest constructor is zero-step-only. A manifest with
`actual_optimizer_steps > 0` is valid only when its `cuda_preflight_identity`
was produced by the real CUDA preflight bridge and is bound to the same
structured execution-provenance facts.

For the fixed Teacher-vs-Teacher smoke corpus, both accepted Teacher-v1
artifact identities are required in `behavior_policy_source_identities` and
in `opponent_policy_source_identities`; the latter cannot be left empty merely
because the labels are sourced from the behavior side.

## 8. Canonical weight export

The canonical inference weight body has this exact order:

```text
identity domain:string = ocgforge.phase6.canonical_weight_export.v1
identity schema:string = ocgforge.phase6.canonical_weight_export.v1
numeric codec identity:string = ocgforge.phase6.numeric.f32_ieee754_be.v1
architecture config identity:string
tensor count:u32
for each tensor in the declared parameter order:
  tensor name:string
  rank:u32
  shape:u64be[rank]
  dtype identity:string = ocgforge.phase6.numeric.f32_ieee754_be.v1
  element count:u64
  byte length:u64
  exact f32 big-endian raw bytes
```

The canonical weight-content identity is:

```text
phase6_weight_content.v1.<lowercase SHA-256 of the complete weight body>
```

The exporter MUST reject wrong names/order/shapes/dtype, truncated/trailing
bytes, NaN, infinity, optimizer state, gradient state, sharding state, device
state, worker state, cache state, and framework-native serialization objects.

## 9. Checkpoint and inference binding

The OCGForge checkpoint manifest binds the Task-1 fields to the exact
architecture config, numeric/export codec, and canonical weight-content
identity. Hardware/framework execution provenance may be attached as a
sidecar, but never enters checkpoint semantic identity.

The inference numeric response contains exactly N finite binary32 scores in
source order. Request/response identities bind checkpoint, the one validated
numeric model-input unit, ordered candidate domain, and public semantic
decision, and the numeric-input identity. Response identity hashes only the canonical selection envelope. A
consumed request cannot be consumed again. An existing Phase-5
`public_candidate_domain_digest` is the ordered-domain identity; the
`phase6_ordered_candidate_domain.v1` digest is used only when the Phase-5
field is absent.

Wrong checkpoint/input/domain/decision, stale/late/duplicate response, wrong
length, non-finite score, invalid ordinal/key, malformed checkpoint, and
capacity smaller than N fail closed. No Teacher, RandomLegal, heuristic,
candidate-zero, first-candidate, retry, or CPU fallback is allowed.

## 10. CUDA preflight and Task-4B gate

Task 4A preflight requires:

```text
torch.cuda.is_available() == True
torch.cuda.device_count() >= 1
device == cuda:0
GPU name == NVIDIA GeForce RTX 4060 Ti
```

It records device type/index, GPU model, PyTorch version, PyTorch CUDA build,
the version reported by `torch.version.cuda`, capability, precision, and
distributed configuration as execution provenance. It does not claim a
separately measured driver/runtime version, create an optimizer, or execute a
step.

On failure it returns a structured CUDA failure with:

```text
CUDA_UNAVAILABLE or CUDA_DEVICE_MISMATCH
actual_optimizer_steps = 0
CPU_FALLBACK = FORBIDDEN
no checkpoint
```

Only a separately reviewed Task 4B authorization may execute exactly one real
CUDA smoke run with at most 500 optimizer steps, verify model/batch/logit/loss
placement on `cuda:0`, export the canonical checkpoint, reload it into a
fresh inference object, and produce smoke evidence. That Task-4B execution
provenance MUST also record `GPU_MEMORY_BEFORE`, `GPU_MEMORY_PEAK`, and
`GPU_MEMORY_AFTER` for the one run. These are diagnostic hardware/provenance
values only and never checkpoint semantic identity; their collection is not a
Task-4A optimizer or smoke run.

## 11. Non-goals

Task 4A does not authorize:

```text
optimizer steps or training
hyperparameter search
convergence claims
Task-5 gameplay/evaluation
PyTorch-primary selection
JAX rejection
RL, self-play, search, or broader decks
```
