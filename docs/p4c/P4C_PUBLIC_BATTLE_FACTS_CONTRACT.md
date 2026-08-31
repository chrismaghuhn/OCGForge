# OCGForge Phase 4C Task 1 — Public Battle Facts and Provable Lethal

Status: **CURRENT / AUTHORIZED — DOCS-ONLY CONTRACT FREEZE**

This document freezes the first Phase-4C public battle/evaluation boundary. It
does not authorize production code, a new observation schema, battle
resolution logic, Teacher integration, ML, or Phase-4C acceptance.

The accepted Phase-4B evidence remains the starting point:

~~~text
Phase-4B H_exec:
cd00c3d34cc41c50ac1e7730a26a0e532cd21902

Phase-4B H_evidence:
32a1adedc50681fd3f5bf2d4b59f8fa3cd7a3030

Phase-4B schema:
ocgforge.phase4b_acceptance.v1
~~~

## 1. Contract identities and purpose

The two new, immutable semantic contract identities are:

~~~text
ocgforge.public_battle_snapshot.v1
ocgforge.provable_lethal.v1
~~~

They identify derived public-policy/evaluation semantics. They are not:

- a replacement for `PublicEnvironmentObservation`;
- a new `PlayerObservation` schema;
- a legality or candidate-completeness contract;
- a gameplay, episode, trajectory, replay, admission, receipt, or dataset
  identity;
- a second rules engine;
- a Teacher v1 predicate or profile contract.

The first implementation is expected to derive these values from the existing
accepted public observation and complete candidate-domain contracts without
changing their canonical bytes. If a required input is not present at that
boundary, the implementation is `BLOCKED` or `UNSUPPORTED`; it must not
silently extend the public observation.

## 2. Authority and input boundary

The owning layers remain:

- `ygo::environment` owns legal candidate construction, candidate
  completeness and order, `public_action_key`, public observation projection,
  and V2 submission legality.
- The pinned ocgcore/rules bundle owns battle legality and battle resolution.
- `ygo::teacher` may consume only the public values below when a future task
  implements a battle extractor.
- Phase-4B Teacher v1 artifacts and identities remain immutable.
- Trajectory, replay, admission, receipt, and dataset layers remain unchanged.

The exact future battle input is:

~~~text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
~~~

The battle layer must not consume or derive from:

- `DecisionFrame`;
- `SubmissionToken`;
- `CoreHost`;
- private `PlayerObservation` data;
- raw engine queries or raw engine messages;
- internal `ActionCandidate`;
- internal `semantic_key`;
- response bytes;
- hidden locators or private caches;
- continuation-private state;
- opponent hidden cards or hidden deck state.

An attack candidate that is present in the supplied authoritative domain is
already an Environment-owned candidate. If it is absent, the Battle layer
must not infer that a monster already attacked, that it cannot attack, or why
the candidate is absent.

No field is added to `PublicEnvironmentObservation` in this task. No current
observation bytes, public action-key bytes, trajectory bytes, or gameplay
identity changes are authorized.

## 3. PublicBattleSnapshotV1

`PublicBattleSnapshotV1` is a value-owned, decision-local derived
representation. It is not persistent Teacher memory and is not a second
observation schema.

The conceptual field set is:

~~~text
PublicBattleSnapshotV1

schema_id =
  ocgforge.public_battle_snapshot.v1

perspective_player
decision_index
decision_context_kind       // optional canonical public token
turn_phase                  // optional u32
self_life_points            // optional u32
opponent_life_points        // optional u32
candidate_facts[]
~~~

`perspective_player` and `decision_index` are copied from the public
observation. `decision_context_kind`, `turn_phase`, and life points are
projected from the same perspective-safe public safe state and public
decision context. A missing public value remains missing; it is never guessed
from a card, phase name, private state, or candidate absence.

`candidate_facts` contains exactly one entry for every supplied candidate,
in exactly the supplied authoritative order. The vector position is
alignment evidence only. It is not an identity, semantic key, selection
authority, or tie-break. A malformed candidate vector, invalid key, or
duplicate public key fails closed; the extractor must not repair, deduplicate,
truncate, or reorder it.

### 3.1 Candidate fact fields and statuses

Each `PublicBattleCandidateFactsV1` entry contains:

~~~text
PublicBattleCandidateFactsV1

public_action_key

status:
  NOT_APPLICABLE
  SUPPORTED
  UNSUPPORTED
  INVALID

battle_candidate_class

source_current_attack: optional signed integer
source_current_defense: optional signed integer
source_position: optional public position

target_current_attack: optional signed integer
target_current_defense: optional signed integer
target_position: optional public position

public_stat_margin: optional signed integer
reason_ids[]
~~~

