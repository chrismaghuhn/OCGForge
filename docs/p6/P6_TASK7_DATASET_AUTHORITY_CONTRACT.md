# OCGForge Phase 6 Task7 Dataset Authority Contract

## Status and scope

**Status:** **PROPOSED / pending independent review**.

```text
TASK=P6_TASK7_DATASET_AUTHORITY_CONTRACT_FREEZE_01
BASE=c19fda5ac8fafe3807a5873321a3d11630c391f0

TASK7_DATASET_AUTHORITY=DESIGN_ONLY
TASK7_DATASET_AUTHORITY_READY=NO
TASK7_EXECUTION_CONTRACT_FREEZE=AUTHORIZED_BUT_BLOCKED_BY_DATASET_AUTHORITY

TRAJECTORIES_GENERATED=NO
DATASET_GENERATED=NO
TRAINING_RUN=NO
CHECKPOINT_CREATED=NO
MEANINGFUL_EVALUATION_EXECUTED=NO
```

This document freezes the smallest proposed authority boundary for the first
bounded Task7 Teacher collection. It specifies how a later provisioning
implementation may produce trusted collection artifacts, an immutable
`DatasetManifestV1`, a deterministic `TrainingDatasetSplitV1`, and one exact
public `CardVocabularyV1`. It creates none of those artifacts.

The contract does not authorize the provisioning implementation, Task7
execution-contract freeze, a learner, optimizer steps, training, checkpoint
creation, or meaningful evaluation. An identity whose content depends on a
future generated artifact is explicitly `UNISSUED_UNTIL_PROVISIONING`; no
placeholder digest is an authority.

The normative priority remains:

```text
correctness
→ determinism
→ information safety
→ complete legal decisions
→ replay/auditability
→ maintainability
→ performance
→ ML scale
```

## 1. Live audited baseline

The design was prepared against the exact fetched live baseline:

```text
origin/main=c19fda5ac8fafe3807a5873321a3d11630c391f0
PR #60 merged=YES
merge parents:
  122948309eac068f66a85dc9f51d4390fdf55f74
  119a888a3cb6ae8b8c6eb13b833b91f17ca71e25
merged tree=9838ac68ee582bcfc3c16f6fb19045398eb4c14a
post-merge CI run 33958773985=completed/success
```

The current checkout was clean before this documentation change. No current
Task7 DatasetManifest, admission-receipt bundle, trajectory shard bundle, or
split artifact is present in `main`.

The historical Task4B artifacts remain immutable evidence only. Decoding
`docs/p6/task4b/corpus.authority.p6a` shows a Task4-derived
`CorpusAdmissionAuthorityV1` with eight sample records and the historical
dataset/split/vocabulary identities, but not the normative
`DatasetManifestMember` fields (`admission_receipt_id`, candidate-shard hash,
and episode-envelope hash). The Task4B execution report and its strings are
not DatasetManifest authority.

## 2. Purpose and authority chain

The purpose is to define one bounded, replayable, public-only Teacher corpus
that can later feed the already accepted Phase-6 materialization path:

```text
frozen Task7 collection job
        ↓
accepted deterministic Teacher sessions
        ↓
EpisodicEnvironment V2
        ↓
complete terminal episode
        ↓
TrajectoryRecorder
        ↓
semantic replay
        ↓
admission
        ↓
VerifiedAdmissionReceipt
        ↓
EpisodeEnvelope + CandidateTrajectoryShard + restricted evidence
        ↓
DatasetManifest v1
        ↓
TrainingDatasetSplitV1
        ↓
CardVocabularyV1
        ↓
Phase6BcSampleV1 / Task7 exact materialization
```

Ownership is deliberately split:

| Surface | Owner | This contract may do |
| --- | --- | --- |
| rules and legal gameplay | pinned rules bundle and `EpisodicEnvironment V2` | bind exact existing identities only |
| Teacher policy behavior | published Teacher profiles, bindings, and `TeacherRunner` | bind exact artifacts and deterministic sessions |
| trajectory and replay | `TrajectoryRecorder` and Phase-3B semantic replay | require and verify existing V1 artifacts |
| admission | Phase-3B admission and receipt authority | require a verified receipt before membership |
| DatasetManifest membership | `ocgforge.dataset_manifest.v1` | aggregate verified members only |
| train/validation/test split | `ocgforge.phase6.dataset_split.v1` | derive the deterministic episode partition |
| public card mapping | `CardVocabularyV1` | derive one immutable public-passcode mapping |
| Task7 physical rows | accepted Task7 materializer | consume the admitted public Phase-5 values |
| collection schedule | this Task7 collection contract | define orchestration only; not dataset membership |

No schedule, report, cache, or job identity becomes a replacement for the
DatasetManifest, trajectory, replay, admission, sample, or model-input
authority.

## 3. Task4 smoke is not Task7 data

The historical Task4B corpus is explicitly rejected as Task7 training data:

```text
TASK4B_CORPUS_ROLE=SMOKE_ONLY
TASK4B_CORPUS_IS_TASK7_MEANINGFUL_DATASET=NO
TASK4B_DATASET_REUSED_FOR_TASK7=NO
```

The Task4 corpus generator used the exact smoke seeds
`{2,3,5,7,11,13,17,19}`, normal seating only, and
`semantic_action_budget = 1`. That surface intentionally captures a single
decision-shaped smoke projection using
`ocgforge.phase6.task4.numeric_projection.v1`; it cannot prove complete
Teacher episodes or Task7 lossless training membership.

The later collection must use the normal TeacherRunner → recorder → replay →
admission path and must reject one-decision budget exhaustion. The Task4
numeric projection, Task4 corpus artifact, Task4 architecture, and Task4
smoke checkpoint remain historical/provisional surfaces and are never
silently upgraded.

## 4. Fixed Task7 curriculum and Teacher authority

The collection is restricted to the existing accepted curriculum:

| Field | Frozen value |
| --- | --- |
| matchup | `ocgforge.matchup.swordsoul_salamangreat.v1` |
| rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |
| format | `TCG_ADVANCED_2026_05_18` |
| duel mode | `DUEL_MODE_MR5` |
| duel flags | `190464` |
| seat role 0 | `ocgforge.swordsoul_tenyi.ml_v1`; SHA-256 `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` |
| seat role 1 | `ocgforge.salamangreat.ml_v1`; SHA-256 `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` |

The exact accepted Teacher identities are copied without regeneration:

| Deck role | Strategy profile | Teacher binding | PolicyArtifact |
| --- | --- | --- | --- |
| Swordsoul Tenyi | `ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74` | `ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c` | `policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d` |
| Salamangreat | `ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55` | `ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56` | `policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527` |

Both participants use these exact published deterministic Teacher sessions:

```text
teacher producer = ocgforge.policy.teacher_core.v1
selection        = ocgforge.policy.deterministic_lexicographic_argmax.v1
policy RNG       = ocgforge.no_policy_rng.v1
```

The acting participant's deck role, assignment, profile, binding, artifact,
rules bundle, and locked deck hash must all agree. A RandomLegal, heuristic,
fallback, unbound artifact, changed profile, or changed Teacher content is a
collection failure, not a new positive-label source.

The downstream model-facing contracts remain the already accepted values and
are not redefined here:

```text
ocgforge.model_logical_input.v1
ocgforge.model_encoded_input.v1
ocgforge.model_batch_layout.v1
ocgforge.model_card_vocabulary.v1
ocgforge.model_input_identity.v1

Task7 materialization schema:
    ocgforge.phase6.task7.input_materialization.v1
Task7 materialization configuration:
    phase6_task7_input_materialization_config.v1.
    20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a
```

The collection authority must provide public Phase-5 values to that accepted
Task7 materializer after admission. It does not create a second input or
dataset authority.

## 5. Collection schedule identity and job schema

The future provisioner uses two derived orchestration surfaces. They are not
DatasetManifest authority and their content identities are not issued by this
documentation task:

```text
collection schedule schema:
    ocgforge.phase6.task7.dataset_collection_schedule.v1
collection schedule identity:
    phase6_task7_dataset_collection_schedule.v1.<sha256>

collection job schema:
    ocgforge.phase6.task7.dataset_collection_job.v1
collection job identity:
    phase6_task7_dataset_collection_job.v1.<sha256>
```

All canonical schedule/job bytes use the already accepted primitive family:
length-prefixed UTF-8 strings, big-endian unsigned integers, booleans as
`0x00/0x01`, and vectors with an explicit count. No document text, path,
PID, clock, allocation address, worker number, or completion order enters an
identity.

The canonical `Task7CollectionJobV1` field order is:

```text
identity domain:string
identity schema:string
collection profile:string = ocgforge.phase6.task7.dataset_collection.reference.v1
environment contract:string = ocgforge.episodic_environment.v2
matchup:string
rules bundle:string
format:string
duel mode:string
duel flags:u64
root seed:u64
seat assignment:string = NORMAL or MIRROR
starting player:u8
seat-0 deck role:string
seat-0 deck content SHA-256:string
seat-1 deck role:string
seat-1 deck content SHA-256:string
seat-0 Teacher PolicyArtifact identity:string
seat-0 Teacher binding identity:string
seat-1 Teacher PolicyArtifact identity:string
seat-1 Teacher binding identity:string
Teacher producer identity:string
Teacher selection identity:string
Teacher RNG identity:string
engine-process budget:u64
semantic-action budget:u64
cancellation reason:string
cancellation source:string
collector semantic version:string
collector semantic source commit:string
```

The individual job identity excludes job ordinal and excludes DatasetManifest,
split, vocabulary, checkpoint, and completion-result fields. The schedule
identity additionally binds the ordered vector of job identities and the
ordered seed/placement/starting-player vectors. Reordering the vector changes
the schedule identity; it never changes an unchanged individual job identity.

## 6. Exact bounded schedule

The frozen Task7 collection schedule is:

```text
seed vector       = [4, 6, 8, 9]
placement vector  = [NORMAL, MIRROR]
starting players  = [0, 1]
job count         = 4 × 2 × 2 = 16
```

Placements are exactly:

| Placement | Seat 0 | Seat 1 |
| --- | --- | --- |
| `NORMAL` | Swordsoul Tenyi | Salamangreat |
| `MIRROR` | Salamangreat | Swordsoul Tenyi |

Every seat runs the Teacher for the deck role assigned to that seat. This is
a Teacher-vs-Teacher collection, not the Task5 checkpoint-policy matchup.

The canonical nested iteration is:

```text
for seed in [4, 6, 8, 9] in this order:
    for placement in [NORMAL, MIRROR] in this order:
        for starting_player in [0, 1] in this order:
            emit one Task7CollectionJobV1
```

The vector is not sorted by computed identity, filesystem name, hash-map
order, or completion time. There are no duplicate jobs and no implicit
replacement jobs.

The seed vector is the smallest bounded four-seed round chosen for this
reference collection: two placements and two starting-player values are the
minimum role/seat/start symmetry, and four independent seeds give four
complete episodes per placement/start coordinate. It is disjoint from the
frozen Task5 meaningful evaluation seeds `[1,2]` and from the Task4 smoke
seeds `{2,3,5,7,11,13,17,19}`. The accepted fixed-deck full-game calibration
contains 16 complete games with 1,502–1,524 engine processes and 612–621
interactive decisions per game. That calibration is not Task7 data, but it
supports a bounded collection of complete episodes rather than a one-decision
sample. The resulting corpus is a first replayable reference, not a
statistically powered strength evaluation and not a general Yu-Gi-Oh! claim.

## 7. Run-control and complete-episode eligibility

Every scheduled job uses the existing `environment::RunControl` and
`EpisodeSpec` surfaces with these exact collection values:

```text
EpisodeSpec.contract_id       = ocgforge.episodic_environment.v2
RunControl.engine_process_budget = 20000
RunControl.semantic_action_budget = 20000
RunControl.cancellation.reason   = ADMINISTRATIVE_CANCEL
RunControl.cancellation.source   = phase6-task7-dataset-authority-provisioning
build_full_observation           = true (existing V2 environment path)
instrumentation                  = false for semantic collection
```

