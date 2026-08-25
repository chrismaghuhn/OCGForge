# OCGForge Research Report
# EDOPro / WindBot Capability Mining and Architecture Gap Analysis

**Research date:** 2026-08-25  
**Scope:** research / analysis / recommendation only; no production changes performed.  
**Primary repository:** https://github.com/chrismaghuhn/OCGForge

---

## Executive Summary

### Bottom line

**Final recommendation: B — EDOPro/WindBot contain several targeted high-value references, but no architecture should be adopted wholesale.**

OCGForge should **not** have forked EDOPro. EDOPro solves a different product problem: it is an interactive network/local duel client that mirrors state for human presentation, replay viewing and network play. OCGForge instead needs a deterministic, headless, fail-closed semantic boundary suitable for game-AI research. The most reusable Project Ignis value lies in accumulated protocol knowledge, response encoding, edge cases, replay experience, and WindBot's handcrafted Yu-Gi-Oh! heuristics—not in EDOPro's UI architecture or WindBot's mutable client-state model.

The five most valuable things Project Ignis can teach OCGForge are:

1. **Interactive ocgcore protocol knowledge**, especially response encodings and high-complexity selection families.
2. **The long tail of player-facing decision edge cases** that can become fail-closed blockers under support expansion.
3. **Replay design tradeoffs**, particularly response-driven core re-execution versus message-stream playback.
4. **Handcrafted Yu-Gi-Oh! domain knowledge** in WindBot executors: combo state, target/material priorities, threat evaluation and resource management.
5. **Visibility-transition test cases** around shuffles, reveals, face-down state, Extra Deck state, overlays, replay and spectator catch-up.

OCGForge should **absolutely not copy** mutable `ClientCard` identity as semantic identity, omniscient/replay state as player observation, UI prompt lists as authoritative legal domains, heuristic auto-selection, process-global/wall-clock RNG, floating rules inputs, or deck-specific assumptions inside legality.

WindBot is worth using later as a **benchmark/reference and source of heuristic ideas**, but not as a direct teacher until it is adapted to consume exactly the same perspective-safe `PlayerObservation` and legal `ActionCandidate` domain as a model.

### Highest-return one-week activity

> **Build a pinned differential decision-protocol corpus and coverage ledger for the highest-risk interactive ocgcore message families.**

Compare:

```text
pinned ocgcore behavior
vs
EDOPro interpretation/response encoding
vs
OCGForge DecisionRequest / ActionCandidate / response encoding
```

Focus first on `MSG_SELECT_SUM`, `MSG_SELECT_COUNTER`, `MSG_SELECT_UNSELECT_CARD`, `MSG_SORT_CARD`, `MSG_SORT_CHAIN`, `MSG_SELECT_TRIBUTE`, `MSG_SELECT_PLACE`, `MSG_SELECT_DISFIELD`, announce families, forced/pass chain cases and packed idle/battle commands.

This produces permanent correctness evidence, reduces future fail-closed risk, and builds the semantic mapping foundation required by any later WindBot benchmark or teacher.

---

# Repository identities

The following default-branch heads were inspected during the live research session. These are **research reference identities**, not a migration recommendation.

| Repository | Branch | Inspected commit |
|---|---|---|
| OCGForge | `main` | `588f02b4ef879fee999c921c114937a6a1e48557` |
| EDOPro | `master` | `54ea755aa0243e2f18bb6bd2187fc9b2f7e29788` |
| ProjectIgnis/windbot | `master` | `fa0ae767967afc6a820784837f11cd3fabb9c47c` |
| edo9300/ygopro-core | `master` | `46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57` |
| ProjectIgnis/CardScripts | `master` | `150ec5170097128b675b8d5b257b793a6ad6c91e` |
| ProjectIgnis/BabelCDB | `master` | `d1cf9e0aa888255f6031196053a8a9303c887667` |

Important distinction: live edo9300/ygopro-core at `46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57` is **not** OCGForge's rules authority. OCGForge's checked-in lock still records pinned core `9a0c558c2d686542f7914a6d529fd7aa57746aed`, CardScripts `f337c87018ca723c1aded5143e616bb649555273`, BabelCDB `89ad6837b0766a52984d8c715a7d5d4f8447946b`, plus patchset `ocgforge.ocgcore.api_hardening.v1`. Source: [OCGForge rules_bundle.lock.json](https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/third_party/rules_bundle.lock.json).

---

# Architecture comparison

## EDOPro simplified runtime architecture

```text
                      ┌──────────────────────┐
                      │ Card DB / resources  │
                      │ DataManager          │
                      └──────────┬───────────┘
                                 │ cardReader
CardScripts / scripts ───────────┼──────────────┐
                                 │ scriptReader │
                                 v              v
                         ┌──────────────────────────┐
                         │       ocgcore Duel       │
                         │ public OCG API boundary  │
                         └────────────┬─────────────┘
                                      │ engine messages
                         CoreUtils::ParseMessages
                                      │
               ┌──────────────────────┴──────────────────────┐
               │                                             │
        local/single mode                              hosted/network mode
        SingleMode                                     GenericDuel
               │                                             │
               └──────────────────────┬──────────────────────┘
                                      v
                             DuelClient::ClientAnalyze
                                      │
                       ┌──────────────┴──────────────┐
                       v                             v
              mutable ClientField/Card          UI / animations
                       │                             │
                       └──────────────┬──────────────┘
                                      v
                           human response encoding
                                      │
                DuelSetResponse or CTOS_RESPONSE
```

WindBot is launched as a separate executable by EDOPro and joins via network protocol:

```text
EDOPro host/server
      ^
      | CTOS/STOC protocol
      v
WindBot GameClient
      |
GameBehavior -> Duel/ClientField mirror
      |
GameAI -> Executor / DefaultExecutor / deck Executor
      |
encoded response back to EDOPro
```

