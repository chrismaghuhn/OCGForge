# OCGForge Phase 6 Task 5 — Evaluation Execution Contract

## Status and scope

Status: CURRENT / AUTHORIZED — documentation and machine-plan contract freeze
only.

The human-readable semantic contract identity for this document is
ocgforge.phase6.task5.evaluation_execution_contract.v1. This is the only
top-level semantic identity owned by this Markdown document. The artifact
identities listed below name future machine-readable surfaces; they do not
create a second semantic authority.

The task identifier is P6_TASK5_CONTRACT_FREEZE. The accepted starting head is
95c55299fa2220dce18ee6c67474252e9c73d665. This document freezes the execution
architecture for Task 5. It does not implement an evaluator, run gameplay,
train a model, execute an optimizer step, create a checkpoint, choose a
backend, integrate Project Ignis, or authorize Task 6 or Task 7.

The words MUST, MUST NOT, SHOULD, MAY, and FAIL CLOSED are normative. This
contract refines the accepted Phase-6 evaluation plan operationally; it does
not replace or reinterpret the accepted Phase-5, trajectory, admission,
Teacher, checkpoint, inference, or Task-4A contracts.

## 1. Authority and accepted prerequisites

The semantic starting authority for Task 5 is the Phase-6 evaluation plan:
[P6_EVALUATION_PLAN.md](P6_EVALUATION_PLAN.md). The other prerequisite
authorities are:

| Authority | Task-5 use |
| --- | --- |
| [P6_BC_CONTRACT.md](P6_BC_CONTRACT.md) | exact-domain candidate scoring, label meaning, no fallback |
| [P6_DATASET_AND_SPLIT_CONTRACT.md](P6_DATASET_AND_SPLIT_CONTRACT.md) | admitted membership, episode split, sample identity |
| [P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md](P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md) | checkpoint binding, response freshness, deterministic selection |
| [P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md](P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md) | binary32 score codec, exact projection, canonical checkpoint inputs |
| [ADR-0007](../adr/ADR-0007-phase6-behavior-cloning-boundary.md) | accepted Phase-6 ownership and non-goals |
| Phase-3B trajectory/admission contracts | trusted records, replay, receipts, and DatasetManifest membership |
| Phase-5 model contracts | public model input, CardVocabulary, candidate order, and model-input identity |

The accepted Task-4B prerequisite is the immutable recovery result at
docs/p6/task4b/recovery-v1/task4b-acceptance-recovery.json:

    ORIGINAL_SMOKE_PASS=true
    ORIGINAL_TASK4B_PASS=false
    TASK4B_RECOVERY_PASS=true
    TASK4B_FINAL_PASS=true

The original historical Task-4B files remain immutable. In particular,
SMOKE_PASS=true and TASK4B_PASS=false in the original execution and
acceptance records are never rewritten. TASK4B_FINAL_PASS=true is a derived
recovery acceptance signal, not a replacement for the historical
TASK4B_PASS=false field. The smoke checkpoint is a real trained artifact, but
it is not a strategically playable baseline.

For this contract, the Task-4B accepted identities are:

| Value | Accepted identity |
| --- | --- |
| checkpoint | phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327 |
| smoke evidence | phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe |
| training-run evidence | phase6_training_run.v1.5511c410528270605700353468843b0453596a86c7b4e6c844a80a98eedd3dfc |
| source dataset | 24b690ae989f9176fecc8931b86d282dbcd3cb0912044317c32a2a0815b8b936 |
| dataset split | phase6_dataset_split.v1.b4cbecd49889c47f3bd05351d3976e886f3f3740a0a4e9c6f6969e0e782c68f1 |
| architecture | phase6_architecture_config.v1.224e5f200310852bd09c85643d68059582b4da16b529c3790ea166dea7a67003 |
| CardVocabulary | model_card_vocabulary.v1.a565d2b411ae16dd1fc192ed11add10efb948979024f41a0419f9a7222044820 |

These values are accepted inputs for future wiring checks only. This
documentation task does not load or execute them.

The fixed Task-5 curriculum remains exactly:

| Field | Frozen value |
| --- | --- |
| matchup | ocgforge.matchup.swordsoul_salamangreat.v1 |
| rules bundle | 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f |
| format | TCG_ADVANCED_2026_05_18 |
| duel mode | DUEL_MODE_MR5 |
| duel flags | 190464 |
| Swordsoul Tenyi deck | ocgforge.swordsoul_tenyi.ml_v1; SHA-256 8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7 |
| Salamangreat deck | ocgforge.salamangreat.ml_v1; SHA-256 6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188 |

## 2. Human and machine authority

This Markdown owns meanings, invariants, privacy rules, failure semantics,
artifact relationships, acceptance interpretation, slice ownership, and
non-goals. The machine plan at
[task5_execution_plan.v1.json](task5/task5_execution_plan.v1.json) owns only
the deterministic orchestration subset:

    machine plan schema ID = ocgforge.phase6.task5_execution_plan.v1
    schema_id
    human_contract_id
    task_id
    accepted_base_commit
    prerequisite_contract_ids
    planned_slices
    slice_dependencies
    artifact_contract_ids
    gate_ownership
    explicit_not_run_gates
    forbidden_scope
    task5_implementation_authorized

The following overlapping values MUST agree exactly between this document and
the JSON plan:

| Overlap | Markdown declaration | JSON field |
| --- | --- | --- |
| human contract | the identity in the Status and scope section | human_contract_id |
| task | P6_TASK5_CONTRACT_FREEZE | task_id |
| accepted base | 95c55299fa2220dce18ee6c67474252e9c73d665 | accepted_base_commit |
| slices | T5A, T5B, T5C, T5D in that order | planned_slices[].slice_id |
| dependencies | T5A → T5B and T5C; T5B and T5C → T5D | slice_dependencies and planned_slices[].depends_on |
| artifact IDs | the Artifact contract identities table | artifact_contract_ids |
| gate ownership | the Gate ownership table | gate_ownership |
| not-run set | every P6-G00 through P6-G18 | explicit_not_run_gates |
| forbidden scope | the exact token list in the Scope boundary section | forbidden_scope |
| implementation authorization | NO for Task-5 implementation | task5_implementation_authorized |

A future consistency validator MUST:

1. decode the JSON as strict UTF-8 without a BOM and reject duplicate object
   keys, non-finite numeric values, unknown top-level fields, and missing
   required fields;
2. reserialize it with ensure_ascii=true, allow_nan=false, sort_keys=true,
   separators=(",", ":"), and exactly one LF terminator, then require byte
   equality with the committed file;
3. extract the Markdown declarations above and the ordered slice, artifact,
   gate, not-run, and forbidden-scope tables;
4. compare every overlapping scalar, array, map key, map value, and order in
   both directions; and
5. FAIL CLOSED on any disagreement, omission, extra entry, or attempted
   precedence-based repair.

