# OCGForge Phase 6 Task 4B — Acceptance-Recovery V1 Contract

## Status and scope

**Status:** PROPOSED — contract-freeze candidate, pending independent review.

This document defines a separate, versioned recovery boundary for the one
completed Task-4B CUDA smoke and its failed post-smoke verification. It does
not execute recovery, rerun CUDA, rerun the verifier, alter historical
evidence, authorize Phase-6 Task 5, or change any Task-4B semantic contract.

The words **MUST**, **MUST NOT**, **MAY**, and **FAIL CLOSED** are normative.

The historical result remains permanently:

```text
SMOKE_PASS=true
TASK4B_PASS=false
```

`TASK4B_PASS` in the historical execution report and historical acceptance
report MUST never be rewritten to `true`.

## 1. Contract identity and canonical location

| Surface | Frozen value |
| --- | --- |
| evidence schema ID | `ocgforge.phase6.task4b.acceptance_recovery.v1` |
| evidence value | `Task4BAcceptanceRecoveryEvidenceV1` |
| canonical JSON | `docs/p6/task4b/recovery-v1/task4b-acceptance-recovery.json` |
| derived Markdown | `docs/p6/task4b/recovery-v1/task4b-acceptance-recovery.md` |
| recovery directory | `docs/p6/task4b/recovery-v1/` |
| JSON encoding | UTF-8, no BOM |
| JSON serialization | `ensure_ascii=True`, `allow_nan=False`, `sort_keys=True`, `separators=(",", ":")` |

The Markdown file is a presentation-only derivation of the canonical JSON. It
is not an independent authority and MUST NOT contain a status that is absent
from or inconsistent with the JSON.

The canonical JSON MUST contain this exact failure representation:

```text
recovery_failure = null | {
    error_code: non-empty stable string,
    failure_stage: one of:
      historical-evidence-validation
      provenance-validation
      semantic-source-integrity
      build-probe-binding
      gate-execution
      post-gate-integrity
      evidence-publication
    reached_command_count: integer in [0, 14]
}
```

On a successful recovery, `recovery_failure` MUST be `null`. On any
fail-closed recovery failure, it MUST be non-null and both
`TASK4B_RECOVERY_PASS` and `TASK4B_FINAL_PASS` MUST be `false`.

`reached_command_count` is the number of fixed recovery commands that were
actually started and recorded, including a command that returned `FAIL`; it is
zero when failure occurs before gate execution. Commands after the first
nonrecoverable failure MUST be represented as `NOT_RUN` and MUST NOT be
counted. An expected anchor or value is not an observed validation fact until
its validation succeeds; unvalidated observations MUST remain null or
`NOT_RUN` rather than being copied from expected constants.

The recovery directory MUST be newly created for recovery. The original
`docs/p6/task4b/` files remain a separate immutable input set.

## 2. Immutable historical anchors

The recovery evidence MUST bind to these exact immutable commits:

```text
H_SMOKE_EXEC       = 8f682d4c9eb53a32be7cd8f6125048583943f19e
H_FAILURE_EVIDENCE = 5bcec55bd473b1f599c99d7d8cbe5e31ba4c7832
H_VERIFIER_FIX     = 97fd0f6e8445a18a4f7939cc66bb8f131f905dcf
```

`H_SMOKE_EXEC` is the only training-code commit. `H_FAILURE_EVIDENCE` is the
only original evidence commit. `H_VERIFIER_FIX` identifies the corrected
verifier boundary and is not training provenance.

Recovery MUST read the historical files from the Git object named
`H_FAILURE_EVIDENCE` (or verify that the worktree bytes are identical to those
Git objects) before deriving any recovery result. It MUST fail closed on any
missing, changed, substituted, or non-canonical historical byte.

The ten historical files and their required SHA-256 content identities are:

