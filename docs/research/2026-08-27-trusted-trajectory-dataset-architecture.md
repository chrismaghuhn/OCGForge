# Trusted Trajectory, Dataset, and Training-Data Architecture

**Status:** Research recommendation / Phase-3 design basis  
**Date:** 2026-08-27  
**Roadmap:** https://github.com/chrismaghuhn/OCGForge/issues/16  
**Implementation authorization:** None. Phase 3 begins only after `EpisodicEnvironment V2 FINAL PASS`.

## 1. Executive recommendation

After `EpisodicEnvironment V2 FINAL PASS`, OCGForge should implement a custom,
framework-neutral trusted-trajectory contract before beginning model training.

The intended trust pipeline is:

```text
EpisodicEnvironment V2
        ↓
CandidateEpisode
  EpisodeManifest
  DecisionRecord[]
  EpisodeClosure
        ↓
sealed immutable candidate shard
        ↓
structural + schema + privacy validation
        ↓
semantic public replay verification
        ↓
immutable AdmissionReceipt
        ↓
DatasetManifest
        ↓
derived learner views
  tensors / rewards / chunks / masks / priorities
```

The core recommendation is:

> OCGForge's canonical dataset should be a replay-verifiable record of complete
> public semantic decisions and exact policy provenance. Framework episodes,
> tensors, rewards, recurrent chunks, and replay buffers are derived views of
> that record, not its source of truth.

This keeps the project priorities intact:

```text
correctness
→ determinism
→ information safety
→ complete legal decisions
→ replay / auditability
→ maintainability
→ performance
→ ML scale
```

## 2. Hard architectural decisions

The research supports the following direction.

1. Canonical data is an **ordered episode of accepted public decisions**, not
   an algorithm-specific `(s, a, r, s')` table.
2. Every stored decision binds the complete V2 public frame, including the
   complete ordered public legal candidate domain and selected
   `public_action_key`.
3. Candidate index is never semantic action identity.
4. Canonical learner-visible trajectory data must never contain internal
   `semantic_key`, raw response bytes, raw message hashes, internal decision
   or continuation IDs, internal candidate digests, pointers, caches,
   restricted diagnostics, or hidden engine state.
5. `submission_token` is a live freshness/control value and is not persisted
   as trajectory semantics.
6. Complete episodes are canonical. Recurrent chunks, burn-in, TBPTT windows,
   padding, hidden-state caches, returns, advantages, priorities, and replay
   indexes are derived.
7. Objective game outcome is environment truth. Numeric reward semantics are
   external, versioned, and derived.
8. Canonical trajectory bytes should be owned by OCGForge, not by RLDS,
   Minari, RLlib, Arrow, Parquet, Reverb, TorchRL, or another framework.
9. Actor output is not training-eligible merely because it is well formed or
   checksummed. Training eligibility requires a separate immutable replay
   admission receipt.
10. Local Windows, Linux, Kaggle, Colab, and cloud systems are execution
    backends. Provider identity must not enter gameplay, episode, trajectory,
    shard, or dataset semantic identity.

## 3. Framework comparison and role

External systems are useful references and adapters, but should not own the
canonical OCGForge contract.

| System | Useful ideas | OCGForge role |
| --- | --- | --- |
| RLDS | episode nesting, explicit first/last semantics, invalid/incomplete episode handling | export format / conceptual reference |
| Minari | offline episode ergonomics, initial observation plus action/reward/termination arrays | offline-analysis exporter |
| RLlib | stateful episode support, learner transforms, Parquet/Ray Data integration | derived RLlib adapter |
| Reverb | distributed sequence replay, priorities, queues, rate limiting | mutable learner-side replay/cache |
| Arrow / Parquet | efficient ragged and columnar analytics | derived analytics/training cache |
| OCGForge contract | privacy, complete candidate domains, public identity, replay admission, policy provenance | **canonical source of truth** |

The canonical format must remain usable by PyTorch/TorchRL, RLlib, RLDS,
Minari, Reverb, and custom recurrent learners without letting any of those
frameworks redefine OCGForge semantics.

