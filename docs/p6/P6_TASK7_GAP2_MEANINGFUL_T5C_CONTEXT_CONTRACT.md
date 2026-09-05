# Phase 6 Task7 GAP2 meaningful Task5C context contract v1

## Status and scope

```text
STATUS=PROPOSED / pending independent review
CONTRACT_DESIGN_ONLY=YES
IMPLEMENTATION_AUTHORIZED=NO
MEANINGFUL_EVALUATION_EXECUTED=NO
TASK7_TRAINING_AUTHORIZED=NO
```

This document closes the design boundary for the second Task7 readiness gap:
the meaningful fixed-matchup Task5C context and deterministic job path. It is
not an implementation, an evaluation run, a training authorization, or an
acceptance result. It defines the exact future inputs and validation required
before a future real Task7 checkpoint may be evaluated through Task5C.

Only this document and the companion implementation plan are created in the
current documentation slice. Existing Phase-5, Task4A, Task5A, Task5C, Task7,
dataset, trajectory, replay, admission, rules, deck, and checkpoint documents
remain unchanged.

The project priority remains:

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

The normative words MUST, MUST NOT, SHOULD, and FAIL CLOSED retain their
contract meaning. A future implementation that cannot prove one of these
requirements rejects the complete context before gameplay.

## 1. Audited baseline and current gap

The design is based on the exact accepted Main head:

```text
AUDIT_DATE=2026-09-05
AUTHORIZED_MAIN=122948309eac068f66a85dc9f51d4390fdf55f74
POST_MERGE_CI_RUN=33928774672
POST_MERGE_CI_HEAD=122948309eac068f66a85dc9f51d4390fdf55f74
POST_MERGE_CI_STATUS=completed
POST_MERGE_CI_CONCLUSION=success
```

The accepted Task7 Gap1 implementation is already integrated at this Main
head:

```text
TASK7_GAP1_INPUT_MATERIALIZATION_IMPLEMENTED=YES
TASK7_GAP1_FEATURE_FINAL_PASS=YES
TASK7_MATERIALIZATION_SCHEMA=ocgforge.phase6.task7.input_materialization.v1
TASK7_MATERIALIZATION_CONFIG_ID=phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a
```

The live T5C implementation currently accepts only:

```text
corpus_profile=ocgforge.phase6.task5.evaluation_corpus.implementation_acceptance.v1
corpus_kind=IMPLEMENTATION_ACCEPTANCE
checkpoint=phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327
job_count=8
```

`make_implementation_acceptance_context(...)` and its exact eight-job
validation are historical accepted smoke/wiring behavior. They MUST remain
unchanged in meaning. In particular, a non-smoke checkpoint or meaningful
profile passed to that path MUST continue to fail closed.

The open gap is a separate path that can accept a future, independently
validated non-smoke Task7 checkpoint while preserving all existing T5C failure,
replay, admission, and public-model boundaries.

## 2. Authority and ownership

The ownership chain is:

```text
Task5A codec
    → canonical EvaluationJob / corpus / manifest identities

Task5C
    → execution of an already validated ordered job population

EpisodicEnvironment V2
    → legality, public observation, complete candidate domain,
      continuation, response resolution, and engine advancement

checkpoint loader / inference boundary
    → checkpoint manifest/content validation and deterministic inference

trajectory recorder + semantic replay + admission
    → trusted trajectory and outcome acceptance

Task5D
    → derived first-divergence, distribution, and report surfaces
```

Task5C is not allowed to become a second identity authority. The existing T5A
Python codec owns the canonical field order and primitive encoding for jobs,
corpora, manifests, and aggregate identities. A future C++ mirror may validate
the same bytes, as it does for the accepted smoke schedule, but MUST remain
byte-compatible with T5A and MUST NOT introduce a competing hash grammar.

The meaningful path consumes only this already accepted gameplay flow:

```text
immutable meaningful job
        ↓
validated checkpoint-bound PolicySelection
        ↓
EpisodicEnvironment V2
        ↓
PublicEnvironmentObservation
        + complete ordered EnvironmentActionCandidate[N]
        ↓
LogicalModelInputV1
        ↓
EncodedModelInputV1
        ↓
accepted Task7 exact materialization
        ↓
future PyTorch inference provider
        ↓
exact N scores and existing public_action_key selection
        ↓
trajectory recorder
        ↓
semantic replay
        ↓
admission
```

The Environment remains the sole legality and candidate-domain authority.
PyTorch remains physical execution only. Task5C does not feature-engineer,
construct candidates, submit response bytes, or inspect private engine state.

## 3. Preserved implementation-acceptance path

The existing path is explicitly preserved:

```text
make_implementation_acceptance_context(...)
    profile = ocgforge.phase6.task5.evaluation_corpus.implementation_acceptance.v1
    kind = IMPLEMENTATION_ACCEPTANCE
    checkpoint = accepted Task4 smoke checkpoint only
    jobs = existing exact eight-job vector only
```

The future meaningful constructor MUST be a separate API/path. It MUST NOT be
implemented as:

```text
if checkpoint != smoke:
    accept anyway
```

It MUST NOT loosen the existing helper's checkpoint test, replace smoke
constants with wildcards, or make the eight-job matrix optional. The future
tests MUST prove both directions:

```text
meaningful path accepts only a validated meaningful context
implementation-acceptance path still rejects non-smoke/meaningful inputs
```

## 4. Meaningful profile and locked scope

The meaningful profile and kind are the already accepted Task5 identities:

```text
MEANINGFUL_FIXED_MATCHUP_PROFILE=
ocgforge.phase6.task5.evaluation_corpus.meaningful_fixed_matchup.v1

MEANINGFUL_FIXED_MATCHUP_KIND=MEANINGFUL_FIXED_MATCHUP
```

The scope is exactly the existing fixed matchup. No rule, format, deck, or
Teacher expansion is part of GAP2:

| Binding | Frozen value |
| --- | --- |
| matchup | `ocgforge.matchup.swordsoul_salamangreat.v1` |
| rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |
| format | `TCG_ADVANCED_2026_05_18` |
| duel mode | `DUEL_MODE_MR5` |
| duel flags | `190464` |
| deck role 0 | `ocgforge.swordsoul_tenyi.ml_v1`; SHA-256 `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` |
| deck role 1 | `ocgforge.salamangreat.ml_v1`; SHA-256 `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` |

The accepted Teacher identities remain role-specific:

| Acting deck role | Strategy profile | Teacher binding | PolicyArtifact |
| --- | --- | --- | --- |
| Swordsoul Tenyi | `ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74` | `ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c` | `policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d` |
| Salamangreat | `ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55` | `ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56` | `policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527` |

The Salamangreat strategy-profile value above is the accepted value from the
live BC/Task5 authorities; implementations MUST read and validate the exact
T5A/BC constants rather than copying a shortened name. Both roles use:

```text
teacher producer = ocgforge.policy.teacher_core.v1
teacher sampling = ocgforge.policy.deterministic_lexicographic_argmax.v1
teacher RNG      = ocgforge.no_policy_rng.v1
```

The opponent is always the exact accepted Teacher for the opposing deck role.
Teacher selection is evaluation-opponent behavior provenance, not legality
authority or a strategic optimality proof.

## 5. Meaningful seed vector and job population

### 5.1 Claim supported by this bounded corpus

The meaningful corpus is intended to support the first bounded, replayable
fixed-matchup baseline claim for a real Task7 checkpoint:

```text
the checkpoint-bound policy can be run through both locked deck seatings,
both evaluated policy seats/roles, and both starting-player values under
the normal public inference, trajectory, replay, and admission path
```

It is not a statistically powered general-strength claim, arbitrary-deck
claim, convergence claim, or full-game Yu-Gi-Oh! claim. Failure, interruption,
quarantine, replay failure, admission failure, and inference failure remain
separate outcomes and are never silently relabeled as losses.

### 5.2 Exact seeds

