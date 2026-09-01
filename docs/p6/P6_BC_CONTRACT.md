# OCGForge Phase 6 Task 1 — Behavior Cloning Contract Freeze

## Status and scope

**Status:** CURRENT / AUTHORIZED — documentation-only Phase-6 Task-1 contract
freeze.

This document freezes the first OCGForge Behavior Cloning (BC) boundary. It
defines what a trusted BC sample, candidate-scoring learner, training label,
and neural-policy failure mean. It does not implement a learner, select a
framework, start training, generate a checkpoint, or report a Phase-6
acceptance gate as passed.

The normative terms **MUST**, **MUST NOT**, **SHOULD**, and **FAIL CLOSED** have
their usual contract meaning. A later implementation may use a different
execution backend only when it preserves these semantics and the accepted
Phase-5 contracts.

## 1. Authority and entry boundary

Phase 6 is downstream of the accepted public, trajectory, and model-facing
layers:

```text
pinned rules bundle / ocgcore
        ↓
EpisodicEnvironment V2
        ↓
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
        ↓
LogicalModelInputV1
        ↓
EncodedModelInputV1
        ↓
ModelBatchLayoutV1
        ↓
BC candidate scorer
```

Training labels have a separate trusted path:

```text
admitted trusted trajectory
+ VerifiedAdmissionReceipt
        ↓
ModelSupervisionSampleV1
        ↓
one chosen candidate ordinal in the exact current domain
```

The following remain authoritative for their own meanings:

| Question | Authority |
| --- | --- |
| Game legality and engine state | the exact pinned rules bundle and ordered repository patchset |
| Player-visible state and legal public candidate domain | accepted EpisodicEnvironment V2 and its public projection |
| Trusted collection membership and producer attribution | accepted Phase-3B trajectory, admission, receipt, and DatasetManifest contracts |
| Logical/encoded model input and immutable card vocabulary | accepted Phase-5 `ygo::model` contracts |
| BC objective, learner boundary, and neural failure semantics | this Phase-6 contract |
| Physical tensor execution, devices, workers, and serialization libraries | a later implementation, never the semantic authority |

The accepted Phase-5 reference is immutable for this task:

```text
H_EXEC     = 3c99e86c487361fc4e0f5f12678b4867e59232b7
H_EVIDENCE = da3376fc2ab645377f9de2dd9fd6195c1aa8c081
```

This task does not reinterpret `LogicalModelInputV1`,
`EncodedModelInputV1`, `ModelBatchLayoutV1`, `ModelSupervisionSampleV1`,
`public_action_key`, or candidate ordinal semantics.

## 2. Phase-6 contract identifiers

The following identifiers name the frozen semantic surfaces. They are not
framework package names and do not imply that an implementation exists.

| Surface | Contract identity | Meaning |
| --- | --- | --- |
| BC boundary | `ocgforge.phase6.bc_contract.v1` | downstream candidate-scoring learner semantics |
| candidate scorer | `ocgforge.phase6.bc_candidate_scorer.v1` | exactly one score per supplied public candidate |
| initial objective | `ocgforge.phase6.bc_objective.v1` | one Teacher-selected candidate among the exact current domain |
| deterministic tie rule | `ocgforge.phase6.bc.inference_tiebreak.v1` | stable resolution of equal finite scores |
| model-input inspection | `ocgforge.phase6.model_input_inspection.v1` | public-only inspection/debug projection |

An incompatible change to candidate cardinality, candidate ordering, label
meaning, public-input ownership, fallback behavior, or the identity inputs of
these surfaces requires a new versioned contract.

## 3. Ownership boundary

### 3.1 Environment ownership

The Environment remains the sole owner of:

- game legality;
- complete candidate-domain construction and order;
- public action-key validation and resolution;
- continuation lifecycle and current continuation domain;
- response construction and engine advancement;
- public observation and privacy projection.

The BC learner receives the current public observation and the complete
ordered candidate vector. It MUST NOT ask a second legality oracle, rebuild a
candidate domain, or submit an engine response.

### 3.2 Learner ownership

The BC learner owns only:

- encoding the already-accepted Phase-5 model representation for execution;
- scoring each supplied candidate feature row;
- selecting one of the supplied candidates under the frozen deterministic
  inference rule;
- returning the existing `public_action_key` through the control-plane
  adapter.

It does not own legality, visibility, trajectory admission, replay, or engine
advancement. `public_action_key` remains selection/routing identity, not a
learned string feature or a global action class.

### 3.3 Framework neutrality

OCGForge owns semantic contracts. A framework owns physical execution.

The semantic contract MUST remain valid for PyTorch, JAX, or another future
implementation. The following are non-authoritative execution details:

```text
torch.Tensor, jax.Array
CUDA/XLA device
world_size, DDP, FSDP, pjit
mixed-precision implementation
worker PID and process topology
physical checkpoint directory or cache path
wall time and allocation order
```

