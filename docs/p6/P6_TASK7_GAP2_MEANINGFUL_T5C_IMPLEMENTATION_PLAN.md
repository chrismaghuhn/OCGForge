# Phase 6 Task7 GAP2 meaningful Task5C implementation plan v1

## Status and scope

```text
STATUS=PROPOSED / pending independent review
DESIGN_ONLY=YES
IMPLEMENTATION_STARTED=NO
MEANINGFUL_EVALUATION_EXECUTED=NO
TASK7_TRAINING_AUTHORIZED=NO
```

This plan accompanies
`P6_TASK7_GAP2_MEANINGFUL_T5C_CONTEXT_CONTRACT.md`. It specifies the smallest
future implementation for the accepted meaningful fixed-matchup Task5C path.
It does not implement that path, alter an accepted contract, create a
checkpoint or dataset, run meaningful gameplay, or authorize Task7 training.

Semantic ownership remains with the existing Task5A codec, checkpoint and
inference contracts, EpisodicEnvironment V2, trajectory recorder, semantic
replay, and admission authority. This document is an implementation plan, not
an additional identity or gameplay authority.

```text
GAP1_CLOSED=YES
GAP2_IMPLEMENTATION_STARTED=NO
GAP2_IMPLEMENTATION_AUTHORIZED=NO
TASK7_READINESS=BLOCKED
TASK7_AUTHORIZED=NO
TASK7_STARTED=NO
```

## 1. Audited starting point and baselines

The plan is based on the exact accepted Main head and successful PR #59
post-merge workflow:

```text
BASE=122948309eac068f66a85dc9f51d4390fdf55f74
POST_MERGE_CI_RUN=33928774672
POST_MERGE_CI_HEAD=122948309eac068f66a85dc9f51d4390fdf55f74
POST_MERGE_CI=completed/success
```

Live audit findings:

* `tools/phase6/task5_codec.py` owns the accepted canonical job, corpus,
  job-manifest, root-evaluation, and evaluation-manifest encodings. It accepts
  the meaningful profile/kind at DTO level, but currently exposes only
  `implementation_acceptance_jobs()` and only fixes the eight-job length for
  that smoke profile.
* C++ Task5C currently hard-codes the implementation-acceptance profile,
  smoke checkpoint, and exact eight-job coordinate vector. Its smoke factory
  and evaluator must remain strict and separate from a future meaningful path.
* `tools/phase6/task5_offline.py` and `tools/phase6/task5_audit.py` consume
  supplied typed contexts generically; they are not additional schedule or
  identity authorities.
* Task7 Gap1 is integrated and accepted. Its exact materialization schema and
  configuration, never Task4A's float projection, are the future model-input
  bridge.

The required understanding regressions were run against this base before the
docs-only change:

```text
ctest --test-dir build/dev-windows --output-on-failure -j 1 `
  -R "^(phase6_task5c_gameplay_test|logical_model_input_test|card_vocabulary_test|encoded_model_input_test|model_batch_layout_test|model_supervision_sample_test|phase6_task7_input_materialization_test)$"
→ 7/7 PASS

python -m tests.phase6.phase6_task7_input_materialization_test \
  build/dev-windows/phase6_task7_materialization_probe.exe \
  build/dev-windows/phase6_task7_input_materialization_test.exe
→ 10/10 PASS

python -m unittest -v \
  tests.phase6.phase6_task4a_codec_test \
  tests.phase6.phase6_task4a_model_test \
  tests.phase6.phase6_task4a_inference_test \
  tests.phase6.phase6_task4a_cuda_preflight_test