## 4. Owning layer and trust boundary

The proposed owning layer is conceptually:

```text
ygo::trajectory
```

positioned as:

```text
CoreHost / ocgcore
        ↓
Decision Protocol
        ↓
EpisodeDriver
        ↓
EpisodicEnvironment V2
        ↓
trajectory recorder + canonical codecs
        ↓
replay verifier + shard admission
        ↓
dataset manifests
        ↓
model / framework adapters
```

The trajectory layer may consume only immutable V2 public values and accepted
closure values. It must not:

- query `CoreHost`;
- inspect raw engine state;
- obtain internal `ActionCandidate.semantic_key`;
- inspect response bytes;
- reconstruct or repair legal actions;
- bypass the public observation projector;
- alter lifecycle or advance ocgcore;
- sort, filter, truncate, deduplicate, or fabricate candidate domains.

## 5. Logical data model

The recommended logical hierarchy is:

```text
EpisodeEnvelope
├── EpisodeManifest
├── DecisionRecord[0..N)
└── EpisodeClosure

TrajectoryShard
├── CandidateShardManifest
└── EpisodeEnvelope[0..M)

Admission
└── AdmissionReceipt

Dataset
└── DatasetManifest
```

The `AdmissionReceipt` is important: it structurally distinguishes actor output
from independently replay-verified training data. Do not use a mutable
`verified=true` field inside an existing shard.

## 6. Phase 3A versus Phase 3B normative split

Do not ratify every future data/storage identity in one oversized prerequisite
PR.

### Phase 3A — Core trajectory semantics

Ratify the semantic model required to record one trusted public episode:

```text
EpisodeManifest
DecisionRecord
EpisodeClosure
PolicyProvenance
public gameplay trajectory identity
temporal convention
canonical-versus-derived boundary
reward boundary
```

Recommended future names, subject to Phase-3A review and ratification:

```text
ocgforge.trusted_trajectory.v1
ocgforge.episode_manifest.v1
ocgforge.decision_record.v1
ocgforge.episode_closure.v1
ocgforge.policy_provenance.v1
ocgforge.public_gameplay_trajectory_identity.v1
ocgforge.trajectory_record_identity.v1
```

These names are **not accepted constants merely because they appear in this
research document**.

### Phase 3B — Persistence and admission semantics

Only after the core episode semantics are accepted, ratify:

```text
CandidateShardManifest
AdmissionReceipt
DatasetManifest
shard / dataset identities
immutable publication model
corruption / deduplication / conflict behavior
```

Recommended future names, also subject to explicit review:

```text
ocgforge.trajectory_shard_manifest.v1
ocgforge.trajectory_shard_identity.v1
ocgforge.trajectory_admission_receipt.v1
ocgforge.dataset_manifest.v1
ocgforge.dataset_identity.v1
```

Reward and generic derived-view identities should remain deferred until their
first concrete consumer requires them.

## 7. Identity hierarchy

OCGForge should not overload one hash with several meanings.

The intended hierarchy is:

```text
environment_semantic_id
        ↓
episode_semantic_id
        ↓
public semantic decisions
        ↓
public_gameplay_trajectory_id
        ↓
trajectory_record_id
        ↓
shard / dataset membership identities
```

The important distinctions are:

```text
same reset + different actions
→ same episode_semantic_id
→ different public_gameplay_trajectory_id

same public gameplay produced by different policies
→ same public_gameplay_trajectory_id
→ different trajectory_record_id

same semantic dataset repacked differently
→ same dataset_semantic_id
→ different physical artifact hashes are permitted
```

Semantic identity and artifact/provenance identity must remain separate.
Compression level, file path, host, PID, provider job ID, and object-store
location are not gameplay semantics.

## 8. Episode manifest ownership

A future `EpisodeManifest` should bind or copy the replay-relevant immutable
inputs and collection provenance at episode scope.

Conceptually:

```text
EpisodeManifest {
    trajectory_contract_id
    environment_contract_id
    environment_config
    environment_semantic_id

    episode_spec
    episode_semantic_id

    public observation/action/decision schema IDs

    semantic_job_id
    rollout_generation_id
    run_control
    collection_profile_id

    policy_provenance_envelope
}
```