No such detail may define dataset identity, sample identity, checkpoint
identity, candidate identity, public action identity, evaluation-job identity,
legality, or public gameplay semantics.

## 4. Initial fixed curriculum and eligible behavior sources

The first BC curriculum is exactly the already certified fixed matchup. This
is a data/training scope boundary, not a claim of general Yu-Gi-Oh! support.

| Field | Frozen value |
| --- | --- |
| matchup | `ocgforge.matchup.swordsoul_salamangreat.v1` |
| rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |
| format | `TCG_ADVANCED_2026_05_18` |
| duel mode | `DUEL_MODE_MR5` |
| duel flags | `190464` |
| first locked deck | `ocgforge.swordsoul_tenyi.ml_v1` / SHA-256 `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` |
| second locked deck | `ocgforge.salamangreat.ml_v1` / SHA-256 `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` |

Only the following accepted Teacher v1 identities are eligible as positive
behavior-policy sources for the initial BC corpus:

| Acting deck role | Strategy profile | Teacher binding | PolicyArtifact |
| --- | --- | --- | --- |
| Swordsoul Tenyi | `ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74` | `ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c` | `policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d` |
| Salamangreat | `ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55` | `ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56` | `policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527` |

Both rows bind to the accepted Teacher producer
`ocgforge.policy.teacher_core.v1`, the deterministic selection contract
`ocgforge.policy.deterministic_lexicographic_argmax.v1`, and the accepted
Teacher v1 provenance rules. The fixed Teacher uses
`ocgforge.no_policy_rng.v1`; its artifact/profile/binding identities are
immutable content identities, not aliases.

The materializer MUST verify that the acting participant's
`PolicyArtifact`, assignment, profile, binding, rules bundle, and locked deck
role resolve to this table. A different Teacher profile, a changed Teacher
artifact, an unbound source, or a RandomLegal source is outside the initial
positive-label corpus. RandomLegal trajectories MUST NOT be relabeled as
Teacher demonstrations.

The opponent policy identity is retained as collection provenance when it is
relevant to a trajectory, but it does not change the selected label. The
source policy selected a candidate; it did not prove that candidate to be
strategically optimal.

## 5. Variable-domain candidate scorer

The minimum learner boundary is deliberately generic:

```text
EncodedModelInputV1
        ↓
state_encoder
        ↓ shared state context
candidate feature row[0..N-1]
        ↓
candidate_encoder + candidate_scoring_function
        ↓
finite score[0..N-1]
        ↓
one supplied public_action_key
```

The conceptual interface is:

```text
state_encoder(public_state) -> state_representation
candidate_encoder(state_representation, candidate_row[i]) -> candidate_representation[i]
candidate_scoring_function(state_representation, candidate_representation[i]) -> score[i]
```

The exact neural architecture is intentionally not frozen. The scorer MUST be
capable of evaluating every supplied candidate independently of domain
cardinality and MAY use shared state context. The semantic result for a
current domain of size `N` is always exactly `N` scores.

The learner MUST NOT introduce independent action authorities such as normal
summon, target, chain, or option heads. If a continuation produces a new
decision, the Environment owns that continuation and the learner scores the
complete current candidate domain at that step.

For every valid current public domain:

```text
N supplied candidates
→ N scores
→ exactly one supplied candidate
→ its existing public_action_key
```

Candidate rows and routing keys remain paired in their original source order.
The learner MUST NOT:

```text
use a fixed global action vocabulary as legality owner
truncate or top-K the domain
reconstruct, fabricate, deduplicate, sort, or drop candidates
insert candidate zero or first-legal behavior
split one semantic domain across independent decisions
turn a candidate ordinal into public action identity
```

An empty, malformed, duplicate-key, or incomplete domain is an Environment or
Phase-5 failure, not a reason for the learner to make a guess.

## 6. Initial BC objective and label meaning

The initial objective is framework-neutral exact-domain classification:

```text
one chosen candidate ordinal among the exact supplied N candidates
```

For a real domain with finite scores `s[0..N-1]` and Teacher label `y`, the
semantic objective is conceptually:

```text
loss = -log(exp(s[y]) / sum(exp(s[i]) for i in 0..N-1))
```

This defines the candidate set over which the loss is evaluated. It does not
freeze an optimizer, learning-rate schedule, network shape, numeric dtype, or
batch implementation.

The materialized physical batch MUST carry an explicit real-candidate mask:

```text
real candidate mask = 1 for the exact N supplied candidates
padding row mask    = 0 for every physical padding row
```

Only the `N` real candidates contribute to the semantic loss. Padding is not an
extra class, and a physical width smaller than `N` is a structured failure.
The existing Phase-5 capacity witnesses `N=24`, `N=25`, and `N=129` remain
mandatory future acceptance inputs.