The JSON plan is not allowed to introduce a semantic rule absent from this
document. A semantic change updates this Markdown and, when an overlapping
orchestration value changes, the JSON plan in the same reviewed change.

The ordered prerequisite_contract_ids value is:

    ocgforge.public_environment_observation.v1
    ocgforge.public_action_identity.v1
    ocgforge.episodic_environment.v2
    ocgforge.trusted_trajectory.v1
    ocgforge.admission_receipt.v1
    ocgforge.dataset_manifest.v1
    ocgforge.policy_provenance.v1
    ocgforge.model_logical_input.v1
    ocgforge.model_encoded_input.v1
    ocgforge.model_batch_layout.v1
    ocgforge.model_supervision_sample.v1
    ocgforge.phase6.bc_contract.v1
    ocgforge.phase6.dataset_membership.v1
    ocgforge.phase6.dataset_split.v1
    ocgforge.phase6.checkpoint_manifest.v1
    ocgforge.phase6.checkpoint_artifact.v1
    ocgforge.phase6.canonical_weight_export.v1
    ocgforge.phase6.inference_request.v1
    ocgforge.phase6.inference_response.v1
    ocgforge.phase6.inference_numeric.v1
    ocgforge.phase6.bc.inference_tiebreak.v1
    ocgforge.phase6.task4.numeric_projection.v1
    ocgforge.phase6.task4.smoke_corpus.v2
    ocgforge.phase6.task4.corpus_authority.v1
    ocgforge.phase6.task4b.smoke_evidence.v1
    ocgforge.phase6.task4b.acceptance_recovery.v1
    ocgforge.phase6.gameplay_metrics.wilson_95.v1
    ocgforge.phase6.first_divergence.v1

## 3. Scope boundary and forbidden work

This task changes documentation only. It may create the human contract and
machine plan, and it may update no production source, test, rules bundle,
deck, Teacher, checkpoint, or generated evaluation evidence. It does not
execute any future Task-5 slice.

The exact machine-plan forbidden-scope tokens are:

    task5_evaluation_implementation
    gameplay_evaluation_execution
    model_training
    optimizer_steps
    new_checkpoint
    pytorch_jax_backend_bakeoff
    ignis_integration
    task6
    task7
    rl
    self_play
    new_evaluation_evidence

Task-5 implementation authorization remains NO after this freeze. Task 6,
backend winner selection, RL, self-play, and the separate OCGForge-Ignis I2–I5
lane remain outside this repository's dependency graph. No Task-5 artifact may depend on OCGForge-Ignis.

## 4. Evaluation architecture and output authority

The future execution flow is:

    immutable evaluation manifest and job manifest
        ↓
    normal public observation and complete candidate domain
        ↓
    checkpoint-bound PolicySelection
        ↓
    EpisodicEnvironment V2
        ↓
    normal trajectory recorder
        ↓
    semantic replay and admission
        ↓
    canonical machine-readable evidence
        ↓
    validated typed read model
        ↓
    deterministic derived report.md

Machine-readable evidence is authoritative after strict validation. The
human-readable report is presentation only. A report generator MUST accept one
validated typed read model and MUST NOT accept independently supplied metric
values, counts, identities, or status overrides. A Markdown report MUST NOT
be hand-edited to make a gate pass.

The conceptual artifact layout is:

    artifacts/phase6/task5/<evaluation_identity>/
    ├─ manifest.json
    ├─ summary.json
    ├─ offline/
    │  ├─ metrics.json
    │  ├─ slices.json
    │  └─ samples.jsonl
    ├─ gameplay/
    │  ├─ jobs.jsonl
    │  ├─ outcomes.json
    │  └─ replay_admission.json
    ├─ divergence/
    │  └─ first_divergences.jsonl
    ├─ distribution_shift/
    │  └─ comparison.json
    └─ report.md

No directory, filename, mutable alias, or physical path is an authority.
The path is a locator for the content-addressed evaluation identity.

### Artifact contract identities

The following V1 identities are proposed and frozen for future implementation.
The identity prefix shown is the lexical prefix of the content identity; the
content hash is not issued by this documentation task.

| Artifact or nested value | Contract/schema identity | Authority classification |
| --- | --- | --- |
| evaluation manifest | ocgforge.phase6.task5.evaluation_manifest.v1; phase6_evaluation_manifest.v1.<sha256> | authoritative orchestration and semantic bindings |
| top-level evaluation summary | ocgforge.phase6.task5.evaluation_summary.v1; phase6_evaluation_summary.v1.<sha256> | authoritative typed summary/index |
| evaluation job manifest | ocgforge.phase6.task5.evaluation_job_manifest.v1; phase6_evaluation_job_manifest.v1.<sha256> | authoritative ordered job population |
| one evaluation job | ocgforge.phase6.task5.evaluation_job.v1; phase6_evaluation_job.v1.<sha256> | authoritative job identity and definition |
| evaluation contract identity | ocgforge.phase6.evaluation_contract_identity.v1; phase6_evaluation_contract.v1.<sha256> | authoritative semantic contract binding |
| evaluation job identity | ocgforge.phase6.evaluation_job_identity.v1; phase6_evaluation_job.v1.<sha256> | authoritative content identity of one job |
| evaluation corpus identity | ocgforge.phase6.evaluation_corpus_identity.v1; phase6_evaluation_corpus.v1.<sha256> | authoritative content identity of a fixed job population |
| offline summary | ocgforge.phase6.offline_metrics.v1; phase6_offline_metrics.v1.<sha256> | authoritative machine evidence |
| offline slice result | ocgforge.phase6.offline_slice.v1; phase6_offline_slice.v1.<sha256> | authoritative machine evidence |
| offline sample result | ocgforge.phase6.offline_sample.v1; phase6_offline_sample.v1.<sha256> | authoritative machine evidence |
| candidate score vector | ocgforge.phase6.score_vector.v1; phase6_score_vector.v1.<sha256> | authoritative numeric diagnostic evidence |
| gameplay job result | ocgforge.phase6.gameplay_job_result.v1; phase6_gameplay_job_result.v1.<sha256> | authoritative per-job machine evidence |
| gameplay summary | ocgforge.phase6.gameplay_summary.v1; phase6_gameplay_summary.v1.<sha256> | authoritative machine evidence |
| replay/admission summary | ocgforge.phase6.task5.replay_admission_summary.v1; phase6_replay_admission_summary.v1.<sha256> | authoritative result of existing replay/admission authorities |
| first-divergence record | ocgforge.phase6.first_divergence.v1; phase6_first_divergence.v1.<sha256> | authoritative public-safe machine evidence |
| distribution-shift summary | ocgforge.phase6.distribution_shift.v1; phase6_distribution_shift.v1.<sha256> | authoritative separate-population comparison |
| Wilson metric | ocgforge.phase6.gameplay_metrics.wilson_95.v1 | accepted metric identity retained from P6 evaluation planning |
| derived human report | ocgforge.phase6.task5.report.v1; phase6_task5_report.v1.<sha256> | derived presentation only |