The smallest non-degenerate seed vector that exercises the existing accepted
seed dimension is:

```text
MEANINGFUL_SEED_VECTOR=[1,2]
K=2
```

These are the already accepted deterministic seed values used by the smoke
schedule. Reusing the numeric seed values does not reuse the smoke corpus:
the meaningful profile, future checkpoint identity, and resulting job/corpus
identities are distinct. No new random seed generator is introduced.

One seed would leave the seed dimension untested. A larger vector would add
population size without evidence for a stronger claim in this first bounded
baseline.

### 5.3 Exact policy-placement vector

The current T5C job fields support independent deck seating and evaluated
policy seat. To avoid conflating those dimensions, the meaningful schedule
uses the smallest complete four-entry placement vector:

| Placement ordinal | Seat 0 deck | Seat 1 deck | Evaluated policy seat/role | Opponent seat/role | Environment seat assignment |
| ---: | --- | --- | --- | --- | --- |
| 0 | Swordsoul Tenyi | Salamangreat | P0 / Swordsoul Tenyi | P1 / Salamangreat Teacher | `Normal` |
| 1 | Swordsoul Tenyi | Salamangreat | P1 / Salamangreat | P0 / Swordsoul Tenyi Teacher | `Normal` |
| 2 | Salamangreat | Swordsoul Tenyi | P0 / Salamangreat | P1 / Swordsoul Tenyi Teacher | `Mirror` |
| 3 | Salamangreat | Swordsoul Tenyi | P1 / Swordsoul Tenyi | P0 / Salamangreat Teacher | `Mirror` |

This is deliberately four placements rather than the existing smoke path's
two role-seat arrangements with the evaluated policy always on seat 0. The
additional two entries are required to make the evaluated-policy-seat field a
real symmetric dimension while retaining both deck seatings. This is the
smallest complete placement product for the claim in section 5.1.

### 5.4 Exact nested job order and count

The canonical job vector is generated without sorting by computed identity:

```text
for seed in [1, 2] in that order:
    for placement in [0, 1, 2, 3] in that order:
        for starting_player in [0, 1] in that order:
            emit one EvaluationJobV1
```

The exact coordinate vector is therefore:

| Job ordinal | Seed | Placement | Starting player | Evaluated policy | Opponent Teacher |
| ---: | ---: | ---: | ---: | --- | --- |
| 0 | 1 | 0 | P0 | P0 Swordsoul | P1 Salamangreat |
| 1 | 1 | 0 | P1 | P0 Swordsoul | P1 Salamangreat |
| 2 | 1 | 1 | P0 | P1 Salamangreat | P0 Swordsoul |
| 3 | 1 | 1 | P1 | P1 Salamangreat | P0 Swordsoul |
| 4 | 1 | 2 | P0 | P0 Salamangreat | P1 Swordsoul |
| 5 | 1 | 2 | P1 | P0 Salamangreat | P1 Swordsoul |
| 6 | 1 | 3 | P0 | P1 Swordsoul | P0 Salamangreat |
| 7 | 1 | 3 | P1 | P1 Swordsoul | P0 Salamangreat |
| 8 | 2 | 0 | P0 | P0 Swordsoul | P1 Salamangreat |
| 9 | 2 | 0 | P1 | P0 Swordsoul | P1 Salamangreat |
| 10 | 2 | 1 | P0 | P1 Salamangreat | P0 Swordsoul |
| 11 | 2 | 1 | P1 | P1 Salamangreat | P0 Swordsoul |
| 12 | 2 | 2 | P0 | P0 Salamangreat | P1 Swordsoul |
| 13 | 2 | 2 | P1 | P0 Salamangreat | P1 Swordsoul |
| 14 | 2 | 3 | P0 | P1 Swordsoul | P0 Salamangreat |
| 15 | 2 | 3 | P1 | P1 Swordsoul | P0 Salamangreat |

```text
MEANINGFUL_JOB_COUNT=16
MEANINGFUL_JOB_ORDER=seed_then_placement_then_starting_player
```

