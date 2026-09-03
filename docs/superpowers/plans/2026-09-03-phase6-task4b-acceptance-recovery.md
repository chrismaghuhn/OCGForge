# Phase 6 Task 4B — Acceptance-Recovery V1 Plan

## Status and authorization

**Status:** PROPOSED — documentation-only recovery plan, pending independent
contract review.

This plan freezes a possible recovery workflow for the immutable successful
Task-8 smoke and failed Task-9 verification. It is not an implementation plan
authorization. No recovery verifier, recovery CLI, source change, smoke run,
or optimizer step is authorized by this document.

The normative contract is
[P6_TASK4B_ACCEPTANCE_RECOVERY_V1.md](../../p6/P6_TASK4B_ACCEPTANCE_RECOVERY_V1.md).

## Immutable inputs

The future recovery owner MUST bind to these exact values:

```text
H_SMOKE_EXEC       = 8f682d4c9eb53a32be7cd8f6125048583943f19e
H_FAILURE_EVIDENCE = 5bcec55bd473b1f599c99d7d8cbe5e31ba4c7832
H_VERIFIER_FIX     = 97fd0f6e8445a18a4f7939cc66bb8f131f905dcf
```

The ten files at `H_FAILURE_EVIDENCE` are immutable inputs. Their exact
content hashes, the original execution-report hash, checkpoint identity,
smoke-evidence identity, probe hash, and the historical
`full-non-long-ctest` exit-8 failure are part of the V1 contract. The original
status remains `SMOKE_PASS=true`, `TASK4B_PASS=false`.

## Future recovery phases

The following phases describe the only acceptable future workflow. Each phase
requires a separate authorization before execution.

### 1. Freeze and identify the recovery implementation

Create a dedicated recovery-only implementation at an exact committed source
head. Record `recovery_verifier_source_commit` separately from
`training_code_commit`. The recovery owner MUST NOT relabel the verifier fix
or recovery implementation as training code.

If implementation is later authorized, the initial source allowlist is exactly
`tools/phase6/task4b_acceptance_recovery.py` and
`tests/phase6/phase6_task4b_acceptance_recovery_test.py`; any additional path
requires an explicit contract decision.

The implementation MUST read historical inputs from Git objects or verify
their exact hashes before any gate. It MUST not call `run_task4b_smoke`, the
CUDA smoke CLI, an optimizer, or the authoritative corpus probe.

### 2. Prove semantic source integrity

Compare `H_SMOKE_EXEC` to `H_VERIFIER_FIX`, excluding only the historical
generated evidence directory. Require the exact two non-evidence paths:

```text
tests/phase6/phase6_task4b_verification_test.py
tools/phase6/task4b_verify.py
```

The protected semantic diff MUST be empty. The proof covers rules, decks,
Teacher sources, Phase-5 model contracts, codecs, corpus derivation, model
semantics, and training semantics. A path not classified by the frozen
allowlist fails closed.

### 3. Validate immutable original evidence

Validate the exact canonical JSON bytes and SHA-256 identities of all ten
historical files. Decode/validate the existing corpus, authority, checkpoint,
manifest, smoke evidence, receipt projection, execution report, verification
report, and acceptance report using existing H_exec-owned contracts/codecs.

This phase reads old evidence only. It never writes to the old directory and
never changes its `TASK4B_PASS=false` value.

### 4. Execute the corrected gate set once

Produce exactly ten logical gate statuses and fourteen command records. Keep
`phase6_task4b_runner_test` in the explicit PyTorch `task4-focused-python`
command. Use the corrected full CTest exclusion:

```text
P4A_HEAVY_REPLAY|M4_HEAVY_LIFECYCLE|M4_ACCEPTANCE_SCALE|P6_PYTORCH_REQUIRED
```

The corrected full CTest sweep therefore does not redundantly select the
PyTorch-required runner test. Any admitted-forward probe execution, if
retained by the accepted recovery implementation, is ephemeral, hash-bound,
private-output regression only; it is not authoritative corpus production.

### 5. Derive recovery and final status

Set only the new recovery fields:

```text
ORIGINAL_SMOKE_PASS=true
ORIGINAL_TASK4B_PASS=false
TASK4B_RECOVERY_PASS=<all V1 recovery predicates>
TASK4B_FINAL_PASS=ORIGINAL_SMOKE_PASS && ORIGINAL_TASK4B_PASS == false && TASK4B_RECOVERY_PASS
```

The old execution, verification, and acceptance files remain byte-identical.
No status is inferred from a command that was not run, and no convergence or
gameplay-strength claim is introduced.

### 6. Atomically publish separate recovery evidence

Write only:

```text
docs/p6/task4b/recovery-v1/task4b-acceptance-recovery.json
docs/p6/task4b/recovery-v1/task4b-acceptance-recovery.md
```

Both are derived from one typed result. A publication failure is a recovery
failure; it does not alter the original smoke or failure evidence. The future
evidence commit, if authorized, contains only this recovery directory and has
an evidence-only commit message selected by the acceptance workflow.

## Required future failure behavior

There is no retry for any recovery phase. A failure records the reached facts,
sets `TASK4B_RECOVERY_PASS=false` and `TASK4B_FINAL_PASS=false`, preserves the
old successful smoke unchanged, and stops for review. It MUST NOT:

- rerun the CUDA smoke;
- rerun the authoritative probe;
- perform an optimizer/model-training step;
- rewrite the historical evidence;
- turn the historical `TASK4B_PASS` true;
- use a missing, skipped, or inferred gate as PASS;
- begin Phase-6 Task 5.

## Frozen current-state assertion

This documentation-only freeze itself has:

```text
PRODUCTION_CODE_CHANGED=NO
CUDA_SMOKE_RERUN=NO
VERIFIER_RERUN=NO
TASK4B_RECOVERY_EXECUTED=NO
ACTUAL_OPTIMIZER_STEPS_ADDED=0
PHASE6_TASK5_AUTHORIZED=NO
```