The Task-4B checkpoint, Task-4B smoke evidence, accepted DatasetManifest,
admission receipts, and Phase-5 values are external accepted inputs. They are
not rewritten into Task-5 output authority.

## 5. Canonical serialization and identity separation

New machine artifacts use canonical JSON for transport and the accepted
Phase-3/Phase-5 primitive encoding for content identities:

    u8/u16/u32/u64: unsigned big-endian
    signed i32: two's-complement u32 bits
    bool: u8 value 0 or 1
    string: u32be byte length followed by strict UTF-8 bytes
    vector: u32be count followed by entries in declared order
    optional: presence u8 followed by the value when present

JSON artifacts are UTF-8 without a BOM, use ensure_ascii=true,
allow_nan=false, sort_keys=true, separators=(",", ":"), and exactly one LF
terminator. JSON object key order is canonical serialization order only.
Semantic vectors retain their declared order. A vector described as a set is
sorted by unsigned UTF-8 bytes and has no duplicates. No JSON floating-point
value is authoritative for a score or identity.

Every content identity is recomputed from its canonical payload with its
identity domain and schema included in the declared field order. A declared
identity is not accepted merely because its digest is self-consistent; all
referenced contract, corpus, checkpoint, dataset, split, and policy
identities must also validate against the expected context.

### Semantic evaluation identities

The top-level evaluation identity is:

    phase6_evaluation.v1.<lowercase hexadecimal SHA-256>

Its canonical fields, in order, are evaluation_contract_identity,
evaluation_corpus_identity, checkpoint_identity, evaluator_semantic_version,
evaluator_semantic_source_commit, and the ordered population identities
referenced by the manifest. The top-level identity is a container binding; it
does not replace any sub-identity.

#### evaluation_contract_identity

The evaluation contract identity is:

    phase6_evaluation_contract.v1.<lowercase hexadecimal SHA-256>

It identifies the semantic evaluation rules, not one run or one worker. Its
canonical fields, in order, are:

    identity domain = ocgforge.phase6.evaluation_contract_identity.v1
    identity schema = ocgforge.phase6.evaluation_contract_identity.v1
    human contract identity
    evaluation manifest schema identity
    evaluation job manifest schema identity
    evaluation job schema identity
    offline metrics schema identity
    offline slice schema identity
    gameplay job-result schema identity
    gameplay summary schema identity
    first-divergence schema identity
    distribution-shift schema identity
    report derivation schema identity
    score-vector schema identity
    accepted inference numeric identity
    accepted binary32 codec identity
    accepted inference tie-break identity
    accepted Wilson-95 metric identity
    fixed matchup identity
    fixed rules-bundle identity
    fixed deck-role identities in role order
    failure/quarantine semantics = this human contract identity
    public/privacy semantics = accepted Phase-5 public-boundary identities
    replay/admission path identities = trusted_trajectory.v1 and admission_receipt.v1
    separate-population identity = ocgforge.phase6.distribution_shift.v1

Changing any semantic field above requires a new evaluation contract version or
an explicit migration. Framework, device, worker, PID, path, and wall time do
not enter this identity.

#### evaluation_job_identity

One job identity is:

    phase6_evaluation_job.v1.<lowercase hexadecimal SHA-256>

Its canonical fields, in order, are:

    identity domain = ocgforge.phase6.evaluation_job_identity.v1
    identity schema = ocgforge.phase6.evaluation_job_identity.v1
    evaluation schema/version
    evaluation_contract_identity
    evaluation corpus profile identity
    matchup identity
    rules-bundle identity
    seat/deck-role assignment
    locked deck identity for seat 0
    locked deck identity for seat 1
    checkpoint identity
    Phase-5 logical model-input contract identity
    Phase-5 encoded model-input contract identity
    Phase-5 batch-layout contract identity
    CardVocabulary identity
    Teacher policy producer identity
    Teacher policy artifact/binding identity for each acting role
    exact opponent policy identity for each opposing role
    dataset identity with explicit absence when not relevant
    dataset split identity with explicit absence when not relevant
    deterministic seed as u64
    starting-player assignment
    evaluator semantic version
    evaluator semantic source commit

The seat/deck-role assignment is a semantic token, not a seat number inferred
from map order. The starting-player assignment is explicit. For offline-only
work, the dataset and split fields are present and exact; for gameplay-only
work, both use the canonical absent optional value. There is no silent
wildcard.

The evaluator semantic source commit is an immutable 40-character lowercase
Git commit because evaluator code can change semantic results. A mutable
branch, tag, workspace, or path is not a source identity. The evaluator
semantic version and source commit are not available for issuance in this
documentation task.

#### evaluation_corpus_identity

One fixed population identity is:

    phase6_evaluation_corpus.v1.<lowercase hexadecimal SHA-256>

Its canonical fields, in order, are:

    identity domain = ocgforge.phase6.evaluation_corpus_identity.v1
    identity schema = ocgforge.phase6.evaluation_corpus_identity.v1
    evaluation_contract_identity
    corpus profile identity
    fixed matchup, rules bundle, and locked deck-role identities
    checkpoint identity when the corpus evaluates a checkpoint
    dataset and split identities when the corpus contains offline samples
    ordered vector of evaluation_job_identity values

The ordered job vector is the explicit corpus schedule. Reordering jobs
changes the corpus identity but not the identity of an unchanged individual
job. The implementation/acceptance and meaningful fixed-matchup profiles are
distinct even when they later use overlapping semantic dimensions.

### Identity-input applicability matrix

The identity layers intentionally do not all repeat the same fields. This
matrix is normative and makes every requested identity input explicit:

| Semantic input | evaluation_contract_identity | evaluation_job_identity | evaluation_corpus_identity | first_divergence_identity |
| --- | --- | --- | --- | --- |
| evaluation schema/version | direct contract field | direct field | direct through contract and job vector | inherited from job and record schema |
| rules bundle, matchup, and locked decks | fixed contract field | direct fields and explicit seat assignment | direct fixed fields and job vector | inherited from the job |
| checkpoint identity | not a contract field; varies by evaluation | direct field | direct field when checkpointed | inherited from the job |
| Phase-5 model/input contract identities | direct contract field | direct fields | inherited through contract and jobs | inherited through the job |
| DatasetManifest and split identity | not a universal contract field | present for offline jobs, explicit absent for gameplay jobs | present for offline populations, explicit absent otherwise | inherited from the job when relevant |
| Teacher and opponent policy identities | fixed policy rule and identity family | direct producer, binding, artifact, and opponent fields | inherited through the ordered job vector | inherited from the job |
| seed and job definition | not a run field | direct deterministic u64 seed and job definition | ordered job-identity vector | inherited from the job |
| seat/deck-role and starting-player assignment | fixed role vocabulary only | direct explicit fields | inherited through the ordered job vector | inherited from the job |
| evaluator semantic version/source identity | contract rule for immutable source binding | direct version and immutable source commit | inherited through jobs and top-level binding | inherited from the job |

