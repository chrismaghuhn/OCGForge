# OCGForge Phase 6 Task 1 — Evaluation Plan

## Status and scope

**Status:** CURRENT / AUTHORIZED — documentation-only evaluation freeze.

This plan freezes offline imitation, frozen gameplay, first-divergence,
distribution-shift, privacy, and model-input inspection requirements for the
first BC milestone. The gates in this document are future gates and are all
`NOT_RUN` in this task. No neural network, training run, checkpoint, gameplay
evaluation, or generated acceptance evidence is created here.

The initial evaluation scope remains the fixed
`ocgforge.matchup.swordsoul_salamangreat.v1` matchup, the accepted rules
bundle, and the two accepted Teacher v1 source identities in
[the BC contract](P6_BC_CONTRACT.md#4-initial-fixed-curriculum-and-eligible-behavior-sources).

## 1. Evaluation authority and common identity

Evaluation is downstream of the normal public environment path:

```text
PolicySelection
    → EpisodicEnvironment V2
    → trajectory recorder
    → replay/admission
```

There is no neural shortcut around legality, public observation, complete
candidate construction, continuation handling, trajectory recording, or
admission.

The future evaluator MUST bind each result to immutable identities:

```text
evaluation contract identity
evaluation corpus/job identity
rules bundle and fixed deck identities
policy/checkpoint identities
dataset/split identity where applicable
evaluator source commit
seed/job manifest
```

An evaluation job identity MUST be content-addressed. Names such as `latest`,
`best`, `run-17`, an output directory, or a mutable branch are locators only.
Changing the corpus, seed list, policy assignment, checkpoint, rules/deck
identity, evaluator code, or metric contract creates a new evaluation identity.
Framework/backend, device, worker count, and distributed execution may be
recorded as evaluator provenance, but MUST NOT define evaluation-job identity
or gameplay semantics.

The initial fixed matchup values are:

```text
matchup_id       = ocgforge.matchup.swordsoul_salamangreat.v1
rules_bundle_id  = 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
swordsoul_deck   = ocgforge.swordsoul_tenyi.ml_v1
salamangreat_deck = ocgforge.salamangreat.ml_v1
```

The evaluator must retain the existing positive-lethal limitation:

```text
POSITIVE_LETHAL_CAPABILITY = BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT
```

No optimistic lethal label, hidden response, or battle-sidecar inference may
enter a Phase-6 model input or evaluation target.

## 2. Offline imitation evaluation

Offline evaluation uses only admitted trajectory-derived
`ModelSupervisionSampleV1` values and the exact current candidate domain.
Validation and test partitions are episode-disjoint under
`TrainingDatasetSplitV1`; individual decision rows are never repartitioned.

### 2.1 Required metrics

For each evaluated sample and aggregate slice, report:

- exact-domain cross-entropy / BC loss over only the `N` real candidates;
- Teacher top-1 agreement;
- top-`K` agreement when a declared `K` is useful and `K <= N` for the sample;
- sample count, valid/scored count, and every rejected or unscored count;
- Teacher-selected candidate presence and exact selected public key/ordinal
  consistency.

An aggregate top-1 number MUST NOT be the sole skill claim. Offline metrics
measure agreement with the selected Teacher behavior; they do not prove
optimal gameplay, strategic quality, or parity on states the Teacher did not
visit.

### 2.2 Required slices

At minimum, every report partitions results by:

```text
decision/request family
candidate-domain size (including exact N=24, N=25, and N=129 witnesses)
phase and turn/decision context
acting participant and deck role
starting-player partition
continuation vs. non-continuation decision
rare or critical decision slices when they are identifiable
```

The report MUST preserve enough counts to distinguish “Teacher never selected
this candidate” from “candidate absent from the domain.” It MUST NOT invent a
negative strategic label for an unselected candidate, call an unselected
candidate illegal, or treat Teacher behavior as ground-truth optimality.

Offline evaluation must also report domain-capacity failures, padding-mask
violations, privacy/input rejection, and label mismatches separately from
loss. A failed sample is not silently removed from the denominator without a
reason and count.

## 3. Frozen gameplay evaluation

Frozen gameplay evaluation runs an immutable checkpoint through the normal
public policy boundary. The evaluator MUST use fixed evaluation seeds/jobs and
fixed opponent policy identities. The initial corpus covers the accepted fixed
rules/deck slice and both deck roles, with the existing starting-player and
mirrored-seat partitions retained.

For each job:

```text
fixed environment/configuration and seed
    → current public observation + complete public domain
    → BC inference request/response validation
    → one public_action_key through PolicySelection
    → EpisodicEnvironment V2 step
    → trajectory recording and replay/admission
```

No special neural action path may bypass `PolicySelection`, environment
validation, continuation semantics, trajectory recording, or admission.

The report MUST include:

```text
total jobs
completed terminal jobs
wins, losses, draws, interrupted jobs, failed jobs
neural-policy response failures and quarantined jobs
fallback-assisted job count (must be zero for trusted BC evaluation)
replay/admission result counts
fixed policy/checkpoint/opponent/rules/deck identities
```

A model timeout, crash, transport failure, stale response, wrong checkpoint,
wrong model input, wrong candidate domain, invalid score, or fallback-assisted
job is not a neural-policy win. It is reported as failure/quarantine evidence;
the evaluator MUST NOT retry under another policy or silently convert it to a
Teacher/RandomLegal result.

Win rate is reported with its numerator, denominator, and a two-sided 95%
Wilson interval under the versioned metric identity
`ocgforge.phase6.gameplay_metrics.wilson_95.v1`. Failed/interrupted jobs are
reported separately and are not silently relabeled as losses or removed from
the reliability report. Training-run win rate is never frozen-evaluation
evidence.

## 4. First-divergence evaluation

For deterministic shared initial jobs, the evaluator runs Teacher and BC from
the same accepted fixed environment/job identity and compares their semantic
public decision sequence until the first divergence.

At the first divergence, it records only public-safe evidence:

```text
evaluation job identity
public observation/model-input identity
ordered complete public candidate keys
candidate count and public action kinds
model candidate scores in source order
Teacher selected public_action_key
BC selected public_action_key
decision/request family
continuation vs. non-continuation context
```

The record MUST NOT include hidden engine state, private hand/deck identity,
face-down identity, internal semantic keys, raw response bytes, pointers,
submission tokens, or omniscient debug values. Candidate scores are allowed
only as scores over the already-public supplied candidate rows.

The divergence record is content-addressed under
`ocgforge.phase6.first_divergence.v1`. The same frozen job list, checkpoint,
Teacher source identities, and evaluator implementation MUST reproduce the
same first divergence or the same explicit policy failure. A model failure
before a divergence is a failure record, not a fabricated divergence.

## 5. Distribution-shift evaluation

Teacher-state validation is necessary but insufficient. Phase 6 MUST compare
Teacher-state validation with states induced by the BC policy's own earlier
decisions.

The BC-induced evaluation reports at minimum:

- public state/decision-family/domain-size distribution;
- complete-domain and exact-N capacity compliance;
- inference failure, quarantine, and replay/admission rates;
- Teacher agreement when the Teacher can be evaluated on the BC-induced public
  frame as a diagnostic comparison;
- outcome and terminal/interrupt/failure metrics;
- degradation from Teacher-state validation across the same required slices.

These are separate populations and MUST retain separate identities and
counts. High offline Teacher agreement MUST NOT be reported as online parity,
strategic optimality, or proof that the BC policy is safe outside the fixed
curriculum.

## 6. Model-input inspection gate

Before larger training, a deterministic inspection tool is required under
`ocgforge.phase6.model_input_inspection.v1`. It consumes only accepted public
observation/candidate values and the Phase-5 model representations. It MUST
not query `CoreHost`, raw engine state, or private `PlayerObservation` state.

The inspection output must make these fields visible and auditable:

```text
self vs. opponent perspective
phase, turn, and decision context
zones and visibility status
visible vs. redacted card identities
public source/target references
candidate action kinds
candidate-specific optional fields and presence masks
exact candidate order and candidate count
selected training label and public_action_key
LogicalModelInputV1 / EncodedModelInputV1 identities
```

The tool is a diagnostic projection, not a second observation or legality
authority. It must preserve the same privacy and ordering rules as the
training path.

## 7. Privacy and deterministic inference gates

The paired-hidden-world test constructs two worlds with different private
hidden information but identical accepted public observation and complete
public candidate domain. It requires:

```text
same LogicalModelInputV1
same EncodedModelInputV1
same model_input_identity
same deterministic checkpoint/configuration
same candidate score outputs under the declared numeric contract
same selected public_action_key
```

No hidden passcode, private semantic key, hidden-derived hash, or omniscient
debug value may appear in the model input, diagnostic, or checkpoint input.
Deterministic inference must not depend on unordered iteration, process
scheduling, device-specific incidental ordering, or mutable aliases.

The exact candidate-capacity tests retain `N=24`, `N=25`, and `N=129`. A
physical width smaller than a real domain fails closed; no truncation, top-K
selection, domain split, candidate reconstruction, or candidate-zero fallback
is accepted.

## 8. Frozen future acceptance matrix

The following are future Phase-6 gate categories. They are intentionally
`NOT_RUN` in Task 1; this table is a contract freeze, not acceptance evidence.

| Gate | Future requirement | Task-1 status |
| --- | --- | --- |
| P6-G00 | public/model boundary unchanged | NOT_RUN |
| P6-G01 | admitted dataset membership only | NOT_RUN |
| P6-G02 | episode-level train/validation/test separation | NOT_RUN |
| P6-G03 | deterministic split identity | NOT_RUN |
| P6-G04 | exact N→N candidate scoring | NOT_RUN |
| P6-G05 | N=24/25/129 capacity preservation | NOT_RUN |
| P6-G06 | padding excluded from semantic loss | NOT_RUN |
| P6-G07 | Teacher label exactness | NOT_RUN |
| P6-G08 | no silent policy fallback | NOT_RUN |
| P6-G09 | stale/wrong inference response rejection | NOT_RUN |
| P6-G10 | checkpoint content/provenance validation | NOT_RUN |
| P6-G11 | paired-hidden-world model equality | NOT_RUN |
| P6-G12 | deterministic frozen inference | NOT_RUN |
| P6-G13 | offline evaluation slices | NOT_RUN |
| P6-G14 | BC-induced online evaluation | NOT_RUN |
| P6-G15 | first-divergence reproducibility | NOT_RUN |
| P6-G16 | trusted trajectory/replay compatibility | NOT_RUN |
| P6-G17 | fixed rules/deck identities unchanged | NOT_RUN |
| P6-G18 | Phase-5 regression | NOT_RUN |

No row may be promoted to `PASS` from documentation review, a focused smoke,
an aggregate accuracy, a native fallback command, or an unexecuted future
gate. Future evidence must bind every result to the exact relevant source
head, contracts, identities, commands, and worktree state.

## 9. Explicit non-goals

This plan does not authorize:

```text
training or GPU usage
checkpoint generation
PyTorch/JAX selection or ML dependency adoption
RL, self-play, search, MCTS, or league training
multi-deck or arbitrary-deck evaluation
Project Ignis/EDOPro deployment
human demonstration admission
battle/lethal contract expansion
```

## 10. Related authority

- [BC contract](P6_BC_CONTRACT.md)
- [Dataset and split contract](P6_DATASET_AND_SPLIT_CONTRACT.md)
- [Checkpoint and inference contract](P6_CHECKPOINT_AND_INFERENCE_CONTRACT.md)
- [Phase-5 model contract](../p5/P5_MODEL_CONTRACT.md)
- [Phase-4C acceptance and lethal limitation](../p4c/P4C_ACCEPTANCE.md)
- [Phase-6 implementation plan](P6_IMPLEMENTATION_PLAN.md)
