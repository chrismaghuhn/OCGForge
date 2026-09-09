# OCGForge Phase 6 Task7 V2 Dataset Authority and Provisioning Contract

## Status

```text
Status = CURRENT V2 SUCCESSOR, implementation pending independent review
Base = eaaa851f990584b14aa009a7a9f4796a85bfb10a
TASK7_V1_AUTHORITY = HISTORICAL
TASK7_V2_COLLECTION_STACK = REQUIRED
TASK7_DATASET_AUTHORITY_READY = NO_UNTIL_IMPLEMENTATION_REVIEW
TASK7_RUN_A = NOT_AUTHORIZED_BY_THIS_DOCUMENT
TASK7_RUN_B = NOT_AUTHORIZED_BY_THIS_DOCUMENT
TRAINING = NOT_AUTHORIZED_BY_THIS_DOCUMENT
```

This document is the current successor to the historical proposed
`P6_TASK7_DATASET_AUTHORITY_CONTRACT.md`. The historical document remains
immutable V1 evidence. Its V1 identities and validators are not widened.

The current authority chain is:

```text
TeacherRunnerV3
→ EpisodicEnvironment V3
→ TrustedTrajectory V2
→ Shard V2
→ Restricted Evidence V2
→ Admission V2
→ Receipt V2
→ DatasetManifest V2
→ TrainingDatasetSplitV1
→ CardVocabularyV1
```

Partial artifacts are diagnostic evidence only. No partial result is a Task7
dataset authority.

## Version boundary

Historical Task7 V1 remains bound to:

```text
ocgforge.episodic_environment.v2
ocgforge.policy.teacher_core.v1
ocgforge.policy.public_action_key.v1
ocgforge.trusted_trajectory.v1
ocgforge.admission_receipt.v1
ocgforge.dataset_manifest.v1
```

The V2 successor requires explicit V3/V2 values. V1 validators and codecs are
never broadened, and mixed V1/V2 artifacts fail closed.

## Frozen curriculum

```text
matchup = ocgforge.matchup.swordsoul_salamangreat.v1
rules bundle = 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
format = TCG_ADVANCED_2026_05_18
duel mode = DUEL_MODE_MR5
duel flags = 190464
```

Locked decks remain byte-identical:

```text
ocgforge.swordsoul_tenyi.ml_v1
8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7

ocgforge.salamangreat.ml_v1
6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188
```

The audited V2 Teacher identities are:

```text
Swordsoul binding:
ocgforge.teacher_policy_binding.v1.4da70292d08b5608552d9f9246050c2ea5b9c3b52c962ea26a8f9816c5447a5f
Swordsoul artifact:
policy_artifact.v1.efbd7962734c993d9374acc4c527f722a2413e7279b851d10340a83defccfc01

Salamangreat binding:
ocgforge.teacher_policy_binding.v1.0ec1d4ce29956c72e7e8537d24ba04834dbed4f820ff222d0209f9d7880bc7c0
Salamangreat artifact:
policy_artifact.v1.17b2395a97e820c645f59037e7203f17bab808f90c1b70936c1000591efdb40e
```

Both seats require:

```text
producer = ocgforge.policy.teacher_core.v2
action adapter = ocgforge.policy.public_action_key.v2
sampling = ocgforge.policy.deterministic_lexicographic_argmax.v1
policy RNG = ocgforge.no_policy_rng.v1
```

Factory identity drift is a fail-closed provisioning error.

## Fixed schedule and job identities

The schedule is exactly:

```text
seeds = [4, 6, 8, 9]
placements = [NORMAL, MIRROR]
starting players = [0, 1]
job count = 16
```

Canonical iteration is seed, placement, then starting player. `NORMAL` maps
seat 0 to Swordsoul and seat 1 to Salamangreat; `MIRROR` reverses those roles.
Jobs are never sorted by hash, filename, worker, or completion order. No retry,
replacement, or additional job is permitted.

The explicit orchestration IDs are:

```text
ocgforge.phase6.task7.dataset_collection_job.v2
ocgforge.phase6.task7.dataset_collection_schedule.v2
ocgforge.phase6.task7.dataset_collection.reference.v2
```

