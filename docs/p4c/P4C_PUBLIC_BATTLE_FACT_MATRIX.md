# OCGForge Phase 4C — Public Battle Fact Matrix

Status: **CURRENT / AUTHORIZED — DOCS-ONLY DESIGN MATRIX**

This matrix records what a future implementation may consume from the
accepted public boundary. It is not a runtime registry, does not execute a
gate, and does not authorize production code.

The frozen derived identities are:

~~~text
ocgforge.public_battle_snapshot.v1
ocgforge.provable_lethal.v1
~~~

## Method and authority

The source authority is the accepted
`PublicEnvironmentObservation`/`PublicSafeStateView` projection and the
complete ordered `EnvironmentActionCandidate[]` published by
`ygo::environment`. The pinned ocgcore/rules bundle remains authoritative for
legality and battle resolution.

`DIRECT` means the value is copied or selected from an already public,
perspective-safe value. `SAFE_DERIVATION` means a deterministic, checked
derivation over such values that does not claim engine resolution. `BLOCKED`
means the current accepted public boundary does not prove the capability.
`UNSUPPORTED` is the runtime result for a required value or proof that is not
available; `INVALID` is reserved for malformed or contradictory public input.

No row permits private engine access, hidden-card inference, candidate
filtering, candidate reordering, or a negative conclusion based only on
missing candidates.

## Public and safely derived facts

| Fact/capability | Source | Classification | Exact derivation rule | Privacy rationale | Determinism rationale | Failure behavior | Future consumer |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Perspective player | `PublicEnvironmentObservation.perspective_player` | DIRECT | Copy the public participant value and retain its existing validation. | Already public ownership context; no other participant view is read. | Copy a scalar; no iteration or fallback. | Malformed participant value is `INVALID`. | Battle snapshot routing |
| Self life points | Same-perspective `PublicSafeStateView.globals().life_points` | DIRECT | Select the entry for the snapshot perspective. Do not infer from events or damage. | Life points are explicitly public in the safe state. | Use the ordered public life-point vector and fixed perspective mapping. | Missing or inconsistent entry is `UNSUPPORTED` or `INVALID` if malformed. | Descriptive battle context; future proof precondition |
| Opponent life points | Same public life-point vector | DIRECT | Select the opponent entry for the current perspective in a two-player duel. | No hidden state is consulted. | Fixed participant mapping. | Missing or inconsistent entry is `UNSUPPORTED` or `INVALID` if malformed. | Descriptive context; lethal proof input when otherwise sufficient |
| Turn phase | `PublicSafeStateView.globals().phase` | DIRECT | Copy the optional public phase value without treating it as an ordered time scale. | Phase is a public global. | Scalar copy. | Absent phase is `UNSUPPORTED` for a phase-dependent feature. | Battle context classification |
| Decision context kind | `PublicEnvironmentObservation.public_decision_context.kind` | DIRECT | Copy the optional canonical public request-family token. | Context is already projected safe. | Canonical token bytes are stable. | Malformed token is `INVALID`; absent value is `UNSUPPORTED` where required. | BattleCommand shape checks |
| Candidate action kind | `EnvironmentActionCandidate.action_kind` | DIRECT | Copy the exact public enum value from each supplied candidate. | No internal action or response is consulted. | Preserve supplied candidate order. | Unknown/malformed enum is `INVALID`; non-battle kind is `NOT_APPLICABLE` to battle facts. | Candidate classification |
| Candidate public action key | `EnvironmentActionCandidate.public_action_key` | DIRECT | Copy the existing key exactly; require valid and unique keys across the supplied domain. | Public key is the existing safe candidate identity. | No key regeneration, sorting, or vector-index identity. | Invalid or duplicate key is `INVALID`; never repair or deduplicate. | Snapshot and lethal result identity |
| Candidate source reference kind | Candidate `source_reference.kind` | DIRECT | Copy `VisibleCard` or `RedactedSlot` only when the candidate supplies the public reference. | Kind is already public-safe. | Copy the typed value. | Malformed reference is `INVALID`; absent is absence, not direct-attack proof. | Public entity join |
| Candidate target reference kind | Candidate `target_reference.kind` | DIRECT | Copy the public target-reference kind when present. | No hidden target is recovered. | Copy the typed value. | Malformed reference is `INVALID`; absent is not a direct-attack claim. | Public entity join |
| Visible entity controller | Exact entity in same-perspective `PublicSafeStateView.entities` | DIRECT | Resolve an exact `observation_locator`; copy public controller. | Same-perspective public state only. | Exactly one locator match; no map-order dependency. | Missing/duplicate locator is `INVALID`; redacted identity-dependent join is `UNSUPPORTED`. | Candidate context |
| Visible entity zone | Exact public entity zone | DIRECT | Resolve exact locator and copy the public zone. | Zone is part of the safe projection. | Scalar copy after exact join. | Missing/duplicate join is `INVALID`; unavailable entity is `UNSUPPORTED`. | Source/target context |
| Visible entity position | Exact public entity position | DIRECT | Resolve exact locator and copy position when present. | Position is public only for the projected entity. | Scalar copy. | Missing position is `UNSUPPORTED` for a position-dependent feature; inconsistent join is `INVALID`. | Battle candidate facts |
| Visible entity current ATK when present | Exact public entity `current.attack` | DIRECT | Resolve one visible entity and copy its current public ATK; do not use printed/database/CardScripts values. | Current value is restricted to the public projection. | Scalar copy; no external catalog lookup. | Absent current ATK is `UNSUPPORTED`; contradictory visibility/join is `INVALID`. | Stat feature and future proof |
| Visible entity current DEF when present | Exact public entity `current.defense` | DIRECT | Resolve one visible entity and copy its current public DEF; do not reconstruct it. | No hidden or private stat source. | Scalar copy. | Absent current DEF is `UNSUPPORTED`; contradictory visibility/join is `INVALID`. | Stat feature and future proof |
| Visible zone counts | `PublicSafeStateView.zones` | DIRECT | Copy the public zone records and their counts without inferring card identities. | Counts are already perspective-safe and identity-redacted as required. | Preserve canonical zone ordering from the safe state. | Malformed/duplicate zone records are `INVALID`; missing required count is `UNSUPPORTED`. | Contextual battle features |

