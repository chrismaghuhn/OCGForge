# WindBot Strategy Mining for an OCGForge Deterministic Teacher

**Research date:** 2026-08-28  
**OCGForge checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Status:** research/reference only; non-normative

## 1. Purpose

This report mines WindBot for game-AI concepts that may help OCGForge's first deterministic heuristic Teacher. It does **not** recommend embedding WindBot, copying its client mirror, translating executors line-for-line, or treating WindBot as rules authority.

The comparison question is narrow:

> Which ideas from WindBot's executor, target, material, battle, chain, combo-state, and resource-management code can be reused safely behind OCGForge's existing public policy boundary?

The answer is constrained by OCGForge's required interface:

```text
PublicEnvironmentObservation
+
complete ordered EnvironmentActionCandidate[]
        ↓
Teacher
        ↓
exactly one current public_action_key
```

WindBot was designed for a different boundary: a YGOPro/EDOPro client-side duel mirror, callbacks, and deck-specific executors. Its strengths and weaknesses must therefore be separated deliberately.

## 2. Source pins

### 2.1 Current research-reference repository heads

The following heads were inspected on 2026-08-28. They are **read-only research references**.

| Repository | Branch | Exact commit | Inspection role |
|---|---|---|---|
| [`ProjectIgnis/windbot`](https://github.com/ProjectIgnis/windbot) | `master` | `bffe6b62679c8b2fafea8f59740e03a132517da4` | Current executor architecture and deck code |
| [`ProjectIgnis/WindBot-Ignite`](https://github.com/ProjectIgnis/WindBot-Ignite) | `master` | `3539531b381316d1e045040c899bfa295a77a9f2` | Historical standalone project/readme context |
| [`edo9300/edopro`](https://github.com/edo9300/edopro) | `master` | `54ea755aa0243e2f18bb6bd2187fc9b2f7e29788` | Client/network context |
| [`edo9300/ygopro-core`](https://github.com/edo9300/ygopro-core) | `master` | `46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57` | Current public core/API reference |
| [`ProjectIgnis/CardScripts`](https://github.com/ProjectIgnis/CardScripts) | `master` | `3c7b16fc095e8e51f12f1f31c41bd853e7c1460b` | Current script-head comparison only |
| [`ProjectIgnis/BabelCDB`](https://github.com/ProjectIgnis/BabelCDB) | `master` | `bf9db8c27b466ba317ae171ea73598a34cc43818` | Current database-head comparison only |

### 2.2 OCGForge rules authority remains pinned

The current heads above do **not** replace the certified OCGForge environment. For the locked matchup, rules authority remains:

| Input | OCGForge pinned identity |
|---|---|
| ocgcore base | `9a0c558c2d686542f7914a6d529fd7aa57746aed` |
| CardScripts | `f337c87018ca723c1aded5143e616bb649555273` |
| BabelCDB | `89ad6837b0766a52984d8c715a7d5d4f8447946b` |
| repository patchset | `ocgforge.ocgcore.api_hardening.v1` |
| canonical rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |

All card-mechanics claims used for the exact Teacher profile were checked against the OCGForge-pinned CardScripts, not silently upgraded to current `master`.

## 3. WindBot architecture observed

### 3.1 High-level flow

**SOURCE FACT.** WindBot's design is centered on a client-side duel representation and deck-specific executor callbacks:

```text
EDOPro/YGOPro protocol
        ↓
mutable Duel / ClientField / ClientCard mirror
        ↓
ExecutorBase
├── GameAI callback selection state
├── AIUtil helpers
├── DefaultExecutor generic rules
└── deck-specific Executor
        ↓
ordered first-matching rule
        ↓
queued selections / direct protocol choice
```

This differs materially from OCGForge:

```text
ocgcore owns legality
        ↓
complete public semantic candidate domain
        ↓
perspective-safe public observation
        ↓
policy ranks all current candidates
        ↓
one public_action_key
```

### 3.2 `Executor`

Inspected source:

- [`ExecutorBase/Game/AI/Executor.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/ExecutorBase/Game/AI/Executor.cs)

**SOURCE FACT.** `Executor` owns or exposes:

- `Duel`;
- the bot and opponent `ClientField` values;
- `GameAI`;
- `AIUtil`;
- an ordered `Executors` collection;
- callbacks for action execution, hand choice, chain selection, card selection, materials, targets, positions, places, options, battle, and lifecycle events.

Deck executors register rules with `AddExecutor(...)`. Registration order is strategic priority. The first rule whose action/type/card and predicate match can become authoritative.

**Useful concept:** deck-specific knowledge is modularized behind an executor class.

**OCGForge limitation:** the executor owns a wider mutable duel/client boundary and first-match decision semantics. Neither is acceptable as the OCGForge policy contract.

### 3.3 `GameAI`

Inspected source:

- [`ExecutorBase/Game/GameAI.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/ExecutorBase/Game/GameAI.cs)

**SOURCE FACT.** `GameAI` coordinates executor traversal and staged callback selections. It contains selector state for later card, material, option, place, and position callbacks.

Important observed fallback behavior includes:

- selecting the first `min` cards when no explicit card selection is provided;
- selecting the first forced chain candidate;
- returning option `0` when no explicit option is queued;
- returning `positions[0]` when no explicit position is chosen;
- returning the first announced value when no explicit announcement is chosen;
- taking the first valid combinatorial result in several selection paths.

These fallbacks are pragmatic for a room bot. They are incompatible with OCGForge's requirement that every supported nonempty domain receive a documented deterministic strategic decision and that no implicit `candidate[0]` authority be hidden in an adapter.

### 3.4 `DefaultExecutor`

Inspected source:

- [`ExecutorBase/Game/AI/DefaultExecutor.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/ExecutorBase/Game/AI/DefaultExecutor.cs)

**SOURCE FACT.** `DefaultExecutor` provides broad generic logic:

- common card IDs and card-family exceptions;
- generic hand traps, negates, removal, setting, battle and reposition behavior;
- turn/chain memory;
- lists of resolved or currently affected cards;
- Infinite Impermanence column memory;
- Called by the Grave and similar temporal status tracking;
- generic opponent-card danger classifications;
- chain-target and already-reserved target checks.

**Useful concept:** repeated tactical ideas belong in a generic layer rather than being duplicated in every deck.

**OCGForge limitation:** the generic layer is coupled to mutable `ClientCard` objects, large hardcoded identity catalogs, broad special cases, and a callback execution model. OCGForge should extract dimensions, not code shape.

### 3.5 `AIUtil`

Inspected source:

- [`ExecutorBase/Game/AI/AIUtil.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/ExecutorBase/Game/AI/AIUtil.cs)

**SOURCE FACT.** `AIUtil` contains reusable-looking helpers for:

- problematic enemy monster/spell/card selection;
- strongest own/opponent monster selection;
- chain inspection;
- targetability and effect-state checks;
- material and card-choice preference lists;
- selection-count repair.

The broad target hierarchy often resembles:

```text
floodgate
→ dangerous card
→ difficult/invincible card
→ stronger battle body
→ generic strongest/first available target
```

**Useful concept:** threat evaluation should be factored into composable predicates and ordered threat classes.

**OCGForge limitation:** `CheckSelectCount`-style repair can append arbitrary first-available cards or trim a selection after the strategic callback. OCGForge must rank the legal continuations already supplied by the environment; it must never repair or fabricate a response outside the complete candidate domain.

## 4. Deck executors inspected

### 4.1 Current `SwordsoulExecutor`

Inspected source:

- [`Game/AI/Decks/SwordsoulExecutor.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/Game/AI/Decks/SwordsoulExecutor.cs)

#### Useful patterns

The executor demonstrates:

- explicit priority blocks for quick interaction, engine starters, Synchro payoffs, extenders, and fallback summons;
- deck-count tables used to avoid searching or sending cards that are no longer available;
- a Longyuan discard hierarchy;
- Taia banish/send priorities;
- Tenyi setup checks;
- public-board danger ranking;
- Wyrm-only restriction memory;
- effect-use and turn-reset memory;
- distinct going-first/going-second and Main Phase/Battle considerations;
- target reservation and negate-state tracking.

#### Critical mismatch with OCGForge

The executor's represented deck is not the exact locked OCGForge list. It references cards such as Baronne de Fleur, Nibiru, Maxx “C”, Pot of Desires, Crossout Designator, Psychic End Punisher, and other cards absent from `Swordsoul Tenyi ML v1`. Conversely, OCGForge's exact role/copy budget must be derived from its locked list.

Several selection lists are randomized before selecting a reveal, spell/trap zone, or otherwise equivalent candidate. The exact randomness/provenance behavior does not satisfy OCGForge's deterministic Teacher requirements.

#### Mining result

Use the resource and preference concepts. Reject the exact sequence, card list, randomization, and mutable client-object implementation.

### 4.2 Current `SalamangreatExecutor`

Inspected source:

- [`Game/AI/Decks/SalamangreatExecutor.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/Game/AI/Decks/SalamangreatExecutor.cs)

#### Useful patterns

The executor demonstrates:

- explicit tracking of Gazelle, Sanctuary, Wolf reincarnation, Miragestallio, Jaguar, Foxy, and other once-per-turn resources;
- material preference lists for Sunlight Wolf and other Links;
- Jack Jaguar recycle targets;
- linked-zone placement concerns;
- reincarnation Link Summon state;
- target selection for Rage and interaction;
- turn-reset lifecycle;
- separation of some engine and interaction priorities.

#### Critical mismatch with OCGForge

The executor reflects an older list and plan. It references Lady Debug, Fowl, Borrelsword, Violet Chimera, and other absent cards. It does not model the exact OCGForge package built around:

- Salamangreat of Fire;
- Weasel;
- Code of Soul;
- Raging Phoenix;
- Pyro Phoenix;
- Promethean Princess;
- Charge.

Its queued `SelectCard`, `SelectNextCard`, `SelectMaterials`, `SelectOption`, and `SelectPlace` calls prescribe later callbacks. That is brittle when the expected next domain changes after an interruption.

#### Mining result

Use explicit reincarnation/copy/resource state and material/target priorities. Reject the stale line script and callback queue.

### 4.3 `AlbazExecutor`

Inspected source:

- [`Game/AI/Decks/AlbazExecutor.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/Game/AI/Decks/AlbazExecutor.cs)

Useful patterns:

- explicit fusion target goals;
- material exclusion and priority lists;
- current-destroy/current-negate reservation;
- restriction and effect-use tracking;
- many fallback line checks.

Anti-patterns:

- large mutable state with implicit lifecycle;
- target/material values stored for later callbacks;
- hardcoded identities spread through the executor;
- ordered first-match rules with global priority coupling.

Mining conclusion: **ADAPT** the concept of explicit material objectives and preservation constraints; **REJECT** staged hidden callback authority.

### 4.4 `LabrynthExecutor`

Inspected source:

- [`Game/AI/Decks/LabrynthExecutor.cs`](https://github.com/ProjectIgnis/windbot/blob/bffe6b62679c8b2fafea8f59740e03a132517da4/Game/AI/Decks/LabrynthExecutor.cs)

Useful patterns:

- current-turn set/summoned/event tracking;
- avoiding duplicate destruction/negation targets;
- detailed interaction timing and resource recycling;
- turn/chain reset logic;
- targetability and threat-class checks.

Anti-patterns:

- very large flag/list surface;
- random list shuffling;
- mutable `ClientCard` identity across lifecycle events;
- difficult-to-audit interactions between executor order and state flags.

Mining conclusion: **ADAPT** explicit target reservation and public temporal facts; **REJECT** opaque mutable object graphs.

## 5. Pattern classification

The classification meanings are:

- `REUSE_CONCEPT` — sound domain idea compatible with OCGForge after clean-room implementation;
- `ADAPT` — useful, but ownership/interface/determinism must change materially;
- `REFERENCE_ONLY` — valuable example or test case, not a production pattern;
- `REJECT` — conflicts with OCGForge invariants.

| WindBot pattern | Classification | OCGForge interpretation |
|---|---|---|
| One deck-specific module per strategy | `REUSE_CONCEPT` | Immutable `StrategyProfile` per exact locked deck/policy artifact |
| Generic tactical helpers shared across decks | `REUSE_CONCEPT` | Generic `TeacherCore` evaluators |
| Explicit action-priority knowledge | `ADAPT` | Score components/goal classes over every candidate, not first-match authority |
| Threat tiers: floodgate, dangerous, battle threat | `REUSE_CONCEPT` | Public-state threat dimensions and deterministic tiers |
| Search/send/discard preference lists | `ADAPT` | Centralized role/resource preservation modifiers in profile |
| Material preference/exclusion lists | `ADAPT` | Rank only legal material continuation candidates; no independent legality |
| Current destroy/negate target reservation | `REUSE_CONCEPT` | Public-safe chain-local reservation facts to avoid waste |
| Once-per-turn and temporary restriction memory | `ADAPT` | Explicit episode-local ledger reconciled against current public observation/history |
| Turn/chain lifecycle callbacks | `ADAPT` | Explicit reset/expiry transitions tied to accepted environment progression |
| Going-first/going-second distinctions | `REUSE_CONCEPT` | Strategic goal modes selected from public state and experiment context |
| Linked-zone placement awareness | `REUSE_CONCEPT` | Generic zone utility plus deck profile modifiers |
| Reincarnation-summon memory | `REUSE_CONCEPT` | Explicit public resource/goal fact for Salamangreat profile |
| Deck remaining-count estimates | `ADAPT` | Use exact own locked deck plus perspective-authorized own draws/searches/history; never inspect hidden order |
| Card-specific choke-point lists | `ADAPT` | Matchup/profile interaction map with public investment/replacement context |
| Battle helpers based on ATK/DEF and phase | `ADAPT` | Deterministic lethal/safety/pressure evaluator, not generic first attack |
| Executor registration order | `REFERENCE_ONLY` | Documents strategic precedence, but must be normalized into explicit score dimensions |
| Large exact combo callback sequence | `REFERENCE_ONLY` | Mine line goals and dependencies only |
| Mutable `ClientCard` references | `REJECT` | No policy identity based on engine/client objects or pointers |
| Full mutable client duel mirror as policy input | `REJECT` | Teacher receives only OCGForge public frame |
| Candidate/selection repair by appending first items | `REJECT` | Environment domain is complete and authoritative; no repair |
| First option/position/card/forced-chain fallback | `REJECT` | Every candidate receives documented utility; canonical key only final equality tie-break |
| Pre-queued target/material/option callbacks | `REJECT` | Re-evaluate every continuation frame after observing the current public state |
| Randomly shuffled equivalent choices | `REJECT` for v1 | Deterministic exact ordering; any future stochastic policy needs explicit policy RNG provenance |
| Card IDs scattered through generic logic | `REJECT` | Centralize identities/roles in immutable deck profile |
| Opaque flags without explicit reset/acceptance ownership | `REJECT` | Versioned state schema, reconciler, reset and accepted-transition gates |
| Hidden opponent-card or secret-state access | `REJECT` | Privacy BLOCKER even if available in a client/core mirror |
| Candidate vector index as action authority | `REJECT` | Return current `public_action_key` only |

## 6. Detailed reusable concepts

### 6.1 Explicit strategic precedence

WindBot's ordered executors make strategic precedence visible in code. The useful idea is not “first rule wins”; it is that policy behavior needs an explicit hierarchy:

```text
survive / take lethal
→ guaranteed lethal
→ mandatory interruption
→ active plan/recovery
→ board breaking
→ resource conversion
→ follow-up/grind
→ safe fallback
```

OCGForge should express that hierarchy as frozen lexicographic score dimensions so every candidate still participates.

### 6.2 Threat classification

WindBot repeatedly distinguishes:

- floodgates;
- dangerous effect monsters;
- difficult-to-remove bodies;
- strongest battle bodies;
- face-up vs face-down threats;
- already disabled or already targeted cards.

OCGForge should preserve the abstraction but only from public observations. A face-down candidate may be “unknown set interaction” or “occupied zone”; it may never inherit its hidden card identity from a client mirror.

### 6.3 Resource-preserving selection

Deck executors contain valuable examples of:

- preferring duplicate or spent cards as discard/material;
- retaining starters or follow-up;
- selecting graveyard resources that trigger another effect;
- conserving Extra Deck copies;
- choosing targets that are not already being destroyed/negated;
- respecting temporary deck locks.

These should become explicit feature dimensions and role tags. They should not remain implicit list order.

### 6.4 Temporal memory

WindBot demonstrates that a competent Yu-Gi-Oh! policy needs more than a snapshot:

- effects used this turn;
- temporary restrictions;
- current chain targets;
- whether an engine bridge resolved;
- whether a line-specific summon occurred;
- whether a reusable trap or resource was recovered.

OCGForge should use bounded strategy memory, but memory is subordinate to the current public observation and may advance only after an accepted action. Every fact needs scope:

```text
chain
turn
phase
while-card-remains-public
until-knowledge-destroying-transition
episode
```

### 6.5 Recovery intent

WindBot often contains fallback predicates after a primary route becomes unavailable. That is useful evidence that exact combo scripts are insufficient.

OCGForge should model recovery explicitly:

```text
active line precondition fails
→ record public-safe invalidation reason
→ evaluate declared recovery edges
→ choose reachable goal
→ re-rank complete current domain
```

It should not reproduce fallback by adding another later executor whose dependence on earlier failed state is implicit.

## 7. Explicit anti-pattern analysis

### 7.1 Omniscient or over-wide client state

WindBot executors can navigate `Duel`, `Bot`, `Enemy`, `ClientCard`, locations, chains, and client events directly. The architecture is not a versioned perspective-safe model boundary comparable to OCGForge's `PublicEnvironmentObservation`.

This does not prove that every WindBot path reads a hidden opponent card ID; clients often redact unknown identities. It does prove that privacy is enforced by the client/protocol representation rather than by an OCGForge-style policy contract.

**OCGForge classification:** `REJECT`. A future Teacher must not receive the wider mirror at all.

### 7.2 Mutable `ClientCard` identity

Executors retain `ClientCard` objects in lists such as current negates, destroyed targets, materials, or used effect bodies. Object identity can survive through callbacks and locations according to client implementation details.

**OCGForge risk:** hidden physical-card tracking, pointer/object identity dependence, stale references, nondeterministic semantics, and unclear identity destruction at shuffle boundaries.

**Classification:** `REJECT`. Store only public semantic facts with explicit validity scope.

### 7.3 Deck-specific legality assumptions

Executor predicates frequently infer whether a future summon/material/activation line is possible from local client state and hardcoded card logic.

**OCGForge risk:** a second incomplete legality engine and stale future assumptions.

**Classification:** `REJECT` as legality. A profile may state a preference conditional on a candidate the engine already supplied.

### 7.4 Automatic candidate selection and repair

First-item fallbacks and selection-count repair keep a room duel running.

**OCGForge risk:** fabricated or undocumented strategic decisions, candidate index authority, low-quality labels, and audit ambiguity.

**Classification:** `REJECT`.

### 7.5 Implicit priority ordering

Registration order across hundreds of executors becomes hidden global policy. Adding an earlier rule can alter many unrelated states.

**OCGForge risk:** brittle maintenance, difficult review, unclear utility tradeoffs, and insufficient provenance granularity.

**Classification:** `ADAPT` precedence into explicit frozen score bands; reject first-match authority.

### 7.6 Global/static mutable state and RNG

WindBot code contains local `Random` construction and uses shared random helpers such as `Program.Rand` in deck executors. Some target/placement lists are deliberately shuffled.

The inspected code does not provide OCGForge's required immutable policy-RNG artifact and per-decision provenance. Even behavior intended to be “deterministic enough” for a bot cannot be admitted as OCGForge deterministic evidence without exact seeding/state identity.

**Classification:** `REJECT` for Teacher v1. Use no policy RNG.

### 7.7 Hardcoded card IDs everywhere

Large generic and deck executors mix identity constants, tactical exceptions, and line logic.

**OCGForge risk:** duplicate meanings, silent copy/list drift, impossible profile auditing, and changes without artifact identity.

**Classification:** `ADAPT` identities into a centralized immutable role catalog; reject generic ID sprawl.

### 7.8 Exact sequence scripts and queued selections

Selecting an activation and then enqueuing expected materials, targets, options, and places assumes later callbacks and domains.

**OCGForge risk:** stale plan after a negate/removal, hidden state transitions, candidate reconstruction, and inability to explain recovery.

**Classification:** `REJECT`. Every continuation frame is a fresh public decision under the active plan.

### 7.9 Flags with unclear lifecycle

Flags such as “summoned,” “only Wyrm Special Summon,” “Wolf summoned using itself,” and lists of activated IDs are useful, but lifecycle ownership is distributed among callbacks.

**OCGForge risk:** cross-episode leakage, state advancing after rejected action, forgotten expiry, and replay mismatch.

**Classification:** `ADAPT` into typed facts with explicit chain/turn/episode scope and acceptance semantics.

### 7.10 Non-versioned behavior

A source commit identifies code, but WindBot executor behavior is not expressed as an immutable OCGForge `PolicyArtifact` with exact profile, preference, fallback, and tie-break identity.

**Classification:** `REJECT` for trusted trajectory attribution; repackage clean-room Teacher logic as immutable content/versioned policy artifacts.

## 8. Target-selection mining

### 8.1 What to reuse

WindBot demonstrates that target choice benefits from separate questions:

1. Is this target a floodgate or immediate engine blocker?
2. Is it an active dangerous effect source?
3. Is it difficult to remove later?
4. Is it a battle obstacle?
5. Is it already negated, spent, or reserved for another effect?
6. Is the effect likely to resolve against it under known public properties?

### 8.2 What to improve

OCGForge should not collapse these into one `GetProblematicEnemyCard()` result. It should create an integer target vector for every legal target candidate and add deck/matchup modifiers.

Target utility should include public opponent investment and replacement routes. For example, negating Gazelle after the opponent has already spent a normal summon and triggered its special summon may differ from negating it when another public extender remains. The profile must not use a hidden hand oracle.

## 9. Material/cost-selection mining

### 9.1 What to reuse

WindBot deck code repeatedly encodes:

- preferred materials;
- protected materials;
- duplicate vs unique cards;
- graveyard benefits;
- Extra Deck copy recycling;
- effect-use state;
- restrictions caused by the selected bridge.

### 9.2 What to improve

OCGForge should rank legal continuation candidates with a generic preservation vector:

```text
consume spent/duplicate/recursive resource
preserve unique starter
preserve extender after interruption
preserve interaction
preserve follow-up
preserve required copy/zone/restriction compatibility
```

A material list is not a legal-material generator. The complete domain remains the sole legal source.

## 10. Interaction-timing mining

WindBot's common logic recognizes that not every counterable effect should be negated. Useful concepts include:

- the current chain source;
- whether the opponent is responding to a low-value own effect;
- whether the target is already negated;
- phase/timing;
- whether a stronger known public choke point is likely to follow;
- whether interaction is scarce;
- whether the current effect produces immediate danger.

OCGForge should turn these into named, auditable score components:

```text
resolution_threat
engine_criticality
public_investment
replacement_availability
future_choke_value
interaction_scarcity
lethal_risk
chain_effectiveness
```

The first Teacher should remain deterministic and non-probabilistic.

## 11. Battle-logic mining

WindBot's default and deck-specific battle code is useful as a corpus of tactical cases:

- strongest/weakest body selection;
- dangerous target removal by battle;
- attack/defense repositioning;
- Main Phase 2 preservation;
- battle-trigger awareness;
- lethal-oriented boss selection.

OCGForge should reuse only the case taxonomy. It needs a complete-domain battle evaluator that considers every legal attack/target/phase action and emits an explanation. A generic strongest-monster-first rule is insufficient.

## 12. Determinism review

| Risk | WindBot evidence | OCGForge requirement |
|---|---|---|
| Rule ordering | First matching executor is authoritative | Explicit canonical score layout; all candidates evaluated |
| Utility ties | Often resolved by first item/list order | Public key only final exact-equality tie-break |
| Unordered containers | LINQ/list/dictionary use varies | Canonical iteration/sorting for non-semantic collections |
| Floating point | Not the principal executor issue | Avoid entirely in Teacher v1 |
| Randomness | Random/list shuffling appears in executor paths | No policy RNG in v1; future RNG must be versioned/provenanced |
| Plan transitions | Implicit in flags/callback queues | Typed plan/recovery transition and accepted-action commit |
| Strategy memory | Mutable objects/flags | Episode-local, participant-isolated, observation-reconciled facts |
| Candidate iteration | First-match/first-valid | Complete ordered domain with exact evaluation count |

## 13. Privacy review

### BLOCKER — Copying the WindBot policy input boundary

A Teacher with `Duel`/`ClientField`/`ClientCard` access would bypass OCGForge's model-facing privacy contract.

### BLOCKER — Copying callback selection repair/defaults

Automatic first-card/option/material selection would create undocumented labels and weaken complete-domain authority.

### BLOCKER — Retaining hidden physical-card identity

Any client object, pointer, or locator that persists identity through a knowledge-destroying transition is forbidden.

### MAJOR — Copying current deck executors as strategy truth

Both relevant executors materially differ from the exact locked lists. Their lines cannot be imported as exact Teacher plans.

### MAJOR — Copying executor-order semantics

Order is useful research evidence but produces brittle hidden priority when used as final policy authority.

### MAJOR — Copying mutable lifecycle flags directly

The concepts are required; the untyped lifecycle is not.

## 14. Recommended clean-room translation

WindBot concept:

```text
AddExecutor(Activate, card_id, predicate)
AI.SelectCard(priority_list)
AI.SelectMaterials(material_list)
```

OCGForge clean-room equivalent:

```text
for candidate in complete_public_domain:
    facts = extract_public_candidate_facts(candidate)
    line_features = profile.match_line_edges(public_state, strategy_state, facts)
    tactical = core.evaluate_tactical(public_state, facts)
    resource = core.evaluate_resource_cost(profile, public_state, facts)
    candidate.score = deterministic_vector(line_features, tactical, resource, ...)

selected = exact_argmax(all_candidates)
return selected.public_action_key
```

No WindBot callback queue, client object, fallback index, or legality predicate crosses this boundary.

## 15. Final WindBot answer

### Concepts to borrow

- deck-specific strategy modules;
- centralized generic tactical evaluation;
- explicit strategic priority;
- threat tiers;
- target reservation;
- target/material/discard/search preference concepts;
- public temporal resource/use/restriction memory;
- going-first/going-second modes;
- linked-zone and Extra Deck copy awareness;
- recovery/fallback case mining;
- battle and interaction scenario corpus.

### Architecture/behavior to reject

- mutable full client mirror as Teacher input;
- `ClientCard` identity and object lifetime;
- internal/hidden data access;
- deck-specific legality assumptions;
- ordered first-match final authority;
- pre-queued selection callbacks;
- automatic first-item and repair fallbacks;
- candidate index authority;
- random list/zone/material ordering without exact provenance;
- hardcoded identity sprawl;
- opaque mutable flags;
- exact stale combo scripts;
- behavior changes without immutable policy/profile identity.

### Bottom line

WindBot is a valuable **strategy-pattern mine and adversarial test corpus**. It is not a suitable OCGForge Teacher substrate. The reusable unit is a tactical or resource concept, translated into public-state features and deterministic complete-domain scores—not an executor method or queued action sequence.