The names are semantic names, not permission to infer a value. The eventual
codec must assign fixed canonical codes under the snapshot schema; it must
not reuse a Phase-4B status code without declaring that mapping in the
implementation contract.

The v1 descriptive class set is intentionally small:

~~~text
NON_BATTLE_CANDIDATE
BATTLE_COMMAND_UNCLASSIFIED
~~~

`NON_BATTLE_CANDIDATE` means that the supplied public candidate is not an
`EnvironmentActionKind::BattleCommand`; it is not a claim that the candidate
is legal for any unrelated action family. A well-formed non-battle candidate
may therefore be `NOT_APPLICABLE` for battle facts.

`BATTLE_COMMAND_UNCLASSIFIED` means that the public action is in the
BattleCommand family, while the current public shape does not prove whether
it is an attack declaration, direct attack, targeted attack, or battle-phase
control command. That distinction remains `UNSUPPORTED`/`BLOCKED` in v1.

`SUPPORTED` means only that the stated public fact or safe derivation is
available and valid. It is not a claim that the engine will resolve the
candidate successfully, deal damage, destroy a card, or end the duel.

`UNSUPPORTED` means required public information or a safe semantic
classification is unavailable. `INVALID` means malformed or contradictory
public input, impossible optional-field combination, invalid arithmetic, or
a failed exact public join. `NOT_APPLICABLE` is reserved for a well-formed
candidate outside the battle feature being described.

## 4. Current public BattleCommand shape

The current decoder/projection is the authority for this description. The
public candidate contains only the safe fields in
`EnvironmentActionCandidate`; it does not expose the internal response or
engine command encoding.

For the current `MSG_SELECT_BATTLECMD` family:

| Public field | Current meaning | Contract boundary |
| --- | --- | --- |
| `action_kind` | `EnvironmentActionKind::BattleCommand` | Safe family classification only |
| `public_action_key` | Existing public identity for the projected candidate | Existing environment identity; use as candidate evidence identity |
| `phase` | Decoder-derived public command-list marker | Descriptive metadata; it is not an ordered time scale |
| `source_reference` | Optional public source-card reference if the projection resolves it | May join only against the same perspective-safe public state |
| `target_reference` | Optional public target-card reference when the projection proves one | Absence is not proof of direct attack |
| `choice` | Optional typed public choice metadata derived from the candidate | Does not by itself prove attack subtype or outcome |
| `source_index` | Not a v1 BattleCommand attack classifier | Must not be invented or recovered from private data |
| `position`, `amount`, `continuation_operation` | Optional fields only where the public candidate actually supplies them | No battle meaning may be inferred from absence |

The pinned decoder currently emits source-bearing BattleCommand candidates
for activatable/attackable entries with decoder list markers (including
markers conventionally represented as phase `0` and phase `1`) and
source-less battle control candidates with other markers (including `2` and
`3`). These markers describe the decoder's current command lists. They do
not, by themselves, prove a successful attack declaration, direct attack,
target selection, or battle resolution.

Consequently, v1 does not freeze a numeric `phase` predicate as an attack
legality rule. A future implementation may classify an exact shape only
after a focused decoder/projection test proves the complete shape and its
meaning. If that proof is unavailable, the class remains
`BATTLE_COMMAND_UNCLASSIFIED` and the precise classification is
`UNSUPPORTED`.

The same rule applies to the distinction between an omitted target and a
direct attack. The current public boundary does not permit the extractor to
turn an absent target into a direct-attack claim.

## 5. Public entity joins and current stats

Source and target joins follow the strict Phase-4B public role-lookup
boundary.

### VisibleCard

A `VisibleCard` reference may be resolved only against the exact
same-perspective `PublicSafeStateView` decoded from the current
`PublicEnvironmentObservation`.

The join must find exactly one public entity with the exact
`observation_locator`. Missing or duplicate matches are `INVALID`. A
`VisibleCard` paired with a matched entity that is not identity-known or
has no public identity where the projection requires one is inconsistent
public input and is `INVALID`; it is not a negative battle result.

Current ATK, DEF, position, controller, and zone may be copied only from
the matched entity's current public observation fields. A known passcode
does not authorize a CardScripts/database lookup for current stats. If the
requested current ATK or DEF is absent from the public entity, the requested
feature is `UNSUPPORTED`.

The accepted observation types use enum sentinels for some fields rather than
optional storage. They have the following exact mapping:

~~~text
ObservedCard.position == Position::Unknown
    -> no concrete public battle position
    -> snapshot optional position is absent
    -> position-dependent feature is UNSUPPORTED

ObservedCard.zone == SemanticZone::Unknown
    -> no concrete public zone
    -> zone-dependent derivation is UNSUPPORTED