## Safe derivations

| Fact/capability | Source | Classification | Exact derivation rule | Privacy rationale | Determinism rationale | Failure behavior | Future consumer |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Candidate is a BattleCommand | Candidate `action_kind` | SAFE_DERIVATION | `action_kind == EnvironmentActionKind::BattleCommand` yields a descriptive battle-family flag. | Uses only the public enum. | Boolean over the supplied candidate. | Malformed action kind is `INVALID`; otherwise false is `NOT_APPLICABLE` for battle. | Snapshot class selection |
| Visible source stat availability | Candidate source reference plus exact public entity | SAFE_DERIVATION | For `VisibleCard`, exact same-perspective locator join; report available only when the requested current stat is present. | No passcode/database/private lookup. | Exactly one match and scalar option state. | Missing/duplicate join `INVALID`; missing stat `UNSUPPORTED`; `RedactedSlot` `UNSUPPORTED`. | Candidate-local stat facts |
| Visible target stat availability | Candidate target reference plus exact public entity | SAFE_DERIVATION | Same exact join rule as source, for the requested current ATK/DEF. | Same-perspective only. | No unordered iteration authority. | Missing/duplicate join `INVALID`; missing stat `UNSUPPORTED`; redacted target `UNSUPPORTED`. | Candidate-local stat facts |
| Public ATK-vs-ATK delta | Current visible source ATK and target ATK | SAFE_DERIVATION | Compute checked `source_current_attack - target_current_attack` only when the future classifier has proven the comparison context. | Only public current stats are used. | Fixed signed arithmetic; no floating point. | Missing stats `UNSUPPORTED`; unrepresentable subtraction `INVALID`. | Descriptive stat margin |
| Public ATK-vs-DEF delta | Current visible source ATK and target DEF | SAFE_DERIVATION | Compute checked `source_current_attack - target_current_defense` only for a proven public comparison context. | No engine rule or hidden modifier is queried. | Fixed signed arithmetic. | Missing stats `UNSUPPORTED`; unrepresentable subtraction `INVALID`. | Descriptive stat margin |
| Public stat margin | One of the explicitly proven public stat comparisons | SAFE_DERIVATION | Store the checked signed result as a margin, never as damage or outcome. | Arithmetic reveals no hidden identity. | Fixed operand order and checked integer path. | Ambiguous comparison context `UNSUPPORTED`; overflow `INVALID`. | Evaluation diagnostic only |
| Current candidate-domain BattleCommand count | Complete supplied candidate vector | SAFE_DERIVATION | Count entries whose public `action_kind` is BattleCommand, preserving the original domain. | No missing candidate is inferred or queried. | One linear pass in supplied order; count is not identity. | Malformed domain `INVALID`; count is never a legality/completeness oracle. | Descriptive feature only |

## Blocked classifications and proof capabilities