`environment_semantic_id` and `episode_semantic_id` are validation fields and
must be recomputable from their owning inputs.

`run_control` is episode collection provenance required to interpret
interruptions, but it remains excluded from episode semantic identity.

## 9. Policy provenance

A single episode-global `behavior_policy_id` is insufficient for alternating
self-play because both participants generate behavior.

Use immutable participant assignments instead:

```text
PolicyProvenanceEnvelope
├── PolicyArtifactManifest[]
└── ParticipantPolicyAssignment[]
```

A policy artifact should be content-addressed and should bind the immutable
components required to identify the actor that generated behavior, such as:

```text
policy artifact payloads / checkpoint
policy kind
model/checkpoint schema where applicable
inference adapter
observation adapter
action adapter
tensorizer where applicable
sampling contract
```

A participant assignment should bind:

```text
player
seat role
deck role
policy role
policy artifact ID
policy RNG contract / root / stream / cursor
optional league generation/member/role
optional matchmaking snapshot
```

Every stored decision should then contain an
`acting_policy_assignment_id`.

This makes it possible to answer exactly:

```text
Which immutable policy artifact produced this accepted action?
Which player/seat/deck role did it control?
Which opponent artifact was active?
Which rollout/league generation produced the data?
Which policy RNG stream/cursor was used?
```

Bit-for-bit neural inference reproducibility is a separate ML provenance
property. Environment replay must not depend on re-running floating-point
policy inference; it replays the recorded public action key.

## 10. Decision record and temporal convention

Let `F_t` be the public actionable frame at environment decision index `t`, and
`a_t` the accepted `public_action_key`.

Canonical semantics are:

```text
F_t --a_t--> F_(t+1)
```

or:

```text
F_t --a_t--> Closure
```

A decision record therefore owns:

```text
DecisionRecord_t =
    (F_t, a_t, accepted_transition_t, successor_reference_t)
```

It should not duplicate `observation_(t+1)` inside record `t`; the next public
frame owns that observation.

Conceptually:

```text
DecisionRecord {
    episode_semantic_id
    decision_index
    acting_player
    public_semantic_decision_id

    public frame {
        PublicEnvironmentObservation
        public_observation_digest
        EnvironmentDecisionRequest
        complete ordered EnvironmentActionCandidate[]
        public_candidate_domain_digest
    }

    selected_public_action_key

    accepted transition {
        core_response_submitted
        next boundary
    }

    policy decision provenance
}
```

Required coupling includes:

```text
decision_index
== PublicEnvironmentObservation.decision_index

acting_player
== PublicEnvironmentObservation.perspective_player
== EnvironmentDecisionRequest.player
```

The selected `public_action_key` must occur exactly once in the complete
current public candidate domain.

## 11. Continuation and rejection semantics

Continuation decisions are real decision records.

Expected classification:

```text
intermediate continuation
→ accepted DecisionRecord
→ core_response_submitted = false
→ successor is another actionable decision

final continuation choice
→ accepted DecisionRecord
→ core_response_submitted = true
→ exactly one final internal response
```

Rejected submissions create no accepted transition record, do not increment the
public decision index, and must produce zero authoritative mutation.

Under the default trusted collection profile, actor-generated rejected
submissions should make the candidate episode ineligible for policy training
rather than silently discarding failed behavior until a valid choice appears.

## 12. Episode closure

Closure is exactly one of:

```text
TERMINAL
INTERRUPTED
FAILED
```

Only a true terminal carries objective outcome (`winner`, `win_reason`).
Interruption has no implicit loss/draw/reward meaning. Failed episodes are
quarantined and never admitted for learner training.

Terminal public observations are closure values, not fake final decisions with
fake actions.

A reset that reaches terminal before any action is a valid zero-decision
terminal episode. A reset failure before a valid episode identity exists should
be represented separately as generation failure evidence rather than malformed
trajectory data.

## 13. Canonical versus derived data boundary

### Canonical public gameplay

