# Phase 6 Task6 — PyTorch primary-backend readiness record

## Status

```text
TASK=P6_TASK6_PYTORCH_PRIMARY_BACKEND_READINESS_01
BASE=c3959d2812b679cbc78f54dbbd089787f0f32963
REMOTE_BASE_VERIFIED=c3959d2812b679cbc78f54dbbd089787f0f32963
BRANCH=chris/phase6-task6-pytorch-readiness

PHASE6_PRIMARY_BACKEND_DIRECTION=PYTORCH
JAX_STATUS=DEFERRED_CANDIDATE
JAX_REJECTED=NO
JAX_PHASE6_REQUIREMENT=NO
JAX_IMPLEMENTATION_AUTHORIZED=NO
PYTORCH_DOES_NOT_BECOME_SEMANTIC_AUTHORITY=YES

TASK7_FRAMEWORK=PYTORCH
TASK7_FRAMEWORK_VERSION=2.12.1+cu126
TASK7_READINESS=BLOCKED
TASK6_READINESS=BLOCKED_BY_IMPLEMENTATION_GAP
TASK6_TECHNICAL_REVIEW=PASS
TASK6_INDEPENDENT_REVIEW=PASS
TASK6_FINAL_PASS=YES
TASK7_AUTHORIZED=NO
TASK7_STARTED=NO

TASK6_SELF_FINAL_PASS=NO
TASK6_STATUS=FINAL_PASS_AFTER_INDEPENDENT_REVIEW
```

This record reflects the bounded Task6 audit and the independent technical
review PASS. No Task7 work is authorized by this record.

## Verified Task4B provenance

The accepted Task4B execution report and recovery record were inspected, and
the ten original historical files were rehashed against the recovery contract
anchors. The observed values are:

| Field | Result |
| --- | --- |
| Task4 backend identity | `ocgforge.phase6.backend.pytorch.provisional.v1` |
| `torch.__version__` | `2.12.1+cu126` |
| `torch.version.cuda` | `12.6` |
| device | `cuda:0` |
| device count | `1` |
| GPU provenance | `NVIDIA GeForce RTX 4060 Ti`, capability `8.9` |
| distributed provenance | `single_process`, world size `1` |
| deterministic provenance | strict algorithms, `warn_only=false`, matmul precision `highest` |
| historical smoke checkpoint | `phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327` |
| historical smoke evidence | `phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe` |
| original historical status | `SMOKE_PASS=true`, `TASK4B_PASS=false` |
| additive recovery status | `TASK4B_RECOVERY_PASS=true`, `TASK4B_FINAL_PASS=true` |

The accepted execution report records `actual_optimizer_steps=500`; this is
historical evidence only. No Task6 training or CUDA smoke was run. The
accepted environment also has no independently measured driver/runtime value
in the contract, so none is claimed.

## Readiness audit

| Audit item | Result | Evidence inspected | Notes |
| --- | --- | --- | --- |
| `TASK4_MODEL_PATH_REUSABLE_FOR_TASK7` | `NO` | `tools/phase6/task4_model.py`; `docs/p6/P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md` | The current model consumes the Task4A numeric projection. The contract calls that projection lossy and accepted for the Task4A smoke only. A non-smoke Task7 projection/materialization path is an unimplemented follow-up. |
| `TASK4_INFERENCE_PATH_REUSABLE_FOR_TASK7` | `YES` | `tools/phase6/task4_inference.py`; Task4A inference tests | Canonical request/response binding, exact score count, deterministic selection, freshness ledger, and no fallback are reusable at the adapter boundary once an accepted Task7 input projection exists. |
| `TASK4_CANONICAL_EXPORT_REUSABLE_FOR_TASK7` | `YES` | `tools/phase6/task4_inference.py`; checkpoint artifact decode/round-trip | Export contains only canonical inference weights and OCGForge manifest inputs; a future Task7 checkpoint receives a new identity. |
| `TASK4_CANONICAL_RELOAD_REUSABLE_FOR_TASK7` | `YES` | `load_checkpoint_for_inference`, `reload_model_from_checkpoint`; completion receipt | Strict architecture, Phase-5 contracts, vocabulary, dataset/split, export codec/content, and fresh model reload are validated. |
| `TASK5_EVALUATOR_REUSABLE_FOR_TASK7` | `NO` | `src/phase6/task5c_gameplay.cpp`; `docs/p6/P6_TASK5_EVALUATION_EXECUTION_CONTRACT.md` | Current T5C C++ context validation accepts only `IMPLEMENTATION_ACCEPTANCE`, the eight-job schedule, and the Task4 smoke checkpoint. The meaningful profile has no current T5C execution context. |
| `EXACT_N_TO_N_SCORING_PRESERVED` | `YES` | `task4_model.py`, `task4_inference.py`, Task4A/Task5 tests | One finite score per supplied candidate; physical width below N fails closed. |
| `VARIABLE_CANDIDATE_DOMAIN_PRESERVED` | `YES` | Task4A/Task5 contracts and model tests | No global action vocabulary or fixed legal-domain authority is introduced. |
| `SOURCE_ORDER_PRESERVED` | `YES` | Task4A codecs/inference and Task5 score-vector tests | Candidate rows, keys, scores, and tie resolution retain declared source order. |
| `PADDING_NON_SEMANTIC` | `YES` | `exact_domain_cross_entropy_from_padded`; Task4A/Task5 mask tests | Padding mask rows are excluded from semantic loss. |
| `NO_FALLBACK_PRESERVED` | `YES` | Task4A inference tests; Task5C failure tests | No Teacher, RandomLegal, heuristic, first-candidate, or retry path follows neural failure. |
| `CHECKPOINT_BINDING_PRESERVED` | `YES` | Task4A loader and Task5C policy binding | Checkpoint identity is validated before inference and evaluation. |
| `MODEL_INPUT_BINDING_PRESERVED` | `YES` | Task4A pending-execution sidecar and Task5B provider validation | Model-input/domain/decision identities bind to the exact rows and response. |
| `PUBLIC_ACTION_KEY_AUTHORITY_UNCHANGED` | `YES` | Phase-5 contracts, Task4 inference, Task5C evaluator | `public_action_key` remains existing Environment routing/selection identity. |
| `PRIVACY_BOUNDARY_UNCHANGED` | `YES` | Phase-5 public-boundary tests; Task4 projection/model code; Task5C paired-world tests | No hidden identity, raw engine value, response bytes, pointer, PID, path, or wall-time path is added. |
| `REPLAY_ADMISSION_BOUNDARY_UNCHANGED` | `YES` | Task5C evaluator and replay/admission tests | Trusted outcomes still require normal recording, semantic replay, and admission. |