The coordinate ordinal is schedule metadata only. It is not part of an
individual job identity. The exact fields `evaluated_policy_seat`,
`opponent_policy_seat`, `evaluated_policy_deck_role_id`, and
`opponent_policy_deck_role_id` are nevertheless serialized in each T5A job
identity and validated against the placement row.

## 6. Future checkpoint binding

### 6.1 No checkpoint identity is issued here

The future meaningful checkpoint is intentionally unknown in this design task.
The implementation MUST NOT use a zero digest, placeholder, mutable `latest`,
branch name, filesystem path, or the Task4 smoke checkpoint. The following
values remain unissued until a separate Task7 training/checkpoint contract and
acceptance produce them:

```text
future meaningful checkpoint_identity=UNISSUED
future evaluation_job_identities=UNISSUED
future evaluation_job_manifest_identity=UNISSUED
future evaluation_corpus_identity=UNISSUED
future evaluation_identity=UNISSUED
```

The future factory accepts a concrete checkpoint binding only after the
accepted future Task7 checkpoint/inference loader has validated the exact
manifest, canonical exported weight content, and all transitive checkpoint
bindings. The binding owner is that loader; T5C is a consumer only.

```text
BINDING_OWNER=accepted future Task7 checkpoint/inference loader
T5C_ROLE=consumer only
```

### 6.2 Opaque validated binding capability

The future implementation MUST consume a typed in-memory capability named
`MeaningfulCheckpointBindingV1`. It is deliberately **OPAQUE / NON-FORGEABLE**
to callers of the meaningful context factory. It is not a freely constructible
DTO and is not made authoritative by a caller-supplied boolean.

The following are immutable facts carried by an issued capability (the
`checkpoint_manifest_validated=true` condition is an internal issuance
invariant, not a public field that a caller may set):

```text
checkpoint_identity
checkpoint_manifest_validated=true (internal capability invariant)
model_architecture_config_identity
phase5_logical_model_input_contract_identity
phase5_encoded_model_input_contract_identity
phase5_batch_layout_contract_identity
card_vocabulary_identity
dataset_identity
dataset_split_identity
training_contract_identity
canonical_weight_export_codec_identity
canonical_weight_content_identity
task7_materialization_schema_id
task7_materialization_config_identity
```

Only the accepted future Task7 checkpoint/inference loader may issue this
capability, and only after it has parsed the canonical checkpoint artifact,
recomputed the manifest/content identities, validated the architecture,
Phase-5 contracts, concrete vocabulary, DatasetManifest/split, training
contract, export codec/content, and Task7 materialization configuration. The
capability has no public/default constructor, no public mutation, and no
identity-only conversion. T5C receives immutable accessors and cannot
synthesize a capability from strings, digests, a checkpoint path, or a
`validated=true` value. A test-only friend/access seam may exercise T5C's
consumer-side rejection checks, but a test-built capability is never evidence
that a real Task7 checkpoint was loaded.

The capability is not a new checkpoint identity or a second manifest codec. It
is an issued, validated view of the existing checkpoint manifest/content
authority. The checkpoint identity itself is the content identity of the
canonical manifest; the fields above are checked from accepted bytes and are
not recanonicalized by T5C.

The proof MUST enforce:

```text
checkpoint_identity has prefix phase6_checkpoint.v1. and a nonzero lowercase digest
checkpoint_identity != accepted Task4 smoke checkpoint identity
checkpoint manifest schema/version are accepted
all canonical weight/export/content identities recompute
all three Phase-5 input contracts equal the accepted IDs
card_vocabulary_identity is concrete and resolves to the supplied vocabulary
dataset_identity and dataset_split_identity are present, valid, and paired
architecture/config resolves to the Task7 exact materialization schema/config
```

The exact Task7 compatibility values are:

```text
task7_materialization_schema_id=
ocgforge.phase6.task7.input_materialization.v1

task7_materialization_config_identity=
phase6_task7_input_materialization_config.v1.20f394c888e959446fa263c3520f3dd3b1f48b3a23e58373da7153a691ab1e7a
```

