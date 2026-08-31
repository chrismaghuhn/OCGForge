# OCGForge Phase 4C Task 2A — Battle Protocol Flow

Status: **CURRENT / AUTHORIZED — DOCS-ONLY PREREQUISITE DECISION**

This document traces the current pinned BattleCommand path from the engine
message to the V2 submission boundary and the next protocol decisions. It is
evidence for the Task-2A prerequisite decision; it does not authorize a
public-boundary implementation, a lethal evaluator, or Teacher integration.

The accepted starting point is:

~~~text
Task 2 snapshot head:
261ded8660780da1ba819e8c639f485e65a49360

Snapshot contract:
ocgforge.public_battle_snapshot.v1
~~~

## 1. Source authority

The relevant current sources are:

| Source | Relevant owner and evidence |
| --- | --- |
| src/protocol/message_decoder.cpp | Decodes MSG_SELECT_BATTLECMD into internal DecisionRequest and current list-marker candidates; see decode_battle and decode_messages |
| src/environment/episodic_environment.cpp | Projects internal candidates and observations into the public V2 frame; validates public-domain coupling |
| src/environment/episode_driver.cpp | Submits the selected internal response to the engine and advances until the next boundary |
| src/core/core_host.cpp | Calls OCG_DuelProcess and OCG_DuelSetResponse |
| .cache/derived/ocgcore/playerop.cpp | Pinned ocgcore construction and validation of the BattleCommand message and its list markers |
| .cache/derived/ocgcore/processor.cpp | Pinned ocgcore BattleCommand state machine, target selection, response windows, and damage-step flow |

The cached ocgcore checkout is a pinned rules-bundle input selected by the
repository's canonical CMake rules-bundle configuration. It is used here as
source authority, not as a new application API.

## 2. Engine message construction

The pinned ocgcore SelectBattleCmd processor writes one
MSG_SELECT_BATTLECMD message for the current turn player. Its current layout
is:

~~~text
message type
acting player
activatable count
  for each activatable entry:
    card code
    controller
    location
    sequence
    effect description
    client mode
attackable count
  for each attackable entry:
    card code
    controller
    location
    sequence
    direct_attackable flag
to-main-phase-2 flag
to-end-phase flag
~~~

The repository decoder reads this message in
src/protocol/message_decoder.cpp::decode_battle:

- each activatable entry becomes an internal BattleCommand candidate with
  command/list marker 0;
- each attackable entry becomes an internal BattleCommand candidate with
  command/list marker 1;
- the final two flags become source-less internal candidates with markers 2
  and 3.

The decoder retains card code, controller, location, sequence, command, and
list index for source-bearing entries. It reads and discards the ocgcore
direct_attackable byte from an attackable entry. That byte is not part of the
current public candidate.

The internal response for a card entry is the little-endian integer:

~~~text
command | (list_index << 16)
~~~

The source code builds that response as the exact response bytes associated
with the internal candidate. Marker 2 and marker 3 use source-less responses
containing the corresponding control value.

The decoder emits the current internal request family
DecisionRequestKind::BattleCommand with engine message name
MSG_SELECT_BATTLECMD. It does not emit a target selection in this request.

## 3. Public projection

EpisodicEnvironment::Impl::project_frame enforces the internal
request/observation coupling before projection:

- the request must be a supported public decision family;
- observation perspective and engine-step metadata must match the request;
- the attached decision context must match the request;
- every internal candidate must project successfully;
- the public candidate count must equal the internal count;
- the public keys must form a canonical, unique domain.

For each internal BattleCommand candidate, project_candidate performs the
following safe projection:

| Internal information | Public result |
| --- | --- |
| BattleCommand action kind | EnvironmentActionKind::BattleCommand |
| source card/controller/location/sequence | optional VisibleCard or RedactedSlot reference after the current public observation join |
| target card fields | optional public target reference only if the internal candidate has a target card; current BattleCommand decoder entries have none |
| command/list marker | optional public phase |
| list index | typed public EffectChoice metadata when the internal candidate has a choice index |
| source index | not copied into the public BattleCommand candidate |
| internal response bytes | not copied into the public candidate or observation |
| internal semantic key | used only in the private binding; not public |

The public public_action_key is generated from the projected public
descriptor. It contains no raw response bytes or internal semantic key. The
public observation contains only the already accepted perspective-safe state
and public decision-context fields.

The resulting public candidate vector retains the internal candidate count
and order. The environment, not Battle code, remains the authority for
candidate legality and completeness.

## 4. Current marker semantics proven by the pinned source

The following mapping is justified by the current pinned source:

| Public marker | Pinned source meaning | What it proves |
| ---: | --- | --- |
| phase=0 | Entry from core.select_chains, the activatable list | The current BattleCommand selects an activatable battle-phase effect entry |
| phase=1 | Entry from core.attackable_cards, the attackable list | The current BattleCommand selects one engine-proven attackable monster entry |
| phase=2 | core.to_m2 control flag and accepted t=2 response | The current BattleCommand requests the available transition toward Main Phase 2 |
| phase=3 | core.to_ep control flag and accepted t=3 response | The current BattleCommand requests the available transition toward the End Phase |

This is a derived description of the current decoder/rules path. It is not
an ordered phase scale and it is not a claim that every marker is a complete
gameplay action.

In particular, phase=1 proves only that the selected card was in the
engine's current attackable_cards list. It does not publish the
direct_attackable byte, a target, a resolved target, or an attack result.
The accepted PublicBattleSnapshotV1 therefore correctly keeps the class
BATTLE_COMMAND_UNCLASSIFIED for all current BattleCommand candidates.