→ 24/24 PASS
```

The combined CTest result includes the five Phase-5 model/layout tests, the
existing Task5C implementation-acceptance test, and the Task7 materialization
test. These are baseline facts, not evidence that the future meaningful path
exists.

## 2. Frozen meaningful schedule

The companion context contract freezes:

```text
MEANINGFUL_FIXED_MATCHUP_PROFILE=
ocgforge.phase6.task5.evaluation_corpus.meaningful_fixed_matchup.v1
MEANINGFUL_FIXED_MATCHUP_KIND=MEANINGFUL_FIXED_MATCHUP
MEANINGFUL_SEED_VECTOR=[1,2]
MEANINGFUL_JOB_COUNT=16
MEANINGFUL_JOB_ORDER=seed_then_placement_then_starting_player
```

The four literal placement rows are:

| Placement | Seat 0 | Seat 1 | Evaluated policy | Opponent Teacher | Environment assignment |
| ---: | --- | --- | --- | --- | --- |
| 0 | Swordsoul Tenyi | Salamangreat | P0 / Swordsoul | P1 / Salamangreat | `Normal` |
| 1 | Swordsoul Tenyi | Salamangreat | P1 / Salamangreat | P0 / Swordsoul | `Normal` |
| 2 | Salamangreat | Swordsoul Tenyi | P0 / Salamangreat | P1 / Swordsoul | `Mirror` |
| 3 | Salamangreat | Swordsoul Tenyi | P1 / Swordsoul | P0 / Salamangreat | `Mirror` |

The job vector is generated exactly as:

```text
for seed in [1, 2] in vector order:
    for placement in [0, 1, 2, 3] in vector order:
        for starting_player in [0, 1] in vector order:
            emit one GAMEPLAY EvaluationJobV1
```

Thus 2 × 4 × 2 = 16 jobs. The four placements are the smallest bounded
product that exercises both deck seatings and both evaluated-policy
seat/role assignments, while the two seeds and two starting players exercise
the accepted deterministic dimensions. This is a replayable fixed-matchup
baseline, not a statistically powered strength or arbitrary-deck claim.

The exact locked matchup remains:

```text
MATCHUP=ocgforge.matchup.swordsoul_salamangreat.v1
RULES_BUNDLE=3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
FORMAT=TCG_ADVANCED_2026_05_18
DUEL_MODE=DUEL_MODE_MR5
DUEL_FLAGS=190464
SWORDSOUL=ocgforge.swordsoul_tenyi.ml_v1
SWORDSOUL_SHA256=8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7
SALAMANGREAT=ocgforge.salamangreat.ml_v1
SALAMANGREAT_SHA256=6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188
```

No new seed generator, deck, rule bundle, format, or Teacher artifact is
allowed.

## 3. Invariants and stop conditions

Before environment creation, the future implementation must prove:

1. Every job is a `GAMEPLAY` job whose profile/kind, fixed bindings, role/seat
   fields, seed, starting player, checkpoint identity, evaluator version, and
   immutable source commit recompute using the existing T5A field order.
2. The ordered 16-job vector is identical at job, corpus, job-manifest, and
   root-context layers. Hash-map order and worker completion order are never
   semantic order.
3. Gameplay jobs have absent `source_dataset_identity` and
   `dataset_split_identity`; those facts remain checkpoint provenance rather
   than a second gameplay-membership authority.
4. The supplied immutable CardVocabulary identity equals the checkpoint-bound
   vocabulary. The smoke vocabulary is not a default.
5. The checkpoint is a real immutable non-smoke identity validated by the
   accepted checkpoint/inference boundary. `latest`, path, branch, all-zero
   digest, and the smoke checkpoint are rejected.
6. Task7 exact materialization is the only model-input bridge. Task4A's
   `state_rows[*,8]` projection cannot be reached from this path.
7. Normal recorder → semantic replay → admission is mandatory before any
   result is trusted; existing status and failure attribution are preserved.

Any failed proof rejects the complete context. No partial schedule, repair,
fallback policy, row removal, or retry under another policy is permitted. If
the accepted checkpoint loader cannot expose the required proof without a new
identity grammar, the future implementation stops with
`STATUS=BLOCKED_CONTRACT_EXECUTABILITY`.

## 4. Future file map

The smallest expected future implementation delta is:

```text
FUTURE_PRODUCTION_FILES=3
  include/ygo/phase6/task5c_gameplay.hpp
  src/phase6/task5c_gameplay.cpp
  tools/phase6/task5_codec.py