```text
EnvironmentConfig V2
EpisodeSpec
environment_semantic_id
episode_semantic_id

PublicEnvironmentObservation
complete ordered EnvironmentActionCandidate domain
public_observation_digest
public_candidate_domain_digest
public_semantic_decision_id
selected public_action_key
accepted transition classification
terminal / interrupted / failed closure
winner / win_reason only at terminal
```

### Canonical collection provenance

```text
semantic_job_id
rollout_generation_id
policy artifact identities
participant assignments
seat/deck/policy roles
policy RNG identity/cursor
optional raw behavior annotations under explicit collection profiles
```

These are immutable facts about collection, but are not authoritative game
semantics.

### Restricted admission evidence

```text
verifier build/provenance
internal trace comparisons
internal response hashes
restricted diagnostics
replay gate results
AdmissionReceipt
```

Restricted admission evidence is not learner-visible feature data.

### Derived / regenerable data

```text
numeric rewards
shaped rewards
tensors
tokenized observations
candidate embeddings
local candidate row/index
padded candidate domains
candidate masks
recurrent chunks
burn-in windows
TBPTT layouts
hidden-state caches
returns / discounted returns
advantages / GAE
V-trace targets
importance weights
replay priorities
replay-buffer indexes
sampling weights
normalization statistics
train/validation split views
Arrow / Parquet / RLDS / Minari / RLlib exports
```

Any derived artifact should bind its base dataset semantic identity, derivation
contract/configuration, implementation/artifact identity where relevant, and
output content digest.

## 14. Recurrent / POMDP rule

Canonical storage contains complete global public episodes.

Learner sequence construction is assignment-local:

```text
AgentDecisionStream(episode, assignment) =
    all DecisionRecords produced by that assignment
    ordered by global decision_index
```

The global decision indexes in one agent stream need not be contiguous.

Even when the same checkpoint controls both players, the two participant
assignments have separate recurrent streams and hidden state.

A model-facing recurrent chunk must never concatenate one player's private
perspective with the opponent's private perspective merely because the frames
are adjacent in the global episode. Doing so would create a hidden-information
channel across otherwise safe public observations.

Sequence length, burn-in, TBPTT, overlap, padding, masks, bootstrap policy, and
hidden-state caches remain derived learner configuration.

## 15. Variable legal candidate domains

Canonical:

```text
complete ordered EnvironmentActionCandidate[]
selected public_action_key
```

Derived batch representation may contain:

```text
candidate offsets
candidate tensors
candidate embeddings
padding
candidate_mask
selected_local_row
```

`selected_local_row` is computed by exact matching of the public action key in
the current domain. It is valid only inside that derived batch and is never
semantic identity.

## 16. Reward boundary

The environment owns objective gameplay facts:

```text
accepted actions
state progression
terminal outcome
winner
win reason
interruption
failure
```

The environment does not own:

```text
+1 / -1 / 0
discount
reward shaping
return
advantage
bootstrap semantics
```

A versioned reward adapter derives numeric reward views from perspective-safe
canonical trajectories.

A simple terminal adapter may later map winner/loser/draw to numeric values,
but those numeric values are not game truth and must not change canonical
trajectory identities.

Reward shaping must consume only perspective-safe canonical inputs. Shaping
from hidden engine state is an information-safety violation even if the hidden
input is reduced to one scalar.

## 17. Immutable shards and admission

Actor publication should be immutable and manifest-last.

Conceptually:

```text
write private temporary objects
→ canonicalize and hash
→ verify referenced object closure
→ seal immutable CandidateShardManifest
→ publish immutable content objects
→ publish manifest last
```

There is no in-place transition from `WRITING` to `VERIFIED`.

Instead:

```text
private temporary data
        ↓
CandidateShardManifest   // sealed actor output
        ↓
AdmissionReceipt         // independent verifier result
        ↓
DatasetManifest          // admitted learner membership
```

Every physical object should distinguish semantic content identity from exact
stored artifact hash. Recompression/repacking may change physical artifact
hash while preserving semantic data identity.