### Blocking implementation gaps

1. `P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md` states that the Task4A
   state/candidate projection is intentionally lossy and accepted for the
   Task4A smoke only. `task4_model.py` is explicitly limited to those numeric
   surfaces. A meaningful Task7 run therefore needs a separately authorized
   non-smoke projection/materialization path and its own versioned projection
   identity. Task6 does not implement it.
2. `src/phase6/task5c_gameplay.cpp` validates the C++ evaluation context against
   `kImplementationAcceptanceProfile`, `kSmokeCheckpointIdentity`, and the
   exact eight-job schedule. `make_implementation_acceptance_context` rejects
   any non-smoke checkpoint. The Python T5A codec names a meaningful profile,
   but that does not make the current C++ gameplay evaluator consume a future
   Task7 checkpoint. A separately authorized T5C meaningful fixed-matchup
   context/job path is required. Task6 does not implement it.

These are implementation readiness gaps. Treating the generic codecs or smoke
fixture as already sufficient would overstate Task7 readiness.

## Task6 acceptance-gate audit

| Gate | Evidence inspected | Result | Notes |
| --- | --- | --- | --- |
| T6-G00 exact accepted base | direct `git ls-remote`, `git fetch origin main`, branch parent | `PASS` | Remote `main` and branch parent are exactly `c3959d2812b679cbc78f54dbbd089787f0f32963`. |
| T6-G01 Task4A/4B PyTorch path | Task4A/4B contracts, five requested tools, Task4B evidence | `PASS` | PyTorch path is inspected; historical identity remains provisional. |
| T6-G02 exact framework/provenance | execution report, smoke evidence, manifest, completion receipt, recovery JSON | `PASS` | Exact version/build/device/GPU values agree. |
| T6-G03 canonical export/reload | Task4 inference implementation and completion receipt | `PASS` | Canonical export and strict fresh reload are evidenced. |
| T6-G04 exact N→N scoring | Task4 model/inference and scorer tests | `PASS` | Scores are exactly source-domain cardinality. |
| T6-G05 candidate completeness/order | Phase-5/Task4/Task5 contracts and tests | `PASS` | No truncation, reconstruction, deduplication, or reorder. |
| T6-G06 no fallback | Task4 inference and Task5C failure tests | `PASS` | Failures remain typed and fail closed. |
| T6-G07 public/model authority | Phase-5/Phase-6 contracts and public-boundary tests | `PASS` | PyTorch remains downstream physical execution only. |
| T6-G08 privacy boundary | public projection/model code and paired-world tests | `PASS` | No new private-to-model or private-to-identity path. |
| T6-G09 replay/admission boundary | Task5C implementation and native regression | `PASS` | Normal recorder/replay/admission path remains required. |
| T6-G10 Task5 consumes future Task7 checkpoint | T5A codec, T5B/T5D tooling, T5C C++ context validation | `BLOCKED` | Current T5C implementation rejects non-smoke checkpoints and lacks meaningful-profile execution context. |
| T6-G11 no Task7 production blocker | Task4A smoke-only projection and T5C context audit | `BLOCKED` | The two gaps above require separately authorized production follow-ups. |
| T6-G12 JAX deferred, not rejected | roadmap direction, Task6 scope, ADR-0007 | `PASS` | No JAX implementation or unmeasured bake-off. |
| T6-G13 Task4 history unchanged | recovery anchors, file hashes, original/recovery status fields | `PASS` | All ten original files retain their expected SHA-256 values. |