FUTURE_TEST_FILES=2
  tests/phase6/phase6_task5c_gameplay_test.cpp
  tests/phase6/phase6_task5_codec_test.py

FUTURE_DOC_FILES=0
FUTURE_TOTAL_FILES=5
```

No change is planned for `task5_offline.py` or `task5_audit.py`; both were
audited as generic. If that proves false, stop rather than introduce a third
schedule or identity authority. The two current design documents are
normative inputs, not future production files.

The loader-only issuer of `MeaningfulCheckpointBindingV1` is an external
prerequisite of this five-file T5A/T5C slice, not a caller-filled DTO supplied
by the evaluator. If the accepted future Task7 checkpoint/inference loader
does not expose this capability, the meaningful path must remain uninstantiable
until that owner is separately available; the five-file implementation may not
replace it with identity-string checks or a public `validated=true` field.

## 5. Checkpoint, vocabulary, and Task7 binding

### 5.1 Opaque validated checkpoint binding capability

The accepted future Task7 checkpoint/inference loader remains owner of
manifest schema, canonical manifest bytes, export content, and checkpoint
identity. Before the meaningful factory runs, it must issue a loader-owned
validated capability named:

```text
MeaningfulCheckpointBindingV1
```

```text
BINDING_OWNER=accepted future Task7 checkpoint/inference loader
T5C_ROLE=consumer only
```

The issued capability carries these immutable validated facts. The
`checkpoint_manifest_validated=true` condition is an internal issuance
invariant, not a public field a caller may set:

```text
checkpoint_identity
checkpoint_manifest_validated=true (internal capability invariant)
model_architecture_config_identity
phase5_logical_model_input_contract_identity
phase5_encoded_model_input_contract_identity
phase5_batch_layout_contract_identity
card_vocabulary_identity
dataset_identity
dataset_split_identity
training_contract_identity
canonical_weight_export_codec_identity
canonical_weight_content_identity
task7_materialization_schema_id
task7_materialization_config_identity
```

Only the accepted future Task7 checkpoint/inference loader may issue this
capability, and only after it has parsed the canonical checkpoint artifact,
recomputed the manifest/content identities, validated the architecture,
Phase-5 contracts, concrete vocabulary, DatasetManifest/split, training
contract, export codec/content, and Task7 materialization configuration. The
capability has no public/default constructor, no public mutation, and no
identity-only conversion. T5C receives immutable accessors and cannot
synthesize a capability from strings, digests, a checkpoint path, or a
`validated=true` value. A test-only friend/access seam may exercise T5C's
consumer-side rejection checks, but a test-built capability is never evidence
that a real Task7 checkpoint was loaded.

The capability is a validated view, not a second manifest codec or digest.
Dataset and split remain transitive checkpoint provenance and remain absent
from gameplay job optionals.

The proof checks the nonzero `phase6_checkpoint.v1.` identity, rejects the
smoke checkpoint, proves manifest/export/content validation, checks all Phase-5
contracts, requires paired dataset/split identities, and proves the
architecture consumes:

```text
task7_materialization_schema_id=
ocgforge.phase6.task7.input_materialization.v1
task7_materialization_config_identity=
phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a
```

### 5.2 Separate context and evaluator APIs

The future C++ surface is explicitly separate from the smoke surface.
The capability is opaque at this boundary; the notation below is an interface
sketch, not a public aggregate initializer:

```cpp
class MeaningfulCheckpointBindingV1 final {
public:
    MeaningfulCheckpointBindingV1(const MeaningfulCheckpointBindingV1&) = default;
    MeaningfulCheckpointBindingV1& operator=(
        const MeaningfulCheckpointBindingV1&) = default;

private:
    MeaningfulCheckpointBindingV1(/* loader-issued validated state */);
    friend class AcceptedTask7CheckpointInferenceLoader;
    friend struct Task7CheckpointBindingTestAccess; // test-only consumer seam
};