Start Phase 3 with a simple uncompressed or narrowly pinned representation.
Compression, content deduplication, packing, Arrow, and Parquet are later
internal/derived optimizations and must preserve exact reconstruction.

## 18. Replay admission gate

Required admission flow:

```text
actor output
→ sealed candidate shard
→ artifact integrity
→ schema/canonical-byte validation
→ identity recomputation
→ sequence/closure validation
→ candidate completeness/action-membership validation
→ privacy validation
→ policy provenance validation
→ semantic public replay
→ restricted internal audit comparison
→ deduplication/conflict detection
→ immutable AdmissionReceipt
→ DatasetManifest
→ learner eligibility
```

Semantic replay uses the stored accepted V2 replay inputs and ordered public
action keys to regenerate the environment. Stored observations and domains are
evidence to compare; they are never injected into the engine.

Replay should compare, at every decision:

```text
full PublicEnvironmentObservation
public observation digest
request kind
candidate count
candidate fields/membership/order
public action keys
public candidate-domain digest
public semantic decision ID
selected public action transition
closure
```

Default first-corpus admission policy:

```text
TERMINAL    → eligible after all gates
INTERRUPTED → separate verified-prefix profile only
FAILED      → never learner-eligible
```

## 19. Duplicate and conflict rules

Deduplication should key logical actor work by deterministic semantic job ID.

```text
same semantic job + same trajectory_record_id
→ idempotent duplicate
→ count once

same semantic job + different trajectory_record_id
→ correctness incident
→ fail closed

independent jobs + identical public gameplay
→ permitted
→ provenance remains distinct
```

Arrival order must never choose the winner of a conflict.

## 20. Remote / Kaggle portability

Provider-neutral topology:

```text
rollout coordinator
    ↓
CPU actor backends
  local / Linux / Kaggle / Colab / cloud
    ↓
sealed candidate shards
    ↓
trusted verifier / admission
    ↓
immutable storage
    ↓
GPU learner
    ↓
immutable checkpoint
    ↓
independent evaluator
```

Semantic job, gameplay, trajectory, shard, and dataset identity must not depend
on:

```text
host name
cloud provider
Kaggle/Colab job ID
worker slot
PID
wall time
absolute path
bucket/object location
completion order
```

Before a new actor platform class can produce trusted data, it should reproduce
a fixed environment corpus under fixed public action sequences. Every
production shard still receives normal replay admission.

Kaggle is therefore a compute backend, not part of the OCGForge data contract.

## 21. Checkpoint direction

Future policy checkpoints should be immutable and content-addressed.

A checkpoint artifact should eventually bind enough provenance to identify:

```text
model weights
model schema
tensorizer schema
candidate scorer schema
parent checkpoint
training dataset/view identities
training configuration identity
```

Actors receive exact artifacts, never mutable aliases such as `latest` as
semantic provenance.

This is future ML architecture and is not part of Phase-3A implementation.

## 22. Phase-3 acceptance categories

The research proposes a full acceptance campaign. Exact gate numbering and
wording should be ratified with the Phase-3 implementation plan, but the
following requirements should be treated as mandatory categories.

### BLOCKER categories

- independent canonical golden codecs and unknown-version rejection;
- recorder consumes V2 public boundary only;
- environment/episode/public identities independently recompute;
- zero-decision terminal/interruption and pre-identity failure semantics;
- exact temporal action/successor alignment;
- rejected submissions produce zero record and zero mutation;
- candidate count/membership/fields/order preserved exactly;
- selected `public_action_key` appears exactly once;
- paired-world learner-visible trajectory privacy;
- continuation intermediate/final-response semantics;
- independent semantic public replay;
- terminal/interrupted/failed closure separation;
- exact per-decision policy producer attribution;
- deterministic shard construction independent of worker completion order;
- corruption/partial-publication injection remains unadmitted;
- deterministic duplicate/conflict handling;
- dataset identity independent of physical re-sharding;
- reward-view changes leave canonical identities unchanged;
- recurrent perspective isolation;
- cross-platform actor equivalence before hosted data is trusted;
- repeated writer/verifier soak;
- clean-checkout evidence regenerated without hand-editing.

### MAJOR categories

