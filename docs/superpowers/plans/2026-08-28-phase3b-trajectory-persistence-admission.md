# Phase 3B — trusted trajectory persistence and replay admission implementation plan

> **Execution note:** This plan is executed in the current isolated worktree
> on branch `chris/phase3b-trajectory-persistence-admission`. The user has
> approved the seven-slice decomposition and explicitly authorized proceeding
> without another confirmation.

## Goal

Implement Phase 3B above the accepted EpisodicEnvironment V2 boundary as one
reviewable PR. Add authoritative C++ canonical values/codecs, recorder,
immutable physical artifacts, semantic replay/admission, AdmissionReceipt,
DatasetManifest, and fresh acceptance evidence while preserving every Phase
3A logical contract and keeping M4 unchanged.

## Constraints and invariants

- Base is the verified `origin/main` SHA
  `689710a90e751b046c062a8c0b3f56ec2cef5500`.
- Main checkout is not touched; all work is in the isolated worktree.
- No Teacher, ML/model projection, cloud/remote transport, framework export,
  arbitrary-deck support, or Phase 4 behavior.
- V2 owns legality, ordering, continuation, response submission, and engine
  advancement. The trajectory layer consumes only public value DTOs.
- Canonical C++ encoding is the only authoritative codec. Python only runs
  acceptance and compares already-produced bytes/hashes.
- No candidate filtering, sorting, deduplication, truncation, default,
  compatibility migration, or partial shard admission.
- A genuine Phase-3A ambiguity or contradiction stops the implementation with
  a `BLOCKER` report.

## Slice 1 — canonical values and strict codecs

### Files

- Add `include/ygo/trajectory/types.hpp` for owned Phase-3A trajectory,
  provenance, closure, identity, and physical-contract value types.
- Add `include/ygo/trajectory/codec.hpp` and
  `src/trajectory/codec.cpp` for bounded big-endian readers/writers,
  canonical encoders, strict decoders, and SHA-256 helpers.
- Add `include/ygo/trajectory/policy_provenance.hpp` and
  `src/trajectory/policy_provenance.cpp` for exact provenance/RNG identity
  validation and local explicit resolver interfaces.
- Add `include/ygo/trajectory/identity_resolver.hpp` and its implementation
  only if the V2 environment/episode input decoder requires a separate seam.
- Add `docs/contracts/trajectory-shard-v1.md`,
  `docs/contracts/restricted-collection-evidence-bundle-v1.md`,
  `docs/contracts/admission-receipt-v1.md`,
  `docs/contracts/dataset-manifest-v1.md`, and
  `docs/contracts/dataset-identity-v1.md` with exact physical field orders.
- Add `tests/trajectory/codec_test.cpp` with fixed golden vectors and
  `tests/trajectory/codec_corruption_test.cpp` for strict failures.
- Add the new source/library/test target wiring to `CMakeLists.txt`.

### TDD sequence

1. Write failing golden assertions for every Phase-3A value listed in the
   task, including exact expected bytes and SHA-256 values.
2. Write failing negative tests for unknown schema/enum, invalid optional and
   bool bytes, malformed UTF-8, truncation, trailing bytes, overflow,
   duplicate/unsorted collections, invalid identities, digest mismatch, and
   cross-field mismatch.
3. Implement the bounded reader/writer and exact value codecs.
4. Implement strict V2 identity-input decoding as decode/re-encode equality;
   resolve only the current certified immutable config and exact EpisodeSpec.
5. Run only the new codec tests and the existing identity/provenance tests.

### Closure criteria

- Every canonical golden round-trips with byte equality.
- Every listed mutation fails closed.
- New contract docs agree with the accepted Phase-3A field order and do not
  redefine any Phase-3A domain.
- The existing V2/Phase-3A tests pass unchanged.

## Slice 2 — TrajectoryRecorder

### Files

- Add `include/ygo/trajectory/recorder.hpp` and
  `src/trajectory/recorder.cpp`.
- Add `tests/trajectory/recorder_test.cpp` and
  `tests/trajectory/recorder_continuation_test.cpp`.

### TDD sequence

1. Add failing unit fixtures for reset plus atomic action, terminal closure,
   zero-decision terminal, admin interrupt with pending frame, budget
   interruption, failure, and policy-origin rejection.