The Teacher-selected candidate is found by exact equality of its existing
`public_action_key` in the accepted ordered domain, then represented as the
zero-based local candidate ordinal. The ordinal is training-label metadata
only. It is not replay identity, public action identity, candidate identity,
or a fixed global class.

An unselected candidate means only that this Teacher did not select it in that
public frame. It does not mean that the candidate is illegal, objectively bad,
or a negative strategic example. Dataset and evaluation reporting MUST
distinguish “Teacher selected candidate” from “Teacher never selected
candidate”; this is a provenance/reporting distinction and MUST NOT create one
unrequested negative strategic label for every unselected candidate. Teacher
unsupported or weak regions remain an evaluation limitation.

## 7. Privacy, determinism, and failure boundary

The learner may consume only accepted `PublicEnvironmentObservation`, the
complete ordered public candidate vector, and the accepted Phase-5 logical and
encoded representations. It MUST NOT consume `CoreHost`, raw engine queries,
`PlayerObservation`, internal semantic keys, response bytes, submission
tokens, hidden locators, hidden passcodes, inferred archetypes, beliefs, or
private deck/hand identity.

For paired worlds with different private hidden information but the same
accepted public observation and complete public candidate domain, the learned
path MUST receive:

```text
same LogicalModelInputV1
same EncodedModelInputV1
same model_input_identity
```

For one frozen deterministic checkpoint and configuration, it MUST then
produce the same candidate scores under the declared numerical comparison
contract and the same selected `public_action_key`. Hidden-world values MUST
NOT appear in model inputs, normal diagnostics, or checkpoint manifests.

The following conditions fail closed and MUST NOT invoke another gameplay
policy:

```text
model timeout or process crash
transport failure
missing output, NaN, Inf, or wrong score length
stale, duplicated, late, wrong-checkpoint, or wrong-input response
wrong ordered candidate-domain identity
invalid selected ordinal or key
insufficient physical candidate capacity
privacy or public-input validation failure
```

There is no Teacher, RandomLegal, heuristic, candidate-zero, first-candidate,
retry-with-another-policy, or other silent policy fallback. A fallback-assisted
game is never a neural-policy win. The future runner records the structured
failure/quarantine state without fabricating a winner, reward, action, or
trusted trajectory admission.

## 8. External tooling and backend decision boundary

The following roles are future adoption points only:

| Tool or family | Possible later role | Authority it does not receive |
| --- | --- | --- |
| Accelerate | learner execution abstraction | no training or gameplay semantics |
| Safetensors | canonical physical neural-weight format | no checkpoint identity authority |
| Hugging Face Datasets | derived data loader/cache | no DatasetManifest membership authority |
| Trackio | optional metrics/dashboard presentation | no evaluation acceptance or gameplay authority |
| `huggingface_hub` | optional artifact transport/publication | no immutable checkpoint authority beyond pinned content |

Task 1 adds none of these dependencies.

Task 1 does not select PyTorch, JAX, or another backend. A later **Backend
Bake-Off** MUST run only after a real BC workload exists and MUST compare
PyTorch and JAX implementations under the same DatasetManifest, split,
Phase-5 inputs, architecture specification, objective, training budget,
evaluation corpus, and checkpoint export semantics. The primary backend
decision belongs before Phase 7 and after evidence, not in this contract
freeze.

## 9. Explicit future triggers and non-goals

Project Ignis / EDOPro integration is deferred. It may begin only after:

```text
first useful trained checkpoint
+ canonical checkpoint identity
+ frozen inference contract
+ successful frozen evaluation
```

That future adapter must remain a thin deployment boundary. Human games from
it begin as evaluation/debug evidence, not automatically trusted training
data; importing human demonstrations requires a separate admission contract.

The accepted positive-lethal limitation is unchanged:

```text
POSITIVE_LETHAL_CAPABILITY = BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT
```

This task does not authorize neural networks, optimizer code, training, GPU
usage, checkpoints, Project Ignis/EDOPro, RL, self-play, search, broader decks,
human replay ingestion, or any battle/lethal contract expansion.

## 10. Related authority

- [Phase-5 model contract](../p5/P5_MODEL_CONTRACT.md)
- [Phase-5 final acceptance evidence](../p5/P5_ACCEPTANCE_EVIDENCE.md)
- [Phase-4B Teacher contract](../p4b/P4B_TEACHER_CONTRACT.md)
- [Phase-4B acceptance identities](../p4b/P4B_ACCEPTANCE.md)
- [Dataset manifest v1](../contracts/dataset-manifest-v1.md)
- [Policy provenance v1](../contracts/policy-provenance-v1.md)
- [ADR-0007](../adr/ADR-0007-phase6-behavior-cloning-boundary.md)