- derived tensors/masks/recurrent chunks regenerate from base data + config;
- framework export preserves episode membership, decision ordering, complete
  candidate domains, and selected public keys.

No performance target should weaken a semantic acceptance gate. Storage size
and throughput should be measured, but optimization remains subordinate to
correctness and semantic equivalence.

## 23. Concrete implementation roadmap after Phase 2

### Phase 3A — ADR and core normative contracts

Define and review:

```text
trajectory temporal convention
EpisodeManifest
DecisionRecord
EpisodeClosure
PolicyProvenance
identity hierarchy
canonical-versus-derived boundary
reward boundary
privacy threat model
golden-vector plan
```

No production recorder yet.

### Phase 3B — Pure values, codecs, and core identities

Implement the accepted Phase-3A DTOs, canonical encoders, validators, and
identity recomputation.

No gameplay changes.

### Phase 3C — Single-process local recorder

Record one V2 episode using only public V2 values:

```text
complete actionable frame
complete ordered candidate domain
selected public_action_key
accepted transition
closure
policy assignment
```

No remote actors, RPC, tensors, rewards, or cloud work.

### Phase 3D — Semantic replay verifier

Implement independent parsing, canonical validation, public replay, privacy
validation, policy-provenance validation, and `AdmissionReceipt` generation.

This creates the trust boundary between actor output and learner data.

### Phase 3E — Immutable local shard/dataset persistence

Implement content-addressed local objects, temporary write area, sealed
candidate manifests, atomic publication, admission receipts, dataset manifests,
and deterministic duplicate/conflict handling.

### Phase 3F — Fixed-matchup FINAL acceptance

Stay on the certified fixed matchup and generate the full canonical trajectory,
replay, paired-world, continuation, provenance, corruption, soak, and
clean-checkout evidence.

Only after all accepted Phase-3 gates pass should OCGForge claim a trusted
trajectory/dataset final pass.

### Phase 3G — Minimal interoperability probes

After canonical acceptance, add read-only adapters such as:

```text
Python decoder
PyTorch iterable adapter
TorchRL sequence probe
Arrow / Parquet analytics exporter
```

These remain adapters, never the canonical format.

## 24. Explicitly deferred work

This research does **not** authorize implementation of:

```text
behavior cloning
PPO
IMPALA / V-trace
R2D2
self-play
league training
reward shaping
production tensorization / embeddings
Reverb
RLlib training integration
remote actor service
Kaggle deployment automation
GPU learner
checkpoint publication service
multi-deck generalization
EDOPro remote-play integration
Discord challenge service
```

Those belong to later milestones after the trusted data foundation exists.

## 25. Research references

Primary reference families considered by the research:

- Google Research RLDS — https://github.com/google-research/rlds
- Farama Minari — https://github.com/Farama-Foundation/Minari
- Ray RLlib offline data / episodes — https://docs.ray.io/en/latest/rllib/rllib-offline.html
- DeepMind Reverb — https://github.com/google-deepmind/reverb
- Gymnasium termination/truncation semantics — https://gymnasium.farama.org/api/env/
- PettingZoo AEC — https://pettingzoo.farama.org/
- OpenSpiel — https://openspiel.readthedocs.io/
- TorchRL replay / burn-in facilities — https://docs.pytorch.org/rl/
- IMPALA / V-trace — Espeholt et al., 2018
- R2D2 — Kapturowski et al., 2019

These systems are references for data/learner ergonomics. OCGForge retains
ownership of its canonical semantics, privacy boundary, action identity,
replay contract, and acceptance evidence.

## 26. Final boundary

Immediately after `EpisodicEnvironment V2 FINAL PASS`, the next work should be:

```text
1. Phase-3A trajectory ADR / core contracts
2. canonical deterministic codecs and identities
3. single-process public recorder
4. independent semantic replay verifier
5. privacy / policy provenance validation
6. immutable local candidate shards
7. immutable AdmissionReceipt
8. deterministic DatasetManifest
9. fixed-matchup Phase-3 acceptance campaign
```

Training algorithms remain deferred until that trust boundary is accepted.