The 20,000/20,000 ceilings are hard bounds, not a target and not a license to
claim a terminal result. They are aligned with the existing long-run
environment tests and leave substantial headroom over the accepted fixed-deck
calibration. If a Teacher episode reaches either budget before a true engine
terminal, it closes as `INTERRUPTED` and is ineligible for this training
corpus. If the environment or protocol closes as `FAILED`, it is ineligible.

Only the following result is eligible for a DatasetManifest member:

```text
EpisodeEnvelope.closure = TERMINAL
collection disposition = CLEAN
valid trajectory_record_id exists
semantic replay succeeds
admission succeeds
VerifiedAdmissionReceipt exists
```

An `INTERRUPTED` closure, including budget exhaustion or administrative
cancellation, is never silently converted into a complete Teacher trajectory.
The existing restricted replay companion is retained when required by the
V1 admission contract, but the interrupted episode is not admitted to the
Task7 training manifest. A `FAILED` or quarantined closure has no trusted
trajectory identity and cannot be a member.

The fixed schedule requires all 16 jobs to produce clean terminal, admitted
episodes. If any job fails, is interrupted, is quarantined, or cannot be
replayed/admitted, the collection run is `UNUSABLE_FOR_TASK7` and no combined
DatasetManifest or split identity is issued. There is no ad-hoc retry, seed
substitution, fallback policy, partial-manifest publication, or removal of a
problematic job to improve statistics.

## 8. Required execution path and policy semantics

The future implementation must reuse `TeacherRunner` for each job and must
construct both participant sessions from the exact published role identities.
The normal sequence remains:

```text
TeacherPolicySession::select
    → Environment ActionSelection using the selected public_action_key
    → Environment step
    → Teacher session commit
    → TrajectoryRecorder record
    → semantic replay
    → admission
```

The Environment remains the only legality, continuation, response, and
engine-advancement authority. The Teacher never queries `CoreHost` directly,
never fabricates a candidate, and never submits response bytes. Policy
selection remains deterministic lexicographic argmax with no policy RNG. A
Teacher or environment failure is attributed to the existing collection or
admission failure semantics; it is never relabeled as a Task7 learner failure.

## 9. Artifact bundle and authority relationships

The provisioner must persist the following immutable bytes for every scheduled
job and the combined collection. All future content identities are
`UNISSUED_UNTIL_PROVISIONING`.

| Artifact | Owner/schema | Canonical content and binding | Authority status |
| --- | --- | --- | --- |
| EpisodeEnvelope | trusted trajectory V1 / `ocgforge.trusted_trajectory.v1` | exact canonical envelope bytes; `episode_envelope_sha256` | immutable trusted episode input |
| CandidateTrajectoryShard | trajectory shard V1 | canonical shard bytes containing the exact envelope entry; `candidate_shard_artifact_sha256` | immutable physical admission input |
| RestrictedCollectionEvidenceBundle | restricted/replay V1 | canonical restricted evidence bytes; required for interruption/replay verification and retained exactly when emitted | admission companion, not learner data |
| AdmissionReceipt | `ocgforge.admission_receipt.v1` | canonical receipt bytes and `admission_receipt.v1.<sha256>` ID | proof of whole-shard replay/admission |
| DatasetManifest | `ocgforge.dataset_manifest.v1` | canonical ordered member bytes; `dataset_semantic_id` from member `trajectory_record_id` values | sole membership authority |
| TrainingDatasetSplitV1 | `ocgforge.phase6.dataset_split.v1` | canonical partition bytes; `phase6_dataset_split.v1.<sha256>` | sole episode partition authority |
| CardVocabularyV1 | `ocgforge.model_card_vocabulary.v1` | canonical ascending public-passcode bytes; `model_card_vocabulary.v1.<sha256>` | sole public ID mapping |
| collection schedule/job manifest | Task7 collection schedule/job schemas above | canonical ordered schedule/job vectors | orchestration/provenance only |
| collection execution report | future derived report schema | generated from job outcomes and artifact identities | derived evidence only |
| collection completion evidence | future derived acceptance record | generated, never hand-edited | derived evidence only |

