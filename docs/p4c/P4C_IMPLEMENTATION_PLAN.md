# OCGForge Phase 4C — Implementation Plan

Status: **Task 4 CURRENT / AUTHORIZED — DOCS-ONLY INTEGRATION DECISION**

This plan is the dependency and acceptance boundary for the future
Phase-4C public battle-proof work. Tasks 1, 2, 2A, and 3 are accepted; this
branch records the Task-4 Teacher integration decision. It does not authorize
Teacher v2 production work, the Task-5 harness, or any later Phase-4C task.
Phase 4C is not final.

Task-1 base:

~~~text
32a1adedc50681fd3f5bf2d4b59f8fa3cd7a3030
~~~

The frozen derived identities are:

~~~text
ocgforge.public_battle_snapshot.v1
ocgforge.provable_lethal.v1
~~~

## Architecture and sequencing rules

The environment remains the sole owner of legal candidate construction,
candidate completeness/order, public action identity, public observation
projection, and V2 submission. The pinned ocgcore/rules bundle remains the
authority for battle legality and resolution. Teacher and evaluation code may
consume only the accepted public observation and the complete ordered public
candidate vector.

Phase-4C Battle/Lethal acceptance has its own probe owner. The accepted
Phase-4B `tools/p4b/teacher_probe.cpp` and its binary are not extended for
Phase 4C. The Phase-4C probe is test/evidence infrastructure only:

~~~text
tools/p4c/phase4c_battle_probe.cpp
build/dev-windows/phase4c_battle_probe.exe
~~~

No future task may silently:

- add fields or bytes to the accepted public observation;
- rebuild attack legality;
- query CoreHost, raw engine state, private PlayerObservation, or hidden
  locators;
- turn candidate absence into attack-history evidence;
- add battle facts to trajectory/replay/admission schemas;
- mutate Phase-4B Teacher v1 identities or meanings;
- introduce a future-action queue, private search, MCTS, ML, BC, RL, or
  self-play.

If an exact public input required for an attack classification or positive
lethal proof is missing, the work is BLOCKED/UNSUPPORTED until a separate
public-contract task is explicitly authorized. Do not expand Task 2 or Task 3
by implication.

## Task sequence

### Task 1 — Battle Facts / Provable Lethal contract freeze

**Status:** COMPLETED / ACCEPTED at `250f59f1abda357767b74c276ed2afba5f70329e`.

**Owning layer:** Project contract and acceptance documentation; no runtime
owner is changed.

**Exact files:**

~~~text
docs/p4c/P4C_PUBLIC_BATTLE_FACTS_CONTRACT.md
docs/p4c/P4C_PUBLIC_BATTLE_FACT_MATRIX.md
docs/p4c/P4C_IMPLEMENTATION_PLAN.md
docs/ROADMAP.md
~~~

**Invariants affected:** None in production. The task freezes public-only
input, exact candidate-domain preservation, strict joins, positive-only
lethal semantics, privacy, determinism, identity versioning, and future
acceptance requirements.

**Semantic changes:** None to executed gameplay. It creates the versioned
design identities and marks Phase 4C as future work.

**Internal-only changes:** None.

**Privacy implications:** The contract prohibits private state, hidden
identity, physical-card carry-over across shuffle, and private engine
queries. Redacted/stat-dependent joins fail closed.

**Determinism implications:** Future values must retain candidate order,
canonical reason ordering, checked integer arithmetic, and process-stable
serialization. No current runtime output changes.

**Replay implications:** No trajectory, replay, receipt, admission, or
dataset schema changes are authorized.

**Focused acceptance gates:** Docs-only verification consists of the two
existing rules/profile Python checks, git diff --check, and exact four-file
scope inspection. No CTest or build is run.

**Stop condition:** Satisfied by the accepted Task-1 contract freeze. No
Task-1 document is modified by Task 2A.

### Task 2 — PublicBattleSnapshotV1 DTO and extractor

**Status:** COMPLETED / ACCEPTED at `261ded8660780da1ba819e8c639f485e65a49360`.

**Owning layer:** ygo::teacher owns the value DTO/extractor. ygo::environment
continues to own the public observation and complete candidate inputs.

**Accepted implementation files:**

