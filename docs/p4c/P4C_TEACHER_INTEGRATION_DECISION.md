# OCGForge Phase 4C Task 4 — Teacher Battle/Lethal Integration Decision

Status: **CURRENT / AUTHORIZED — DOCS-ONLY ARCHITECTURE DECISION**

Base:

~~~text
baff374cc326f57d04d4d36013e213a51e1004a4
~~~

Accepted references:

~~~text
Public battle snapshot:
ocgforge.public_battle_snapshot.v1

Provable lethal:
ocgforge.provable_lethal.v1

Task 3 final implementation:
baff374cc326f57d04d4d36013e213a51e1004a4
~~~

This document decides whether the currently accepted Battle/Lethal
derivations justify changing Teacher action selection. It does not implement
TeacherCoreV2, modify Teacher v1, change policy production code, or authorize
Task 5.

## 1. Decision

The selected outcome is:

~~~text
TEACHER_V1_PLUS_EVALUATION_SIDECAR
~~~

The current public Battle/Lethal layer is useful evidence, but it contains no
supported new semantic signal that should change action selection or
strategy-state behavior:

~~~text
Teacher v1
    -> selects exactly as in accepted Phase 4B

PublicBattleSnapshotV1
    -> derived current public battle evidence

ProvableLethalV1
    -> derived fail-closed lethal evidence

both
    -> evaluation/audit sidecar only
~~~

No Teacher v2 identity is created merely to carry evidence that is not
consumed by gameplay selection.

The frozen decision summary is:

~~~text
CURRENT_BATTLE_SIGNAL_CHANGES_SELECTION = NO
CURRENT_LETHAL_SIGNAL_CHANGES_SELECTION = NO
NEW_SCORE_CONTRIBUTION = NO_NEW_SCORE_CONTRIBUTION
NEW_STATE_TRANSITION_SEMANTICS = NO_NEW_STATE_TRANSITION_SEMANTICS
~~~

## 2. Evidence from accepted Task 2A and Task 3

Task 2A established that the current public phase=1 BattleCommand selects an
entry from ocgcore's attackable list, but does not publish a complete
attack/direct/target/resolution proof.

Task 3 then evaluated the accepted PublicBattleSnapshotV1 corpus:

- non-battle candidates become NOT_APPLICABLE;
- candidate-level invalid input becomes INVALID without dropping its record;
- current BattleCommandUnclassified candidates become UNSUPPORTED;
- every current result has no guaranteed loss lower bound;
- no result is PROVEN_LETHAL, including the high-ATK/low-LP trap;
- paired hidden worlds produce identical snapshot and lethal evidence;
- independent processes produce identical snapshot and lethal bytes.

The accepted limitation remains:

~~~text
POSITIVE_LETHAL_CAPABILITY =
BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT
~~~

UNSUPPORTED means that the required proof is unavailable. It is not a
negative strategic evaluation and must not lower a candidate's score.

## 3. Evaluation of the required outcomes

### A — TEACHER_V2_REQUIRED_NOW

Rejected. No currently accepted Battle/Lethal result provides a supported
fact that should alter the current selected public action, candidate score,
fallback level, or strategy-state transition.

The existence of a value-owned DTO, an UNSUPPORTED result, or an audit
diagnostic is not by itself a new gameplay semantic.

### B — TEACHER_V1_PLUS_EVALUATION_SIDECAR

Selected. The sidecar can report public battle coverage, unsupported
capabilities, false-positive traps, paired-world equality, deterministic
bytes, and audit diagnostics while leaving Teacher selection unchanged.

### C — PHASE4C_BLOCKED

Rejected. The fixed-matchup evaluation goal can be completed with Teacher v1
and a derived Battle/Lethal sidecar. No Teacher semantic change is required
to evaluate the current fail-closed limitation.

## 4. Frozen sidecar boundary

The future Task-5 harness uses the following composition:

~~~text
PublicEnvironmentObservation
+ complete EnvironmentActionCandidate[]
        │
        ├── Teacher v1
        │     ↓
        │   PolicySelection
        │
        └── PublicBattleSnapshotV1
              ↓
            ProvableLethalV1
              ↓
            evaluation/audit evidence
~~~

Battle/Lethal receives the same current public boundary independently. It
must not feed derived evidence back into Teacher v1.

The sidecar may be used for:

- public battle coverage reporting;
- unsupported-capability reporting;
- false-positive detection;
- privacy comparison;
- independent-process determinism comparison;
- audit diagnostics;
- explicit reporting of the accepted positive-lethal limitation.

The sidecar must not change:

- selected public action key;
- candidate scores;
- fallback stage;
- V2 submission;
- strategy-state transition;
- trajectory DecisionRecord;
- semantic replay;
- AdmissionReceipt;
- DatasetManifest;
- Teacher state;
- participant or deck assignment.