“Not a contract field” means the value is deliberately supplied by the
concrete job or corpus identity; it is not omitted from the evaluation
identity. “Inherited” means the referenced identity is included exactly once
at its owning layer and is not duplicated with a second, independently
canonicalized value.

### Semantic versus provenance-only values

The following values enter semantic identities when their section requires
them:

    evaluation schema/version
    evaluation contract identity
    evaluation corpus/job definition
    fixed rules-bundle identity
    fixed matchup identity
    locked deck identities and seat/deck-role assignment
    checkpoint identity
    Phase-5 model-input contract identities and CardVocabulary identity
    DatasetManifest identity and split identity for offline populations
    Teacher and exact opponent policy identities
    deterministic seed
    starting-player assignment
    evaluator semantic version and immutable source commit

The following are provenance-only and MUST NOT enter semantic gameplay,
evaluation, corpus, job, score-vector, checkpoint, or divergence identities:

    framework backend and framework version
    GPU name, CUDA driver/runtime detail, and accelerator capability
    worker count, process topology, PID, thread ID, and scheduling
    wall-clock time, elapsed duration, and allocation order
    absolute filesystem path, cache path, and output directory
    mutable branch, mutable alias, and uncommitted workspace
    host name, environment-variable ordering, and log ordering

Provenance may be attached to machine evidence for diagnosis. A framework
version cannot be smuggled into semantic identity by calling it a seed or
evaluator version.

## 6. Exact candidate-score persistence

The authoritative score field is score_f32_bits. It is exactly eight
lowercase hexadecimal characters representing the four raw IEEE-754 binary32
bytes in big-endian order:

    byte 0 → hex characters 0..1
    byte 1 → hex characters 2..3
    byte 2 → hex characters 4..5
    byte 3 → hex characters 6..7

For example, binary32 1.0 is 3f800000 and negative zero is 80000000.
Positive zero and negative zero remain distinct. NaN, positive infinity,
negative infinity, wrong-length strings, uppercase hex, and non-hex values
fail closed. Decimal-rendered text is never the score authority.

The candidate-score record preserves:

    source ordinal
    exact public_action_key
    public action kind
    accepted public candidate fields and optional-presence values
    score_f32_bits

The candidate vector is an array in exact source order. Multiplicity and candidate-to-key pairing are preserved. The implementation MUST NOT sort,
deduplicate, truncate, top-K, or regroup the authoritative vector by score.
The accepted public domain still requires valid unique public_action_key values;
an invalid duplicate key rejects the complete domain before scoring. A
score-ranked report or UI is a derived view and never replaces the source
vector.

The score-vector identity is:

    phase6_score_vector.v1.<lowercase hexadecimal SHA-256>

Its canonical fields, in order, are the identity domain, identity schema,
candidate count as u32be, then for each source-order candidate the exact
public_action_key string and four score bytes. Candidate descriptors remain
bound by the separate ordered candidate-domain identity. A human-friendly
decimal score may be rendered only from score_f32_bits by a versioned
presentation formatter; it never enters score-vector identity.

Selection inherits
ocgforge.phase6.bc.inference_tiebreak.v1:

    higher finite binary32 score wins
    exact equal-score ties choose bytewise-ascending public_action_key

The tie rule resolves a selection from the current routing sidecar. It does
not reorder the candidate vector or feed public keys to the learned feature
path.

## 7. Offline evaluation

### 7.1 Population authority

The offline evaluator consumes only admitted
ModelSupervisionSampleV1-derived validation and test data:

    validated DatasetManifest
        + verified AdmissionReceipt values
        + clean trusted trajectory records
        + TrainingDatasetSplitV1
        + exact public ModelSupervisionSampleV1 values
        + exact Phase-5 logical/encoded model input
        + one validated checkpoint-bound inference response

The split is episode-disjoint. No individual DecisionRecord, supervision row,
candidate row, or physical cache row is repartitioned. Training samples are
not part of the validation/test metric population. The Task-4B corpus.p6c is
a derived smoke projection, not membership authority; it may be used only
when it resolves back to the accepted authority sidecar, DatasetManifest,
split, and admitted source-sample identities.

One accepted decision record yields one sample result. Continuation decisions
are ordinary samples with their own complete current domain. A missing,
duplicated, or inconsistent selected public_action_key rejects that sample;
the evaluator never repairs it by removing a candidate or choosing the first
match.

### 7.2 Required offline result

The offline summary and every slice result MUST report, at minimum:

    total_count
    scored_count
    rejected_count
    unscored_count
    exact-domain BC loss over N real candidates
    Teacher top-1 agreement
    optional declared top-K agreement
    exact selected public_action_key and local candidate ordinal checks
    label-consistency result
    failure-reason counts
    capacity, padding-mask, privacy/input, and label-mismatch counts

For each valid sample with finite score vector s[0..N-1] and Teacher label y,
the semantic loss is:

    loss = -log(exp(s[y]) / sum(exp(s[i]) for i in 0..N-1))

Only the exact N real candidates contribute. A padding row has mask 0 and is
not a class. A physical width smaller than N is a structured failure. The
existing Phase-5 capacity witnesses N=24, N=25, and N=129 remain mandatory
future inputs.

The authoritative score vector is persisted in source order with
score_f32_bits. If a future implementation persists an aggregate loss as a
floating value, it MUST also persist its exact declared numeric representation
or a canonical bit field; a decimal rendering alone is insufficient.

Top-K is optional and must be declared per evaluation. It is eligible only
when the declared K is no greater than the exact sample domain size. A
derived ranking uses the accepted exact-score ordering and tie rule; it never
changes the authoritative candidate order. Top-1 agreement is agreement with
the Teacher-selected key, not proof that the Teacher selected the uniquely
optimal action.

An unselected candidate means only that the Teacher did not select it in that
public frame. It is not a negative strategic label, an illegal candidate, or
proof of poor play. The evaluator MUST distinguish candidate absent from the
domain, candidate present but not selected, sample rejected, and sample
unscored.

### 7.3 Required deterministic slices

Offline results MUST retain separate counts and identities for at least:

    decision/request family
    exact candidate-domain size
    N=24 witness
    N=25 witness
    N=129 witness
    phase and turn/decision context
    acting participant and locked deck role
    starting-player partition
    continuation versus non-continuation
    rare or critical slices when identified

Slice definitions use a fixed dimension order and explicit absence values.
Slice arrays are emitted in that order; no map iteration defines report order.
An empty slice is NOT_PRESENT, not a zero-valued passing result. Slice
identities include the source population identity, dimension contract, exact
slice coordinates, and the typed result fields. A slice result never replaces
the aggregate summary.