~~~text
include/ygo/teacher/public_battle_snapshot.hpp
src/teacher/public_battle_snapshot.cpp
tests/teacher/public_battle_snapshot_test.cpp
tools/p4c/phase4c_battle_probe.cpp
tests/teacher/phase4c_public_boundary_test.py
tests/teacher/phase4c_battle_determinism_test.py
tests/teacher/phase4c_battle_paired_world_test.py
CMakeLists.txt                  # registration only, if required
~~~

These files are the accepted Task-2 implementation scope. They remain
unchanged by Task 2A.

**Invariants affected:** Exact one-to-one candidate facts, supplied order,
public action-key membership, same-perspective visible joins,
RedactedSlot -> UNSUPPORTED, checked stat arithmetic, and no public
observation byte change.

**Semantic changes:** Adds only a derived, decision-local snapshot. It must
not classify an attack/direct/targeted/control shape until the live decoder
and public projection prove that exact equivalence class.

**Internal-only changes:** Decoder-shape assertions and safe-state lookup
helpers may be private to the extractor. No private engine access is allowed.

**Privacy implications:** Source/target locators are transient join inputs.
Current ATK/DEF/position comes only from the same perspective-safe public
entity. No passcode/database/CardScripts lookup may reconstruct a stat.

**Determinism implications:** Candidate facts are emitted in supplied order;
reason IDs are sorted/unique; keys are not regenerated; arithmetic is checked
and integer-only. An invalid or duplicate domain fails closed without repair.

**Replay implications:** The derived snapshot is not persisted in
DecisionRecord, shard, receipt, manifest, or replay requirements.

**Focused acceptance gates:** Public-boundary, N-record/order, visible-stat,
redacted/absent-stat, exact BattleCommand-shape, checked-arithmetic,
independent-process, and paired-world tests.

The Phase-4C probe initially covers PublicBattleSnapshotV1 in Task 2. It is
owned by Phase 4C and is not a modification of the accepted Phase-4B probe.

**Stop condition:** If any required attack distinction or public field cannot
be proven, stop with that capability BLOCKED and insert Task 2A below before
authorizing Task 3. Do not guess.

### Task 2A — Battle/Lethal public-boundary prerequisite decision

**Status:** COMPLETED / ACCEPTED at `5d09c3257dc1c2b432d24cc9da3fa715146cf27b`.

**Owning layer:** Contract/architecture documentation, informed by the
Environment/ocgcore protocol path. No runtime owner changes.

**Exact files:**

~~~text
docs/p4c/P4C_BATTLE_PROTOCOL_FLOW.md
docs/p4c/P4C_LETHAL_PREREQUISITE_DECISION.md
docs/p4c/P4C_IMPLEMENTATION_PLAN.md
~~~

**Invariants affected:** None in production. The decision preserves the
accepted public observation, snapshot, candidate-domain, privacy, determinism,
identity, and replay boundaries.

**Semantic changes:** Selects
POSITIVE_LETHAL_BLOCKED_UNDER_CURRENT_ACTION_CONTRACT. The live source proves
only that phase=1 selects an attackable-list entry; later target, response,
damage, and terminal semantics are not part of the current candidate.

**Internal-only changes:** The protocol-flow document records source-grounded
control flow and does not create a runtime semantic classifier.

**Privacy implications:** Hidden response availability, hidden effect-use
state, private card identity, and CoreHost state remain inaccessible. No
response-absence oracle is authorized.

**Determinism implications:** No bytes, keys, ordering, or process behavior
change. Future proof work remains integer-only, canonical, and current-input
bound.

**Replay implications:** No observation, action, DecisionRecord, trajectory,
replay, admission, receipt, or dataset schema changes.

**Focused acceptance gates:** Docs-only rules/profile Python checks and
git diff --check. No build or CTest is run.

**Stop condition:** Satisfied by the accepted Task-2A decision. Its decision
documents are not modified by Task 4.

**Architecture outcome:**

~~~text
PREREQUISITE_DECISION =
POSITIVE_LETHAL_BLOCKED_UNDER_CURRENT_ACTION_CONTRACT
TASK2A_REQUIRED_FOR_POSITIVE_LETHAL = YES
~~~

The exact marker mapping remains useful descriptive evidence, but a separate
derived BattleCommand classifier is not sufficient to make lethal provable
and is not authorized as Task 2B here.

### Task 3 — fail-closed ProvableLethalV1 evaluator

**Status:** COMPLETED / ACCEPTED at `baff374cc326f57d04d4d36013e213a51e1004a4`.