optional controller absent
    -> controller-dependent feature is UNSUPPORTED
~~~

`Position::Unknown` and `SemanticZone::Unknown` are not supported concrete
values. An invalid enum value remains `INVALID`. An absent controller is
`UNSUPPORTED` unless another frozen structural invariant makes that
combination contradictory, in which case it is `INVALID`. The extractor must
not encode either Unknown sentinel as a concrete battle fact.

### RedactedSlot

For identity- or stat-dependent semantics:

~~~text
RedactedSlot -> UNSUPPORTED
~~~

No hidden engine state, hidden passcode, deck entry, private locator, or
inferred identity may be used to resolve it.

### Evidence

Locators are transient join inputs only. They must not become persistent
Teacher state, Battle identity, replay identity, or semantic evidence.
Candidate evidence remains keyed by the existing `public_action_key`.
Passcodes and locators used during a join must not be emitted as reason IDs,
explanation data, state fields, or diagnostic strings.

## 6. Public arithmetic is not damage proof

If two current public stats are present and the future extractor has an exact
public comparison context, it may derive a checked descriptive margin such
as:

~~~text
source_current_attack - target_current_attack
source_current_attack - target_current_defense
~~~

The result is a public stat margin, never `damage`. The arithmetic must be
checked; signed overflow or an unrepresentable result is `INVALID`, never
wrapped, clamped, or saturated. No floating point is permitted.

A stat margin does not prove:

- battle damage;
- successful attack resolution;
- card destruction;
- terminal victory;
- guaranteed lethal;
- absence of an opponent response or replacement effect.

In particular:

~~~text
current source ATK >= opponent LP
~~~

is never sufficient for `PROVEN_LETHAL`.

## 7. ProvableLethalV1

`ProvableLethalV1` is a per-candidate, current-decision-local positive-proof
result:

~~~text
ProvableLethalCandidateV1

schema_id =
  ocgforge.provable_lethal.v1

public_action_key

status:
  NOT_APPLICABLE
  PROVEN_LETHAL
  NOT_PROVEN
  UNSUPPORTED
  INVALID

guaranteed_opponent_lp_loss_lower_bound:
  optional unsigned integer

proof_reason_ids[]
~~~

The status meanings are strict:

- `PROVEN_LETHAL` is a positive proof that the current supplied candidate
  guarantees terminal defeat of the opponent.
- `NOT_PROVEN` means that the available public facts are valid but the
  positive proof is incomplete. It is not a claim of non-lethality.
- `UNSUPPORTED` means a required public input, exact semantic class, or
  public proof capability is unavailable.
- `INVALID` means malformed input, an inconsistent join, or failed checked
  arithmetic.
- `NOT_APPLICABLE` means the supplied candidate is outside the current
  lethal feature.

The distinction is normative:

~~~text
NOT_PROVEN != PROVEN_NON_LETHAL
~~~

Only a `PROVEN_LETHAL` result may make a positive lethal claim. A future
implementation must not publish an unverified loss lower bound as if it were
proof; for non-positive statuses the bound should be absent unless a later
version explicitly defines a separate non-authoritative diagnostic meaning.

### 7.1 Conditions for PROVEN_LETHAL

`PROVEN_LETHAL` may be emitted only when all applicable requirements are
proven from the accepted public boundary:

1. The candidate is a member of the authoritative current supplied domain.
2. Every battle/damage input required by the proof is present in the accepted
   public boundary.
3. All arithmetic is exact, checked, and representable.
4. The guaranteed opponent LP-loss lower bound is at least current opponent
   LP.
5. No unresolved target selection required by the proof remains outside the
   current candidate.
6. No unresolved continuation required by the proof remains.
7. No opponent or other private information is required.
8. No hidden effect-use state is required.
9. No unmodelled public or private damage modifier can lower the guaranteed
   bound.
10. No intervening response/decision window can prevent the asserted
    terminal result unless that absence is itself publicly and contractually
    proven.

If any requirement is not proven, the result is `NOT_PROVEN` or
`UNSUPPORTED`, according to whether the limitation is an incomplete proof
over valid known inputs or unavailable required public information. It is
never optimistic `PROVEN_LETHAL`.

The current v1 boundary does not, by this freeze alone, prove generic attack
resolution, response absence, damage modifiers, destruction immunity, direct
attack permission, or multi-action lethal. Therefore a future implementation
must expect many current BattleCommand candidates to remain
`NOT_PROVEN`/`UNSUPPORTED`.

## 8. One-current-action boundary

Phase-4C v1 is limited to the current supplied decision. It forbids:

- queuing attack A followed by attack B;
- assuming a future target still exists;
- assuming a future candidate domain;
- assuming the opponent passes;
- assuming a follow-up resolves;
- searching hidden future states;
- omniscient `CoreHost` simulation;
- MCTS, rollout, or private-state search;
- future-action scripts or queues.