## 8. Frozen gameplay evaluation

### 8.1 Normal execution path

Every future gameplay job MUST use this path:

    immutable checkpoint policy
        → normal PolicySelection
        → EpisodicEnvironment V2
        → normal trajectory recorder
        → semantic replay
        → admission

The model receives only the current public observation and complete ordered
public candidate domain. The Environment remains the sole legality,
continuation, response, and engine-advancement authority. A neural shortcut,
direct engine response, reconstructed candidate, or policy-side continuation
is not a valid evaluation.

The inference response must bind checkpoint identity, model-input identity,
ordered candidate-domain identity, current public decision identity where
available, exact N score count, source-order score bits, selected local
ordinal, and selected public_action_key. A timeout, crash, transport error,
stale response, wrong response binding, non-finite score, wrong length,
invalid ordinal, or invalid key fails closed. It never invokes Teacher,
RandomLegal, a heuristic, first-candidate, candidate-zero, or a retry.

### 8.2 Initial small implementation/acceptance corpus

The initial small corpus profile is:

    ocgforge.phase6.task5.evaluation_corpus.implementation_acceptance.v1

It contains eight jobs: two explicit seeds, both seat/deck-role assignments,
and both starting-player assignments. The job-vector order is:

| Order | Seed | Seat/deck-role assignment | Starting player | Evaluated role | Exact opponent |
| ---: | ---: | --- | --- | --- | --- |
| 0 | 1 | P0 Swordsoul Tenyi / P1 Salamangreat | P0 | P0 Swordsoul Tenyi | Salamangreat Teacher artifact |
| 1 | 1 | P0 Swordsoul Tenyi / P1 Salamangreat | P1 | P0 Swordsoul Tenyi | Salamangreat Teacher artifact |
| 2 | 1 | P0 Salamangreat / P1 Swordsoul Tenyi | P0 | P0 Salamangreat | Swordsoul Teacher artifact |
| 3 | 1 | P0 Salamangreat / P1 Swordsoul Tenyi | P1 | P0 Salamangreat | Swordsoul Teacher artifact |
| 4 | 2 | P0 Swordsoul Tenyi / P1 Salamangreat | P0 | P0 Swordsoul Tenyi | Salamangreat Teacher artifact |
| 5 | 2 | P0 Swordsoul Tenyi / P1 Salamangreat | P1 | P0 Swordsoul Tenyi | Salamangreat Teacher artifact |
| 6 | 2 | P0 Salamangreat / P1 Swordsoul Tenyi | P0 | P0 Salamangreat | Swordsoul Teacher artifact |
| 7 | 2 | P0 Salamangreat / P1 Swordsoul Tenyi | P1 | P0 Salamangreat | Swordsoul Teacher artifact |

The displayed order is a schedule coordinate, not part of an individual job
identity. The seed is an explicit u64 value. The locked matchup, rules
bundle, format, duel mode, deck identities, and Teacher producer are the
values fixed by P6_BC_CONTRACT.md. The exact Teacher PolicyArtifact identities
are:

    Swordsoul:
    policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d

    Salamangreat:
    policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527

Each job also retains the relevant Teacher binding identity,
ocgforge.policy.teacher_core.v1, the deterministic lexicographic selection
identity, and ocgforge.no_policy_rng.v1. The evaluated checkpoint policy is
the only policy under evaluation; the opponent identity is not inferred from
the deck name.

This corpus is for codec, wiring, deterministic execution, failure, replay,
and admission acceptance. It is not a statistically powered strength
evaluation and must not be described as one.

### 8.3 Later meaningful fixed-matchup corpus

The later meaningful corpus has a distinct profile identity:

    ocgforge.phase6.task5.evaluation_corpus.meaningful_fixed_matchup.v1

It remains limited to the same locked matchup and exact Teacher/opponent
identities. Its explicit seed vector, job count, job manifest, and resulting
content identity MUST be authored and reviewed in a later authorized
evaluation slice. This contract deliberately freezes no large count merely
for statistical power. The meaningful corpus MUST NOT reuse the
implementation/acceptance corpus identity, and a small smoke result cannot
stand in for it.

### 8.4 Outcome and reliability accounting

Gameplay machine evidence MUST report these categories separately:

    total jobs
    completed terminal jobs
    trusted wins
    trusted losses
    trusted draws
    interrupted jobs
    failed jobs
    quarantined jobs
    fallback-assisted jobs
    replay failures
    admission failures
    inference/request failures

The categories are represented by explicit per-job status fields. A failed
inference is not a loss. An interrupt is not a loss. A replay or admission
failure is not silently removed from the denominator. A terminal result counts
as a trusted win, loss, or draw only when the normal trajectory, replay, and
admission path succeeds and fallback_assisted is false.

fallback_assisted MUST be false for trusted BC evaluation and its aggregate
count MUST be zero. No failed inference may be converted into Teacher,
RandomLegal, heuristic, or continuation behavior. A fallback attempt is
itself a fail-closed evaluation failure and quarantines the affected job.

The accepted uncertainty identity is
ocgforge.phase6.gameplay_metrics.wilson_95.v1. The primary decisive win-rate
metric uses trusted wins as numerator and trusted wins plus trusted losses as
denominator. Draws, interruptions, failures, quarantines, replay failures,
admission failures, and fallback-assisted jobs are reported separately and
are not relabeled as losses. When the decisive denominator is zero, the
interval status is NOT_APPLICABLE rather than a fabricated zero. The
numerator, denominator, interval inputs, and metric identity are all emitted
by machine evidence.

## 9. Task-4B smoke-checkpoint boundary

The accepted Task-4B smoke checkpoint may potentially exercise only executable
mechanics:

    checkpoint loading and expected-context binding
    exact-domain score and loss wiring
    N-to-N candidate handling and N=24/N=25/N=129 capacity checks
    canonical output codecs and score-bit persistence
    deterministic job/evaluation serialization
    first-divergence record construction
    replay/admission integration mechanics
    structured failure and quarantine handling
    paired-hidden-world equality when the required public worlds exist

Those exercises may establish implementation evidence for a future slice only
after that slice runs its own accepted gates. This contract task runs none of
them. The smoke checkpoint MUST NOT be used to claim:

    strategic playability
    convergence
    Teacher parity
    meaningful win rate
    general gameplay strength
    backend superiority

If a future gate requires a meaningful trained baseline rather than an
executable checkpoint, it remains NOT_RUN and BLOCKED until that baseline is
separately authorized and accepted. No smoke metric may be promoted into a
gameplay-strength claim.

## 10. First-divergence record

The first-divergence artifact is public/model-audit-safe and uses the existing
identity family ocgforge.phase6.first_divergence.v1. The evaluator runs Teacher
and BC from the same immutable initial job identity and compares the semantic
public decision sequence. The first record is the earliest semantic
divergence; later divergences are not substituted for it.