If the future architecture configuration cannot prove that it consumes this
Task7 materialization configuration, the meaningful context is rejected before
the evaluator is constructed.

### 6.3 Avoiding duplicate authority

The existing T5A job/corpus/root identities already bind `checkpoint_identity`.
The checkpoint manifest already binds architecture/configuration, Phase-5
contracts, concrete CardVocabulary, dataset/split, BC training contract, and
canonical weight content. Therefore:

| Value | Existing owner and meaningful-path rule |
| --- | --- |
| checkpoint manifest/content | accepted future Task7 checkpoint/inference loader issues the opaque capability; represented transitively by `checkpoint_identity` |
| `MeaningfulCheckpointBindingV1` | non-forgeable loader-issued validation capability; T5C consumes it and never issues it |
| architecture/config identity | checkpoint manifest and accepted architecture contract; no second T5C hash |
| concrete CardVocabulary identity | checkpoint manifest plus equality with the supplied `CardVocabularyV1.identity()` |
| DatasetManifest identity | checkpoint manifest; gameplay jobs keep T5A dataset optionals absent |
| training split identity | checkpoint manifest; gameplay jobs keep T5A dataset optionals absent |
| Task7 materialization schema/config | accepted Task7 contract and the future architecture/config binding proof |
| Phase-5 source contracts | existing T5A job fields 21–24 and checkpoint manifest fields |

The future meaningful context may carry the binding proof for validation, but
it MUST NOT add a second independently hashed copy of these values to the T5A
job/corpus preimages. A checkpoint change changes the checkpoint identity and
therefore the existing T5A job/corpus/root identities; no duplicate field is
needed to obtain that invalidation.

### 6.4 Context factory boundary

The future API is a separate strict path, conceptually. The capability is
opaque at this boundary; the notation below is an interface sketch, not a
public aggregate initializer:

```text
make_meaningful_fixed_matchup_context(
    MeaningfulCheckpointBindingV1 checkpoint_binding,
    CardVocabularyV1 concrete_vocabulary,
    evaluator_semantic_source_commit
) -> MeaningfulFixedMatchupContextV1
```

Conceptually, the type has private construction/mutation and a loader-only
issuer:

```text
MeaningfulCheckpointBindingV1
  = opaque immutable capability
  = issued only by accepted future Task7 checkpoint/inference loader
  = consumed, never constructed, by T5C
```

The future test suite may use an explicitly test-only friend/access seam to
exercise T5C consumer rejection paths. That seam is not a loader, cannot
validate a real artifact, and cannot make `REAL_TASK7_CHECKPOINT_BINDING` pass.

`MeaningfulFixedMatchupContextV1` contains the existing T5A-compatible
`EvaluationContextV1` plus the validated binding proof needed by the future
T5C evaluator configuration. It is not a new identity codec. The factory:

1. validates the binding proof and supplied vocabulary;
2. constructs the exact 16-job vector from section 5;
3. sets every job to the meaningful profile/kind and the concrete future
   checkpoint identity;
4. leaves `source_dataset_identity` and `dataset_split_identity` absent on
   gameplay jobs, as required by the T5A job grammar;
5. recomputes every job, corpus, job-manifest, and root identity through the
   accepted T5A-compatible field order; and
6. returns no context on any mismatch.

The existing `make_implementation_acceptance_context(...)` remains a separate
smoke constructor and validator. The future `create_frozen_gameplay_evaluator`
path may gain a meaningful overload or a distinct
`create_meaningful_frozen_gameplay_evaluator` constructor, but a meaningful
binding MUST never be silently accepted by the smoke constructor.

## 7. Task7 inference and candidate boundary

The meaningful evaluator uses the integrated Gap1 materialization, never the
Task4A numeric projection:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[N]
    → Phase-5 LogicalModelInputV1
    → Phase-5 EncodedModelInputV1
    → RaggedModelBatchV1 / Task7 exact materialization
    → future PyTorch adapter/model
    → exactly N finite score values
    → existing deterministic public_action_key selection