A multi-action public search would require a separate versioned architecture
decision and is not authorized here.

## 9. Rules authority and fail-closed behavior

The pinned rules bundle and ocgcore remain authoritative for legal attack
construction, battle resolution, effect interaction, damage replacement,
destruction, and terminal outcome. The Battle feature layer must not duplicate
those rules.

The allowed conceptual separation is:

~~~text
public facts published by OCGForge
        +
safe checked arithmetic over those facts
        !=
full battle resolution owned by ocgcore/rules
~~~

If a lethal proof would require reimplementing a nontrivial engine rule, the
proof path is `UNSUPPORTED` unless a narrow equivalence contract is frozen in
a later task.

Malformed candidate metadata, malformed safe-state entity joins,
contradictory visibility, invalid public keys, duplicate candidate keys, and
checked arithmetic failure all fail closed. They never trigger candidate
filtering or a guessed fallback action.

## 10. Canonical and deterministic representation

Future DTO/codec work must use the repository's canonical value/byte
conventions and must preserve:

1. fixed schema/domain identity;
2. fixed field order;
3. exact supplied candidate count and order;
4. exact existing `public_action_key` values;
5. explicit optional-value presence;
6. canonical enum/status codes;
7. canonical sorted/unique `reason_ids`;
8. checked signed integer arithmetic;
9. no floating point, RNG, pointer, address, time, process, thread,
   provider, or filesystem-path input.

No authoritative unordered-container iteration is allowed. Independent
processes given identical public inputs must produce canonically identical
battle snapshots and lethal results.

## 11. Privacy and knowledge-destroying boundaries

Future acceptance must construct paired private worlds that differ in hidden
hand identity, hidden deck order, face-down identity, and/or private semantic
keys while projecting the same:

- `PublicEnvironmentObservation`;
- ordered public candidate vector;
- public action keys;
- battle snapshot inputs.

The resulting `PublicBattleSnapshotV1` and `ProvableLethalV1` values must be
identical. No hidden passcode, private semantic key, raw response, private
locator, secret-derived hash, pointer, PID, path, wall time, thread, or
provider value may appear in output.

After a knowledge-destroying transition, a later hidden slot is not the same
physical card merely because an internal engine identity or locator happens
to look related. `RedactedSlot` stat lookup remains `UNSUPPORTED`.

## 12. Identity and replay implications

Adding a derived battle snapshot implementation, without changing public
observation or gameplay semantics, does not change:

- public gameplay identity;
- episode identity;
- trajectory schema;
- `DecisionRecord`;
- admission receipt;
- dataset identity.

If a future Teacher changes its decisions by consuming Battle or Lethal
semantics, it must create new semantic provenance before publication:

- a new TeacherCore semantic identity;
- new `TeacherPolicyBinding` identities;
- new `PolicyArtifact` identities;
- a new predicate-registry identity if battle predicates are introduced.

The meaning of `ocgforge.policy.teacher_core.v1` and
`ocgforge.policy.teacher_predicate.v1` must not be mutated. Existing admitted
Phase-4B trajectories remain interpreted under their original provenance.

Battle facts and lethal proof remain derived policy/evaluation data in this
phase. They must not be added to `DecisionRecord`,
`CandidateTrajectoryShard`, `AdmissionReceipt`, `DatasetManifest`, or
semantic replay requirements. Teacher actions continue through:

~~~text
PolicySelection
→ EpisodicEnvironment V2
→ TrajectoryRecorder
→ CandidateTrajectoryShard
→ semantic replay
→ admission
→ receipt/dataset
~~~

No Battle-specific shortcut is permitted.

## 13. Future acceptance status

This Task 1 freezes future implementation and acceptance requirements only.
No Phase-4C gate is executed by this task. The proposed gate matrix and
future task ownership are in:

- `P4C_PUBLIC_BATTLE_FACT_MATRIX.md`;
- `P4C_IMPLEMENTATION_PLAN.md`.

Every future gate must report `PASS`, `FAIL`, `NOT_RUN`, `SKIPPED`, or
`BLOCKED` from actual evidence. Missing proof is never a PASS.

## 14. Explicit non-goals

This task does not authorize:

- production code;
- new `PlayerObservation` fields;
- new `PublicEnvironmentObservation` bytes;
- new legality logic;
- private engine queries;
- future-action search;
- MCTS;
- ML, BC, RL, or self-play;
- arbitrary-deck support;
- copy-budget inference;
- a duplicate battle simulator;
- optimistic lethal heuristics;
- a Teacher v2 integration;
- trajectory/replay/admission changes;
- Phase 4C final acceptance.