| Fact/capability | Source | Classification | Exact derivation rule | Privacy rationale | Determinism rationale | Failure behavior | Future consumer |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Exact battle candidate class: attack declaration | Current BattleCommand decoder shape | BLOCKED | The current public fields do not freeze an exact attack-declaration equivalence class independent of engine internals. | Avoids interpreting private command details. | No phase/name heuristic is authoritative. | `UNSUPPORTED` until a narrow shape contract is proven. | Future snapshot implementation |
| Exact battle candidate class: direct attack | Candidate target absence plus public fields | BLOCKED | Never equate absent `target_reference` with direct attack. | Prevents hidden target/legality inference. | No guessed classification. | `UNSUPPORTED`; absence is not a negative or positive direct-attack proof. | Future battle classifier |
| Exact battle candidate class: targeted attack | Candidate source/target references | BLOCKED | Do not infer a resolved target or attack subtype from optional references alone. | Target visibility may be incomplete. | Requires explicit future decoder proof. | `UNSUPPORTED` unless exact shape is proven. | Future battle classifier |
| Exact battle candidate class: battle-phase control | BattleCommand phase marker | BLOCKED | Do not treat decoder phase markers as an ordered time scale or complete control classification. | Avoids private protocol interpretation. | Numeric marker is descriptive only. | `UNSUPPORTED` until proven. | Future battle classifier |
| Hidden face-down ATK/DEF | Hidden entity/private engine state | BLOCKED | No lookup or reconstruction is permitted. | Protects hidden identity and information-destroying boundaries. | No hidden-dependent output. | `UNSUPPORTED`; never infer a value. | None in v1 |
| Opponent hidden hand identity | Private opponent state | BLOCKED | Never read, hash, or infer hidden hand entries. | Information-safety boundary. | Output independent of hidden state. | `UNSUPPORTED`; paired worlds must agree. | None in v1 |
| Hidden deck order | Private deck state | BLOCKED | Never consume deck order or derive future draws. | Protects hidden strategy information. | No private iteration. | `UNSUPPORTED`. | None in v1 |
| Hidden Extra Deck identity | Private/face-down Extra Deck state | BLOCKED | Never resolve hidden Extra Deck slots. | Prevents identity leakage. | No hidden identity in keys or reasons. | `UNSUPPORTED`. | None in v1 |
| Physical identity after shuffle | Internal card identity/locator history | BLOCKED | A later hidden slot is not joined to a pre-shuffle physical card. | Knowledge-destroying transition is a privacy boundary. | No persistent physical identity. | `UNSUPPORTED` and any attempted carry-over is invalid evidence. | None in v1 |
| Private effect-use state | Raw engine/private state | BLOCKED | Do not infer whether an effect was used from absence or prior history outside the public contract. | Protects private engine state. | No hidden mutable state. | `UNSUPPORTED`. | Future narrow public fact |
| Raw engine battle state | CoreHost/ocgcore query | BLOCKED | Battle features consume only the public observation and candidate vector. | Maintains layer boundary. | No pointer/provider/process dependence. | `UNSUPPORTED`/`BLOCKED`; no shortcut. | None in v1 |
| Monster already attacked | Candidate absence or private attack history | BLOCKED | No inference from a missing attack candidate or an unexposed attack-use flag. | Avoids private state reconstruction. | Absence does not change semantics. | `UNSUPPORTED`; never a non-lethal claim. | Future public fact if exact |
| Remaining number of attacks | Private effect/turn state | BLOCKED | No count is inferred from prior candidates or card identity. | Hidden effect-use/turn information remains private. | No future-state simulation. | `UNSUPPORTED`. | Future public fact if exact |
| Future attack availability | Future candidate domain | BLOCKED | Only the current supplied domain is visible; no future domain is assumed. | No future private state. | Current decision only. | `UNSUPPORTED`; no queue/search. | None in v1 |
| Future attack target availability | Future target domain | BLOCKED | Do not assume a target remains after the current action. | Avoids hidden future state. | No future enumeration. | `UNSUPPORTED`. | None in v1 |
| Generic can-attack-directly state | Rules inference from names/target absence | BLOCKED | No generic direct-attack permission is reconstructed in the feature layer. | Requires rules/private context not public here. | No heuristic predicate. | `UNSUPPORTED`. | Future exact contract |
| Future direct-attack permission | Future engine/rules state | BLOCKED | Do not assume a later direct-attack permission. | No future or hidden state. | No speculative sequence. | `UNSUPPORTED`. | None in v1 |
| Piercing damage | Card/effect/rules resolution | BLOCKED | No piercing property is inferred from card identity or printed text. | Public identity does not authorize effect simulation. | No duplicate rules engine. | `UNSUPPORTED`. | Future narrow equivalence |
| Double-battle-damage state | Card/effect/rules resolution | BLOCKED | No damage multiplier is assumed. | Hidden/public modifiers may intervene. | No optimistic arithmetic. | `UNSUPPORTED`. | Future narrow equivalence |
| Battle-damage replacement/modification | Effect/rules resolution | BLOCKED | No replacement or modifier is inferred absent an exact public proof. | Avoids opponent/private effect assumptions. | No speculative branch. | `UNSUPPORTED`. | Future narrow equivalence |
| Battle destruction immunity | Card/effect/rules resolution | BLOCKED | Current stats do not prove destruction. | Identity/effect state may be incomplete. | No resolution recreation. | `UNSUPPORTED`. | Future narrow equivalence |
| Battle damage immunity | Card/effect/rules resolution | BLOCKED | Current stats do not prove damage can be applied. | No private effect state. | No optimistic lower bound. | `UNSUPPORTED`. | Future narrow equivalence |
| Temporary combat modifiers absent publicly | Public events/effects not sufficient | BLOCKED | Do not assume absent modifiers merely because they are not in the current snapshot. | Absence of a public field is not proof of absence in the engine. | Fail closed. | `UNSUPPORTED`. | Future public/equivalence contract |
| Future opponent chain/response availability | Future opponent decision | BLOCKED | Do not assume opponent passes or that no response window can intervene. | Protects opponent private/response information. | No future simulation. | `UNSUPPORTED`/`NOT_PROVEN`. | Provable lethal prerequisites |
| Hidden interaction availability | Private opponent state | BLOCKED | No hidden hand/effect availability is inferred. | Direct privacy boundary. | Paired hidden worlds must match. | `UNSUPPORTED`. | None in v1 |
| Multi-action lethal sequence | Future action sequence | BLOCKED | No attack A then attack B plan, future target assumption, or queue. | No future/private state. | Current-action-only contract. | `NOT_PROVEN` or `UNSUPPORTED`. | Future separate architecture |
| Future-action queue | Policy/controller queue | BLOCKED | No battle feature may emit or consume queued future actions. | Keeps derived facts outside gameplay control state. | No scheduling-dependent behavior. | `UNSUPPORTED`; no queue is created. | None in v1 |
| Search over private future states | CoreHost/MCTS/rollout | BLOCKED | No private-state search or omniscient simulation. | Prevents hidden information leakage. | No RNG/thread/path dependence. | `UNSUPPORTED`; fail closed. | None in v1 |
| Exact Extra Deck copy budget | Hidden physical/card-count state | BLOCKED | Do not infer remaining copies from generic entity counts or deck lists. | Copy identity/count may not be public. | No hidden count. | `UNSUPPORTED`. | Future registered public fact |
| Battle damage | Battle resolution | BLOCKED | A stat margin is never relabeled as damage. | Resolution may depend on hidden/public effects. | No heuristic conversion. | `NOT_PROVEN`/`UNSUPPORTED`. | Provable lethal evaluator |
| Successful attack resolution | Engine transition/result | BLOCKED | Current candidate presence and stats do not prove resolution. | No response/engine shortcut. | Current input only. | `NOT_PROVEN`/`UNSUPPORTED`. | Future result contract |
| Card destruction | Engine resolution | BLOCKED | Do not equate a stat comparison with destruction. | Immunities and effects may be unavailable. | No duplicated rules logic. | `NOT_PROVEN`/`UNSUPPORTED`. | Future evaluator |
| Terminal victory | Engine terminal state/complete resolution | BLOCKED | Do not infer victory from current ATK, LP, or candidate class. | Terminal result belongs to environment/rules. | No optimistic terminal claim. | `NOT_PROVEN`/`UNSUPPORTED`. | Future evaluator |
| Guaranteed lethal | Complete current-action proof | BLOCKED | Requires every positive-proof condition in `ocgforge.provable_lethal.v1`; no generic v1 path is enabled by this matrix alone. | No hidden response or modifier assumption. | Checked, complete, deterministic proof only. | `NOT_PROVEN` or `UNSUPPORTED`; never `PROVEN_LETHAL` without proof. | Future ProvableLethal evaluator |

## Consequences for future implementation

The matrix deliberately leaves the exact attack/direct/targeted/control
classification and generic lethal proof blocked until a future task supplies
an exact public decoder-shape and proof-equivalence contract. A candidate
domain count is descriptive only and must never become a second legality or
completeness oracle.

For all derived records:

- candidate facts retain exact supplied order and one-to-one membership;
- reason IDs are canonical sorted/unique tokens;
- public locators are transient join inputs only;
- `RedactedSlot` stat lookup is `UNSUPPORTED`;
- signed arithmetic is checked and overflow is `INVALID`;
- `NOT_PROVEN` never means proven non-lethal.

No Phase-4C matrix row is executed or marked PASS by this Task 1.