## 5. What happens after phase=1 is submitted

The pinned ocgcore BattleCommand processor receives the response
command=1 | (index << 16) and enters the attack path. The current processor
flow is materially longer than the current public candidate:

~~~text
MSG_SELECT_BATTLECMD
    phase=1 candidate selects an attackable monster
        ↓
BattleCommand step 1 stores core.attacker
        ↓
attack-cost effects and attack-cost chain processing
        ↓
BattleCommand step 2 solves the cost/chain state
        ↓
target computation at BattleCommand step 4
        ↓
direct-vs-target decision or target selection
        ↓
BattleCommand step 6 stores core.attack_target
        ↓
attack announcement and attack-point events
        ↓
possible quick-effect/chain response windows and replays
        ↓
damage-step events, damage calculation, replacements, destruction
        ↓
possible next battle action, phase control, terminal result, or failure
~~~

The exact target branch is controlled by private/current engine state:

1. get_attack_target computes the target candidates and the
   direct_attackable state.
2. If a direct attack is possible and there are no selected monster targets,
   the processor can proceed without a card target.
3. If a direct attack and monster targets are both possible, the processor
   asks a yes/no question about the direct attack before selecting a monster
   target when the answer is negative.
4. If a monster target must be chosen, the processor emits a SelectCard
   process. Depending on the rules/effects, that can be a later
   MSG_SELECT_CARD boundary.
5. If no target and no direct attack is possible, the attack announcement is
   marked failed and the BattleCommand process restarts or exits the attack
   path.

The current public phase=1 candidate is therefore an attacker-list choice,
not a complete targeted/direct attack choice. Even a visible source entity
with a public current ATK does not close the target or resolution boundary.

## 6. Later response and decision windows

After the attacker is selected, the pinned processor can execute attack-cost
effects and invoke chain/quick-effect processing before target selection.
During the attack announcement and damage-step events it can again invoke
point-event and quick-effect processing.

The repository protocol dispatcher recognizes these interactive message
families:

~~~text
MSG_SELECT_BATTLECMD
MSG_SELECT_IDLECMD
MSG_SELECT_EFFECTYN
MSG_SELECT_YESNO
MSG_SELECT_POSITION
MSG_SELECT_PLACE
MSG_SELECT_DISFIELD
MSG_SELECT_CHAIN
MSG_SELECT_CARD
MSG_SELECT_OPTION
MSG_SELECT_TRIBUTE
MSG_SELECT_SUM
MSG_SELECT_COUNTER
MSG_SORT_CARD
MSG_SORT_CHAIN
MSG_SELECT_UNSELECT_CARD
MSG_ANNOUNCE_NUMBER
MSG_ANNOUNCE_RACE
MSG_ANNOUNCE_ATTRIB
~~~

The exact message family depends on the current effects, targets, chain
state, and engine process state. The dispatch table is a capability table,
not a claim that every attack emits every family.

The pinned battle state machine also emits non-interactive battle
notifications such as attack announcement, damage-step start, battle
statistics, damage, destruction, and damage-step end. Those notifications
are not inputs to the current phase=1 candidate and do not retroactively
make it a guaranteed lethal action.

## 7. V2 submission and advancement

The public policy path is:

~~~text
public_action_key
    ↓
EpisodicEnvironment::step(ActionSelection)
    ↓
membership and public frame/token/decision checks
    ↓
public key -> private semantic-key binding
    ↓
EpisodeDriver::apply_semantic_key
    ↓
CoreHost::submit_response
    ↓
OCG_DuelSetResponse
    ↓
CoreHost::process / OCG_DuelProcess
    ↓
decode next engine output
    ↓
next public DecisionFrame, terminal, interruption, or failure
~~~

EpisodicEnvironment::step validates that the selected public key belongs to
the current complete public candidate vector and resolves it through the
ephemeral public/private binding. It constructs an AcceptedActionTransition
with the current public decision index and selected public key, then
delegates actual response submission to the driver.

The driver submits the exact response bytes owned by the selected internal
candidate and repeatedly calls the core process until it reaches a new
interactive boundary, terminal, interruption, or failure. It does not
promise that the accepted public key is itself a terminal engine action.

The accepted transition proves submission attribution for the current
decision. It does not prove target selection, damage, immunity absence,
response absence, or terminal defeat.

## 8. Consequence for public lethal proof

The current path proves:

- phase 1 is a current engine-provided attackable-list entry;
- the visible source may be joined to current public entity data;
- current public ATK/DEF may be copied when present;
- the complete public candidate vector is preserved;
- the environment submits the exact engine response for the selected key.

It does not prove from the same current public candidate:

- that the candidate is already a complete attack declaration;
- whether the attack will be direct or targeted;
- which target will be selected;
- whether a later target/yes-no continuation will be required;
- whether an attack-cost or response chain can intervene;
- whether attack announcement will be canceled or replayed;
- what damage will resolve after modifiers and replacement effects;
- whether battle destruction or battle damage will occur;
- that the opponent will be unable to respond;
- that the current action guarantees terminal defeat.

The current public observation and candidate DTOs intentionally do not add
these facts. No private response oracle, hidden-effect query, future-domain
query, or CoreHost lookahead may be introduced to fill the gap.

Therefore:

~~~text
phase=1 attackable-list selection
    !=
PROVEN_LETHAL
~~~

The Task-2 snapshot remains valid and useful as descriptive evidence. The
positive lethal prerequisite is the missing proof boundary, not a defect in
the snapshot extractor.