**Owning layer:** ygo::teacher/public evaluation layer. The rules bundle
remains the battle-resolution authority.

**Proposed exact files:**

~~~text
include/ygo/teacher/provable_lethal.hpp
src/teacher/provable_lethal.cpp
tests/teacher/provable_lethal_test.cpp
tests/teacher/provable_lethal_paired_world_test.cpp
tools/p4c/phase4c_battle_probe.cpp
tests/teacher/phase4c_battle_determinism_test.py
tests/teacher/phase4c_battle_paired_world_test.py
CMakeLists.txt                  # registration only, if required
~~~

**Invariants affected:** Positive-only proof, current-action boundary,
complete candidate membership, no optimistic damage/lethal conversion,
checked arithmetic, and NOT_PROVEN != PROVEN_NON_LETHAL.

**Semantic changes:** Adds the ProvableLethalV1 result boundary with a
fail-closed current implementation. PROVEN_LETHAL is allowed only when every
frozen proof requirement is satisfied by a separately available public or
equivalence proof; under the current Task-2A decision, current candidates
remain NOT_PROVEN/UNSUPPORTED.

**Internal-only changes:** A proof helper may compose snapshot facts and
rules-equivalence evidence, but may not simulate private state or duplicate
ocgcore battle resolution.

**Privacy implications:** No opponent hidden hand, hidden deck, face-down
identity, effect-use state, or private response availability may enter the
proof. Redacted stats remain unsupported.

**Determinism implications:** Exact current candidate, fixed operand order,
checked integer arithmetic, canonical proof-reason ordering, and independent
process equality are required. No float/RNG/future search.

**Replay implications:** Lethal results remain derived evaluation data and
are outside trajectory/replay/admission schemas.

**Focused acceptance gates:** Positive-proof negative matrix,
no-optimistic-false-positive matrix, missing-response/effect negative matrix,
checked-integer matrix, no-future-queue test, paired-world privacy, and
independent-process determinism. A positive example cannot be added without
closing the classified prerequisites.

Task 3 may modify the already authorized Phase-4C probe and the two named
determinism/paired-world producers only to extend public snapshot evidence
with ProvableLethalV1 evidence. It may not extend or repurpose the Phase-4B
probe.

**Stop condition:** Satisfied by the accepted fail-closed evaluator. No
positive lethal path is available under the current action contract.

### Task 4 — Teacher Battle/Lethal integration decision

**Status:** CURRENT / AUTHORIZED — docs-only decision on this branch.

**Owning layer:** Contract/architecture documentation, preserving
ygo::teacher, ygo::policy, and trajectory/provenance ownership. No runtime
owner changes.

**Exact files:**

~~~text
docs/p4c/P4C_TEACHER_INTEGRATION_DECISION.md
docs/p4c/P4C_IMPLEMENTATION_PLAN.md
docs/ROADMAP.md
~~~

**Invariants affected:** None in production. The decision keeps Teacher v1,
the complete public candidate domain, participant perspective, provenance,
NONE-RNG, and the trusted trajectory path unchanged.

**Semantic changes:** Selects Teacher v1 plus an evaluation-only
BattleSnapshot/ProvableLethal sidecar. It does not change action selection,
candidate scores, fallback, or strategy state.

**Internal-only changes:** Documentation of the future Task-5 composition;
no TeacherCoreV2 or policy adapter implementation.

**Privacy implications:** The sidecar receives only the same current public
observation and complete public candidate vector; it cannot bridge private
information into Teacher v1.

**Determinism implications:** Existing Teacher v1 output remains the
authority. Sidecar evidence is canonical, public-only, and independently
deterministic without RNG or candidate reordering.

**Replay implications:** No Battle/Lethal fields enter DecisionRecord,
trajectory, replay, admission, receipt, or dataset schemas.

**Focused acceptance gates:** Docs-only reference checks for this decision;
future Task-5 sidecar, fixed-matchup, identity, privacy, determinism, and
trusted-trajectory gates.

**Stop condition:** Commit only the three authorized decision files, push, and
leave Task 5 unauthorized.

### Task 5 — Frozen fixed-matchup Teacher evaluation harness

**Status:** NOT AUTHORIZED; requires the accepted Task-4 decision, not a
Teacher v2 implementation.