2. Add failing real-V2 integration cases for intermediate and final
   continuation actions; assert one record per accepted public action and no
   response metadata in the persisted value.
3. Implement the recorder state machine using copied V2 public values.
4. Add rejection/quarantine tests proving zero records, no accepted-history
   mutation, and no clean restoration after a successful retry.
5. Verify exact ordered candidate count/membership/metadata is copied from the
   V2 frame and never reconstructed or capped.

### Closure criteria

- Every accepted action adds exactly one record.
- Rejections add zero records and irreversibly quarantine policy collection.
- Continuation classification, successor ownership, terminal closure, and
  pending interrupt semantics match the Phase-3A temporal contract.
- Restricted fields, internal IDs, tokens, response bytes/hashes, and raw
  diagnostics are absent from canonical recorder values.

## Slice 3 — Candidate shard and restricted evidence

### Files

- Add `include/ygo/trajectory/shard.hpp` and `src/trajectory/shard.cpp`.
- Add `include/ygo/trajectory/restricted_evidence.hpp` and
  `src/trajectory/restricted_evidence.cpp`.
- Add `include/ygo/trajectory/storage.hpp` and
  `src/trajectory/storage.cpp` for atomic local publication.
- Add `tests/trajectory/shard_test.cpp`,
  `tests/trajectory/restricted_evidence_test.cpp`, and
  `tests/trajectory/publication_test.cpp`.

### TDD sequence

1. Write failing shard golden bytes for one and multiple envelopes, including
   digest-sorted ordering and empty container behavior if the contract allows
   it.
2. Write failing tests for duplicate entries, wrong envelope digest, bad
   length, unsorted entries, truncation, trailing bytes, and single-byte
   corruption.
3. Write failing restricted-bundle tests for exact shard binding, sorted
   interrupted evidence, sorted RNG material, missing/extra evidence, and
   conflicting initialization material.
4. Implement deterministic readers/writers and the write/flush/reread/hash/
   atomic-rename publication helper.
5. Test identical existing final bytes as idempotent and nonidentical bytes as
   conflict; prove temporary partial files are not final artifacts.

### Closure criteria

- Shard bytes are independent of input order and process metadata.
- Restricted evidence is physically separate and contains only admission
  material.
- All corruption and publication failures fail closed.
- No final artifact is overwritten.

## Slice 4 — replay verifier and whole-shard admission

### Files

- Add `include/ygo/trajectory/admission.hpp` and
  `src/trajectory/admission.cpp`.
- Add `tests/trajectory/replay_test.cpp`,
  `tests/trajectory/interrupted_replay_test.cpp`, and
  `tests/trajectory/admission_test.cpp`.

### TDD sequence

1. Write failing replay tests for terminal, zero-decision terminal, real
   continuation intermediate/final paths, administrative interruption,
   semantic-action-budget interruption, and engine-process-budget
   interruption.
2. Add failing mismatches for every frame field, public candidate order,
   observation/domain/decision digest, selected key, transition class,
   successor, closure, terminal views, interruption counts, and restricted
   evidence field.
3. Implement exact environment/episode reconstruction through strict identity
   decoding and public V2 reset/step/interrupt calls only.
4. Add provenance/RNG admission checks and failed/quarantined non-admission.
5. Implement whole-shard atomic validation: one bad entry yields no accepted
   subset and no receipt.

### Closure criteria

- Regenerated V2 values, not stored observations, are replay authority.
- Internal semantic keys, EngineTrace, response bytes/hashes, tokens, and
  protocol IDs never enter replay inputs or admitted values.
- Interrupted admission requires exact restricted evidence.
- Failed episodes remain auditable but cannot be admitted.
- All admission checks run for every entry before any receipt is produced.

## Slice 5 — AdmissionReceipt

### Files

- Add `include/ygo/trajectory/receipt.hpp` and
  `src/trajectory/receipt.cpp` (or keep the type/implementation cohesive in
  `admission.*` if no independent ownership is needed).
- Add `tests/trajectory/receipt_test.cpp` and extend corruption tests.

### TDD sequence