Primary source references:
- [EDOPro `gframe/generic_duel.cpp`](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/generic_duel.cpp), `GenericDuel::Process`.
- [EDOPro `gframe/duelclient.cpp`](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/duelclient.cpp), `DuelClient::ClientAnalyze`, `SendResponse`.
- [EDOPro `gframe/windbot.cpp`](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/windbot.cpp), `WindBot::Launch`.
- [EDOPro `gframe/dllinterface.cpp`](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/dllinterface.cpp), core API loading/version checks.
- [WindBot `Game/GameBehavior.cs`](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/GameBehavior.cs).

## Layer-by-layer classification

| EDOPro component | Classification | OCGForge relevance |
|---|---|---|
| ocgcore C API wrapper | `DIRECTLY_RELEVANT` | Independent reference for lifecycle and compatibility, but OCGForge keeps its own narrow CoreHost. |
| `DuelClient::ClientAnalyze` message parsing | `DIRECTLY_RELEVANT` | High-value protocol interpretation and edge-case reference. |
| response formatting/UI handlers | `DIRECTLY_RELEVANT` / `REFERENCE_ONLY` | Useful to cross-check wire encoding; UI domain is not the OCGForge action domain. |
| `ClientField`/`ClientCard` mutable mirror | `REFERENCE_ONLY` | Useful transition reference; unsuitable as authoritative model state or semantic identity. |
| `GenericDuel` / network server | `CLIENT_SPECIFIC` | Useful only for understanding WindBot integration and spectator/catch-up behavior. |
| GUI/animations/audio | `NOT_RELEVANT_TO_OCGFORGE` | No authoritative simulation value. |
| database preload/cache | `POTENTIALLY_USEFUL_LATER` | Internal immutable caching concepts may be useful after measurement. |
| replay subsystem | `POTENTIALLY_USEFUL_LATER` | Valuable design reference for response replay vs packet-stream replay. |
| repo updater | `CLIENT_SPECIFIC` | Operationally useful for a client, contrary to OCGForge's explicit pin discipline. |
| WindBot launcher | `REFERENCE_ONLY` | Shows bot integration is network-client integration, not a rules-engine API. |

## OCGForge contrast

OCGForge's live architecture exposes a semantic boundary rather than a client mirror. The live decoder generates `DecisionRequest` and `ActionCandidate`; continuations keep ocgcore paused until one exact final response is constructed; unknown interactive decisions fail closed. See [OCGForge `message_decoder.cpp`](https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/message_decoder.cpp) and [continuation.cpp](https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/continuation.cpp).

**INFERENCE:** this separation is materially better suited to deterministic game-AI research because it makes legality, semantic identity, privacy and replay contracts explicit rather than coupling them to a presentation-state object graph.

---

# Engine-message comparison

## Current OCGForge interactive coverage found in live `main`

`decode_messages` directly handles:

- `MSG_SELECT_IDLECMD`
- `MSG_SELECT_BATTLECMD`
- `MSG_SELECT_EFFECTYN`
- `MSG_SELECT_YESNO`
- `MSG_SELECT_POSITION`
- `MSG_SELECT_PLACE`
- `MSG_SELECT_DISFIELD`
- `MSG_SELECT_CHAIN`
- `MSG_SELECT_CARD`
- `MSG_SELECT_OPTION`
- `MSG_SELECT_TRIBUTE`
- `MSG_SELECT_SUM`
- `MSG_SELECT_COUNTER`
- `MSG_SORT_CARD`
- `MSG_SORT_CHAIN`
- `MSG_SELECT_UNSELECT_CARD`
- `MSG_ANNOUNCE_NUMBER`
- `MSG_ANNOUNCE_RACE`
- `MSG_ANNOUNCE_ATTRIB`

The live code explicitly categorizes `MSG_REQUEST_DECK`, `MSG_ROCK_PAPER_SCISSORS`, and `MSG_ANNOUNCE_CARD` as unsupported interactive messages and fails closed. Source: [message_decoder.cpp](https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/message_decoder.cpp).

## Engine-message gap table

