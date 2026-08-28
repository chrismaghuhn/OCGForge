# Phase 3B immutable trusted-trajectory persistence and replay admission

## Status

This matrix is the acceptance record for the Phase-3B implementation PR. It
does not upgrade the repository's fixed-deck M3/M3.5 claims into general
Yu-Gi-Oh! support or ML readiness. A gate is `PASS` only after the named
command or review has run at the exact final PR head. `NOT_RUN`, `SKIPPED`,
and `BLOCKED` are not acceptance passes.

| Field | Value |
| --- | --- |
| Accepted Phase-3A base | `689710a90e751b046c062a8c0b3f56ec2cef5500` |
| Implementation branch | `chris/phase3b-trajectory-persistence-admission` |
| Scope | One Phase-3B PR; seven internally separated vertical slices |
| Rules input | The pinned `third_party/rules_bundle.lock.json` and its verified local cache |
| Matrix status | P3B gates recorded after exact-head execution; hosted CI remains `NOT_RUN` because this task does not push or open a PR |

The terminal replay boundary is deliberate. Accepted Phase 3A excludes
`RunControl` from the terminal identity and forbids arbitrary run-control
metadata in the envelope. Therefore `ReplayOptions::terminal_run_control` is
an explicit verifier input. It is never persisted, defaulted, inferred, or
migrated. Terminal replay without a valid caller-supplied value is rejected.
Interrupted replay similarly requires the exact restricted evidence and an
explicit safe cancellation source for the public V2 interrupt call.

## Gate matrix