```

The future `CheckpointInferenceProviderV1` remains the narrow inference
boundary. Its Task7 implementation may call the accepted C++ materializer and
Python adapter, but T5C does not reinterpret those values or query gameplay
state. The response MUST bind:

```text
checkpoint_identity
model_input_identity
ordered_candidate_domain_identity
current public decision identity when available
exact score count N
source-order score bits
selected local ordinal
selected public_action_key
```

For every current domain:

```text
N supplied candidates = N scores = one supplied public_action_key
```

N=1 remains an externally supplied one-candidate domain. N=24, N=25, and
N=129 remain exact-domain witnesses. No candidate sorting, truncation,
deduplication, domain split, candidate-zero fallback, Teacher fallback, or
RandomLegal fallback is allowed.

Seed, job identity, checkpoint path, evaluator commit, process ID, wall time,
worker number, GPU, and framework metadata are evaluation/provenance values;
none is a learner feature or gameplay input.

## 8. Teacher and opponent policy boundary

The evaluated policy is the future checkpoint-bound policy. The opponent is
the fixed deterministic Teacher associated with the opposing deck role in the
placement row. The opponent path remains:

```text
TeacherCore
→ deterministic lexicographic argmax
→ no policy RNG
→ normal PolicySelection
```

The future implementation MUST validate, per job:

```text
teacher producer identity
teacher sampling identity
no-policy-RNG identity
role-specific strategy profile
role-specific Teacher binding
role-specific PolicyArtifact
opponent seat and deck role
```

An opponent Teacher failure is an environment/evaluation failure under the
existing `GameplayFailureStage::Environment` attribution. It MUST NOT be
reported as checkpoint inference failure and MUST NOT be converted into a
trusted loss or neural-policy win.

## 9. Replay, admission, and failure semantics

Every meaningful job uses the existing T5C execution path:

```text
normal PolicySelection
→ EpisodicEnvironment V2
→ normal trajectory recorder
→ semantic replay
→ existing admission authority
```

The existing status values remain authoritative and are not extended by GAP2:

```text
GameplayJobStatus
ReplayAdmissionStatus
GameplayFailureStage
```

Trusted win/loss/draw accounting is allowed only when the normal trajectory,
semantic replay, and admission path succeed and `fallback_assisted=false`.
Inference failure, opponent failure, environment failure, interruption,
replay failure, admission failure, and quarantine remain distinct. A result
with a failed response MUST NOT invoke Teacher, RandomLegal, a heuristic, a
first candidate, candidate zero, or a retry under another policy.

The schedule is a fixed population, not a completion log. A missing or failed
job remains represented by its exact job identity and typed failure result.
The evaluator MUST NOT remove it from the denominator or reorder results by
worker completion.

## 10. Identity derivation and timing

The future implementation reuses these accepted T5A functions and identities:

```text
evaluation_job_identity(EvaluationJobV1)
evaluation_corpus_identity(EvaluationCorpusV1)
evaluation_job_manifest_identity(EvaluationJobManifestV1)
evaluation_identity(EvaluationIdentityV1)
```

The meaningful profile, kind, fixed bindings, placement fields, seed, starting
player, evaluator semantic version, evaluator semantic source commit, and
future checkpoint identity flow through the already frozen T5A field order.
The 16 job identities are retained in that exact vector order in both the
corpus and job manifest.

Identity issuance is staged:

```text
schedule template (this document)
    → future checkpoint binding proof
    → future evaluator implementation version/source commit
    → concrete EvaluationJobV1 values
    → job identities
    → ordered job manifest/corpus identities
    → root evaluation identity