struct MeaningfulFixedMatchupContextV1 final {
    EvaluationContextV1 evaluation_context;
    MeaningfulCheckpointBindingV1 checkpoint_binding;
};

MeaningfulFixedMatchupContextV1 make_meaningful_fixed_matchup_context(
    MeaningfulCheckpointBindingV1 checkpoint_binding,
    const model::CardVocabularyV1& concrete_vocabulary,
    std::string evaluator_semantic_source_commit);

FrozenGameplayEvaluatorCreateResult
create_meaningful_frozen_gameplay_evaluator(
    MeaningfulFixedMatchupEvaluatorConfigV1 config) noexcept;
```

These names and separation are part of the design. The loader-only issuer
is outside the T5C consumer API; no public/default constructor or mutator may
be exposed to a context caller. A test-only friend/access seam may create a
capability-shaped value solely to exercise consumer-side rejection paths, but
that seam cannot validate a real checkpoint or establish
`REAL_TASK7_CHECKPOINT_BINDING=PASS`. Private execution helpers may be shared, but the
public smoke validator may not become permissive. The existing
`make_implementation_acceptance_context(...)` and
`create_frozen_gameplay_evaluator(...)` retain their exact smoke checkpoint,
profile, and eight-job behavior.

The meaningful factory validates the proof and vocabulary, constructs the
literal schedule, sets every job to the same future checkpoint and source
commit, leaves gameplay dataset optionals absent, recomputes all T5A-compatible
job/corpus/manifest/root identities, then returns no context on mismatch.

### 5.3 T5A codec delta

`tools/phase6/task5_codec.py` should add a public
`meaningful_fixed_matchup_jobs(...)` helper taking the required future
checkpoint identity and evaluator source commit. It must emit the exact 16
jobs, including evaluated-policy seat/role fields, and reuse the existing
canonical encoders and identity functions. It must add meaningful-profile
schedule validation while preserving `implementation_acceptance_jobs()`
byte-for-byte and semantically. It must reject a smoke checkpoint and never
sort by computed identity.

No second hashing grammar, manifest codec, or offline/audit schedule owner is
allowed.

## 6. C++ Task5C implementation sequence

1. Add meaningful profile/kind and Task7 schema/config constants plus the
   typed binding/context DTOs; keep smoke defaults unchanged.
2. Add a private placement-aware job builder and meaningful validator. It must
   set both deck-seat roles, evaluated/opponent seats and roles, exact Teacher
   artifacts/bindings, seed, starting player, future checkpoint, and source
   commit from the frozen vector.
3. Validate the full cross-artifact graph using T5A-compatible bytes: no mixed
   checkpoint, wrong root/corpus/job-manifest identity, duplicate/reordered/
   missing/extra job, nonempty gameplay dataset/split, or mutable checkpoint.
4. Require concrete CardVocabulary equality with the validated checkpoint proof
   and require the exact Task7 materialization schema/config compatibility.
5. Add the separate meaningful evaluator constructor. Its configuration must
   contain the proof, immutable vocabulary, checkpoint-bound PolicyArtifact,
   certified environment, inference provider, provenance resolver, and run
   control.
6. Reuse `run_one_job`-style execution only after validation. Keep
   `GameplayJobStatus`, `ReplayAdmissionStatus`, and
   `GameplayFailureStage` unchanged. Preserve inference-vs-opponent failure
   attribution and the recorder → replay → admission sequence.

The inference provider receives only the accepted public observation and
complete candidate vector, creates Phase-5 logical/encoded input, invokes
Task7 exact materialization and the future PyTorch adapter, and returns exactly
N finite scores bound to the existing request and selected public key. It does
not query `CoreHost`, build legality, feature-engineer, or use a fallback.

## 7. Future test matrix

Only the two test files in the file map are expected to change.

### 7.1 Python codec tests

The Task5 codec tests must prove:

```text
meaningful profile/kind accepted
exact 16 jobs and exact seed/placement/start order
both deck seatings and both evaluated policy seats/roles
role-specific Teacher artifact/binding/profile/producer/sampling/no-RNG values
smoke helper/identities unchanged
smoke checkpoint rejected by meaningful helper
non-smoke checkpoint rejected by smoke helper
wrong profile/kind/matchup/rules/deck/hash/Teacher/source fields rejected
seed omitted/added/duplicated/reordered rejected
job omitted/added/duplicated/reordered rejected
checkpoint mismatch/mixed checkpoints rejected
vocabulary/config mismatch rejected
gameplay dataset/split optionals rejected
one changed job changes its identity and aggregates
an unchanged job keeps its identity when another job changes
fresh processes reproduce all identities byte-for-byte
```

### 7.2 C++ Task5C tests

`phase6_task5c_gameplay_test.cpp` must add witnesses for:

| Area | Required proof |
| --- | --- |
| Context | meaningful profile/kind and exact 16-job vector accepted |
| Smoke isolation | smoke path unchanged; meaningful path rejects smoke; smoke path rejects meaningful/non-smoke |
| Fixed bindings | wrong matchup/rules/format/mode/flags/deck IDs/hashes rejected |
| Teacher | wrong role artifact/binding/profile/producer/sampling/no-RNG rejected |
| Checkpoint | loader-issued capability required; malformed/mixed/mismatched checkpoint, artifact, provenance, export, or Task7 config rejected |
| Schedule | seed/placement/start/job omission/addition/duplicate/reorder and wrong seat/role rejected |
| Vocabulary | checkpoint-bound concrete vocabulary mismatch rejected; no smoke fallback |
| Identity | changed semantic job changes affected and aggregate identities; unrelated job identity stable |
| Inference | Task7 provider binding, exact N scores, N=1/24/25/129, no fallback/split/truncation |
| Failure | inference remains inference; opponent Teacher remains environment; replay/admission/quarantine remain distinct |
| Privacy | real paired hidden worlds give equal Phase-5 inputs, Task7 bytes/tensors/masks/offsets, routing sidecars, score cardinality, selected key |
| Determinism | fresh processes preserve identities, request/response bindings, and result order |
| Regression | existing Phase-5, Task7, and eight-job smoke tests remain green |

The paired-world witness must use the established
`EpisodicEnvironmentTestAccess::project_frame_for_test` path, not merely two
synthetic DTOs. N=1 remains an externally supplied one-candidate domain; N=24,
25, and 129 remain complete ordered domains.

### 7.3 Failure matrix and privacy

The future tests must also exercise unknown schema, wrong profile/kind,
identity mismatch, invalid inference response/selected key, checkpoint or
vocabulary detachment, forbidden fallback, nonempty gameplay dataset fields,
replay/admission failure, quarantine, and all fixed-binding mutations. Public
diagnostics contain no private locator, semantic key, response bytes, hidden
card, pointer, or filesystem path. No new engine query is permitted.

## 8. Dependency order and commands

The future execution order is:

```text
T5A meaningful schedule helper/tests
  → C++ DTO/constants/validator
  → checkpoint/vocabulary/Task7 binding proof
  → separate evaluator constructor
  → Task7 provider integration
  → paired-world/fresh-process/failure gates
  → existing regressions