| Message family | EDOPro / core handling | OCGForge status | Missing knowledge / risk | Recommended action |
|---|---|---|---|---|
| Idle commands | Core emits typed lists; response packs command in low 16 bits and source index in high 16 bits. | Supported | Need ongoing differential evidence as core format evolves. | P1 differential fixtures. |
| Battle commands | Same packed command/index structure; attack list has direct-attack metadata. | Supported; M3 battle fixture exists. | Ensure every engine-provided legal command maps 1:1 to semantic candidates. | P1 differential fixture corpus. |
| Yes/no, effect yes/no | `int32` 0/1; core rejects other values. | Supported | Low protocol risk. | Retain regression tests. |
| Position | `int32` single allowed position bit; core validates against mask. | Supported | Single-choice auto-resolution by core is noninteractive; adapter must match actual emitted messages. | Retain pinned-core fixtures. |
| Select option | Response is option index. | Supported | Options are values/descriptions, but response semantics are index-based. | Confirm ordering stability in fixtures. |
| Select card | Core sorts candidate domain, accepts multiple wire encodings and `-1` when cancelable/min=0. | Supported, including continuation. | Highest risk is completeness/canonicalization, not basic parsing. | P0 differential corpus for cardinality/cancel/large domains. |
| Select unselect card | Core sends selected + unselected lists; response `-1` means finish/cancel, otherwise one combined index using response type 1. | Supported. | `finishable` and `cancelable` share `-1`; semantic distinction exists only in request context. | P0 differential fixtures for all flag combinations. |
| Select tribute | Weighted contribution (`release_param`); maximum is response cardinality but minimum is tribute value. | Supported continuation. | Easy to misread `min` as count rather than required tribute value. | P0 weighted-domain oracle tests. |
| Select sum | Mandatory + optional cards, packed dual contributions, exact and greater-sum modes. | Supported continuation. | Complex combinatorial semantics; prime candidate-domain completeness risk. | P0 exhaustive small-domain differential tests. |
| Select counter | Per-card 16-bit allocation whose total must equal required count and not exceed capacities. | Supported continuation. | Allocation domain can explode; completeness must be proven without cap. | P0 combinatorial oracle tests. |
| Select place / disfield | Response is repeated `(player, location, sequence)` triples; forbidden mask semantics. | Supported continuation for multi-zone selection. | Zone-bit translation and opponent-side bits are error-prone. | P0 bitmask ↔ tuple differential tests. |
| Sort card / chain | Response is byte permutation; first signed byte `-1` bypasses sorting. | Supported ordering continuation. | Ordering is semantically meaningful; byte domain and bypass need explicit coverage. | P0 permutation/bypass fixtures. |
| Select chain | Response is chain index; `-1` pass only if not forced. | Supported | Forced/pass semantics and engine ordering are critical. | P0 forced/unforced differential tests. |
| Announce race | Core emits `count + u64 available`; response u64 mask with exact popcount. | Supported | 64-bit domain and mask completeness. | P1 multi-bit fixtures. |
| Announce attribute | Core emits `count + u32 available`; response u32 mask. | Supported | Similar to race. | P1 multi-bit fixtures. |
| Announce number | Engine sends values; response is **index**, not announced number itself. | Supported | Common reverse-engineering trap. | Keep explicit format fixture. |
| Announce card | Engine sends opcode program describing declarable cards; response is card code. | **Fail-closed unsupported** | Requires faithful declarability semantics over full card DB, aliases/tokens and opcodes. | **P0 research gap** before arbitrary deck expansion. |
| Rock-paper-scissors | Core sends one-player prompt; response int32 1..3. | Fail-closed unsupported | Not a duel-state ML action for many environments, but protocol still unresolved if exposed. | P2 unless environment lifecycle requires it. |
| Request deck | Interactive core family. | Fail-closed unsupported | Need to determine if reachable in intended OCGForge runtime. | P2 reachability study. |
| Fusion material selection | WindBot handles via selection callback/hints; engine protocol generally uses generic select-card/unselect families. | Generic families supported; fixed matchup has Fusion-unrelated coverage limits. | Mechanics legality is engine-owned; need specific fixtures when supporting Fusion-heavy decks. | P1 deck/fixture closure, not a new action type by default. |
| Synchro material selection | Generic selection families; WindBot has dedicated material heuristics. | M3 Swordsoul Synchro paths engine-verified. | Fixed-deck verification is not global mechanics certification. | P1 support expansion only as demanded. |
| Xyz material selection | Generic selection plus overlay state. | M3/M3.5 Miragestallio and overlay query slice verified. | Preserve overlay privacy/locator contract. | Retain M3.5 gates. |
| Link material selection | Generic selection families; WindBot uses `SelectMaterials`. | M3 Salamangreat Link continuations verified. | Fixed-slice only. | Reuse current evidence model for future decks. |
| Ritual material selection | Generic selection families where emitted by scripts/core. | No general claim found. | Reachability/mechanics coverage unknown outside fixed slice. | P2 when a target deck demands it. |

### MAJOR — `MSG_ANNOUNCE_CARD` is a concrete, evidenced future gap

**FACT FROM SOURCE:** current OCGForge marks `MSG_ANNOUNCE_CARD` unsupported.  
**FACT FROM SOURCE:** current core `playerop.cpp` implements a nontrivial stack-machine predicate (`is_declarable`) over opcodes and checks card database properties, alias/token rules, then accepts a card-code response only if the predicate succeeds.  
**FACT FROM SOURCE:** EDOPro parses the opcode list and provides an announce-card UI.

This is the strongest directly evidenced message-family gap because supporting it correctly requires more than exposing a finite list already provided by the engine.

---

# Response-encoding findings

This section distinguishes engine truth from EDOPro interpretation.

## 1. Card subsets: multiple accepted wire encodings

Current core `parse_response_cards` accepts four selector encodings:

- type `0`: count + `uint32` indices;
- type `1`: count + `uint16` indices;
- type `2`: count + `uint8` indices;
- type `3`: bitset;
- type `-1`: cancel when allowed.