1. Write failing receipt golden bytes and exact receipt-ID assertions.
2. Add failing tests for wrong candidate/evidence artifact hashes, duplicate
   or unsorted commitments, conflicting record bytes, and metadata injection.
3. Implement receipt derivation from the verified admission result, sorted by
   `trajectory_record_id`, with no self-reference or environment metadata.
4. Verify receipt bytes are reproducible from identical verified inputs and
   idempotently publishable.

### Closure criteria

- A receipt binds the exact verified artifact bytes and every admitted entry.
- No receipt exists for failed, quarantined, incomplete, or partially
  admitted input.
- Receipt identity is independent of paths, names, hosts, PIDs, and time.

## Slice 6 — DatasetManifest and dataset identity

### Files

- Add `include/ygo/trajectory/dataset_manifest.hpp` and
  `src/trajectory/dataset_manifest.cpp`.
- Add `tests/trajectory/dataset_manifest_test.cpp` and
  `tests/trajectory/resharding_test.cpp`.

### TDD sequence

1. Write failing manifest golden bytes and dataset identity assertions.
2. Add failing duplicate/unknown receipt/conflict tests.
3. Implement receipt-only membership validation and record-ID ordering.
4. Implement independent dataset semantic identity from sorted unique record
   IDs and the accepted dataset identity schema.
5. Build two valid physical shard packings of the same admitted record set;
   assert physical hashes may differ while dataset semantic ID is equal.

### Closure criteria

- Only trusted admitted records become members.
- Duplicate membership and conflicting provenance fail closed.
- Logical dataset identity excludes physical artifact packing and paths.
- Physical manifest retains sufficient artifact provenance without leaking
  restricted evidence into dataset identity.

## Slice 7 — acceptance, clean checkout, and determinism evidence

### Files

- Add `docs/trajectory/PHASE3B_ACCEPTANCE.md` with P3B-G01..G29, exact
  PASS/FAIL/NOT_RUN/BLOCKED condition, owner, command/evidence, and severity.
- Add one or more acceptance scripts under `tests/trajectory/` or `tools/`
  that generate deterministic candidate/restricted/receipt/manifest fixtures;
  generated evidence is never hand-edited.
- Update `docs/ARCHITECTURE.md` and `docs/ROADMAP.md` only for Phase-3B
  discoverability and explicit non-scope.

### Verification sequence

1. Run all focused slice tests after the previous slice is green.
2. Run C++ trajectory tests, existing CTest, Python unittest discovery,
   protocol/observation/ownership guards, M4 integrity/failure-isolation,
   determinism, episodic replay, and paired-world tests.
3. Run two independent processes with identical semantic inputs; compare
   every required canonical artifact byte/hash and identity.
4. Run the full corruption/truncation/duplicate/conflict matrix and privacy
   structural/paired-world checks.
5. Perform the adversarial cross-slice review using the task threat-model
   questions; classify findings BLOCKER/MAJOR/MINOR/NOTE and stop on open
   BLOCKER or MAJOR.
6. Re-run from a fresh clean checkout at the exact final head with pinned
   rules-bundle verification, configure/build, targeted/full tests, generated
   evidence, `git diff --check`, and clean-tree validation.
7. Inspect exact hosted CI for the exact final head before opening the PR;
   never infer hosted status from local results.

### Closure criteria

- P3B-G01..G29 have fresh bound evidence; unrun/unavailable gates are marked
  `NOT_RUN`/`BLOCKED`, never promoted.
- Existing M4/V2 semantics are unchanged and regression gates are fresh.
- Candidate shard, restricted bundle, receipt, and manifest are byte-identical
  across independent processes for identical logical inputs.
- Re-sharding invariance, privacy, atomic publication, and whole-shard
  admission are independently demonstrated.
- Final handoff contains exact base/head, IDs, artifact hashes, test counts,
  hosted status, and outstanding-status counts.

## Review checkpoints

After each slice, record the focused command and result in the working
acceptance notes before starting the next slice. Before finalization, perform
one cross-slice review specifically checking these mappings:

```text
V2 public frame order -> recorder record -> envelope bytes
envelope digest -> shard entry -> restricted evidence reference
replayed frame/closure -> admission commitment
record ID set -> manifest member order -> dataset semantic ID
```

Any mismatch is a blocker, not a compatibility opportunity.