| Gate | Exact `PASS` condition | Owning layer | Evidence | Severity | Status |
| --- | --- | --- | --- | --- | --- |
| P3B-G01 Accepted Phase-3A contract preserved | The diff preserves every accepted Phase-3A field order, identity input, enum, privacy exclusion, and V2 boundary; no lower-layer semantic change is needed. | Architecture / trajectory | Phase-3A contract diff audit; codec, recorder, and replay tests; final source review | BLOCKER | `PASS` |
| P3B-G02 Authoritative C++ values/codecs | Canonical values, codecs, shard/evidence validation, replay, receipt, and manifest semantics are implemented in C++; Python only orchestrates or summarizes. | `ygo::trajectory` | `trajectory_*` CTest targets; source/dependency audit | BLOCKER | `PASS` |
| P3B-G03 Strict canonical decode/re-encode | Every accepted new value decodes, validates, and re-encodes to byte-identical canonical bytes; malformed, noncanonical, truncated, overflowed, and trailing input rejects. | Codec | `trajectory_codec_test`, shard/evidence/receipt/manifest corruption cases | BLOCKER | `PASS` |
| P3B-G04 Complete ordered public domain persistence | Each recorded frame retains the complete V2 ordered public request/domain and exact public digests; no filter, reorder, deduplication, cap, default, or truncation exists. | Recorder / replay | `trajectory_recorder_test`; real replay fixture and source audit | BLOCKER | `PASS` |
| P3B-G05 One accepted action = one record | Each accepted public V2 action creates exactly one `DecisionRecord`; no accepted action is fabricated at a boundary. | Recorder | `trajectory_recorder_test`; real atomic fixture | BLOCKER | `PASS` |
| P3B-G06 Rejected call quarantine / zero record | A policy-origin `StepRejected` creates zero records, does not advance the accepted index, irreversibly quarantines the session, and a retry cannot restore `CLEAN`. | Recorder / admission | `trajectory_recorder_test`; quarantine non-admission integration case | BLOCKER | `PASS` |
| P3B-G07 Continuation semantics preserved | Real intermediate and final continuation actions each create one record; intermediate does not submit a final response and final submission remains owned by V2 exactly once. | V2 facade / recorder | Real seed-4 continuation replay; existing M1 continuation regression | BLOCKER | `PASS` |
| P3B-G08 Policy/participant provenance validation | Every accepted record resolves to an exact valid artifact, assignment, role, and immutable producer identity; unknown or inconsistent provenance rejects. | Provenance resolver | `trajectory_codec_test`, provenance validation, whole-shard admission | BLOCKER | `PASS` |
| P3B-G09 Policy RNG initialization/material validation | Every referenced non-`NONE` initialization recomputes exactly from restricted raw material; invalid `CURSOR`, missing, extra, or conflicting material rejects; `NONE` uses the exact accepted contract. With no registered policy-owned RNG state codec in this V1 scope, all non-`NONE` material is explicitly rejected closed. | Restricted evidence / provenance | `trajectory_restricted_evidence_test`; admission RNG checks | BLOCKER | `PASS` |
| P3B-G10 Deterministic candidate shard bytes | The uncompressed shard has explicit ordering by envelope digest, exact bytes, and golden SHA; input order cannot alter canonical output. | Storage / shard codec | `trajectory_shard_test`; artifact determinism fixture | BLOCKER | `PASS` |
| P3B-G11 Immutable atomic publication | Temporary publication is flushed, reread, hash-checked, and atomically linked into the content-addressed final name without replacement; publication is idempotent only for identical bytes, rejects symbolic-link/non-file targets, and never overwrites conflicting final bytes. | `trajectory::storage` | Storage publication test and final source audit | BLOCKER | `PASS` |
| P3B-G12 Corruption/truncation rejection | Single-byte changes, bad lengths, bad entry hashes, truncation, invalid fields, and trailing bytes reject for every physical codec; partial temp files cannot be admitted. | Physical codecs / admission | Codec, shard, evidence, receipt, manifest negative cases | BLOCKER | `PASS` |
| P3B-G13 Terminal semantic replay | Real terminal replay compares both perspective-safe terminal views, outcome, counts, indexes, and canonical terminal closure/public gameplay identity; terminal does not persist or compare `final_engine_step_index`; zero-decision terminal remains supported; missing terminal control rejects. | Semantic replay | Real seed-2 terminal fixture and explicit missing-input negative | BLOCKER | `PASS` |
| P3B-G14 Administrative interruption replay | Real administrative cancellation replays to the exact unacted public frame, invokes the public V2 interrupt boundary, and compares exact closure/evidence. | Semantic replay | Real seed-43 fixture and continuation pending-frame fixture | BLOCKER | `PASS` |
| P3B-G15 Budget interruption replay | Engine-process and semantic-action budget interruptions use exact restricted budgets/counts/step and reproduce the accepted public prefix. | Semantic replay / restricted evidence | Real seed-42 and seed-41 fixtures | BLOCKER | `PASS` |
| P3B-G16 Failed/quarantined non-admission | Failed and quarantined envelopes may be structurally retained but receive no normal gameplay/record identity and no admission receipt. | Recorder / admission | Failure and quarantine tests; whole-shard negative | BLOCKER | `PASS` |
| P3B-G17 Restricted evidence exact matching | Every interrupted candidate has exactly one matching restricted evidence entry; no evidence for terminal/failed/non-interrupted entries, missing evidence, or extras is accepted. | Restricted evidence / replay | `trajectory_restricted_evidence_test`; replay admission | BLOCKER | `PASS` |
| P3B-G18 AdmissionReceipt exact artifact binding | A receipt binds the exact candidate/evidence artifact digests and sorted admitted commitments and is issued only from verified admission state. | Admission / receipt | `trajectory_receipt_test`; real receipt issuance and decode | BLOCKER | `PASS` |
| P3B-G19 Whole-shard atomic admission | Every shard entry passes before one receipt is produced; one bad, failed, quarantined, or mismatched entry produces no partial receipt or partial admission. | Admission | Packed good/bad shard integration test | BLOCKER | `PASS` |
| P3B-G20 DatasetManifest membership correctness | The manifest contains only verified admitted receipt commitments, sorted uniquely by record ID, with exact physical provenance and no mutable paths. | Dataset | `trajectory_dataset_manifest_test`; real packed/split integration | BLOCKER | `PASS` |
| P3B-G21 Dataset identity re-sharding invariance | Identical admitted record membership produces one identical logical `dataset_semantic_id` across valid physical shard/receipt packings, while physical manifests may differ. | Dataset identity | Dataset golden and packed-vs-split integration | BLOCKER | `PASS` |
| P3B-G22 Duplicate/conflict fail-closed behavior | Duplicate envelope/record/evidence/RNG membership, conflicting bytes, unknown receipt, wrong artifact binding, and missing evidence never use first-wins or silent deduplication. | All persistence/admission layers | Negative tests in shard/evidence/receipt/manifest/admission suites | BLOCKER | `PASS` |
| P3B-G23 Privacy / hidden-information boundary | Paired worlds have equal permitted public projection despite private/internal differences; no internal key/domain digest/protocol ID/raw hash/response/token/private observation/seed/restricted material crosses into public or learner-facing values. | V2 public projection / trajectory | `trajectory_privacy_test`; existing paired-world and terminal privacy tests; structural audit | BLOCKER | `PASS` |
| P3B-G24 Cross-process byte determinism | Two independent processes with identical semantic inputs emit byte-identical envelope, shard, restricted bundle, receipt, manifest, and identical semantic/artifact IDs. | All canonical outputs | `trajectory_artifact_determinism_test` run twice; captured output comparison | BLOCKER | `PASS` |
| P3B-G25 Existing V2 determinism/replay/privacy regression | Existing V2 determinism, replay, paired-world, observation, decision, and ownership tests pass at the exact final head. | Existing V2 | Python regression commands plus relevant CTest targets | BLOCKER | `PASS` |
| P3B-G26 M4 failure-isolation regression | Existing M4 integrity, worker protocol, failure-isolation, process metrics, job-generation, and available integration tests pass without M4 semantic changes. | M4 | Existing M4 Python tests and full CTest | BLOCKER | `PASS` |
| P3B-G27 Rules/decks/lock inputs unchanged | Exact rules bundle verifies; locked decks and rules inputs are unchanged; no generated lock/evidence artifact is hand-edited to pass. | Rules / repository | `verify_rules_bundle.py`; diff audit; clean-checkout evidence | BLOCKER | `PASS` |
| P3B-G28 No Teacher/model/ML/remote scope creep | The PR adds no Teacher, model, tensor, framework export, remote actor, cloud, RPC, or Phase-4 behavior. | Scope governance | Dependency/source/diff audit; non-scope declaration | BLOCKER | `PASS` |
| P3B-G29 Clean-checkout reproducibility | A fresh detached checkout at the exact final head verifies rules, configures/builds, runs targeted trajectory and existing regression/determinism gates, compares independent artifact outputs, passes `git diff --check`, and remains clean after evidence generation. | Acceptance | `tools/trajectory/phase3b_clean_checkout_acceptance.ps1` and retained worktree report | BLOCKER | `PASS` |