```

No meaningful gameplay or training occurs before every prior proof passes. The
future gates are:

```text
ctest --test-dir build/dev-windows --output-on-failure -j 1 `
  -R "^(phase6_task5c_gameplay_test|logical_model_input_test|card_vocabulary_test|encoded_model_input_test|model_batch_layout_test|model_supervision_sample_test|phase6_task7_input_materialization_test)$"
python -m unittest -v tests.phase6.phase6_task5_codec_test
python -m unittest -v tests.phase6.phase6_task4a_codec_test tests.phase6.phase6_task4a_model_test tests.phase6.phase6_task4a_inference_test tests.phase6.phase6_task4a_cuda_preflight_test
fresh-process identity/materialization probes
git diff --check
```

The future implementation must report, from executed evidence,
`MEANINGFUL_PROFILE_SUPPORTED=YES`, `IMPLEMENTATION_ACCEPTANCE_PROFILE_UNCHANGED=YES`,
`CHECKPOINT_CONSUMER_BINDING=PASS`, `REAL_TASK7_CHECKPOINT_BINDING=NOT_RUN`,
`CARD_VOCABULARY_BOUND=YES`,
`TASK7_MATERIALIZATION_BOUND=YES`, `CANDIDATE_N_TO_N=PASS`,
`NO_FALLBACK=PASS`, `REPLAY_ADMISSION_REQUIRED=YES`,
`PRIVACY_SEMANTICS_CHANGED=NO`, `GAMEPLAY_SEMANTICS_CHANGED=NO`,
`LEGALITY_CHANGED=NO`, and `TASK4_SMOKE_PATH_CHANGED=NO`.

