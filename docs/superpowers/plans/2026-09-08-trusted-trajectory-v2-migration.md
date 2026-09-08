# Trusted Trajectory V2 Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox syntax for tracking.

**Goal:** Add an explicit trusted-trajectory V2 path for EpisodicEnvironment V3 and public_action.v2 while preserving every historical V1 byte, validator, identity, and downstream boundary.

**Architecture:** Keep V1 and V2 logical types, codecs, validators, and contract IDs generation-specific. Reuse only child contracts whose semantic meaning and canonical bytes are unchanged. Move from public V2 values to persistence and admission in bounded, independently reviewable slices; do not wire TeacherRunnerV3 or downstream datasets until each earlier boundary is final.

**Tech Stack:** C++20, CMake/Ninja, GoogleTest/CTest, existing OCGForge canonical byte writers/readers, SHA-256 identity helpers, EpisodicEnvironment V3, and the repository's public-observation and policy-provenance codecs.

---

## Scope map

| Slice | Files owned | Deliverable | Explicitly excluded |
| --- | --- | --- | --- |
| TTV2-A0 | docs/contracts/trusted-trajectory-v2.md, this plan | Contract and migration boundary freeze | all production and test implementation |
| TTV2-A1 | include/ygo/trajectory/types.hpp, include/ygo/trajectory/codec.hpp, src/trajectory/codec.cpp, new V2 codec tests, CMakeLists.txt if registration is required | V2 logical values, candidate/frame/record/envelope canonical codec, V1/V2 rejection matrix | recorder, replay, admission, persistence, Runner |
| TTV2-A2 | new V2 recorder files or repository-approved recorder successor files, identity tests, CMakeLists.txt if registration is required | V3 recorder, public gameplay identity V2, trajectory record identity V2 | admission, receipt, shard, dataset, Runner wiring |
| TTV2-A3 | restricted replay successor files, admission successor files, receipt/shard boundary files only where separately authorized | V2 restricted replay and explicit downstream acceptance/rejection | model, Task7, training |
| TTV2-A4 | TeacherRunnerV3 trajectory adapter and focused integration tests | explicit RunnerV3 to trusted trajectory V2 wiring | V1 Runner migration, model, Task7 |
| TTV2-A5 | Task7/model/dataset successor surfaces | downstream V2 collection and materialization only after a new authorization | training, RUN_A, RUN_B unless separately authorized |

Every slice is independently reviewable. A failed gate stops that slice and does not
authorize the next one.

### Task 1: TTV2-A0 contract freeze

**Files:**
- Create: docs/contracts/trusted-trajectory-v2.md
- Create: docs/superpowers/plans/2026-09-08-trusted-trajectory-v2-migration.md

- [x] **Step 1: Verify the exact source base**

Run:

~~~powershell
git fetch origin
git rev-parse HEAD
git status --short
git branch --show-current
~~~

Expected:

~~~text
HEAD=e23dc196f0718069b6a9b9b08078f16b69a95af8
WORKTREE_CLEAN=YES
~~~

- [x] **Step 2: Record the V1 source inventory**

Inspect:

~~~text
include/ygo/trajectory/types.hpp
include/ygo/trajectory/codec.hpp
src/trajectory/codec.cpp
include/ygo/trajectory/recorder.hpp
src/trajectory/recorder.cpp
include/ygo/trajectory/restricted_evidence.hpp
src/trajectory/restricted_evidence.cpp
include/ygo/trajectory/admission.hpp
src/trajectory/admission.cpp
include/ygo/trajectory/receipt.hpp
src/trajectory/receipt.cpp
include/ygo/trajectory/shard.hpp
src/trajectory/shard.cpp
tests/trajectory/**
~~~

The contract records every V1 binding, whether a successor is required, which child
codecs remain shared, and the future slice that owns the migration.

- [x] **Step 3: Freeze exact V2 bytes and rejection rules**

The contract records the complete V2 candidate, request, frame, record, closure,
manifest, restricted-evidence, public-gameplay-identity, and trajectory-record
canonical orders. It records u8 operation codes 0=None, 1=Select, 2=Unselect,
big-endian primitive encodings, V2 identity recomputation, and the complete version
purity matrix.

- [x] **Step 4: Run A0 documentation gates**

Run:

~~~powershell
git diff --check
$patterns = @('T'+'B'+'D', 'T'+'O'+'D'+'O', 'IN'+'COMPLETE', 'UN'+'SPECIFIED'); rg -n ($patterns -join '|') docs/contracts/trusted-trajectory-v2.md docs/superpowers/plans/2026-09-08-trusted-trajectory-v2-migration.md
~~~

Expected:

~~~text
git diff --check = PASS
placeholder scan = no matches
~~~

- [ ] **Step 5: Commit only the A0 documents**

Run:

~~~powershell
git add docs/contracts/trusted-trajectory-v2.md docs/superpowers/plans/2026-09-08-trusted-trajectory-v2-migration.md
git commit -m "docs: freeze trusted trajectory v2 migration"
git push -u origin chris/p6-trusted-trajectory-v2-contract
~~~

Stop for independent review. A1 remains unauthorized until the A0 contract is
accepted.

### Task 2: TTV2-A1 logical V2 DTOs and canonical codec

**Files:**
- Modify: include/ygo/trajectory/types.hpp only if repository policy allows successor declarations in this header; otherwise create the repository-approved V2 trajectory types header
- Modify: include/ygo/trajectory/codec.hpp only for explicit V2 declarations
- Modify: src/trajectory/codec.cpp only for V2 implementation
- Create or modify: focused V2 trajectory codec tests
- Modify: CMakeLists.txt only if the new focused test requires registration

- [ ] **Step 1: Add explicit V2 logical types**

Define EpisodeManifestV2, PublicFrameSnapshotV2, DecisionRecordV2,
TerminalClosureV2, InterruptedClosureV2, FailedClosureV2,
EpisodeClosureV2, EpisodeEnvelopeV2, and RestrictedReplayEvidenceV2.

Do not add a generation enum to a V1 type. Use
episodic_environment_contract_id in the V2 manifest and frame. Preserve shared
policy provenance, policy-RNG, public-observation, safe-state, and episode-identity
types where their existing canonical bytes are unchanged.

- [ ] **Step 2: Add V2 candidate canonicalization**

Implement the exact order from the A0 contract:

~~~text
schema:string
action_kind:string
card_selection_operation:u8
public_action_key:string
choice:optional public-choice bytes
source_reference:optional public-reference bytes
target_reference:optional public-reference bytes
phase:optional u32be
position:optional u8
source_index:optional u32be
amount:optional signed-i32-as-u32be
continuation_operation:string
submits_engine_response:bool
~~~

Validate the stored key with is_public_action_key_v2 and recompute it with
public_action_key_v2. Reject unknown operation codes, V1 keys, malformed
descriptors, and request-family contradictions.

- [ ] **Step 3: Add V2 request/frame/domain/decision validation**

Preserve supplied candidate order and cardinality. Reject empty domains, duplicates,
mixed key generations, V1 environment contracts, and mismatched V2 domain or decision
identities. Use only V2 environment identity helpers in the V2 path.

- [ ] **Step 4: Add V2 canonical record, closure, manifest, and envelope codecs**

Reuse transition, successor, and closure numeric meanings without renumbering.
Separate the public record projection from the full collection record so that policy
assignment and RNG provenance remain outside public gameplay identity.

- [ ] **Step 5: Add focused tests**

Cover TTV2-G01 through TTV2-G13:

~~~text
V1 bytes/goldens unchanged
V3/V2 accepted only by V2
V2-environment/V1 trajectory rejected
V1 action rejected by V2
V2 action rejected by V1
mixed domains rejected
Select and Unselect bytes differ
operation codes are exactly 0, 1, 2
candidate order preserved
duplicates rejected
selected key occurs exactly once
V2 domain digest recomputes
V2 decision identity recomputes
~~~

- [ ] **Step 6: Run A1 gates**

Run:

~~~powershell
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows -R "trajectory|trusted_trajectory_v2" --output-on-failure
ctest --preset dev-windows -R "teacher|episodic|identity" --output-on-failure
~~~

Require historical tests and all focused V2 tests to pass. Commit and stop for review.

### Task 3: TTV2-A2 V3 recorder and trajectory identities

**Files:**
- Create or modify: explicit V2 recorder declarations and implementation
- Modify: V2 codec declarations/implementation only where A1 exposes the required public projection
- Create: V2 recorder and identity tests
- Modify: CMakeLists.txt only for test registration

- [ ] **Step 1: Construct V2 frames from V3 public DecisionFrames**

Copy only public observation, request, V2 public identities, episode identity,
decision index, acting player, and the complete ordered candidate domain. Reject a
frame whose contract or identity generation is not V3/V2.

- [ ] **Step 2: Implement accepted transition lifecycle**

Preserve AtomicEngineResponse, IntermediateContinuation, FinalContinuationResponse,
NextFrame, Terminal, Interrupted, and Failed meanings and numeric codes. Preserve
pending interruption frames and successor index checks.

- [ ] **Step 3: Implement public gameplay identity V2**

Hash exactly the public fields frozen in A0. Prove that changing policy provenance or
collection disposition does not change the public gameplay identity.

- [ ] **Step 4: Implement trajectory record identity V2**

Bind the V2 public gameplay identity to the shared policy provenance, record
attribution, and collection disposition. Prove that changing collection provenance
changes only the record identity.

- [ ] **Step 5: Run A2 gates**

Cover TTV2-G07 through TTV2-G21 as applicable, including paired public-equivalent
worlds, fresh-process canonical bytes, and absence of submission token, engine step,
internal semantic key, and private locator. Commit and stop for review.

### Task 4: TTV2-A3 restricted replay and downstream acceptance boundary

**Files:**
- Create or modify: explicit V2 restricted replay evidence implementation
- Create or modify: replay/admission successor interfaces only after a separate A3 review of the A0 boundary
- Create: V2 replay and rejection tests
- Modify: receipt/shard files only when the approved A3 scope explicitly includes them

- [ ] **Step 1: Reconstruct V3 identity inputs**

Decode the V2 manifest environment identity input, require
CertifiedEnvironmentConfig::canonical_v3(), verify the V3 environment semantic ID,
and decode the episode identity input against that environment identity.

- [ ] **Step 2: Validate V2 evidence**

Require the V2 evidence ID, V2 trajectory ID, V3 environment ID, matching episode
identity, valid interruption reason, nonzero budgets, and bounded observed counts.

- [ ] **Step 3: Preserve historical replay rejection**

Prove that historical V1 replay remains V2-environment-only and rejects V3 values.
Prove that no V1 shard, receipt, admission artifact, or dataset validator accepts a
V2 trajectory by accident.

- [ ] **Step 4: Run A3 gates**

Run restricted replay, privacy, determinism, historical trajectory, and explicit
mixed-version rejection tests. Do not widen a V1 validator. Commit and stop for review.

### Task 5: TTV2-A4 TeacherRunnerV3 trusted trajectory wiring

**Files:**
- Modify: explicit V3 Runner trajectory adapter files approved by the A4 scope
- Create or modify: Runner-to-V2-trajectory focused tests
- Modify: CMakeLists.txt only for test registration

- [ ] **Step 1: Require a V2 trajectory boundary**

The Runner must refuse V1 trajectory recording when its policy session produces
V2 public-action keys. It must pass the complete V3 public frame and candidate domain
unchanged to the V2 recorder.

- [ ] **Step 2: Exercise accepted lifecycle**

Cover initial frame, selection, accepted transition, continuation, terminal closure,
interruption, failure, and exact pending-frame behavior without consulting hidden
engine state or reconstructing operation metadata.

- [ ] **Step 3: Run A4 gates**

Require Runner policy/session provenance, public equivalence, privacy,
determinism, V2 replay construction, and rejection of V1 trajectory paths. Stop for
review before any downstream collection.

### Task 6: TTV2-A5 downstream Task7/model/dataset migration

**Files:**
- Exact downstream files are selected only by a later authorization after A3 and A4
- No A0 file is broadened to pre-implement this slice

- [ ] **Step 1: Design downstream successor closure**

Classify each trajectory, admission, receipt, shard, dataset, model, and Task7
contract as shared or successor. Preserve historical V1 data as historical data.

- [ ] **Step 2: Add successor codecs and materialization**

Only after explicit authorization, implement the V2-compatible trajectory-to-dataset
and trajectory-to-model path with exact child identity and byte-order closure.

- [ ] **Step 3: Add collection and training gates**

Run the separately authorized Task7 collection gates. RUN_A, RUN_B, dataset
generation, and training remain forbidden until their own authorization is recorded.

## Verification checklist for every implementation slice

Before committing any future slice, execute:

~~~powershell
git diff --check
git diff --stat
git diff --name-only
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows --output-on-failure
~~~

Then verify:

~~~text
historical V1 bytes and goldens unchanged
V2/V3 mixed values fail closed
public-only input boundary preserved
candidate order and cardinality preserved
fresh-process canonical bytes identical
no trajectory/model/Task7 scope expansion
~~~

No slice may report a broader gate as passing when its command was not executed.

## Self-review

The plan covers the required A0 contract, exact candidate/frame/identity bytes,
source inventory, version-purity matrix, restricted replay boundary, historical V1
preservation, and deferred downstream surfaces. It intentionally separates logical
codec work, recorder/identity work, replay/admission, Runner wiring, and downstream
materialization so that no V1 validator is widened and no large implementation PR
can silently cross a contract boundary.