Canonical job input includes the V3 environment contract, curriculum, seed,
placement, starting player, both deck IDs/SHA-256 values, both V2 Teacher
artifact/binding IDs, producer/action/sampling/no-RNG identities, hard budgets,
cancellation metadata, collector semantic version, and an explicit source
commit. Paths, clocks, PIDs, worker IDs, and completion order are excluded.

The exact job byte order is:

```text
identity domain:string
identity schema:string
collection profile:string
environment contract:string
matchup:string
rules bundle:string
format:string
duel mode:string
duel flags:u64be
root seed:u64be
placement:u8
starting player:u8
for seat 0 then seat 1:
    deck role:u8
    deck ID:string
    deck SHA-256:string
    Teacher artifact ID:string
    Teacher binding ID:string
Teacher producer:string
Teacher action adapter:string
Teacher sampling:string
no-policy-RNG identity:string
engine-process budget:u64be
semantic-action budget:u64be
cancellation reason:string
cancellation source:string
collector semantic version:string
collector source commit:string
```

The schedule bytes contain the profile/environment binding, the ordered seed,
placement, and starting-player vectors, followed by each job's identity and
canonical job bytes in the frozen nested order. The authority bundle contains
the canonical schedule bytes, each ordered job's canonical envelope/shard/
evidence/receipt bytes, the combined DatasetManifest V2 bytes, Split V1
identity bytes, and Vocabulary V1 canonical bytes.

## Real execution and eligibility

The production executor calls `TeacherRunnerV3TrajectoryRunner::run()` for
every job. Test-only `TeacherRunnerV3TrajectoryTestAccess` is never a Task7
authority.

Each eligible job must have:

```text
EpisodeEnvelopeV2
CandidateTrajectoryShardV2
RestrictedCollectionEvidenceBundleV2
AdmissionVerification V2
VerifiedAdmissionReceiptV2
DatasetManifestV2 member
error absent
quarantined false
closure = TerminalClosureV2
collection disposition = Clean
```

Interrupted, failed, quarantined, missing, malformed, or mismatched jobs make
the fixed 16-job result `UNUSABLE_FOR_TASK7`. No partial manifest or fabricated
terminal is issued.

## DatasetManifest V2

The provisioner reuses `trajectory::dataset_v2`. The combined manifest requires
exactly 16 admitted members, no missing/extra/duplicate member, no conflicting
receipt/shard/envelope commitment, strict `trajectory_record_id` ordering, and
successful canonical V2 validation. Its identity is derived only from the
ordered member record IDs.

## Split and vocabulary

The split remains exactly:

```text
ocgforge.phase6.dataset_split.v1
ocgforge.phase6.split.fixed_80_10_10_sha256.v1
```

`make_phase6_split_v1()` receives the exact V2 DatasetManifest identity and the
admitted episode IDs. Partitions must be disjoint, exhaustive, canonical, and
nonempty. Empty train, validation, or test is fail closed and issues no Task7
authority.

The vocabulary remains `CardVocabularyV1` and is derived only from public V2
observations: known public entity passcodes, visible-event public passcodes,
and known public deck passcode vectors. Values are unique, ascending, and
nonzero. No database lookup, hidden identity, private locator, CoreHost state,
or archetype inference is allowed.

## Model boundary

This task does not modify or widen:

```text
LogicalModelInputV1
EncodedModelInputV1
ModelSupervisionSampleV1
Task7 materialization V1
candidate feature schemas
```

V2 `card_selection_operation` is not dropped or reinterpreted as a V1 model
feature. A future model/materialization successor requires separate authority.

## Privacy, determinism, and evidence

Restricted replay evidence remains restricted. Semantic authority uses only V3
public observations, complete ordered V2 candidate domains, and V2 public action
identities. Raw responses, submission tokens, internal semantic keys, hidden
passcodes, PIDs, clocks, paths, pointers, and private locators are excluded.

Fresh-process pure gates compare schedule/job bytes and identities, combined
DatasetManifest V2 bytes/identity, Split V1 bytes/identity, and Vocabulary V1
bytes/identity. The 16 real duels are not run by this implementation task.

The only positive result is an immutable authority bundle containing the exact
schedule, job identities, per-job V2 commitments, DatasetManifest V2,
TrainingDatasetSplitV1, and CardVocabularyV1. This contract does not authorize
RUN_A, RUN_B, or training.