The canonical record retains:

    evaluation_job_identity
    semantic decision identity when the public frame provides it
    public observation digest
    model-input identity
    complete ordered candidate-domain identity
    candidate count
    candidate public_action_key vector in source order
    candidate action kinds and accepted public candidate fields in source order
    exact score_f32_bits vector in source order
    score-vector identity
    Teacher-selected public_action_key
    model-selected public_action_key
    decision/request family
    continuation versus non-continuation context
    first-divergence ordinal
    explicit record kind
    explicit failure-before-divergence value when applicable

The allowed record kinds are DIVERGENCE,
NO_DIVERGENCE_TERMINAL, and FAILURE_BEFORE_DIVERGENCE. A failure-before-
divergence record has a stable public failure stage and error code. Model
scores and model-selected key are absent when no model response was accepted;
the evaluator never invents a candidate or a divergence. If no public
decision identity exists before a failure, the field is explicitly absent
with the failure record kind, not silently replaced by an internal ID.

The public candidate descriptor vector contains, in source order, only the
accepted public fields action_kind, optional choice, optional source_reference,
optional target_reference, optional phase, optional position, optional
source_index, optional amount, continuation_operation, and
submits_engine_response. Optional presence is preserved. public_action_key is
the separate routing vector. Internal semantic keys and private locators are
never descriptor fields.

The first-divergence identity canonical fields, in order, are:

    identity domain = ocgforge.phase6.first_divergence.v1
    identity schema = ocgforge.phase6.first_divergence.v1
    evaluation_job_identity
    record kind
    first-divergence ordinal
    semantic decision identity optional value
    public observation digest
    model-input identity
    ordered candidate-domain identity
    candidate count
    ordered public_action_key vector
    ordered public candidate descriptor vector
    ordered score_f32_bits vector
    score-vector identity
    Teacher-selected public_action_key optional value
    model-selected public_action_key optional value
    decision/request family
    continuation context
    failure-before-divergence value optional

The canonical record MUST NOT contain CoreHost state, raw engine state, hidden
opponent card identities, private hand/deck contents, face-down Extra Deck
identity, private or internal locators, raw response bytes, SubmissionToken,
pointers, object IDs, hidden-derived hashes, or omniscient debug state. A
privacy validation failure before publication is FAIL CLOSED and quarantines
the job; it is not rendered as a convenient public record.

For identical accepted job, checkpoint, policy, public-input, and evaluator
identities, the same record or the same explicit failure-before-divergence
record must be reproduced. The score vector is diagnostic evidence over the
already-public candidate rows; it is never hidden state.

## 11. Distribution-shift evidence

Teacher-state validation and BC-induced public states are separate populations
with separate identities:

    phase6_teacher_state_population.v1.<lowercase hexadecimal SHA-256>
    phase6_bc_induced_population.v1.<lowercase hexadecimal SHA-256>

The Teacher-state population identity binds the accepted DatasetManifest,
split identity, selected validation/test partitions, evaluator contract, and
the ordered source sample identities. Its source is admitted
ModelSupervisionSampleV1 data.

The BC-induced population identity binds the evaluation corpus identity,
checkpoint identity, evaluator contract, and the ordered public decision
sequence actually induced by BC jobs. It contains only public observation,
model-input, candidate-domain, and outcome/failure identities. It does not
contain hidden engine state or a private reconstruction.

The distribution-shift summary identity is:

    phase6_distribution_shift.v1.<lowercase hexadecimal SHA-256>

Its canonical payload references both population identities and retains,
without merging them:

    decision/request-family distributions
    candidate-domain-size distributions
    exact capacity and padding compliance
    inference failure and quarantine rates
    replay and admission rates
    Teacher agreement where the Teacher can be evaluated diagnostically
    terminal outcome and interruption/failure differences
    all required offline slices and corresponding BC-induced slices

The comparison MUST preserve population labels, counts, and denominators.
Teacher agreement on BC-induced states is diagnostic only. Offline agreement
is not online parity, strategic optimality, or evidence outside the fixed
curriculum. A missing population is NOT_PRESENT/BLOCKED, not an empty
successful comparison.

## 12. Failure, quarantine, and replay semantics

The following failures are explicit machine result values:

    model timeout or process crash
    transport or serialization failure
    missing, malformed, non-finite, or wrong-length response
    stale, duplicated, late, wrong-checkpoint, wrong-input, or wrong-domain response
    invalid selected ordinal or public_action_key
    insufficient candidate capacity
    privacy or public-input validation failure
    exact label or split inconsistency
    replay failure
    admission failure
    unexpected rules/deck/Teacher identity
    any fallback-assisted attempt

The affected sample or gameplay job is rejected and, where the existing
trajectory semantics require it, quarantined. A failed job may be marked
FAILED or INTERRUPTED only with the existing environment/trajectory meaning.
It never receives a fabricated action, reward, winner, loss, label, replay
receipt, or trusted admission. The evaluator does not retry under another
policy and does not repair a stale or incomplete response.

For a gameplay job, the normal public action is submitted only after the
checkpoint/inference response passes all bindings. Intermediate continuation
actions remain adapter-local according to the accepted environment contract;
only the terminal public action advances the engine. A failed continuation
response fails the job and cannot be replaced by a Teacher continuation.

Replay and admission use the existing trusted trajectory/admission contracts.
Task 5 creates no alternate replay authority. Only a job with a successful
normal trajectory, deterministic semantic replay, and accepted admission can
contribute trusted gameplay outcome counts. A replay or admission failure
remains visible in its own counter and result record.

## 13. Privacy and information safety

Task-5 artifacts and reports are public/model-audit-safe. They may contain:

    accepted public observation and model-input identities
    public candidate-domain identity and exact public_action_key values
    public action kinds and accepted public optional fields
    visible/redacted public card descriptors and presence masks
    public phase, turn, participant, deck-role, seat, and starting-player context
    checkpoint, contract, dataset, split, Teacher, opponent, and job identities
    score_f32_bits over the supplied public candidate rows
    public outcome, failure, quarantine, replay, and admission counts

They MUST NOT contain:

    CoreHost or raw engine state/query payloads
    raw engine messages or response bytes
    SubmissionToken values
    raw pointers, addresses, object IDs, or private semantic keys
    opponent hidden hand identities or hidden Main Deck order/entries
    opponent face-down Extra Deck identities
    face-down field identity not permitted by the public contract
    private/persistent locators or identity preserved across a shuffle boundary
    hidden passcodes, hidden-derived hashes, beliefs, inferred archetypes, or reconstructed hands
    private continuation state, hidden engine-step data, or omniscient debug values

An accepted public locator is a current-frame reference only. It is not a
physical-card identity, persistent identity, or permission to inspect private
state. A diagnostic convenience is not authorization to query CoreHost.