Paths, filenames, worker order, process IDs, clocks, and cache formats are
locators or execution provenance only. They never enter DatasetManifest,
dataset-semantic, split, sample, or model-input identity.

## 10. DatasetManifest construction and re-verification

For each of the 16 successful jobs, the provisioner must obtain the exact
`VerifiedAdmissionReceipt` and the corresponding canonical bytes. The
combined manifest uses the existing V1 member shape without extension:

```text
DatasetManifestMember {
    trajectory_record_id
    public_gameplay_trajectory_id
    admission_receipt_id
    candidate_shard_artifact_sha256
    episode_envelope_sha256
}
```

Construction rules are:

1. accept only clean terminal episodes whose normal replay and admission
   succeeded;
2. verify every receipt from its canonical bytes and verify its shard,
   restricted companion, and envelope commitments;
3. require each scheduled job's expected single episode member and reject
   missing, extra, duplicate, or conflicting members;
4. sort manifest members strictly by `trajectory_record_id`, as required by
   `ocgforge.dataset_manifest.v1`;
5. derive `dataset_semantic_id` only from that exact ordered record-ID vector;
6. re-run the existing DatasetManifest validator against all verified
   receipts and commitments; and
7. publish no manifest if any member cannot be independently reverified.

The manifest does not contain the collection schedule identity, training seed,
batch layout, Task7 materialization bytes, checkpoint identity, or filesystem
path. It is the membership authority defined by the existing V1 contract; no
Task4B report or `corpus.authority.p6a` field may fill a missing member field.

## 11. Split derivation and empty-partition policy

The provisioner reuses exactly:

```text
split schema/contract = ocgforge.phase6.dataset_split.v1
partition rule       = ocgforge.phase6.split.fixed_80_10_10_sha256.v1
```

Only after the DatasetManifest has been validated may it collect the unique
`episode_semantic_id` values and call the existing
`make_phase6_split_v1`/equivalent authority path. The split vectors are the
deterministic SHA-256 80/10/10 partition, sorted by unsigned UTF-8 episode
identity, disjoint, and exhaustive over the manifest's episode set. No
individual decision or physical row is repartitioned.

The predeclared empty-partition rule is:

```text
EMPTY_PARTITION_POLICY=FAIL_CLOSED_NO_TASK7_AUTHORITY
```

If train, validation, or test is empty, non-exhaustive, overlapping, or fails
identity recomputation, the collection is unusable for Task7 and no split or
Task7 execution binding is issued. There is no manual episode movement,
statistical repair, silent expansion, or retry with an unlisted seed.

The future implementation must recompute the split in a fresh process and
compare canonical bytes and identity, not merely compare counts.

## 12. CardVocabulary derivation

One immutable vocabulary is derived only from accepted public Phase-5 source
values across the admitted collection, before encoded IDs are consumed:

```text
known public entity passcodes
+ visible-event public passcodes
+ known public own/opponent deck passcode vectors
    → unique ascending nonzero u32 passcodes
    → CardVocabularyV1::from_ascending_passcodes
```

The source is the public logical/model-facing value; no catalog lookup may
fill an absent value or recover a redacted identity. The same vocabulary must
then encode every admitted sample. A known public passcode absent from the
immutable list fails closed.

The reserved values remain exactly:

```text
ID 0 = PAD only
ID 1 = PUBLIC_UNKNOWN_OR_REDACTED
ID >= 2 = ascending public-passcode rank plus two
```

The canonical vocabulary bytes and identity are persisted as an independent
artifact and bound into every encoded sample and later Task7 checkpoint. The
historical Task4 vocabulary identity is not reused by assumption; if the
future public union is byte-identical, that equality is recorded as a derived
result after validation.

## 13. Privacy boundary