**Owning layer:** ygo::policy test/evaluation harness, using existing
environment, Teacher, trajectory, replay, admission, receipt, and dataset
owners.

**Proposed exact files, to be confirmed before authorization:**

~~~text
tools/p4c/phase4c_teacher_evaluation.py
tools/p4c/phase4c_teacher_probe.cpp
tests/teacher/phase4c_teacher_evaluation_test.py
tests/teacher/phase4c_teacher_trajectory_test.cpp
CMakeLists.txt
~~~

No Teacher-specific recorder, trajectory schema, receipt, or admission path
is permitted.

**Invariants affected:** Fixed matchup/rules/deck identity, participant
mapping, complete legal domain, public-only Teacher input, deterministic
selection, and trusted trajectory flow.

**Semantic changes:** Defines a bounded fixed-matchup Teacher-v1 evaluation
corpus, computes BattleSnapshot/ProvableLethal sidecar evidence, and reports
evaluation outcomes; it does not add battle facts to gameplay records.

**Internal-only changes:** Probe/process orchestration and test-only
comparison code.

**Privacy implications:** Include equal-public-world paired runs and search
outputs for hidden codes/locators/private IDs.

**Determinism implications:** Pin public corpus/profile/binding identities;
compare exact ordered evaluations, selected key, proof status, explanation,
and state delta across fresh processes.

**Replay implications:** Every accepted action continues through V2,
recorder, shard, semantic replay, admission, receipt, and dataset manifest.

**Focused acceptance gates:** Fixed matchup rows for the authorized deck/rule
binding, Teacher-v1 identity preservation, sidecar paired worlds,
trajectory/replay/admission compatibility, process determinism, and
Phase-4B regressions.

**Stop condition:** Any arbitrary-deck claim, hidden-state dependency, or
Teacher-specific trajectory shortcut stops the harness.

### Task 6 — Phase-4C final acceptance and evidence

**Status:** NOT AUTHORIZED; requires Tasks 1–5.

**Owning layer:** Acceptance tooling and generated evidence only.

**Proposed exact files, to be confirmed in a separate acceptance-scope review:**

~~~text
tools/p4c/phase4c_acceptance.py
tools/p4c/phase4c_clean_checkout_acceptance.ps1
tests/teacher/phase4c_acceptance_test.py
docs/p4c/P4C_ACCEPTANCE.md
docs/p4c/p4c_acceptance.json
~~~

Generated evidence must be produced by the accepted executable harness, not
hand-edited.

**Invariants affected:** Exact gate identity/cardinality, source-head
binding, fixed-matchup scope, no false PASS, and clean-checkout
reproducibility.

**Semantic changes:** None to gameplay; records executable evidence for the
already frozen contracts.

**Internal-only changes:** Gate runners, validators, and deterministic
rendering of one evidence model into JSON and Markdown.

**Privacy implications:** Evidence may contain only public-safe identities
and approved contract/provenance values; no hidden state or private locator.

**Determinism implications:** Exact command labels, exit status/cardinality,
stdout/stderr digests, source head, and canonical generated documents; no
timestamp/path identity.

**Replay implications:** Acceptance validates existing replay/admission
compatibility without adding Battle/Lethal fields to those schemas.

**Focused acceptance gates:** All proposed P4C-G00 through P4C-G14, fixed
matchup matrix, clean-checkout execution, generated-evidence self-validation,
and exact scope/parentage review.

**Stop condition:** Any missing producer, wrong cardinality, unexecuted
evidence, dirty checkout, source mismatch, or unsupported positive proof
prevents a final Phase-4C claim.

## Proposed Phase-4C gate matrix

These are future gate definitions only. None is executed by Task 1. Their
status is NOT_AUTHORIZED until the owning implementation task authorizes and
runs the producer. A gate may be PASS only on actual evidence.