If a metric or diagnostic requires hidden information, Task 5 marks it
UNSUPPORTED or defines a separately authorized private artifact outside the
public/model-audit output. It is never silently included in a public report.
The first unexplained privacy violation fails closed before evidence
publication.

## 14. Determinism and replay requirements

Future implementations MUST:

    preserve canonical JSON and accepted primitive serialization
    preserve candidate vector source order and multiplicity
    preserve source-order score_f32_bits
    use explicit ordered vectors and stable map-key serialization
    use content-addressed semantic identities
    use the explicit fixed job vector and deterministic job order
    use the accepted exact binary32 score comparison and tie rule
    derive reports from a validated typed read model
    bind replay to the exact job, checkpoint, public input, and candidate domain

No semantic identity or authoritative result may depend on wall time, PID,
thread ID, filesystem path, random UUID, unordered iteration, GPU allocator
state, worker scheduling, mutable alias, or host-specific incidental order.

The deterministic replay invariant is:

    same accepted job and checkpoint
        → same public input/domain sequence
        → same score-bit vectors
        → same selected public_action_key sequence
        → same terminal/replay/admission result

The valid alternative is the same explicit fail-closed failure at the same
semantic boundary. Replay compares semantic public identities and accepted
trajectory values; it does not treat build or hardware provenance hashes as
gameplay identities.

## 15. Task-5 slice decomposition

The future implementation is divided into four independently reviewable
slices. This task only freezes them.

### T5A — Evaluation schemas, codecs, identities, and job manifests

Owning layer: the Phase-6 evaluation codec and manifest layer above accepted
Phase-5 model values and Task-4A checkpoint/inference codecs.

Inputs: this Task-5 contract, accepted prerequisite contract identities,
fixed rules/deck/Teacher identities, accepted checkpoint manifest, and the
declared implementation/acceptance job matrix.

Outputs: evaluation manifest, evaluation job manifest, evaluation-job
identity, corpus identity, score-vector codec, first-divergence field codec,
typed failure values, and canonical serialization validators.

Invariants affected: content-addressed semantic identity, explicit
provenance separation, source-order candidate and score preservation, exact
binary32 bits, public-only fields, duplicate-key rejection, and no mutable
alias identity.

Gates this slice may close after its own implementation evidence:
P6-G04, P6-G05, and the schema/identity portions of P6-G10. It cannot close
any gameplay, replay, outcome, or strength gate.

Negative tests: non-canonical JSON, duplicate object keys, missing/extra plan
entries, wrong contract or checkpoint identity, mutable branch/path identity,
reordered candidates, dropped multiplicity, wrong score-bit length, decimal-
only scores, NaN/Inf, duplicate public keys, and provenance fields entering a
semantic digest.

Privacy impact: establishes the allowlist and rejects private fields before
serialization. Determinism impact: high; all identity and ordering rules
begin here. Replay implication: job and score identities must bind the exact
current public domain. Non-goals: no model forward, gameplay, training, or
report publication.

Prerequisites: accepted Phase-5 model contracts, Task-4A codecs, accepted
Task-4B recovery/checkpoint evidence, and this contract. STOP after codec and
manifest evidence; do not start T5B, T5C, or T5D in the same implementation
slice.

### T5B — Offline evaluator, metrics, and deterministic slicing

Owning layer: the offline Phase-6 data consumer above admitted trajectory
records and the Phase-5 model-facing representation.

Inputs: validated DatasetManifest and receipts, episode-level split,
admitted validation/test ModelSupervisionSampleV1 values, exact public
logical/encoded inputs, checkpoint-bound inference, and T5A codecs.

Outputs: offline sample results, offline slice results, offline summary,
failure-reason counts, exact score vectors, and the
Teacher-state population identity.

Invariants affected: admitted membership only, no individual repartition,
exact-domain N-to-N scoring, N=24/N=25/N=129 capacity, real/padding masks,
exact Teacher label/key consistency, continuation inclusion, public-only
inputs, and no negative strategic labels from non-selection.

Gates this slice may close after its own implementation evidence:
P6-G01, P6-G02, P6-G03, P6-G06, P6-G07, and P6-G13. It cannot close online
parity, gameplay outcome, replay, or strategic-strength claims.

Negative tests: arbitrary parsed files, missing or invalid receipts,
quarantined trajectories, train/test episode leakage, RandomLegal labels,
missing or duplicate selected keys, changed redaction, reordered/dropped
candidates, widths below N, padding scored as a class, top-K domain
replacement, hidden fields, and Teacher labels treated as optimality proofs.

Privacy impact: consumes only public/admitted values and emits only public
sample fields. Determinism impact: fixed population and slice order, exact
score bits, and stable failure denominators. Replay implication: source
admission is required, but offline evaluation does not invent a new replay
path. Non-goals: no gameplay job execution, training, or backend comparison.

Prerequisites: T5A, accepted DatasetManifest/admission/split contracts, and
an accepted checkpoint context. STOP after offline evidence; do not launch
T5C or T5D as an implicit continuation.

### T5C — Frozen gameplay evaluator, outcomes, replay, and admission

Owning layer: the Phase-6 gameplay evaluator above PolicySelection,
EpisodicEnvironment V2, trajectory recording, semantic replay, and admission.

Inputs: T5A job manifests/codecs, accepted checkpoint and inference
contracts, fixed rules/decks, fixed Teacher/opponent identities, explicit
implementation/acceptance jobs, and the later separately authorized
meaningful job list.

Outputs: per-job gameplay results, outcome/failure/quarantine counters,
replay/admission evidence, gameplay summary, and the BC-induced population
identity.

Invariants affected: normal public policy path, exact response freshness,
complete ordered domains, no fallback, continuation immobility, deterministic
seeds, fixed roles/start partitions, trusted outcome accounting, replay
compatibility, and public-only diagnostics.

Gates this slice may close after its own implementation evidence:
P6-G08, P6-G09, P6-G11, P6-G12, P6-G14, and P6-G16. P6-G14 remains
BLOCKED until a meaningful trained baseline is separately accepted; the
smoke checkpoint can exercise wiring only. P6-G17 is regression-only.

Negative tests: timeout, crash, missing output, wrong checkpoint/input/domain,
wrong score count, non-finite score, stale/duplicate/late response, invalid
key, candidate truncation, Teacher/RandomLegal fallback, failed continuation,
rules/deck mismatch, replay divergence, admission failure, and fallback-
assisted win counting.

Privacy impact: the evaluator may use CoreHost internally only behind the
accepted environment boundary; no private value may cross into the model or
published evidence. Determinism impact: fixed job identity, seed, policy
bindings, source order, and replay. Replay implication: every trusted
outcome passes the normal recorder, semantic replay, and admission path.
Non-goals: no statistical claim from the small corpus, no backend winner, no
Ignis path, no RL, and no self-play.

