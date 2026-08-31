# OCGForge Phase 4C Task 2A — Lethal Prerequisite Decision

Status: **CURRENT / AUTHORIZED — DOCS-ONLY ARCHITECTURE DECISION**

Base:

~~~text
261ded8660780da1ba819e8c639f485e65a49360
~~~

The accepted snapshot contract is:

~~~text
ocgforge.public_battle_snapshot.v1
~~~

The positive result contract remains:

~~~text
ocgforge.provable_lethal.v1
~~~

This document decides whether the current public, current-decision boundary
contains enough evidence for a sound PROVEN_LETHAL result. It does not
implement a public-boundary extension, ProvableLethalV1, Teacher behavior,
or Task 3.

## 1. Decision

The selected outcome is:

~~~text
POSITIVE_LETHAL_BLOCKED_UNDER_CURRENT_ACTION_CONTRACT
~~~

The current boundary can prove that a phase=1 BattleCommand selects an entry
from the engine's current attackable list. It cannot prove that the same
current candidate is a complete direct/targeted attack with a guaranteed
damage lower bound and no intervening response or continuation.

Task 3 may therefore be authorized in the future only as a fail-closed
ProvableLethalV1 evaluator. Until a separately approved public/equivalence
contract supplies the missing proof, current BattleCommand candidates must
remain NOT_PROVEN or UNSUPPORTED; no optimistic PROVEN_LETHAL path is
permitted.

## 2. Evidence summary

The live pinned source establishes:

1. ocgcore writes an activatable list and an attackable list into
   MSG_SELECT_BATTLECMD.
2. The repository decoder maps an activatable entry to marker 0 and an
   attackable entry to marker 1.
3. ocgcore's response for marker 1 stores the selected card as
   core.attacker; it does not store a target from that response.
4. The attack processor subsequently computes target candidates and the
   direct-attackable state.
5. It may ask for a direct-vs-target decision, emit a target-selection
   process, execute attack-cost effects, invoke quick effects/chains, enter
   damage-step events, calculate damage, apply replacement/modifier effects,
   and perform destruction processing.
6. The public projection does not publish the internal direct_attackable byte,
   a target, the future target domain, effect-use state, response
   availability, or a resolution result.

The detailed source trace is in P4C_BATTLE_PROTOCOL_FLOW.md.

## 3. Prerequisite classification

The classification vocabulary is exact:

- AVAILABLE_PUBLICLY_NOW: copied from the accepted current public input,
  possibly conditional on an exact visible public join.
- SAFE_DERIVATION_POSSIBLE: a deterministic derivation is possible over
  current public values, without claiming engine resolution.
- REQUIRES_NEW_PUBLIC_CONTRACT: the current public boundary lacks a required
  current-action value or exact semantic relation.
- FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL: proof requires a later decision,
  future domain, hidden response/effect state, or a multi-step resolution.
- BLOCKED: the required fact is private, unmodelled, or would require
  duplicating substantial rules semantics.