| Gate | Invariant | Proposed evidence producer / command | PASS condition | Fail-closed behavior |
| --- | --- | --- | --- | --- |
| P4C-G00 | Battle consumer is public-only | python -B tests/teacher/phase4c_public_boundary_test.py plus a single-target public-boundary CTest | Only PublicEnvironmentObservation and complete public candidates cross the public API; forbidden private types absent | Missing boundary proof is FAIL/BLOCKED |
| P4C-G01 | Exact N-candidate preservation | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^public_battle_snapshot_test$" | One fact per supplied candidate, exact count/order/key membership, no filtering | Malformed/duplicate domain is INVALID; no repair |
| P4C-G02 | Visible current ATK/DEF extraction | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^public_battle_snapshot_test$" | Exact same-perspective visible join copies only current public stats | Missing current stat is UNSUPPORTED; bad join is INVALID |
| P4C-G03 | Redacted/absent stats fail closed | Same snapshot test target with explicit RedactedSlot/absent-stat cases | No hidden lookup; redacted or missing stat never becomes a value | UNSUPPORTED, never guessed |
| P4C-G04 | BattleCommand shape correctness | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^battle_command_shape_test$" | Exact current decoder/projection shapes are documented and tested; unproven subtypes stay unsupported | No phase/name heuristic; UNSUPPORTED |
| P4C-G05 | Independent-process determinism | python -B tests/teacher/phase4c_battle_determinism_test.py --probe build/dev-windows/phase4c_battle_probe.exe | Identical public corpus yields byte/canonical-identical snapshots and lethal results | Any mismatch is FAIL; no environment-specific fallback |
| P4C-G06 | Equal-public-world privacy | python -B tests/teacher/phase4c_battle_paired_world_test.py --probe build/dev-windows/phase4c_battle_probe.exe | Different hidden/private worlds with equal public inputs yield identical derived outputs | Hidden-dependent output is FAIL |
| P4C-G07 | Checked integer arithmetic | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^provable_lethal_test$" | Exact signed subtraction/bounds; overflow/underflow is rejected | INVALID; no wrap/clamp/saturate |
| P4C-G08 | No optimistic false positives | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^provable_lethal_test$" | No PROVEN_LETHAL without all positive-proof conditions; ATK-versus-LP alone fails | NOT_PROVEN/UNSUPPORTED |
| P4C-G09 | Missing response/effect proof | Same lethal test target with response/modifier/effect gaps | Unproven response, modifier, immunity, target, or continuation prevents positive proof | NOT_PROVEN/UNSUPPORTED |
| P4C-G10 | No future queue/search | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^provable_lethal_test$" | Only current decision is evaluated; no future candidate/action/private search | Capability remains unsupported |
| P4C-G11 | Phase-4B Teacher v1 immutable | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^phase4b_teacher_identity_regression_test$" | v1 identities/outputs remain unchanged; v2 changes use new identities | Integration is blocked |
| P4C-G12 | Trajectory/replay/admission compatible | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(trajectory_codec_test|trajectory_recorder_test)$" | Existing trusted path remains byte/semantic compatible and no Battle fields enter schemas | No shortcut; FAIL |
| P4C-G13 | Locked deck/rules identity unchanged | python -B tests/policy/rules_deck_identity_test.py | Fixed certified rules/matchup/deck identities remain exact | Scope is FAIL |
| P4C-G14 | Phase-4B regression | ctest --test-dir build/dev-windows --output-on-failure --no-tests=error --tests-regex "^(teacher_goal_line_test|teacher_recovery_test|teacher_fallback_test|teacher_explanation_test)$" | Existing Phase-4B Teacher behavior remains green under the new work | No v1 mutation; gate fails |

The G00–G14 commands are proposed future commands, not current execution
evidence. A future acceptance task must also prove target cardinality before
accepting a result and must report missing targets as FAIL/BLOCKED, never as
PASS.

## Current authorization and non-claims

At the end of this Task 4 decision:

~~~text
Task 1 — Battle Facts / Provable Lethal contract freeze
FINAL PASS

Task 2 — Battle snapshot implementation
FINAL PASS

Task 2A — Battle/Lethal prerequisite decision
FINAL PASS

Decision:
POSITIVE_LETHAL_BLOCKED_UNDER_CURRENT_ACTION_CONTRACT

Task 3 — fail-closed ProvableLethal evaluator
FINAL PASS

Task 4 — Teacher Battle/Lethal integration decision
CURRENT / AUTHORIZED — docs-only

Decision:
TEACHER_V1_PLUS_EVALUATION_SIDECAR

Task 5 — frozen evaluation harness
NOT AUTHORIZED

Task 6 — Phase-4C acceptance/evidence
NOT AUTHORIZED
~~~

No Phase-4C gate has run. No positive lethal capability has been implemented
or accepted. This plan does not claim arbitrary decks, battle simulation,
ML, or a change to Phase-4B Teacher behavior.