No Battle/Lethal field is added to a trusted trajectory schema.

## 5. Score-contract analysis

The accepted Teacher score vector remains nine-dimensional and its ordering
does not change.

| Score dimension | Current Battle/Lethal contribution | Decision |
| --- | --- | --- |
| ScoreDimension 0: SurvivalOrGuaranteedLethalClass | No proven lethal class or guaranteed loss bound exists | NO_NEW_SCORE_CONTRIBUTION |
| ScoreDimension 7: BattleAndMainPhase2Value | An attackable-list marker and public stat availability do not prove battle outcome or phase value | NO_NEW_SCORE_CONTRIBUTION |

In particular, the following interpretations are forbidden:

~~~text
UNSUPPORTED lethal -> negative score
attackable-list entry -> positive battle score
high ATK -> lethal bonus
missing proof -> proven non-lethal
~~~

Any future score contribution needs an explicit semantic contract proving
exact public meaning, failure behavior, privacy, determinism, and provenance.

## 6. Strategy-state analysis

The current Battle/Lethal outputs provide no supported state transition
semantic. They must not alter:

- active goal;
- active line;
- completed line nodes;
- achieved goals;
- public resource facts;
- public restriction facts;
- public threat facts.

The sidecar result is decision-local derived evidence. It does not become
Teacher strategy memory. In particular, no transient public locator,
passcode, BattleSnapshot value, lethal result, or hidden-derived hash enters
EpisodeLocalStrategyStateV1.

The accepted Teacher v1 state lifecycle remains unchanged:

~~~text
participant-safe current observation
    -> Teacher v1 reconciliation/proposal
    -> accepted Task-6 state transition
~~~

Battle/Lethal sidecar computation is observational and non-authoritative.

## 7. Semantic identity and provenance

Because the sidecar does not affect selection, scores, fallback, or state:

~~~text
Teacher semantic identity:
ocgforge.policy.teacher_core.v1       unchanged

StrategyProfile IDs                         unchanged
TeacherPolicyBinding IDs                   unchanged
PolicyArtifact IDs                         unchanged
Trajectory/replay/admission schemas       unchanged
~~~

No v2 identity is created for evaluation-only data.

If a later task changes Teacher decisions using Battle/Lethal evidence, that
task must create before publication:

- a new TeacherCore semantic identity;
- new TeacherPolicyBinding identities;
- new PolicyArtifact identities;
- a new predicate-registry identity if new predicate semantics are added.

Existing admitted Phase-4B trajectories remain interpreted under their
original v1 provenance. A future v2 policy must not silently rewrite their
meaning.

## 8. Task-5 consequence

Task 5 no longer depends on implementing TeacherCoreV2. Its frozen future
shape is:

~~~text
Frozen fixed-matchup Teacher-v1 evaluation harness
+ PublicBattleSnapshotV1 sidecar evaluation
+ ProvableLethalV1 sidecar evaluation
+ admitted Teacher trajectories
~~~

The harness must bind and report the exact current identities for:

- both accepted Teacher v1 profiles;
- both accepted TeacherPolicyBinding values;
- both accepted PolicyArtifact values;
- PublicBattleSnapshotV1;
- ProvableLethalV1;
- the fixed matchup, rules bundle, and locked decks.

It must exercise normal and mirror seat assignments with starting player 0
and 1. Every accepted Teacher action remains on the existing trusted path:

~~~text
Teacher v1
→ PolicySelection
→ EpisodicEnvironment V2
→ TrajectoryRecorder
→ CandidateTrajectoryShard
→ semantic replay
→ admission
→ receipt/dataset
~~~

The sidecar is computed for evaluation evidence and does not bypass or
modify any step of that path.

## 9. Task-6 consequence

Phase-4C final acceptance may pass with the explicit capability limitation:

~~~text
POSITIVE_LETHAL_CAPABILITY =
BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT
~~~

That acceptance is valid only if the evidence proves the limitation is:

- explicit;
- deterministic;
- privacy-safe;
- fail-closed;
- non-optimistic;
- isolated from Teacher v1 gameplay selection.

Phase-4C acceptance must not claim:

- general provable lethal;
- complete battle resolution;
- arbitrary-deck battle intelligence;
- a Teacher v2 gameplay policy;
- ML capability.

## 10. Authorization state

~~~text
Task 1  FINAL PASS
Task 2  FINAL PASS
Task 2A FINAL PASS
Task 3  FINAL PASS
Task 4  CURRENT / AUTHORIZED — integration decision
Task 5  NOT AUTHORIZED
Task 6  NOT AUTHORIZED

INTEGRATION_DECISION =
TEACHER_V1_PLUS_EVALUATION_SIDECAR
~~~

No production or test implementation is authorized by this document.