```

No final content identity is issued in this design task. Document text, this
Git commit, branch name, path, wall clock, PID, worker order, device, batch
composition, and framework version are not identity inputs.

Changing one semantic input changes the affected job identity and all aggregate
identities that contain its ordered vector. Changing another job leaves an
unchanged job's individual identity unchanged. Reordering the vector changes
the corpus and job-manifest identities but not an unchanged individual job.

## 11. CardVocabulary rule

The Task4 smoke vocabulary is not a meaningful default. A future meaningful
context MUST:

```text
load the concrete CardVocabulary identity from the validated checkpoint
manifest/binding proof
construct or receive that exact immutable CardVocabularyV1
require concrete_vocabulary.identity() == binding.card_vocabulary_identity
reject any mismatch before evaluator construction
```

There is no mutable global vocabulary and no fallback to
`kSmokeCardVocabularyIdentity`. Equality with the smoke vocabulary, if it ever
occurs, is evidence only; it does not authorize using the smoke identity or
smoke checkpoint path.

## 12. Privacy and determinism

The meaningful context adds no private-to-model path. The inference provider
may consume only:

```text
PublicEnvironmentObservation
complete EnvironmentActionCandidate[]
Phase-5 logical/encoded values
accepted Task7 materialization values
```

It MUST NOT receive or serialize:

```text
CoreHost
raw engine query state
PlayerObservation private fields
hidden card/deck/hand identity
private locators
internal semantic keys
response bytes
SubmissionToken as learner data
raw pointers/object identity
beliefs or inferred archetypes
```

Evaluation metadata such as seed, job identity, checkpoint path, evaluator
source commit, process ID, wall time, worker index, GPU, or framework version
MUST NOT enter learner tensors, public gameplay semantics, or candidate
legality. The accepted Task7 materialization keeps routing keys and raw
locator tokens in non-learned control sidecars only.

The same accepted public input and checkpoint/configuration MUST produce the
same exact candidate scores and selected existing public key. All schedule
vectors, role mappings, and serialized identity vectors use explicit order;
unordered iteration and worker completion order are forbidden.

## 13. Strict context-factory validation

The future meaningful factory rejects the complete context for any of these
conditions:

```text
smoke checkpoint or placeholder/mutable checkpoint
malformed or unresolved checkpoint identity
checkpoint manifest/content mismatch
wrong profile or corpus kind
wrong evaluation contract
wrong matchup, rules bundle, format, duel mode, or duel flags
wrong deck role identity or deck hash
wrong architecture/configuration binding
wrong Phase-5 contract binding
wrong Task7 materialization schema/config binding
wrong concrete CardVocabulary identity
missing or unpaired checkpoint dataset/split binding
wrong Teacher artifact, binding, profile, producer, sampling, or RNG identity
wrong evaluator semantic source commit/version
wrong seed vector, placement vector, or starting-player vector
missing, extra, duplicated, reordered, or mixed-checkpoint job
wrong evaluated seat/role or opponent seat/role
job identity not recomputable from its fields
corpus/job-manifest/root identity mismatch
nonempty gameplay source_dataset_identity or dataset_split_identity
```

The factory performs no normalization or repair. It returns no partial context
and does not start an environment when validation fails. A factory call without
a loader-issued capability is rejected even if all visible identity strings
happen to match.

## 14. Future implementation acceptance matrix

The following are requirements for the separately authorized implementation;
none is evidence that implementation exists now.

| Area | Required proof |
| --- | --- |
| Profile/kind | Meaningful profile and `MEANINGFUL_FIXED_MATCHUP` accepted; smoke path remains exact and separate. |
| Schedule | Exact seeds `[1,2]`, 4 placement rows, starting-player `[0,1]`, 16 jobs, and nested order reproduced. |
| Checkpoint | Real non-smoke checkpoint manifest/content accepted only through the loader-issued opaque capability; smoke rejected. |
| Checkpoint bindings | Architecture, Phase-5 contracts, concrete vocabulary, dataset/split, BC training, export/content, and Task7 materialization config all resolve and match. |
| Fixed matchup | Rules, format, duel mode/flags, deck IDs, and deck hashes remain the exact values in section 4. |
| Teacher | Role-specific artifact/binding/profile, deterministic sampling, and no-RNG identities match every placement. |
| Identity timing | No final job/corpus/root identity is issued before real checkpoint and immutable evaluator source commit exist. |
| Identity mutation | One changed semantic job input changes that job and aggregate identities; another job's identity remains unchanged. |
| Ordering | Seed/placement/start order is explicit; job, result, replay, and manifest vectors reject reorder/add/drop/duplicate. |
| Inference | Task7 exact materialization feeds the future provider; Task4 float projection is never used. |
| Candidate domain | N=1/24/25/129 remain N-to-N with exact source order and no fallback/split/truncation. |
| Failure attribution | Inference failures remain inference failures; opponent Teacher failures remain environment failures; replay/admission/quarantine remain unchanged. |
| Replay/admission | Every trusted outcome traverses recorder, semantic replay, and admission; direct win counting is rejected. |
| Privacy | Real paired hidden worlds produce equal public/model inputs, Task7 values, scores, and selected key without private leakage. |
| Determinism | Fresh processes reproduce job identities, requests, score/selection semantics, and result ordering. |
| Regression | Phase-5, Task4A, Task7 materialization, and existing implementation-acceptance T5C tests remain green. |
```