Because T6-G10 and T6-G11 are blocked, the aggregate result is:

```text
TASK7_READINESS=BLOCKED
TASK6_READINESS=BLOCKED_BY_IMPLEMENTATION_GAP
```

## Semantic/privacy/replay classification

```text
SEMANTIC_GAMEPLAY_CHANGE=NO
MODEL_INPUT_CONTRACT_CHANGE=NO
ACTION_CONTRACT_CHANGE=NO
DATASET_CONTRACT_CHANGE=NO
CHECKPOINT_FORMAT_CHANGE=NO
EVALUATION_CONTRACT_CHANGE=NO
BACKEND_DECISION_CHANGE=YES

PRIVACY_SEMANTICS_CHANGED=NO
DETERMINISM_SEMANTICS_CHANGED=NO
REPLAY_SEMANTICS_CHANGED=NO
CANDIDATE_IDENTITY_CHANGED=NO
CHECKPOINT_SEMANTIC_IDENTITY_CHANGED=NO
```

The proposed primary-backend label is execution policy/documentation only.
Framework version, CUDA, GPU, process, path, and wall time remain provenance
and never enter gameplay or model semantic identity.

## Verification ledger

No CUDA smoke, training, optimizer step, backend bake-off, new checkpoint,
Teacher regeneration, or generated evidence was run in Task6.

| Command | Exit code | Count/result |
| --- | ---: | --- |
| `python -m unittest -v tests.phase6.phase6_task4a_codec_test tests.phase6.phase6_task4a_model_test tests.phase6.phase6_task4a_inference_test tests.phase6.phase6_task4a_cuda_preflight_test` | `0` | `24/24 PASS` |
| `python -m tests.phase6.phase6_task4a_corpus_test C:\yogiohML\build\dev-windows\phase6_task4_corpus_probe.exe` | `0` | `1/1 PASS` |
| `python -m tests.phase6.phase6_task4a_admitted_model_test C:\yogiohML\build\dev-windows\phase6_task4_corpus_probe.exe` | `0` | `1/1 PASS` |
| `python -m unittest -v tests.phase6.phase6_task4b_runner_test tests.phase6.phase6_task4b_verification_test tests.phase6.phase6_task4b_acceptance_recovery_test` | `0` | `91/91 PASS` |
| `python -m unittest -v tests.phase6.phase6_task5_codec_test tests.phase6.phase6_task5_offline_test tests.phase6.phase6_task5_audit_test` | `0` | `58/58 PASS` |
| `ctest --test-dir build/dev-windows --output-on-failure -j 1 -R "^(phase6_bc_candidate_scorer_test|phase6_supervision_split_inspector_test|phase6_task5c_gameplay_test|logical_model_input_test|card_vocabulary_test|encoded_model_input_test|model_batch_layout_test|model_supervision_sample_test|phase6_task4a_codec_test|phase6_task4b_runner_test|phase6_task4b_verification_test)$"` | `1` | `10/11 PASS`; `phase6_task4b_runner_test` failed because the CTest-configured Python reported `ModuleNotFoundError: No module named 'torch'` |
| `ctest --test-dir build/dev-windows --output-on-failure -j 1 -LE "P4A_HEAVY_REPLAY|M4_HEAVY_LIFECYCLE|M4_ACCEPTANCE_SCALE|P6_PYTORCH_REQUIRED"` | `0` | `166/166 PASS` |
| `git diff --check` | `0` | `PASS`; staged documentation has no whitespace errors. |

The direct active Python runtime was also inspected without training and
reported PyTorch `2.12.1+cu126`, CUDA build `12.6`, `cuda:0`, one device,
`NVIDIA GeForce RTX 4060 Ti`, capability `8.9`. This confirms the reference
runtime values but does not replace the accepted historical Task4B evidence.

## Historical Task4B integrity anchors

The following ten original files matched their accepted recovery SHA-256
anchors during the audit:

```text
docs/p6/task4b/checkpoint.p6k
docs/p6/task4b/completion-receipt.json
docs/p6/task4b/corpus.authority.p6a
docs/p6/task4b/corpus.p6c
docs/p6/task4b/smoke-evidence.p6e
docs/p6/task4b/task4b-acceptance.json
docs/p6/task4b/task4b-acceptance.md
docs/p6/task4b/task4b-execution-report.json
docs/p6/task4b/task4b-verification.json
docs/p6/task4b/training-run-manifest.p6m
```

Their bytes and historical status fields are not changed by Task6.