Collection may use the Environment's internal engine state behind the already
accepted V2 boundary, because replay/admission require it. Dataset and model
materialization may expose only:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
+ selected existing public_action_key
+ LogicalModelInputV1
+ EncodedModelInputV1
+ ModelSupervisionSampleV1
```

The following are forbidden in model features, sample identity, manifest
identity, diagnostics, or vocabulary derivation:

```text
CoreHost state or raw engine queries
opponent hidden hand/deck identity
hidden passcodes or inferred archetypes
private locators or persistent engine object identity
ActionCandidate.semantic_key
raw response bytes or submission tokens
pointer/address, PID, wall time, filesystem path
```

Future paired-hidden-world evidence must show equal public logical/encoded
inputs, model-input identities, Task7 materialization bytes/tensors/masks,
selected public key, and candidate cardinality for equal public worlds. The
collection contract adds no private-to-model channel.

## 14. Determinism boundary

The semantic collection identity is the exact ordered schedule and its
immutable inputs:

```text
Task7 collection schema/profile
rules bundle and locked deck identities/hashes
published Teacher profile/binding/PolicyArtifact identities
seed, placement, starting player
run-control values and cancellation source
collector semantic version and immutable source commit
```

The DatasetManifest identity is only the exact verified member set; the split
identity is only its deterministic episode partition; artifact hashes are
content commitments. These identities are not combined or substituted.

Identical accepted semantic inputs must reproduce identical canonical
schedule/job bytes, EpisodeEnvelope/shard/receipt bytes where the existing
determinism contracts require them, manifest membership and identities, split
bytes/identity, vocabulary bytes/identity, and derived Task7 materialization.
Filesystem layout, completion order, thread scheduling, device, worker count,
and wall-clock duration are not semantic inputs.

Any discovered nondeterminism in a Teacher episode, canonical artifact, replay
result, manifest ordering, split recomputation, or vocabulary construction is
a provisioning blocker. It is not repaired by sorting a semantic vector after
the fact.

## 15. Failure and quarantine policy

The later provisioning implementation must use existing status and failure
surfaces where they apply and must not invent a positive member from a
failure:

| Failure | Required result |
| --- | --- |
| Teacher/session/profile/binding failure | job failed; no member; fixed collection unusable |
| environment/protocol failure | failed closure; no member; fixed collection unusable |
| process/semantic budget exhaustion | `INTERRUPTED` plus restricted evidence when required; ineligible; fixed collection unusable |
| administrative cancellation | `INTERRUPTED`; ineligible; fixed collection unusable |
| semantic replay mismatch | admission failure/quarantine; no member |
| admission or receipt failure | no member; fixed collection unusable |
| duplicate/conflicting member | manifest rejection; no identity issued |
| invalid/mismatched artifact bytes | fail closed; no identity issued |
| split derivation/recomputation failure | no split identity; no Task7 authority |
| vocabulary derivation/encoding failure | no vocabulary identity; no Task7 authority |
| partial or missing scheduled job | fixed collection unusable; no silent replacement |

No failed or quarantined trajectory enters `DatasetManifest`. A replacement
episode is allowed only if a future contract explicitly freezes a deterministic
expansion rule; this contract freezes no such expansion and therefore permits
none.

## 16. Train/evaluation separation

The Task7 collection and Task5 meaningful evaluation are distinct authority
surfaces:

| Dimension | Task7 collection | Task5 meaningful evaluation |
| --- | --- | --- |
| policy | Teacher vs exact Teacher | future checkpoint policy vs exact opposing Teacher |
| profile | Task7 collection schedule above | `ocgforge.phase6.task5.evaluation_corpus.meaningful_fixed_matchup.v1` |
| seeds | `[4,6,8,9]` | `[1,2]` |
| placements | `NORMAL`, `MIRROR` | four evaluated-policy seat/role placements |
| starting player | `[0,1]` | `[0,1]` |
| authority | DatasetManifest membership | Task5 evaluation corpus/job manifest |

No Task5 evaluation job, identity, or result is changed by this contract. The
seed sets are disjoint, and the Task4 smoke vector is not reused. The two-deck
collection is an acceptance workload, not an architecture limit and not
Meta-8 data.

## 17. Future provisioning implementation slice

The next implementation slice remains separately unauthorized:

```text
P6_TASK7_DATASET_AUTHORITY_PROVISIONING
```

Smallest likely ownership after independent approval:

```text
CREATE  include/ygo/phase6/task7_dataset_authority_provisioning.hpp
CREATE  src/phase6/task7_dataset_authority_provisioning.cpp
CREATE  tests/phase6/phase6_task7_dataset_authority_provisioning_test.cpp
MODIFY  CMakeLists.txt                         # only target/test wiring
```

The provisioner may call/reuse, without creating a competing authority:

```text
ygo::policy::TeacherRunner
ygo::trajectory::TrajectoryRecorder and V1 codecs
ygo::trajectory::admission and VerifiedAdmissionReceipt
ygo::trajectory::DatasetManifest V1
ygo::phase6::make_phase6_split_v1
ygo::model::CardVocabularyV1
ygo::phase6::Task7 materialization
```

`tools/phase6_task4_corpus_probe/main.cpp`, Task4 numeric rows, the Task4
architecture, and the Task4 smoke checkpoint may provide historical fixture
patterns only; they must not be reused as Task7 semantic or membership
authority. A second-process verification may invoke existing split/model
validators; a new probe is not required unless the accepted implementation
cannot otherwise compare canonical artifact bytes independently.

## 18. Future provisioning acceptance gates

These are requirements for the future provisioning slice. None is evidence or
`PASS` from this documentation task:

| Gate | Required future result | Status now |
| --- | --- | --- |
| `COLLECTION_JOB_MANIFEST` | exact 16-job schedule and canonical manifest | `NOT_RUN` |
| `JOB_ORDER_DETERMINISTIC` | explicit seed → placement → starting-player order | `NOT_RUN` |
| `TRAIN_EVAL_SEED_SEPARATION` | no overlap with Task5 `[1,2]` or Task4 smoke seeds | `NOT_RUN` |
| `RULES_BUNDLE_EXACT` | locked rules identity/hash | `NOT_RUN` |
| `LOCKED_DECKS_EXACT` | both accepted deck identities/hashes | `NOT_RUN` |
| `TEACHER_IDENTITIES_EXACT` | profile, binding, artifact, producer, selection, no-RNG identities | `NOT_RUN` |
| `FULL_EPISODE_COLLECTION` | all 16 clean terminal episodes | `NOT_RUN` |
| `NO_ONE_DECISION_SMOKE_DATA` | no budget-1 or artificial partial episode admitted | `NOT_RUN` |
| `TRAJECTORY_RECORDING` | canonical V1 envelopes and records | `NOT_RUN` |
| `SEMANTIC_REPLAY` | independent replay succeeds for every member | `NOT_RUN` |
| `ADMISSION` | verified receipt for every member | `NOT_RUN` |
| `QUARANTINE_ZERO_FOR_ADMITTED_MEMBERS` | no failed/quarantined member in manifest | `NOT_RUN` |
| `DATASET_MANIFEST_V1` | existing V1 canonical manifest validates | `NOT_RUN` |
| `MANIFEST_MEMBER_FIELDS_COMPLETE` | all five normative member fields present | `NOT_RUN` |
| `MANIFEST_MEMBERSHIP_REVERIFIED` | receipt/shard/envelope commitments revalidated | `NOT_RUN` |
| `TRAINING_DATASET_SPLIT_V1` | deterministic split derives from manifest episodes | `NOT_RUN` |
| `SPLIT_RECOMPUTATION_SECOND_PROCESS` | byte-identical independent recomputation | `NOT_RUN` |
| `CROSS_PARTITION_EPISODE_LEAKAGE` | `NO` | `NOT_RUN` |
| `EMPTY_PARTITION_POLICY` | fail closed, no manual repair | `FROZEN_BY_CONTRACT` |
| `CARD_VOCABULARY_V1` | one public-only immutable vocabulary | `NOT_RUN` |
| `VOCABULARY_PRIVACY` | no hidden/catalog-derived identities | `NOT_RUN` |
| `PUBLIC_MODEL_INPUT_ONLY` | Phase-5 public inputs only | `NOT_RUN` |
| `COMPLETE_CANDIDATE_DOMAINS` | exact N-to-N samples with source order | `NOT_RUN` |
| `NO_FALLBACK` | no fallback policy or fabricated label | `NOT_RUN` |
| `FRESH_PROCESS_DETERMINISM` | canonical artifacts/identities repeat exactly | `NOT_RUN` |
| `TASK4B_HISTORY_CHANGED` | `NO` | `NOT_RUN` |
| `TASK5_EVALUATION_JOBS_CHANGED` | `NO` | `NOT_RUN` |

The future provisioning acceptance result must bind every record to the exact
source commit, clean worktree, frozen schedule, artifact bytes, and commands
that actually ran. A generated report cannot promote any unrun gate.

## 19. Findings at contract-freeze time

The absence of generated Task7 artifacts is intentional scope, not evidence
that a historical Task4B artifact is acceptable. At this documentation
checkpoint:

```text
BLOCKERS=0
MAJORS=0
MINORS=0
NOTES=2
```

| Classification | Finding | Treatment |
| --- | --- | --- |
| NOTE | Generated DatasetManifest, split, vocabulary, receipt, shard, and envelope identities do not exist yet. | Keep every content identity `UNISSUED_UNTIL_PROVISIONING`; do not infer it from Task4B. |
| NOTE | Full-episode terminal success and non-empty split partitions cannot be known without the prohibited provisioning run. | Require the frozen fail-closed gates before issuing Task7 authority. |

No unresolved semantic contradiction was found in the accepted V1 authority
surfaces. If provisioning discovers one, it must stop with a structured
failure rather than amend this contract implicitly.

## 20. Relationship to the Task7 execution contract

The dependency is intentionally one-way:

```text
this dataset-authority contract
        ↓ independent review