| Relative path in `docs/p6/task4b/` | SHA-256 |
| --- | --- |
| `corpus.p6c` | `0f410d8cd27aa6d40009fae6fdef156475ee9a0f0f8bce661884a2e5465b63a8` |
| `corpus.authority.p6a` | `a57d691c1d3f8b0514b17ef57813cac8fbafeef14681b7b6379ee9db7488701e` |
| `checkpoint.p6k` | `ccf86148a53c54ec35cb0129be7b652df671c6f7c8114c68705a265d554cc624` |
| `training-run-manifest.p6m` | `5511c410528270605700353468843b0453596a86c7b4e6c844a80a98eedd3dfc` |
| `smoke-evidence.p6e` | `f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe` |
| `completion-receipt.json` | `86a96780322caf4a24bf305c89e365602e06a0210a9cf99d002ee25f9d63d01f` |
| `task4b-execution-report.json` | `051ba2320c32b8b64c0ed8954d85d3a4956038a59094610fad131c62464b4b7f` |
| `task4b-verification.json` | `dbc93c1a9bede7f1290b092e00bfae048e02faa3df3677f50c8c3f9adb1ce90d` |
| `task4b-acceptance.json` | `f63137ccaf0206f194b4661534b9ebac05994280ccb7915bd95e3d58ad7c77f0` |
| `task4b-acceptance.md` | `c346a1db721acf1f47e4c380920ff037c40b409c3107a55d09efac1ac30b93a6` |

The recovery record MUST repeat the complete file-to-hash map, not only the
execution-report hash.

## 3. Original attempt binding

The `original_attempt` object in `Task4BAcceptanceRecoveryEvidenceV1` MUST
contain at least these exact fields:

```text
H_SMOKE_EXEC
H_FAILURE_EVIDENCE
original_execution_report_sha256
original_verification_report_sha256
original_acceptance_report_sha256
original_checkpoint_identity
original_smoke_evidence_identity
original_probe_sha256
original_corpus_probe_source_commit
ORIGINAL_SMOKE_PASS
ORIGINAL_TASK4B_PASS
original_failed_gate_id
original_failed_gate_exit_code
original_command_record_count
original_file_sha256
```

The required known values are:

```text
original_execution_report_sha256      = 051ba2320c32b8b64c0ed8954d85d3a4956038a59094610fad131c62464b4b7f
original_checkpoint_identity          = phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327
original_smoke_evidence_identity      = phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe
original_probe_sha256                 = 074a796dab428af07ca8a81489f03a1f1aa52a1e581979726faee4fe2a0190c2
original_corpus_probe_source_commit   = H_SMOKE_EXEC
ORIGINAL_SMOKE_PASS                   = true
ORIGINAL_TASK4B_PASS                  = false
original_failed_gate_id               = full-non-long-ctest
original_failed_gate_exit_code        = 8
original_command_record_count         = 14
```

The original failed gate is a historical fact. Recovery MUST record it as a
failure and MUST NOT reinterpret it as a skipped gate or as a successful
result.

## 4. Separate provenance identities

The `provenance` object MUST keep these identities in separate fields:

```text
training_code_commit            = H_SMOKE_EXEC
failed_evidence_commit          = H_FAILURE_EVIDENCE
verifier_fix_commit             = H_VERIFIER_FIX
recovery_verifier_source_commit = exact future recovery-verifier commit
recovery_contract_commit        = exact commit containing this accepted contract
```

`recovery_verifier_source_commit` and `recovery_contract_commit` are required
40-character lowercase Git SHAs when a recovery attempt is eventually
executed. They are not supplied by a caller as acceptance claims; the recovery
owner derives and records them from the actual committed source. A recovery
implementation MUST NOT populate `training_code_commit` with either recovery
SHA.

## 5. Semantic source-integrity proof

Recovery MUST prove that the verifier remediation did not change training,
model, codec, corpus, deck, rules, Teacher, or Phase-5 semantics. A Boolean
claim alone is insufficient.

The `semantic_integrity_proof` object MUST contain:

```text
comparison_base_commit            = H_SMOKE_EXEC
verifier_fix_commit               = H_VERIFIER_FIX
observed_non_evidence_paths       = exact sorted path list
expected_verifier_fix_paths       =
  tests/phase6/phase6_task4b_verification_test.py
  tools/phase6/task4b_verify.py
protected_semantic_diff_paths     = empty list
protected_semantic_diff_sha256
rules_deck_teacher_phase5_unchanged = true only after proof
```