Source: [ygopro-core `playerop.cpp`](https://github.com/edo9300/ygopro-core/blob/46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57/playerop.cpp), `parse_response_cards`.

OCGForge deliberately emits one canonical representation—type `0`, count, u32 indices—in [response_builder.cpp](https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/response_builder.cpp).

**RECOMMENDATION:** keep one canonical OCGForge encoding. Do not mirror every wire-equivalent core encoding into separate semantic actions. Differential tests should prove canonical bytes are accepted.

## 2. Idle/battle command packing

Core extracts:

```text
type = response & 0xffff
index = response >> 16
```

and validates the selected list/index. OCGForge uses the same packed representation in semantic candidates.

**Risk:** a UI implementation may obscure that semantic command type and source-list index are distinct dimensions.

## 3. Unselect

Core accepts:

```text
-1            -> finish/cancel when request permits
type == 1
second int32   -> combined selected+unselected index
```

OCGForge's live decoder constructs exactly this response shape.

**Important nuance:** the wire sentinel does not distinguish FINISH from CANCEL; the request flags/context do. OCGForge is correct to preserve the semantic distinction even when the wire bytes coincide.

## 4. Tribute

Core serializes each candidate with `release_param`. The request `min` is the minimum **tribute value**, while selected card count must not exceed `max`. This is non-obvious and valuable independent evidence for OCGForge's weighted continuation.

## 5. Sum

Each card carries packed `sum_param`:

```text
low 16 bits  = primary contribution
high 16 bits = alternate contribution
```

The core has separate exact-sum and greater/equal legality checks. Mandatory cards are semantically part of the calculation but not part of the selectable optional list.

**RECOMMENDATION:** use core behavior as final oracle and EDOPro/WindBot only as an independent interpretation source.

## 6. Counter allocation

Response is an `int16` amount for every candidate card in engine order. Total must equal required count; each amount must not exceed that card's capacity.

This is a strong case for continuation rather than a flattened action list.

## 7. Place/disfield

Wire response is a sequence of 3-byte tuples:

```text
controller
location
sequence
```

The prompt is a bitmask of forbidden positions. Mapping the 32-bit mask to semantic zones is protocol knowledge that should be permanently fixture-tested.

## 8. Sorting

Response is one byte per card representing a permutation. A signed first byte of `-1` (`0xff`) means bypass/keep automatic order.

EDOPro even has UI-level auto-chain-order behavior that sends `-1`. This is useful response-format evidence but is **not** a model policy OCGForge should copy.

## 9. Announcement families

- race: response = u64 bitmask;
- attribute: response = u32 bitmask;
- number: response = **index** of engine-provided value;
- card: response = **card code**, validated against opcode predicate and card database.

## Can EDOPro be used as a response-format oracle?

**Yes, as an independent secondary oracle.** The correct hierarchy is:

```text
pinned OCGForge core behavior
    > OCGForge contract/tests
    > EDOPro interpretation as corroborating evidence
```

EDOPro is particularly valuable where OCGForge historically had to reverse-engineer unusual encodings. It must never override pinned-core evidence.

---

# Hidden-information comparison

## EDOPro client state

`ClientField` owns mutable `ClientCard*` objects in Deck, Hand, Monster Zone, Spell/Trap Zone, Graveyard, Banished and Extra Deck containers. `Initial` creates anonymous card objects for hidden piles; message handling later updates codes and moves/reorders objects. Source: [client_field.cpp](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/client_field.cpp).

Important shuffle behavior from [duelclient.cpp](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/duelclient.cpp):

- `MSG_SHUFFLE_DECK` clears stored deck card codes.
- `MSG_SHUFFLE_HAND` assigns the codes supplied by the message to hand objects.
- `MSG_SHUFFLE_EXTRA` assigns supplied codes to face-down Extra Deck objects.
- `MSG_SHUFFLE_SET_CARD` clears codes while moving the affected set-card objects.
- replay rendering can temporarily make hand cards public for display.

These are useful visibility-transition references but do not establish an ML-safe observation model.

## Classification

| Concept | Classification | Why |
|---|---|---|
| Clear unknown deck identity on shuffle | `SAFE_REFERENCE` | Semantic idea matches knowledge destruction. |
| Represent hidden cards as unknown placeholders | `SAFE_REFERENCE` | Useful concept if identity is not persistent. |
| Mutable `ClientCard*` object carried across moves | `PRIVACY_RISK_IF_COPIED` | Object identity can survive transitions where player knowledge should not. |
| Full replay state / replay reveal behavior | `OMNISCIENT_CLIENT_STATE` | Appropriate for replay viewer, not participant observation. |
| Spectator catch-up packet cache | `OMNISCIENT_CLIENT_STATE` / `UNKNOWN` | Delivery semantics are client/server-specific; not a PlayerObservation contract. |
| Opponent hidden deck `ClientCard` objects | `PRIVACY_RISK_IF_COPIED` | Object existence and continuity are internal implementation details, not model identifiers. |
| Extra Deck mutable object identity | `PRIVACY_RISK_IF_COPIED` | Face-down Extra Deck identity/continuity requires perspective-aware treatment. |

### MAJOR — do not import client object identity into the teacher boundary

OCGForge's `PlayerObservation` rule is stronger: after a knowledge-destroying transition, hidden identity must not persist through engine pointer/object identity or hidden locator. EDOPro's client object graph is therefore a **transition reference**, not a state schema to emulate.

---

# Replay comparison

## EDOPro replay modes

EDOPro supports both legacy response-driven replay and streamed packet replay.

### YRP1 / old replay

The extended header stores:

- replay id/version/flags/timestamp;
- four-word RNG seed;
- duel parameters;
- player names;
- deck contents (for applicable replay types);
- response sequence.

Old playback constructs a new core duel with the stored seed/parameters/decks and feeds stored responses when interactive messages occur. Source:
- [replay.h](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/replay.h)
- [replay_mode_yrp.cpp](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/replay_mode_yrp.cpp)

This is conceptually close to deterministic semantic/action replay, but response bytes are not semantic action identities.

### YRPX / streamed replay

The new replay stores a stream of `CoreUtils::Packet` messages. Playback can reconstruct the client display directly from the packet stream without re-executing every original action. A streamed replay can contain an embedded `OLD_REPLAY_MODE` replay for fallback/export. Source: [replay.cpp](https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/replay.cpp), `ParseStream`.

## Provenance limitations for OCGForge purposes

**INFERENCE from source format:** EDOPro's header captures client/core compatibility information and seeds, but does not provide OCGForge-style exact identities for:

- exact ocgcore commit + OCGForge patchset;
- exact CardScripts commit/content;
- exact BabelCDB artifact hash;
- canonical locked deck bytes;
- observation/action/trace schema versions;
- OCGForge build/provenance identity separated from gameplay identity.

Therefore EDOPro replay bytes are not an adequate deterministic historical reproduction contract for OCGForge.

## Concepts worth reusing

- separate action/response replay from message-stream playback;
- record seed and starting configuration;
- embed/associate deck information;
- support restart/rewind by replaying from a known beginning;
- divergence diagnostics should identify first mismatching message/action/hash.

## Recommendation

Do not adopt `.yrp`/`.yrpX` as OCGForge's authoritative replay format. Study them before a future checkpoint/replay ADR, and consider an importer as external evidence only.

---

# WindBot architecture

## Current structure

### `GameBehavior`

Network/protocol event handler. It parses EDOPro/YGOPro game messages and updates WindBot's local `Duel`, `ClientField` and `ClientCard` state, then asks the AI for responses.

### `GameAI`

Decision-output layer. Deck executors queue preferred cards, materials, options, places and follow-up selections through methods such as `SelectCard`, `SelectNextCard`, `SelectMaterials`, etc.

### `Executor`

Base class that registers ordered `CardExecutor` rules (`ExecutorType` + card/filter + predicate). It owns a `Duel`, `GameAI`, `AIUtil`, Bot/Enemy fields and also creates its own `Random`.

### `DefaultExecutor`

Large generic heuristic layer for common cards and generic tactical concepts: negation, targeting, battle, board wipes, floodgates and common staples.

### deck-specific Executor

Subclasses register archetype-specific rules in priority order and maintain mutable state across turns/chains/selections.

### `AIUtil`

Utility/evaluation functions for board state, chains, targeting, attack/defense, card categories and selection-count repairs.

### `ClientField` / `ClientCard`

Mutable client-side state mirrors. They are convenient for handcrafted bot rules but should not be copied into OCGForge's authoritative observation/identity boundary.

## Architectural consequence

WindBot does not consume an OCGForge-style explicit complete candidate domain and then score candidates. Instead, deck logic decides whether registered action templates should fire, then subsequent callback functions guide individual protocol selections.

That makes WindBot a rich **behavioral reference**, but not a plug-compatible OCGForge policy.

---

# Representative deck-executor study

The objective here is representative capability mining, not exhaustive deck review.

## 1. Salamangreat

Source: [SalamangreatExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/SalamangreatExecutor.cs)

Observed state includes:

- `wasGazelleSummonedThisTurn`
- `wasFieldspellUsedThisTurn`
- `wasWolfSummonedUsingItself`
- `wasVeilynxSummonedThisTurn`
- `wasStallioActivated`
- `wasWolfActivatedThisTurn`
- `FoxyActivatedThisTurn`
- `JackJaguarActivatedThisTurn`
- combo cards in hand
- remembered Impermanence zones

Examples include explicit Link-material choices, reincarnation-Link sequencing, target lists, floodgate handling and resource-loop priorities.

Categories:
`ARCHETYPE_HEURISTIC`, `COMBO_SCRIPT`, `MATERIAL_SELECTION`, `RESOURCE_MANAGEMENT`, `TARGET_SELECTION`, `INFORMATION_TRACKING`.

**High OCGForge relevance:** it matches one locked M3 deck, so these heuristics are a natural future *semantic-idea* source for a deterministic teacher after interface adaptation.

## 2. Swordsoul

Source: [SwordsoulExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/SwordsoulExecutor.cs)

Observed features:

- Maxx "C"/Droll/Impermanence opponent-state tracking;
- lists of activated card IDs and currently-negated monsters;
- level-specific Synchro material construction;
- Tenyi/Blackout constraints;
- threat classification (`floodgate`, `dangerous`, `invincible`);
- deck-count tables;
- random shuffling of equally treated candidate lists via `Program.Rand`.

Categories:
`COMBO_SCRIPT`, `THREAT_EVALUATION`, `MATERIAL_SELECTION`, `INFORMATION_TRACKING`, `RESOURCE_MANAGEMENT`.

**Determinism warning:** random list shuffling is unsuitable for an authoritative deterministic teacher without injected RNG/tie-break rules.

## 3. Albaz / Branded

Source: [AlbazExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/AlbazExecutor.cs)

This is one of the richest material-selection examples. It tracks:

- current fusion target;
- selected fusion materials across callbacks;
- cards sent to GY this turn;
- current negate/destroy targets;
- Branded-in-Red material state;
- Cartesia material state;
- many deck-specific resource and floodgate conditions.

Its `OnSelectCard` branches heavily on `HintMsg.FusionMaterial`, current chain source and fusion target.

Categories:
`MATERIAL_SELECTION`, `COMBO_SCRIPT`, `RESOURCE_MANAGEMENT`, `TARGET_SELECTION`, `INFORMATION_TRACKING`.

**Research value:** excellent source for future Fusion-material closure tests and teacher heuristics; poor candidate for direct BC labels without semantic mapping.

## 4. Labrynth

Source: [LabrynthExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/LabrynthExecutor.cs)

Representative reactive/control behavior:

- tracks traps set this turn;
- tracks opponent set cards;
- tracks cards summoned in chain;
- Dimensional Barrier announcement state/history;
- Cooclock/furniture sequencing;
- current negate/destroy target lists;
- Extra Deck summon counts;
- target-priority utilities;
- randomizes some equal-priority lists.

Categories:
`ARCHETYPE_HEURISTIC`, `THREAT_EVALUATION`, `RESOURCE_MANAGEMENT`, `INFORMATION_TRACKING`, `TARGET_SELECTION`.

**Research value:** useful contrast to combo executors because decision quality depends on temporal/reactive state, not only deterministic combo lines.

## 5. Tearlaments

Source: [TearlamentsExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/TearlamentsExecutor.cs)

Contains extensive state flags for multi-effect cards and fusion progress, lists of remaining cards/materials, chain cards and destination-specific priority tables.

Categories:
`COMBO_SCRIPT`, `MATERIAL_SELECTION`, `RESOURCE_MANAGEMENT`, `INFORMATION_TRACKING`, `THREAT_EVALUATION`.

**Research value:** demonstrates how difficult callback-local card selection becomes when selection semantics depend on prior selections and unresolved chain state.

## 6. Sky Striker

Source: [SkyStrikerExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/SkyStrikerExecutor.cs)

Tracks Link summons per turn and remembered `WidowAnchorTarget`. It includes generic target selection, spell-count/resource checks, Main Phase/Battle Phase timing and end-phase recovery priorities.

Categories:
`RESOURCE_MANAGEMENT`, `TARGET_SELECTION`, `THREAT_EVALUATION`, `BATTLE_HEURISTIC`.

**Research value:** relatively interpretable example of resource-based tactical heuristics rather than deep combo scripting.

## 7. Exosister

Source: [ExosisterExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/ExosisterExecutor.cs)

Tracks many once-per-turn effects, transformations, Xyz targets/material choices, graveyard movement, Maxx "C"/Droll/Impermanence state and selected removal targets.

Categories:
`ARCHETYPE_HEURISTIC`, `MATERIAL_SELECTION`, `THREAT_EVALUATION`, `INFORMATION_TRACKING`, `RESOURCE_MANAGEMENT`.

## 8. Zefra

Source: [ZefraExecutor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/AI/Decks/ZefraExecutor.cs)

Representative Pendulum/combo executor with substantial state for Pendulum summons, spell activations, effect flags, Xyz mode and material lists.

Categories:
`COMBO_SCRIPT`, `MATERIAL_SELECTION`, `RESOURCE_MANAGEMENT`, `INFORMATION_TRACKING`.

**Research value:** useful when OCGForge later evaluates Pendulum-heavy support; not needed for the current locked slice.

---

# WindBot as a future teacher / baseline

| Role | Feasible? | Integration complexity | Primary risks | Expected value |
|---|---|---:|---|---|
| Benchmark opponent | Yes | Medium | Network/version drift; deck brittleness; nondeterministic RNG | High for narrow decks after pinning. |
| Behavioral reference | Yes | Low-Medium | It may make suboptimal/buggy choices; state semantics differ | High for fixture interpretation. |
| Heuristic teacher | Yes, after adaptation | High | Hidden-state dependency, RNG, mutable selection queues | High for first deck-specific teacher. |
| Scripted curriculum | Yes | Medium | Overfitting to fixed lines | Medium-High. |
| Trajectory generator | Technically yes | High | Labels may not map 1:1 to semantic candidates; biased policy | Medium. |
| Regression oracle | Limited | Medium | WindBot is not legality authority and may change | Medium for behavioral consistency only. |

## Required teacher contract

A future OCGForge teacher must:

1. receive only perspective-safe `PlayerObservation`;
2. receive the exact complete `ActionCandidate` domain;
3. choose a candidate by semantic key;
4. never inspect raw CoreHost/query state;
5. use explicit injected RNG if any randomized policy is desired;
6. expose policy/version/RNG provenance;
7. fail closed if its heuristic cannot express a safe decision.

The correct adaptation target is **semantic heuristic ideas**, not the current WindBot object model.

---

# Can WindBot provide supervised labels?

Architecturally: **possibly, but only after an explicit mapping experiment.**

For every WindBot decision, a future adapter would need to classify:

```text
EXACT_MATCH
AMBIGUOUS_MATCH
NO_MATCH
UNSAFE_HIDDEN_DEPENDENCY
UNRECONSTRUCTABLE_CALLBACK_STATE
```

A label is usable only when exactly one OCGForge candidate key corresponds to the action under the same perspective-safe information.

Potential failure modes:

- WindBot queues card IDs or `ClientCard` objects rather than selecting from an explicit semantic candidate set.
- A callback may only make sense because a previous WindBot executor set mutable local state.
- Generic helpers may repair a requested selection to fit the engine-provided list.
- deck-specific executors encode policy biases and matchup assumptions.
- random shuffling/tie-breaking changes labels between runs.
- client state may contain information that is not represented in `PlayerObservation`.

**RECOMMENDATION:** before any BC work, run a mapping-coverage study and report match rate plus every mismatch class. Do not silently discard unmatched decisions.

---

# Deterministic teacher potential

## Observed RNG risks

**FACT FROM SOURCE:** `Program.InitDatas` assigns `Program.Rand = new Random()`. In .NET this default constructor is time-derived. Source: [Program.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Program.cs).

**FACT FROM SOURCE:** base `Executor` also creates `Rand = new Random()`. Source: [Executor.cs](https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/ExecutorBase/Game/AI/Executor.cs).

**FACT FROM SOURCE:** mature deck executors such as Swordsoul, Labrynth and Exosister call `Program.Rand` to shuffle equally considered cards.

Other conceptual risks:

- process/thread timing affects default random seeds;
- network delivery timing can affect bot execution context;
- mutable selection queues must be reset exactly;
- object identity and list ordering can become incidental tie-breakers;
- executor registration order is policy semantics and must be versioned if reused.

## Conceptual changes required

A deterministic OCGForge teacher inspired by WindBot should replace implicit randomness with:

```text
teacher_policy_id
teacher_policy_version
decision_id / semantic candidate keys
explicit teacher_rng_stream_id
seed derived from episode/decision identity
stable tie-break order
```

Randomness must be an explicit policy choice, not a process side effect.

---

# EDOPro performance lessons

These are architectural references, not optimization recommendations.

| Observation | Classification | OCGForge implication |
|---|---|---|
| Database is loaded into long-lived in-memory card structures; containers are reserved up front. | `SEMANTICALLY_SAFE_INTERNAL_OPTIMIZATION` in principle | Immutable card metadata may be cacheable, but measure OCGForge first. |
| `ClientField` incrementally applies protocol messages instead of reconstructing all state every UI frame. | `CLIENT_SPECIFIC_OPTIMIZATION` / potentially useful | A future non-authoritative derived view may benefit, but must not replace authoritative observation semantics. |
| Replay packet stream is parsed once and replayed incrementally. | `POTENTIALLY_USEFUL_LATER` | Useful concept for non-authoritative replay visualization. |
| EDOPro animations call `rand()` during shuffle display. | `CLIENT_SPECIFIC_OPTIMIZATION` + `DETERMINISM_RISK` if copied | Harmless for visual client, unacceptable in authoritative OCGForge semantics. |
| Repo updater parallelizes fetching and mutates checked-out resources. | `CLIENT_SPECIFIC_OPTIMIZATION` | Contrary to OCGForge pinned-input discipline. |
| Core DLL version is checked against expected API major/minor. | `SEMANTICALLY_SAFE_INTERNAL_OPTIMIZATION` / compatibility guard | Useful defense, but OCGForge's exact commit/content pins are stronger. |

OCGForge's known M4 bottleneck is canonical PlayerObservation rendering, especially cumulative visible-event bytes. EDOPro's client-state mirror does **not** prove that OCGForge should cache or delta-encode authoritative observation bytes. That would be a semantic contract change, not a direct optimization transfer.

---

# Rules-bundle management

## EDOPro

Relevant mechanisms include:

- bundled or dynamically loaded core;
- API version compatibility check (`Core::check_api_version`);
- repository manager capable of fetching/updating data/script/core repositories;
- database loading from configured repository paths;
- runtime script-reader callbacks;
- WindBot database paths serialized into launch arguments.

This is appropriate for a continuously updated user client.

## OCGForge

Live lock file uses exact commits, resolved checkout hashes, database artifact SHA-256 and an ordered repository patchset. That is materially stronger provenance for deterministic research.

### Recommendation

Borrow:

- explicit API-version validation;
- separate card reader/script reader boundaries;
- compatibility errors that fail early.

Do not borrow:

- floating update behavior;
- automatic hard reset to fetched resources;
- "client version" as sufficient replay provenance.

---

# Anti-patterns / incompatible assumptions

| Anti-pattern | Why it is appropriate upstream | OCGForge invariant endangered |
|---|---|---|
| Mutable persistent `ClientCard` identity | Convenient UI/bot object model | Information safety; semantic identity |
| Raw client-state object graph as AI input | Practical for handcrafted bot | Perspective-safe observation |
| Automatic UI/bot fallback choice | Keeps interactive duel moving | Complete legal decisions; fail closed |
| Fixed executor priority as legality | Policy implementation | Correctness boundary |
| Wall-clock-seeded `Random` | Acceptable variety for bot | Determinism/replay |
| Random target/list shuffling | Breaks ties for handcrafted bot | Deterministic teacher provenance |
| Spectator/replay omniscience | Needed for viewing/replay | Player privacy |
| UI sorting/bypass preferences | Human convenience | Candidate-domain semantics |
| Floating repo updates | User client freshness | Rules-bundle identity |
| Client/version-level replay compatibility only | Practical replay support | Historical reproducibility |
| Silent/heuristic protocol recovery | Better UX | Fail-closed correctness |
| Deck-specific assumptions in generic legality | Simplifies bot | Rules authority separation |

---

# Things OCGForge already does better for deterministic game-AI research

These are capability-fit comparisons, not claims that OCGForge has broader game coverage.

1. **Explicit legal semantic candidates.** EDOPro presents human UI choices and WindBot chooses through executor/callback logic; OCGForge exposes a typed candidate interface for supported families.
2. **Continuation machinery.** OCGForge can expose combinatorial decisions without truncating to a fixed global action vocabulary.
3. **Perspective-safe observation contract.** EDOPro/WindBot use mutable client mirrors; OCGForge explicitly separates omniscient engine state from model-visible state.
4. **Fail-closed unsupported behavior.** `MSG_ANNOUNCE_CARD` demonstrates the policy: unsupported rather than guessed.
5. **Semantic identity and stale-action rejection.** OCGForge's action identities are contract-level concepts rather than object references.
6. **Rules provenance.** exact upstream commits + patchset hashes + artifact hashes are stronger than floating client resource updates.
7. **Semantic gameplay hashes vs provenance hashes.** This is directly aligned with cross-process/cross-worker research.
8. **Machine-readable conformance evidence.** M3/M3.5 evidence is designed as acceptance data rather than user-client behavior.

---

# Differential-testing opportunities

## A. Message parsing

For a curated set of raw engine message frames:

```text
raw pinned-core bytes
  -> EDOPro field values
  -> OCGForge DecisionRequest
```

Assert shared factual fields: player, cancelability, counts, source indices, masks, values, ordering metadata. Do not assert EDOPro's UI candidate order if it performs presentation sorting.

## B. Response encoding

For every semantic OCGForge terminal candidate:

1. obtain exact OCGForge response bytes;
2. feed those bytes to the pinned core;
3. require acceptance/no `MSG_RETRY`;
4. independently compare the logical choice to EDOPro's encoding path.

Particularly valuable for:
- select-card canonical type-0 encoding;
- unselect type-1 + combined index;
- sort byte permutations / `0xff`;
- multi-zone triples;
- counter int16 arrays;
- race/attribute masks.

## C. Legal-domain completeness

For small combinatorial fixtures, enumerate **all** response assignments accepted by the pinned core and compare to the terminal solutions reachable through OCGForge continuations.

This is strongest for:

- small `SELECT_SUM`;
- weighted `SELECT_TRIBUTE`;
- `SELECT_COUNTER`;
- multi-zone place;
- ordering/permutations.

EDOPro can provide a second interpretation, but the core acceptance set is authoritative.

## D. WindBot action mapping

Run WindBot against controlled scenarios and record its response. Independently decode the same engine prompt through OCGForge and determine whether exactly one `ActionCandidate.semantic_key` matches the behavior.

The output is a mapping-coverage report, not training data.

## E. Replay/state transition

For fixed action traces:

```text
OCGForge semantic replay
vs
old YRP response replay
vs
YRPX message-stream interpretation
```

Compare public state transitions and first-divergence location. This can expose protocol interpretation mismatches while keeping OCGForge trace/hash contracts authoritative.

---

# Ranked opportunity backlog

| Rank | Opportunity | Source project | OCGForge owning layer | Expected value | Risk | Recommended next step |
|---|---|---|---|---|---|---|
| **P0** | Differential interactive-protocol corpus | ocgcore + EDOPro | protocol/tests | Very high correctness & completeness risk reduction | Low | Curate raw frames + accepted responses for high-risk families; engine remains oracle. |
| **P0** | `MSG_ANNOUNCE_CARD` declarability study | ocgcore + EDOPro | protocol + card metadata | Removes a concrete fail-closed future blocker | Medium | Specify opcode semantics, database dependency and candidate-enumeration completeness before implementation. |
| **P0** | Continuation terminal-solution equivalence tests | ocgcore | continuation/tests | Proves no legal subset/allocation/permutation loss | Medium compute, low semantic | Exhaustively enumerate small fixtures and compare accepted response set. |
| **P1** | WindBot semantic-action mapping experiment | WindBot | future teacher/benchmark adapter | Quantifies teacher feasibility before ML | Medium | Produce match/mismatch taxonomy; no BC. |
| **P1** | Replay ADR research using YRP1/YRPX lessons | EDOPro | future replay/checkpoint | Prevents weak persistence/provenance design | Low | Document action replay vs message playback vs future checkpoint semantics. |
| **P1** | Fixed-deck teacher heuristic extraction | WindBot | future policy/teacher | High bootstrap value for locked decks | Medium | Rewrite ideas against `PlayerObservation` + `ActionCandidate`; do not port state model. |
| **P2** | Mechanics-closure mining from Fusion/Pendulum executors | WindBot | conformance planning | Helps choose future fixtures/decks | Low | Use only when expanding support beyond locked matchup. |
| **P2** | RPS/request-deck reachability analysis | ocgcore/EDOPro | environment lifecycle | Clarifies remaining interactive surface | Low | Determine whether intended runtime can emit these families. |
| **P3** | Immutable card/script metadata caching study | EDOPro | CoreHost/internal asset layer | Potential throughput value | Medium semantic risk if scoped badly | Benchmark only after M4 observation hotspot priorities justify it. |
| **P3** | Client-state mirror performance ideas | EDOPro | non-authoritative derived tooling | Limited | High privacy/semantic risk if promoted | Keep out of authoritative observation path. |

P0 is reserved here for opportunities that reduce substantial correctness/completeness uncertainty, not merely easy work.

---

# Finding classification

## BLOCKER

**None established.**

No source evidence inspected proves that a current OCGForge architectural assumption already causes incorrect, nondeterministic, privacy-unsafe, or incomplete behavior inside its claimed certified slice. It would be inappropriate to manufacture a blocker from missing global coverage that OCGForge explicitly does not claim.

## MAJOR — interactive-protocol long-tail remains the highest roadmap risk

Current OCGForge supports many interactive families, but fixed-matchup acceptance is not global proof. `MSG_ANNOUNCE_CARD` is a concrete fail-closed gap, and high-complexity continuations deserve broader differential proof before arbitrary deck expansion.

## MAJOR — WindBot is not directly compatible with the OCGForge teacher contract

Its state model, mutable selection queues, executor priorities and RNG make it unsuitable as-is for perspective-safe deterministic teacher labels.

## MAJOR — EDOPro replay provenance is insufficient for OCGForge historical reproduction

The architectural concepts are valuable; the provenance contract is not.

## MINOR — targeted internal caching ideas exist

Database/card metadata preloading and incremental state processing are useful reference ideas, but OCGForge should not optimize from analogy while current measurements point elsewhere.

---

# Final recommendation

## B. Several targeted high-value references; no wholesale architecture adoption

EDOPro and WindBot have solved expensive adjacent problems for many years. OCGForge should mine those solutions where they overlap the engine boundary:

- protocol decoding;
- response encoding;
- compatibility edge cases;
- replay behavior;
- state transition examples;
- handcrafted game heuristics.

It should not inherit the assumptions of an interactive client or handcrafted bot where they conflict with deterministic research contracts.

No evidence justifies recommendation D (major OCGForge architecture reconsideration), and no single upstream subsystem is currently strong enough to justify a dedicated integration milestone as recommendation C. A short focused research/testing workstream is sufficient.

---

# Most important question

> If the OCGForge team spends one week studying or adapting knowledge from EDOPro/WindBot, what single activity provides the highest expected return without compromising OCGForge's architecture?

**Primary recommendation: construct the differential interactive decision-protocol corpus and completeness ledger.**

Why this wins:

- it targets correctness and decision completeness before performance/ML;
- it turns mature EDOPro protocol knowledge into independent evidence rather than a dependency;
- it identifies future fail-closed blockers early;
- it verifies exact response bytes against the pinned core;
- it strengthens continuation completeness;
- it creates the semantic action mapping needed later for WindBot benchmarks/teachers;
- it requires no OCGForge architecture compromise and no ML work.

The engine remains the final oracle throughout.

---

# Source manifest

Important inspected source files and symbols are recorded in the accompanying machine-readable `OCGForge_EDOPro_WindBot_Evidence_Index_2026-08-25.json`.

Core source links used repeatedly in this report:

- OCGForge protocol decoder: https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/message_decoder.cpp
- OCGForge response builder: https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/response_builder.cpp
- OCGForge continuation layer: https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/src/protocol/continuation.cpp
- OCGForge rules lock: https://github.com/chrismaghuhn/OCGForge/blob/588f02b4ef879fee999c921c114937a6a1e48557/third_party/rules_bundle.lock.json
- EDOPro duel client: https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/duelclient.cpp
- EDOPro client field: https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/client_field.cpp
- EDOPro replay: https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/replay.cpp
- EDOPro old replay: https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/replay_mode_yrp.cpp
- EDOPro WindBot launcher: https://github.com/edo9300/edopro/blob/54ea755aa0243e2f18bb6bd2187fc9b2f7e29788/gframe/windbot.cpp
- ygopro-core player operations: https://github.com/edo9300/ygopro-core/blob/46779fbe40e6a9bd8967f5dc6a03f4eaa6550d57/playerop.cpp
- WindBot GameBehavior: https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/Game/GameBehavior.cs
- WindBot GameAI: https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/ExecutorBase/Game/GameAI.cs
- WindBot Executor: https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/ExecutorBase/Game/AI/Executor.cs
- WindBot DefaultExecutor: https://github.com/ProjectIgnis/windbot/blob/fa0ae767967afc6a820784837f11cd3fabb9c47c/ExecutorBase/Game/AI/DefaultExecutor.cs

---

## Scope statement

No repository was modified. No branch, issue or pull request was created. No model was trained. No WindBot dependency was added. No M5 work was started. All implementation suggestions above are future research recommendations only.