| Lethal prerequisite | Classification | Current evidence and exact gap | Fail-closed result |
| --- | --- | --- | --- |
| Current candidate membership | AVAILABLE_PUBLICLY_NOW | The environment supplies the complete ordered candidate vector and validates public action-key membership. | Invalid/duplicate domain is INVALID; never repair or filter |
| Exact attack-command classification | SAFE_DERIVATION_POSSIBLE | Marker 1 is proven to select an entry from attackable_cards; this is only an attackable-list selection, not a complete attack declaration. | Keep precise attack class UNSUPPORTED |
| Complete attack declaration | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | Marker 1 selects core.attacker; target/direct choice and later attack processing follow after submission. | NOT_PROVEN/UNSUPPORTED |
| Attacker identity | AVAILABLE_PUBLICLY_NOW | A source VisibleCard may join exactly to a same-perspective visible entity; a RedactedSlot remains unavailable. | Missing/redacted identity is UNSUPPORTED; bad join is INVALID |
| Attack target in current candidate | REQUIRES_NEW_PUBLIC_CONTRACT | The current BattleCommand candidate has no target reference. A later target-selection process may publish a separate public domain, but it is not part of this candidate. | No target claim; UNSUPPORTED |
| Direct-attack status | REQUIRES_NEW_PUBLIC_CONTRACT | The engine has an internal direct_attackable flag and target computation, but the current public candidate does not publish that flag. Missing target is not direct attack. | UNSUPPORTED; never infer direct attack |
| Current attacker ATK | AVAILABLE_PUBLICLY_NOW | A visible source entity may provide current.attack in the same public safe state. | Missing/redacted value is UNSUPPORTED |
| Target position | REQUIRES_NEW_PUBLIC_CONTRACT | No target is present in the current candidate; a later target frame cannot be silently joined into this current action. | UNSUPPORTED |
| Target current ATK/DEF | REQUIRES_NEW_PUBLIC_CONTRACT | Target stats are not associated with the phase=1 candidate. Public stats may be read only after an exact current target join. | UNSUPPORTED |
| Checked stat arithmetic | SAFE_DERIVATION_POSSIBLE | Checked signed arithmetic over two present public stats is possible as a descriptive margin. | Missing operand UNSUPPORTED; overflow INVALID |
| Guaranteed opponent LP-loss lower bound | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | A stat margin is not damage; target choice, damage calculation, modifiers, replacement, and resolution follow later. | Bound absent; NOT_PROVEN/UNSUPPORTED |
| Damage replacement/modification | BLOCKED | The current public boundary does not prove every effect or replacement that can alter damage. | UNSUPPORTED; no rules recreation |
| Battle-damage immunity | BLOCKED | No exact public fact proves absence of an effect preventing battle damage. | UNSUPPORTED |
| Battle destruction immunity | BLOCKED | The pinned processor explicitly checks indestructibility after damage calculation; current public input does not prove its absence. | UNSUPPORTED |
| Response-window existence | SAFE_DERIVATION_POSSIBLE | The pinned attack path can invoke QuickEffect/PointEvent processing before and during damage-step processing. | Presence/possibility does not become absence proof |
| Opponent response availability | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | Whether the opponent can respond is a later/private decision-state property and must not be exported as a hidden-response oracle. | UNSUPPORTED/NOT_PROVEN |
| Unresolved target decision | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | The processor can emit direct-vs-target yes/no or SelectCard after marker 1. | UNSUPPORTED; current candidate is not terminal |
| Unresolved continuation | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | Core processing continues through subsequent engine decisions and process steps after V2 submission. | UNSUPPORTED; no queue or lookahead |
| Terminal outcome guarantee | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | Terminal MSG_WIN can occur only after the engine resolves the submitted response and all intervening processing. | NOT_PROVEN/UNSUPPORTED |
| No private information required | BLOCKED | A positive guarantee would otherwise need hidden effect-use/response state or private engine queries. | Positive proof forbidden |
| One-current-action proof | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | The current candidate selects an engine command whose outcome spans later target, response, damage, and possible replay steps. | No multi-step proof in v1 |

## 4. Contract conditions 1–10

The ten positive-proof conditions from
ocgforge.provable_lethal.v1 classify as follows:

| Condition | Classification | Determination |
| ---: | --- | --- |
| 1. Candidate is authoritative current-domain member | AVAILABLE_PUBLICLY_NOW | Supplied by Environment and preserved by the snapshot |
| 2. All required battle/damage inputs are public | REQUIRES_NEW_PUBLIC_CONTRACT | Current phase=1 input lacks target/direct/resolution inputs |
| 3. Arithmetic is exact and checked | SAFE_DERIVATION_POSSIBLE | Applies to public stat arithmetic only, not damage |
| 4. Guaranteed LP-loss lower bound reaches opponent LP | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | No public guaranteed damage result exists for the current candidate |
| 5. No unresolved target selection remains | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | Target/direct branching occurs after marker 1 |
| 6. No unresolved continuation remains | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | V2 submission advances ocgcore into more processing |
| 7. No private information is required | BLOCKED | Response/effect and hidden state cannot be safely exported |
| 8. No hidden effect-use state is required | BLOCKED | Attack capability and effects depend on engine state not in the public DTO |
| 9. No unmodelled modifier can lower the bound | BLOCKED | Damage/replacement/immunity rules are not fully public facts |
| 10. No intervening response can prevent terminal result | FUNDAMENTALLY_NOT_CURRENT_ACTION_LOCAL | The pinned path exposes possible response windows, and absence is not public |