The comparison MUST exclude only the historical generated evidence directory
`docs/p6/task4b/` from the source diff. After that exclusion, the observed
non-evidence path list between `H_SMOKE_EXEC` and `H_VERIFIER_FIX` MUST equal
the two `expected_verifier_fix_paths` above, bytewise and pathwise. Any other
path, any rules/deck/Teacher/Phase-5/model/codec/corpus change, or any
unproven path classification FAILS CLOSED.

The proof MUST also compare the protected semantic tree through Git object
content, using a canonical sorted sequence of:

```text
relative_path UTF-8 bytes + NUL + old_blob_id + NUL + new_blob_id + LF
```

The SHA-256 of that sequence is `protected_semantic_diff_sha256`. An empty
protected diff is required. Physical build paths, CUDA facts, memory values,
and evidence publication paths are not semantic inputs and MUST NOT be used
to manufacture semantic equality.

Any future recovery-only implementation paths MUST be separately authorized
and explicitly listed in the recovery source proof. If a future implementation
is authorized, its initial source allowlist is exactly:

```text
tools/phase6/task4b_acceptance_recovery.py
tests/phase6/phase6_task4b_acceptance_recovery_test.py
```

Any additional path requires a new contract decision. This contract itself
adds no recovery implementation.

## 6. No-new-training and no-mutation counters

The `recovery_execution` object MUST contain internally derived values:

```text
CUDA_SMOKE_RERUN                  = false
AUTHORITATIVE_CORPUS_PROBE_RERUN = false
ADDITIONAL_OPTIMIZER_STEPS       = 0
MODEL_TRAINING_INVOCATIONS       = 0
EVIDENCE_MUTATION                 = false
```

The corrected recovery verifier MUST perform exactly three whitelisted
ephemeral probe regressions with the exact historical probe SHA:

1. the explicit `phase6_task4a_corpus_test` admitted-forward command;
2. the explicit `phase6_task4a_admitted_model_test` admitted-forward command;
3. the `phase6_task4a_corpus_test` process selected by the corrected
   `full-non-long-ctest` gate.

All three regressions MUST write only below private temporary directories and
MUST never write or replace the historical corpus or authority files. They are
recorded separately as
`EPHEMERAL_PROBE_REGRESSION_INVOCATIONS=3`; they are never authoritative
corpus invocations and never training authority. The authoritative count
remains `AUTHORITATIVE_CORPUS_PROBE_RERUN=false`.

`ADDITIONAL_OPTIMIZER_STEPS` counts only real model-training optimizer
updates. Synthetic losses, fake optimizers, mocks, and other test doubles used
inside the mandatory runner regression do not count as model-training
invocations or optimizer steps.

Recovery MUST reuse only the immutable historical smoke artifacts. It MUST
not rebuild the probe, create a checkpoint, reload a model for training, or
invoke the smoke CLI.

## 7. Corrected recovery gate set

Recovery uses the same ten logical gate IDs and fourteen command-record
cardinality as the original verifier:

```text
task4-focused-python
admitted-forward
full-non-long-ctest
project-python
rules-bundle
rules-deck
teacher-binding
public-boundary
source-boundary
base-to-h-exec-diff-check
```

The corrected `full-non-long-ctest` command MUST use one shell-disabled
argument for the exact exclusion expression:

```text
P4A_HEAVY_REPLAY|M4_HEAVY_LIFECYCLE|M4_ACCEPTANCE_SCALE|P6_PYTORCH_REQUIRED
```

`task4-focused-python` MUST continue to contain
`tests.phase6.phase6_task4b_runner_test` exactly once. The PyTorch-dependent
coverage remains mandatory through that explicit gate; it is not removed to
make recovery green.

Each recovery command record MUST contain:

```text
command_id
argv
exit_code
stdout_sha256
stderr_sha256
status = PASS | FAIL | NOT_RUN
```