Prerequisites: T5A, accepted Task-4B checkpoint/recovery input, and the
existing public environment/trajectory/admission contracts. STOP after the
authorized corpus's evidence; do not expand the matchup or seed population.

### T5D — First divergence, distribution shift, and derived report

Owning layer: the public-audit evidence and deterministic presentation layer
above T5B and T5C machine evidence.

Inputs: validated evaluation manifest and summary, offline and gameplay
read-models, both population identities, public shared-job frames, and T5A
identity/score codecs.

Outputs: first-divergence records, separate Teacher-state/BC-induced
distribution comparison, top-level summary references, and report.md.

Invariants affected: earliest-divergence selection, explicit pre-divergence
failure, public-safe fields, population separation, stable denominators,
derived-only report values, and deterministic presentation order.

Gates this slice may close after its own implementation evidence: P6-G15.
It cannot promote offline agreement to online parity or produce a strength
claim. P6-G18 remains regression-only.

Negative tests: later divergence substituted for first divergence, hidden
state in a record, raw response bytes, private locators, fabricated
pre-divergence action, merged populations, missing denominators, hand-entered
metrics, report-only status changes, nondeterministic map order, and changed
report text for identical machine evidence.

Privacy impact: strongest publication boundary; any unexplained privacy
violation fails closed and prevents report publication. Determinism impact:
content-addressed records, fixed section/slice order, and one typed read model.
Replay implication: divergence must reproduce the same public record or the
same explicit failure before any report is derived. Non-goals: no gameplay
execution, training, backend comparison, or external integration.

Prerequisites: T5B and T5C, plus T5A. STOP after report derivation and
first-divergence evidence; do not begin Task 6 or Task 7.

The dependency graph is therefore acyclic and ordered:

    T5A
      ├─→ T5B
      └─→ T5C
    T5B ─→ T5D
    T5C ─→ T5D

## 16. Gate ownership and status

This contract task does not execute a future evaluation gate. Every P6-G00
through P6-G18 status is NOT_RUN after this freeze. A future implementation
must run its applicable evidence at the exact source head; no status below is
promoted by documentation, smoke wiring, or a historical record.

| Gate | Accepted prerequisite or authority | Future owner | Closure allowed by | Status after this freeze |
| --- | --- | --- | --- | --- |
| P6-G00 | accepted public/model boundary | REGRESSION_ONLY | required public/model regression | NOT_RUN |
| P6-G01 | accepted DatasetManifest and admission | T5B | offline membership validation | NOT_RUN |
| P6-G02 | accepted episode-level split contract | T5B | split leakage evidence | NOT_RUN |
| P6-G03 | accepted deterministic split identity | T5B | recomputed split identity | NOT_RUN |
| P6-G04 | accepted Phase-5/Task-3 exact-domain scorer | T5A | codec and N-to-N evidence | NOT_RUN |
| P6-G05 | accepted Phase-5 capacity witnesses | T5A | N=24/N=25/N=129 evidence | NOT_RUN |
| P6-G06 | accepted batch mask semantics | T5B | padding-excluded loss evidence | NOT_RUN |
| P6-G07 | accepted supervision-label derivation | T5B | exact key/ordinal evidence | NOT_RUN |
| P6-G08 | accepted fail-closed inference boundary | T5C | no-fallback gameplay failure evidence | NOT_RUN |
| P6-G09 | accepted request/response freshness binding | T5C | stale/wrong response rejection | NOT_RUN |
| P6-G10 | accepted checkpoint and canonical export contracts | T5A | checkpoint context validation | NOT_RUN |
| P6-G11 | accepted public paired-world boundary | T5C | paired hidden-world inference equality | NOT_RUN |
| P6-G12 | accepted deterministic inference rule | T5C | repeated frozen-job inference | NOT_RUN |
| P6-G13 | accepted offline evaluation plan | T5B | exact offline metrics and slices | NOT_RUN |
| P6-G14 | accepted normal gameplay path | T5C | BC-induced gameplay evidence; meaningful baseline required | NOT_RUN |
| P6-G15 | accepted public first-divergence plan | T5D | reproducible first divergence or failure | NOT_RUN |
| P6-G16 | accepted trajectory/replay/admission path | T5C | accepted replay/admission result | NOT_RUN |
| P6-G17 | fixed rules/decks and Teacher identities | REGRESSION_ONLY | fixed-scope regression | NOT_RUN |
| P6-G18 | accepted Phase-5 regression | REGRESSION_ONLY | Phase-5 regression | NOT_RUN |

P6-G14 is explicitly NOT_RUN/BLOCKED when only the Task-4B smoke checkpoint
exists. P6-G13, P6-G15, and the other future gates also remain NOT_RUN until
their own implementation evidence is actually executed. No future gate is
PASS in this contract or its machine plan.

## 17. Acceptance interpretation and non-goals

Task-5 evidence can establish only the defined evaluation mechanics and the
fixed curriculum scope. Offline Teacher agreement is behavior imitation
evidence, not online parity, strategic quality, or Teacher correctness.
Gameplay wins are meaningful only for a separately accepted meaningful corpus;
the eight-job implementation/acceptance corpus is not a strength baseline.

This contract authorizes no:

    Task-5 evaluator implementation
    gameplay evaluation run
    neural training or optimizer step
    new checkpoint or checkpoint mutation
    PyTorch/JAX backend bake-off or backend winner
    RL, self-play, search, or league training
    Project Ignis/EDOPro integration
    arbitrary-deck or multi-deck expansion
    hidden-state inference, private diagnostics, or positive-lethal expansion
    change to Phase-5, Teacher, rules, deck, trajectory, replay, or admission semantics

The next implementation slice requires separate authorization and independent
review. This task stops after its documentation checks, one commit, push, and
pull request.

## 18. Related authorities

- [Phase-6 evaluation plan](P6_EVALUATION_PLAN.md)
- [Phase-6 implementation plan](P6_IMPLEMENTATION_PLAN.md)
- [Phase-6 BC contract](P6_BC_CONTRACT.md)
- [Phase-6 dataset and split contract](P6_DATASET_AND_SPLIT_CONTRACT.md)
- [Phase-6 checkpoint and inference contract](P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md)
- [Phase-6 Task-4A numeric/provenance contract](P6_TASK4A_NUMERIC_AND_PROVENANCE_CONTRACT.md)
- [Phase-6 Task-4B acceptance-recovery contract](P6_TASK4B_ACCEPTANCE_RECOVERY_V1.md)
- [Phase-5 model contract](../p5/P5_MODEL_CONTRACT.md)
- [ADR-0007](../adr/ADR-0007-phase6-behavior-cloning-boundary.md)
- [Trusted trajectory v1](../contracts/trusted-trajectory-v1.md)
- [Admission receipt v1](../contracts/admission-receipt-v1.md)
- [Dataset manifest v1](../contracts/dataset-manifest-v1.md)