The future test matrix MUST additionally exercise:

```text
wrong profile/kind/matchup/rules/decks/hashes
wrong Teacher and RNG bindings
seed omitted/added/duplicated/reordered
job omitted/added/duplicated/reordered
wrong policy seat/role/opponent role/starting player
mixed checkpoint identities
vocabulary/checkpoint mismatch
N=1/24/25/129 and no fallback
checkpoint inference failure attribution
opponent Teacher failure attribution
replay failure, admission failure, quarantine
paired hidden-world privacy
fresh-process identity determinism
```

## 15. Explicit non-goals

This contract does not authorize:

```text
Task7 architecture or model implementation
optimizer, loss, or training schedule
Behavior Cloning execution
DatasetManifest or TrainingDatasetSplit generation
checkpoint creation or selection
meaningful gameplay evaluation execution
Task5D evidence generation
P6-G14 PASS
Task7 FINAL PASS
RL, self-play, recurrent/world models, search, Meta-8, or JAX
multi-deck/arbitrary-deck expansion
rules/deck/Teacher changes
new gameplay, legality, observation, candidate, replay, or admission authority
```

Completing the design and its independent review is not implementation
acceptance and is not training authorization.

## 16. GAP2 closure semantics and current status

The separate implementation must first be accepted:

```text
GAP2_IMPLEMENTATION_ACCEPTED=YES
```

Only after that implementation is integrated and independently reviewed may
the meaningful context path be considered for GAP2 closure. Even then:

```text
GAP2_IMPLEMENTATION_ACCEPTED != TASK7_TRAINING_AUTHORIZED
```

Training requires a later explicit Task7 readiness/execution-contract decision.

```text
MEANINGFUL_PROFILE=RESOLVED
MEANINGFUL_KIND=RESOLVED
MEANINGFUL_SEED_VECTOR=[1,2]
MEANINGFUL_JOB_COUNT=16
MEANINGFUL_JOB_ORDER=RESOLVED
CHECKPOINT_BINDING=RESOLVED_AS_LOADER_ISSUED_CAPABILITY
MEANINGFUL_CHECKPOINT_BINDING_INTERFACE=PASS
REAL_TASK7_CHECKPOINT_BINDING=NOT_RUN
CARD_VOCABULARY_BINDING=RESOLVED
TEACHER_BINDING=RESOLVED
CORPUS_IDENTITY_DERIVATION=RESOLVED
REPLAY_ADMISSION_PATH=RESOLVED
FAILURE_ATTRIBUTION=RESOLVED

TASK7_GAP2_MEANINGFUL_CONTEXT_CONTRACT=PROPOSED
GAP2_IMPLEMENTATION_STARTED=NO
GAP2_IMPLEMENTATION_AUTHORIZED=NO
TASK7_READINESS=BLOCKED
TASK7_AUTHORIZED=NO
TASK7_FULL_AUTHORIZED=NO
TASK7_STARTED=NO
```