P6_TASK7_DATASET_AUTHORITY_PROVISIONING
        ↓ independent artifact/admission review
TASK7_DATASET_AUTHORITY_READY=YES
        ↓
P6_TASK7_EXECUTION_CONTRACT_FREEZE
        ↓ independent review
Task7 model/materializer runtime implementation
        ↓
Task7 training and canonical checkpoint
```

The later execution contract may bind only the concrete identities actually
issued by provisioning:

```text
source DatasetManifest identity
TrainingDatasetSplitV1 identity
CardVocabulary identity
Task7 architecture/config identity
training/checkpoint identities
```

This document does not issue any of them and does not resume the authorized
but currently blocked execution-contract freeze.

## 21. Explicit non-goals and current status

This contract does not authorize:

```text
Teacher collection or trajectory generation
DatasetManifest or split artifact generation
Task7 materialization over a new corpus
Task7 architecture, learner, optimizer, loss, or training
checkpoint export, checkpoint loading, or meaningful evaluation
Task5 changes or Task5 meaningful gameplay execution
rules/deck/Teacher changes
recurrent models, value learning, RL, self-play, Meta-8, or world models
Kaggle/distributed actor infrastructure
OCGForge-Ignis integration
```

```text
TASK7_REFERENCE_DATASET=BOUNDED
META8_DATASET=NOT_THIS_TASK
LARGE_SCALE_TRAJECTORY_GENERATION=NO
RECURRENT_DATASET=NO
RL_DATASET=NO
SELF_PLAY_DATASET=NO

TASK7_DATASET_AUTHORITY_CONTRACT=PROPOSED
TASK7_DATASET_AUTHORITY_READY=NO
TASK7_EXECUTION_CONTRACT_FREEZE_STARTED=NO
TASK7_IMPLEMENTATION_AUTHORIZED=NO
TASK7_TRAINING_AUTHORIZED=NO
```

The missing DatasetManifest/receipt/shard/envelope authority is an explicit
precondition for provisioning output, not something this document may infer
from historical Task4B strings. The contract must be independently reviewed
before the provisioning implementation is authorized.