## 9. Non-goals and closure semantics

This plan does not authorize Task7 model/architecture work, scorer, embeddings,
loss, optimizer, training, DatasetManifest or split generation, checkpoint
creation/selection, meaningful gameplay, Task5D evidence, RL, self-play,
recurrent/world models, search, Meta-8, JAX, arbitrary-deck expansion, or any
new gameplay/legality/observation/candidate/replay/admission authority.

A future accepted implementation may establish:

```text
GAP2_IMPLEMENTATION_ACCEPTED=YES
```

It does not establish training authorization. After GAP1 and GAP2 are both
integrated and independently accepted, a separate Task7 readiness and
execution-contract decision is required.

No future checkpoint, job, corpus, root, or evaluation identity is issued by
this design. The schedule template becomes concrete only after a real
checkpoint binding and immutable evaluator source commit exist.

## 10. Design-task acceptance state

```text
MEANINGFUL_PROFILE=RESOLVED
MEANINGFUL_KIND=RESOLVED
MEANINGFUL_SEED_VECTOR=[1,2]
MEANINGFUL_JOB_COUNT=16
MEANINGFUL_JOB_ORDER=RESOLVED
CHECKPOINT_BINDING=RESOLVED_AS_LOADER_ISSUED_CAPABILITY
MEANINGFUL_CHECKPOINT_BINDING_INTERFACE=PASS
REAL_TASK7_CHECKPOINT_BINDING=NOT_RUN
CARD_VOCABULARY_BINDING=RESOLVED
TEACHER_BINDING=RESOLVED
CORPUS_IDENTITY_DERIVATION=RESOLVED_VIA_T5A
REPLAY_ADMISSION_PATH=RESOLVED
FAILURE_ATTRIBUTION=RESOLVED
FUTURE_IMPLEMENTATION_FILE_MAP=RESOLVED
FUTURE_TEST_MATRIX=RESOLVED

FUTURE_PRODUCTION_FILES=3
FUTURE_TEST_FILES=2
FUTURE_DOC_FILES=0
FUTURE_TOTAL_FILES=5

CURRENT_DESIGN_FILES_CHANGED=2
CURRENT_PRODUCTION_CODE_CHANGED=NO
CURRENT_TEST_CODE_CHANGED=NO
CURRENT_MEANINGFUL_EVALUATION_EXECUTED=NO
```

Independent review may accept or request changes to this proposed schedule
and API surface. Until that review and a separately authorized implementation,
the meaningful path is design-only and Task7 remains blocked.