## Required exact-head commands

The final report records the actual result of each command; historical or
focused evidence is not promoted to a full-suite pass.

```powershell
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache C:\yogiohML\.cache\rules_bundle
python -m unittest discover -s tests/python -v
python tests/protocol/decision_coverage_test.py
python tests/observation/observation_coverage_test.py
python tests/episodic/episode_driver_ownership_guard.py
python -B -m unittest tests.m4.test_benchmark_integrity tests.m4.test_failure_isolation tests.m4.test_job_generation tests.m4.test_process_metrics tests.m4.test_worker_protocol -v
python tests/determinism/m1_engine_determinism_test.py --probe build/p3b-windows-zig/m1_engine_conformance_test.exe
python tests/determinism/determinism_test.py --probe build/p3b-windows-zig/ygo_core_probe.exe
ctest --test-dir build/p3b-windows-zig --output-on-failure
```

The trajectory-focused command is:

```powershell
ctest --test-dir build/p3b-windows-zig --tests-regex '^(trajectory_codec_test|trajectory_recorder_test|trajectory_shard_test|trajectory_restricted_evidence_test|trajectory_replay_admission_test|trajectory_receipt_test|trajectory_dataset_manifest_test|trajectory_artifact_determinism_test|trajectory_privacy_test)$' --output-on-failure
```

The clean-checkout path is the source of the `P3B-G29` evidence. Use the
default mode for full CTest; `-SkipFullCTest` records `full_ctest=NOT_RUN` and
cannot close the full clean-checkout gate.

## Semantic versus implementation changes

The following are new Phase-3B semantic contracts:

- `ocgforge.trajectory_shard.v1`;
- `ocgforge.restricted_collection_evidence_bundle.v1`;
- `ocgforge.admission_receipt.v1`;
- `ocgforge.dataset_identity.v1`; and
- `ocgforge.dataset_manifest.v1`.

Atomic temporary-file handling, buffer guards, and local build orchestration
are internal implementations. They cannot change the canonical values or
identities.

## Evidence summary

The final exact-head run appends the observed counts and deterministic fixture
identities here. Values below must remain `NOT_RUN` until the commands have
actually completed:

| Evidence | Value |
| --- | --- |
| Candidate shard artifact SHA-256 | `247f534c950b0e572e9394dfd2cc773818c4ce8de6d59ebaed003dfb39101255` |
| Restricted evidence artifact SHA-256 | `1bdbabe2d9905984b61f8b926ea0155a8f83dc6d125d253dd78766f3674f5eed` |
| AdmissionReceipt ID | `admission_receipt.v1.d9cd38dfec176bf83b25c69fde4fa89b4330a3362e1c383c22483d4f79c2e6f6` |
| Dataset semantic ID | `5ea0f9197f4993b1c57052e28cae54853e1276d3a1074526015098d772cee553` |
| Clean-checkout report | `C:\p3b-trajectory\artifacts\trajectory\phase3b-clean-checkout-final\phase3b_clean_checkout.json` |
| Hosted CI exact-head status | `NOT_RUN` |

## Scope stop

This PR ends at immutable candidate/restricted artifacts, semantic V2 replay,
whole-shard admission, receipts, and logical dataset membership. Teacher,
RandomLegal production policy, model/tensor projections, training, framework
exports, distributed actors, cloud transport, RPC, and Phase 4 remain out of
scope.