Conditions 2 and 4–10 are not collectively satisfied by the current
one-decision public input. Therefore the positive status cannot be emitted
for a generic current BattleCommand.

## 5. Evaluation of the four architecture outcomes

### A — EXISTING_PUBLIC_BOUNDARY_SUFFICIENT

Rejected. The current input proves candidate membership and some visible
stats, but not the complete attack/target/damage/response/terminal chain.

### B — DERIVED_BATTLE_COMMAND_SEMANTICS_REQUIRED

Insufficient as the prerequisite decision. A derived contract could name
marker 0 as the activatable list, marker 1 as the attackable list, and
markers 2/3 as phase controls. The live source already justifies that
descriptive mapping. It would not supply target selection, direct-attack
proof, response absence, damage modifiers, or terminal guarantee. No Task-2B
implementation is authorized by this decision.

### C — VERSIONED_PUBLIC_BOUNDARY_EXTENSION_REQUIRED

Not selected for the current prerequisite. Adding a target or direct-attack
field would still not safely publish hidden response availability or all
resolution modifiers. A broad public battle-state dump would violate the
layer boundary and create a second legality/resolution oracle. A future
narrowly scoped extension can be considered only through a new explicit
contract if it proves a specific missing fact is both public and sufficient.

### D — POSITIVE_LETHAL_BLOCKED_UNDER_CURRENT_ACTION_CONTRACT

Selected. A sound positive result necessarily crosses later engine decisions,
effect/response windows, and battle-resolution semantics that are not
contained in the current candidate. The correct v1 behavior is fail-closed,
not optimistic.

## 6. Consequences for Task 3

The next implementation task remains unauthorized. If independently
authorized, it must:

- implement the existing ocgforge.provable_lethal.v1 result boundary;
- preserve NOT_PROVEN != PROVEN_NON_LETHAL;
- return NOT_PROVEN or UNSUPPORTED for current candidates whose proof inputs
  are absent;
- never publish a guaranteed loss bound from ATK-versus-LP arithmetic alone;
- never infer direct attack from a missing target;
- never read hidden response/effect state;
- never query CoreHost or raw engine state;
- never combine future target domains or future decisions;
- never queue actions, run MCTS, or search private states;
- use checked integer arithmetic and canonical proof reasons.

Under the selected D outcome, a future positive PROVEN_LETHAL example
requires a separately accepted public/equivalence contract that closes every
applicable proof condition. Task 3 must not manufacture that contract.

## 7. Identity, privacy, and replay consequences

This decision changes no existing runtime schema or identity:

~~~text
ocgforge.public_environment_observation.v1  unchanged
ocgforge.public_safe_state.v1               unchanged
ocgforge.public_battle_snapshot.v1          unchanged
public action identity                       unchanged
environment semantic identity                unchanged
trajectory DecisionRecord                    unchanged
replay/admission                             unchanged
Teacher v1 semantic identity                 unchanged
~~~

No private response availability, hidden code, physical identity, private
locator, raw response, pointer, process, path, time, or secret-derived hash
may be published to work around the missing proof.

The exact consequence is:

~~~text
phase=1 attackable-list selection
    -> accepted current public candidate
    -> later target/response/resolution processing
    !=
PROVEN_LETHAL
~~~

Existing Phase-4B trajectories and Teacher v1 artifacts remain interpreted
under their original contracts. No new predicate, binding, artifact,
trajectory field, or replay requirement is introduced here.

## 8. Authorization state

~~~text
Task 2A — Battle/Lethal prerequisite decision
CURRENT / AUTHORIZED — docs-only

Decision:
POSITIVE_LETHAL_BLOCKED_UNDER_CURRENT_ACTION_CONTRACT

Task 3 — fail-closed ProvableLethal evaluator
NOT AUTHORIZED
~~~

No Phase-4C implementation or acceptance gate is executed by this decision.