All ten logical gate statuses MUST be `PASS` for recovery success. A missing,
reordered, nonzero, or unrecorded command FAILS CLOSED.

## 8. Status derivation

The recovery evidence MUST use separate status fields:

```text
ORIGINAL_SMOKE_PASS  = true
ORIGINAL_TASK4B_PASS = false
TASK4B_RECOVERY_PASS = <derived bool>
TASK4B_FINAL_PASS    = <derived bool>
```

The only accepted derivation is:

```text
TASK4B_RECOVERY_PASS =
    immutable-original-evidence-validation
    && exact-provenance-validation
    && semantic-source-integrity-proof
    && corrected-required-gate-set-all-PASS
    && CUDA_SMOKE_RERUN == false
    && AUTHORITATIVE_CORPUS_PROBE_RERUN == false
    && ADDITIONAL_OPTIMIZER_STEPS == 0
    && MODEL_TRAINING_INVOCATIONS == 0
    && EPHEMERAL_PROBE_REGRESSION_INVOCATIONS == 3
    && EVIDENCE_MUTATION == false
    && recovery_failure == null

TASK4B_FINAL_PASS =
    ORIGINAL_SMOKE_PASS
    && ORIGINAL_TASK4B_PASS == false
    && immutable-original-evidence-validation
    && TASK4B_RECOVERY_PASS
```

`TASK4B_FINAL_PASS` is a derived recovery status. It does not update the
historical `TASK4B_PASS` field, which remains false forever.

## 9. Recovery evidence publication and failure branches

Recovery writes only the two new files under `recovery-v1/`, atomically and
from the typed recovery result. It MUST never edit, replace, amend, or
regenerate any of the ten historical files.

The two final recovery files MUST be published all-or-fail from one typed
result. The publisher MUST:

1. serialize and validate both final byte payloads before publication;
2. create a private staging directory as a sibling of `recovery-v1/`;
3. write both files into that staging directory, flush and `fsync` each file,
   verify their exact bytes, and `fsync` the staging directory;
4. atomically rename the complete staging directory to the previously absent
   final `recovery-v1/` directory on the same filesystem; and
5. `fsync` the parent directory and reread/validate both final files.

The final directory MUST NOT be created by sequentially replacing its JSON and
Markdown files. If the final directory already exists, the atomic directory
rename is unavailable, either payload fails validation, or either final file
cannot be reread identically, publication FAILS CLOSED at
`evidence-publication` and a partial final evidence set MUST NOT be accepted.
The historical directory is never a publication target.

There is no retry branch. The following conditions produce an auditable
recovery result with `TASK4B_RECOVERY_PASS=false` and
`TASK4B_FINAL_PASS=false`:

- historical file/hash/identity mismatch;
- missing or inconsistent original status;
- verifier-fix or protected-semantic source proof mismatch;
- any unexpected source or worktree change;
- any corrected gate FAIL or NOT_RUN;
- any command omission, reorder, nonzero exit, or hash mismatch;
- any CUDA smoke invocation;
- any authoritative corpus-probe invocation;
- any unexpected or non-whitelisted ephemeral probe invocation;
- any training or model-training invocation;
- any optimizer step;
- any attempt to mutate historical evidence;
- any recovery evidence publication failure.

The ordinary model-forward and inference work required by the two admitted-
forward validation tests is permitted and is not a training invocation. It
MUST remain ephemeral and non-authoritative.

A failed recovery MUST preserve the successful original smoke and its original
`SMOKE_PASS=true` / `TASK4B_PASS=false` result. It MUST be committed, if and
only if separately authorized, as recovery evidence only; it MUST not be used
to conceal the original failed verifier attempt.

## 10. Authorization boundary

This contract freeze authorizes no execution and no implementation:

```text
CUDA_SMOKE_RERUN=NO
VERIFIER_RERUN=NO
TASK4B_RECOVERY_EXECUTED=NO
ACTUAL_OPTIMIZER_STEPS_ADDED=0
PHASE6_TASK5_AUTHORIZED=NO
```

An independently reviewed and accepted V1 contract is required before any
recovery implementation or recovery evidence run is authorized.
