# Swordsoul Tenyi Teacher Strategy for the Exact OCGForge Locked Deck

**Research date:** 2026-08-28  
**OCGForge checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Deck:** `Swordsoul Tenyi ML v1`  
**Deck SHA-256:** `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7`  
**Main / Extra:** 40 / 15  
**Status:** strategy research and future policy-profile design only

## 1. Scope and source discipline

This report models the exact locked OCGForge deck. It does not substitute a current tournament list, Master Duel list, WindBot deck, or generic archetype combo guide.

Exact deck authority:

- [`fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json)
- [`tools/m3/locked_lists.py`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/tools/m3/locked_lists.py)
- [`docs/m3/CARD_COMPATIBILITY.md`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/docs/m3/CARD_COMPATIBILITY.md)
- OCGForge-pinned CardScripts commit `f337c87018ca723c1aded5143e616bb649555273`

Human strategy sources are evidence for line concepts and competitive heuristics only. They often contain absent cards such as Baronne de Fleur, Archnemeses Protos, Pot of Desires, Vessel for the Dragon Cycle, Nibiru, or newer Tenyi support. Those portions are not imported.

### 1.1 Important absent-card warning

The locked Extra Deck contains **no Baronne de Fleur**. The locked Main Deck contains **no Archnemeses Protos, Pot of Desires, Vessel for the Dragon Cycle, Nibiru, or Crossout Designator**.

A future Teacher profile must never score an endboard or recovery path around one of those cards.

## 2. Exact card list and strategic roles

Cards may have multiple legitimate roles. The role catalog should be immutable profile data rather than scattered hardcoded conditions.

### 2.1 Main Deck — monsters

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Swordsoul of Mo Ye | `20001443` | primary normal-summon starter; conditional one-card engine access; token generator; Level-8 bridge; draw resource; reveal dependency; follow-up body | Prefer when a legal Swordsoul/Wyrm reveal exists. The revealed card is not consumed, so rank reveal candidates by information exposure and future value rather than as a discard. Mo Ye plus its Token normally accesses Chixiao, Baxia, Draco Berserker, or Dragite. |
| 3 | Swordsoul Strategist Longyuan | `93490856` | two-card extender; alternative starter; discard outlet; Level-10 bridge; burn pressure; recovery after normal-summon interruption | Requires another discardable Swordsoul/Wyrm. Preserve the best remaining starter/follow-up; duplicates, spent Tenyis, or cards with grave value are preferred costs when payoff is equivalent. It creates Qixing or Chengying without using the Normal Summon. |
| 3 | Swordsoul of Taia | `56495147` | grave-enabled starter; one-card route after Emergence; token generator; Level-8 bridge; deck-to-GY setup; grind/follow-up | Requires a banishable Swordsoul/Wyrm on field/GY. Emergence in GY is standard fuel. When used as Synchro material, Taia sends a Swordsoul/Wyrm from Deck, enabling Mo Ye/Summit, Tenyi recursion, or another resource line. |
| 3 | Incredible Ecclesia, the Virtuous | `55273560` | going-second starter; conditional free extender; starter tutor from Deck; Normal Summon substitute; removal-dodge body | When the opponent controls more monsters, Special Summon Ecclesia to preserve the Normal Summon, then Tribute it during Main Phase to summon Mo Ye or Taia according to reveal/GY resources. Its Fusion-related End Phase recursion is not a planned engine in this locked list. |
| 3 | Tenyi Spirit - Ashuna | `87052196` | free Level-7 body; Monk bridge; Tenyi deck extender; Wyrm material; engine access; restriction source | Special Summon while no Effect Monster is controlled, then convert to Monk when non-effect state is needed. Its hand/GY effect summons a Tenyi from Deck but imposes a Wyrm-only Special Summon restriction for the rest of the turn. Track this explicitly; it excludes Dragite and other non-Wyrm payoffs. |
| 3 | Tenyi Spirit - Vishuda | `23431858` | free Level-7 body; Monk bridge; non-destruction board breaker; Wyrm material; discard resource | From hand/GY with a non-effect monster present, banish it to return an opponent card to hand. Going second, preserve Vishuda until the bounce removes a high-value public threat or forces interaction unless the body is needed for a higher-priority Synchro line. |
| 3 | Tenyi Spirit - Adhara | `98159737` | free Level-1 Tuner; Level-8 bridge with Level 7; banished-Wyrm recursion; Chaofeng bridge; grind resource | With a non-effect monster, banish from hand/GY to recover another banished Wyrm. Avoid consuming it before a valuable Longyuan/Tenyi recovery or Baxia-to-Chaofeng route, unless immediate board value dominates. |
| 1 | Tenyi Spirit - Shthana | `24557335` | free Level-4 body; non-effect-monster protection/recovery; removal trigger; Circle search target; soft extender | Useful as a Circle search after Tributing a targeted Mo Ye: it can join the surviving Token for a Level-8 Synchro. Its grave/hand effect can revive a destroyed non-effect monster and destroy an opponent monster. At one copy, preserve unique utility when relevant. |
| 3 | Ash Blossom & Joyous Spring | `14558127` | defensive hand trap; interaction; emergency Level-3 Tuner; Yazi material in rare lines | Primary role is interaction. Do not spend as material unless the hand-trap window has passed or the tactical payoff is clearly higher. Against Salamangreat, score searches/deck sends/deck summons by engine criticality and public replacement routes. |
| 3 | Effect Veiler | `97268402` | defensive hand trap; monster negate; emergency Level-1 Tuner; Synchro material | Preserve for a public choke point when interaction is scarce. It can contribute to unusual Synchro levels, but material use normally carries a high opportunity cost before the opponent's turn. |

### 2.2 Main Deck — spells and traps

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Swordsoul Emergence | `56465981` | primary searcher; one-card Taia setup; starter/follow-up tutor; level modulation when banished | Default searches a Swordsoul monster; while controlling a Synchro, its legal search range broadens to Wyrms. Emergence → Taia gives Taia banish fuel. Search priority depends on reveal, discard, GY, Normal Summon, and desired endboard—not a fixed card list. |
| 3 | Heavenly Dragon Circle | `51684157` | Quick-Play Wyrm tutor; targeted-negation dodge; body conversion; non-effect-monster payoff; grave Tenyi search; grind resource | Can Tribute a Wyrm as cost and search a Wyrm; after Tributing a non-effect monster it may instead Special Summon the searched Wyrm with its effects negated. Chaining Circle to interaction targeting Mo Ye can preserve Mo Ye's already-triggered Token effect and convert the body into another Wyrm. In GY, with a non-effect monster, Circle can banish itself to add a Tenyi. |
| 1 | Swordsoul Sacred Summit | `93850690` | recursion; recovery extender; Mo Ye/Taia revival; grind follow-up; level modulation when banished | High-value recovery when Taia sends Mo Ye or a starter was previously used. Rank the revive target by current line, reveal resources, restrictions, and whether a fresh trigger remains available. |
| 1 | Called by the Grave | `24224830` | hand-trap protection; grave-effect interaction; temporary same-name negate | Preserve for engine protection or a high-value Salamangreat grave effect. Its banish can also trigger Chengying when public conditions are met. |
| 3 | Infinite Impermanence | `10045474` | flexible monster negate; going-first set interaction; going-second board breaker; column interaction | Use public chain/board value, not a static card name. Avoid redundant negation of a monster already disabled or reserved for Chixiao. |
| 1 | Swordsoul Blackout | `14821890` | two-card opponent removal; own-Wyrm conversion; interruption; Chixiao search target; banished-token extender | Activation needs one own face-up Wyrm plus two opponent cards. Material/target selection is a combined resource decision: sacrifice the most expendable own Wyrm and remove the highest combined public threat pair. When banished, it generates a Swordsoul Token; Chixiao can deliberately banish it only when the extension is worth losing the set interaction. |

### 2.3 Extra Deck

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Monk of the Tenyi | `32519092` | non-effect Link-1; Tenyi enabler; expendable board resource; linked-zone/placement resource | Converts a Tenyi body into a non-effect monster and places the Tenyi in GY. Preserve a useful linked zone and avoid blocking follow-up. A spent Monk is often a good Baxia destruction target or Blackout own-Wyrm resource if the line permits. |
| 1 | Shaman of the Tenyi | `78917791` | grave recovery; discard outlet; late-line extender; battle-trigger removal support | Discard to revive a Wyrm but imposes a severe Extra Deck effect-activation restriction for the rest of the turn except Tenyi. Use as a recovery/late-line tool, not an automatic bridge. One-copy budget is material. |
| 2 | Swordsoul Grandmaster - Chixiao | `69248256` | primary Level-8 payoff; Swordsoul search/banish; monster negate; line bridge; interaction | Going first, usually the default first Level 8 because it converts a starter into search plus interaction. Search Longyuan, Blackout, Summit, Emergence, or another Swordsoul according to resources. Its negate consumes a Swordsoul/Wyrm by banishing; preserve future engine where possible. Two-copy budget supports grind but is not unlimited. |
| 2 | Baxia, Brightness of the Yang Zing | `83755611` | non-destruction board breaker; multi-target shuffle; own-card conversion; Level-4-or-lower revival; recovery bridge; Chaofeng material | Going second, prefer when its on-summon shuffle meaningfully breaks the board. The number of targets depends on distinct original attributes among Wyrm materials: Taia + Token or suitable Tenyi materials can outperform Mo Ye + Token. Its destroy-own/revive effect converts Monk, Token, or another expendable card into Mo Ye/Taia/Ecclesia. |
| 2 | Draco Berserker of the Tenyi | `5041348` | monster-effect pressure; non-negating banish interaction; battle removal; repeated attack; lethal pressure | It banishes an opponent monster whose effect activates but does not negate that effect. Value it when removing the body matters even if the effect resolves, or when battle pressure exceeds Chixiao's search. Two copies allow repeated midgame use. |
| 1 | Yazi, Evil of the Yang Zing | `43202238` | board-break bridge; self-destruction conversion; Wyrm tutor from Deck; awkward-level recovery | Can target a controlled Yang Zing—including itself—and an opponent card for destruction, then replace itself with a Wyrm from Deck when destroyed. Access is less routine in this list; use only when legal materials and board-breaking value are explicit. |
| 1 | Adamancipator Risen - Dragite | `9464441` | Spell/Trap activation negate; matchup-specific interaction; Level-8 alternative | Its relevant locked-deck role is the Spell/Trap negate while a WATER monster is in GY, usually Mo Ye. The deck has no meaningful Main Deck Rock package for the excavation effect. Ashuna's Wyrm-only restriction makes Dragite unavailable after Ashuna's deck-summon effect. |
| 1 | Chaofeng, Phantom of the Yang Zing | `19048328` | attribute lock; Level-9 payoff; tuner recovery; specialized protection line | Baxia + Adhara can produce Chaofeng with a LIGHT Yang Zing material, suppressing LIGHT monster effects. In this exact matchup that can stop Effect Veiler but does not broadly shut the FIRE Salamangreat engine, so it is situational rather than a default endboard. |
| 1 | Swordsoul Sinister Sovereign - Qixing Longyuan | `47710198` | Level-10 interaction; monster banish; Spell/Trap source banish; burn; draw engine; pressure | Strong against Salamangreat's repeated Special Summons and Spell/Trap activations. It does not negate the triggering effect; target/source removal and damage must be valued separately. If summoned before another Wyrm Synchro, its draw trigger adds resource value. |
| 1 | Swordsoul Supreme Sovereign - Chengying | `96633955` | Level-10 resilient payoff; banish scaling; destruction replacement; field-and-GY removal; battle pressure | Prefer when banish synergies and public field/GY targets are valuable or resilience is needed. Chixiao cost, Called by, Vishuda/Adhara, and other banishes can trigger its removal. Destruction replacement consumes own GY/field resources and must preserve follow-up. |

## 3. Deck identity and strategic character

**PLAYER CONSENSUS / HEURISTIC.** Swordsoul Tenyi is a midrange Synchro strategy: compact starter-to-Synchro conversion, Tenyi bodies and grave effects, a small number of high-quality interactions, and recoverable Wyrm resources.

**INFERENCE FOR THE LOCKED LIST.** This exact 40/15 list emphasizes:

- unusually dense access to Mo Ye/Taia/Ecclesia;
- three each of Ashuna, Vishuda, Adhara, Circle, Emergence, Veiler, Ash, and Impermanence;
- only one Blackout, Summit, Shthana, Shaman, Dragite, Chaofeng, Qixing, and Chengying;
- no Baronne/Protos high-roll endboard;
- repeated Level-8 access, two Chixiao/Baxia/Draco copies, and a deliberate grind/board-break Extra Deck.

The Teacher should therefore optimize for robust public-state conversion and recovery rather than a single maximal combo.

## 4. Strategic resources

### 4.1 Core resource vector

The profile should expose at least:

```text
normal_summon_available
mo_ye_access
legal_reveal_quality
taia_banish_fuel
longyuan_discard_quality
ekklesia_free_summon_condition
tenyi_free_body_access
non_effect_monster_present
ashuna_wyrm_lock_active
swordsoul_token_present
synchro_only_extra_lock_while_token_present
level_8_route_count
level_9_route_count
level_10_route_count
water_monster_in_gy_for_dragite
banished_wyrm_recovery_value
blackout_own_wyrm_quality
summit_revival_value
extra_deck_copy_budget
remaining_interaction
next_turn_follow_up
```

### 4.2 Reveal quality

Mo Ye's reveal does not consume the card, but it reveals information and temporarily exposes hand identity. Recommended profile ranking, all else equal:

1. a duplicate or already-public Swordsoul/Wyrm;
2. a card whose identity is strategically obvious from prior public search;
3. a flexible Wyrm not relied on as the only discard/extender;
4. the only Longyuan discard, Taia recovery, or follow-up resource only if necessary.

This is a heuristic, not a legality filter.

### 4.3 Longyuan discard quality

Recommended generic/deck modifiers:

```text
prefer:
- duplicate Longyuan/Taia/Mo Ye not needed for active line
- spent/search-revealed Tenyi whose GY effect remains useful
- Vishuda when going second bounce remains usable from GY
- Ashuna when its GY extension is planned and restriction accepted
- resource recoverable by Adhara/Summit

preserve:
- only legal Mo Ye reveal
- only starter after expected interruption
- only live hand trap when interaction is scarce
- only Taia banish/Summit follow-up
- Shthana when its unique recovery is active
```

The exact order is state-dependent. No card receives a universal “always discard” tag.

### 4.4 Taia/Chixiao banish quality

Prefer resources that are already spent, duplicate, or recoverable by Adhara. Avoid banishing the only future starter or only public interaction source unless the immediate negate/line payoff dominates.

Chixiao deliberately banishing Blackout is an extension branch only when:

- the resulting Token advances a higher-value line;
- losing set Blackout is acceptable;
- zone and Synchro restrictions are compatible;
- the current complete domain supplies the required actions.

## 5. Opening and phase goals

### 5.1 Going first

Priority goals:

1. establish at least one reliable interaction;
2. preserve a second independent interaction or follow-up where resources allow;
3. avoid exhausting all starters/Extra Deck copies;
4. stop before a marginal extension converts a stable board into an exposed one.

Typical preferred outcomes in this exact list:

- Chixiao + Qixing + retained hand interaction;
- Chixiao + Chengying where banish/removal resilience is superior;
- Chixiao + set Blackout + follow-up;
- Qixing or Chengying + Blackout/hand interaction when normal starter was interrupted;
- Chixiao alone plus multiple hand traps as an acceptable low-resource board.

### 5.2 Going second

Priority goals:

1. identify and force/clear the opponent's active interaction;
2. use Vishuda, Baxia, Yazi, Impermanence/Veiler, and battle pressure efficiently;
3. preserve a starter after the first interruption;
4. stabilize with Chixiao/Qixing/Chengying rather than overextend blindly;
5. take deterministic lethal when public state proves it.

### 5.3 Grind state

Goals:

- convert Taia + grave fuel into another Level 8;
- use Summit/Baxia/Shaman only when their restrictions and copy budget are justified;
- use Adhara to recover the highest-value banished Wyrm;
- keep at least one Chixiao/Baxia/Draco route where possible;
- use Chengying to convert routine banishes into field/GY pressure;
- avoid spending the only Monk or Shaman recovery line without payoff.

## 6. Normal Summon and starter priorities

### 6.1 Default ordering is conditional

A static `Mo Ye > Taia > Ecclesia` order is insufficient.

Recommended conditional comparison:

| Public resources | Preferred starter intent |
|---|---|
| Mo Ye + legal reveal | Mo Ye usually maximizes immediate Chixiao + draw access |
| Emergence but no direct starter | Search Taia, Normal Taia, banish Emergence; baseline Chixiao line |
| Taia + useful grave fuel | Taia can be superior, especially going second with Baxia and a valuable deck send |
| Opponent controls more monsters + Ecclesia | Special Ecclesia to preserve Normal Summon, then summon Mo Ye or Taia from Deck |
| Tenyi free body + starter | Establish Monk/non-effect/Tenyi resources before committing the Normal Summon where restrictions and zones permit |
| Only Longyuan + legal discard | Use the Level-10 side line and preserve Normal Summon for a later topdeck/recovery if possible |

### 6.2 Ecclesia target choice

Choose Mo Ye when:

- a legal reveal exists;
- Chixiao search/draw is the desired bridge;
- Taia lacks valuable banish fuel.

Choose Taia when:

- grave/field banish fuel exists;
- Baxia's distinct-attribute shuffle is valuable;
- Taia's deck send enables Summit/Mo Ye/Tenyi recovery;
- Mo Ye reveal quality is poor.

## 7. Tenyi setup

### 7.1 Core public-state predicates

Tenyi hand Special Summons and grave effects require control of no Effect Monsters or a face-up non-effect monster as defined by their exact scripts. Monk is the primary stable enabler.

The Teacher should track:

- whether an Effect Monster is currently controlled;
- whether a legal Tenyi Special Summon candidate exists;
- whether converting the current body to Monk improves or blocks the next zone/material state;
- whether Ashuna's Wyrm lock is acceptable;
- which Tenyi remains in Deck for Ashuna;
- which banished Wyrm Adhara can recover;
- whether Vishuda's bounce is worth more than using it as material.

### 7.2 Common Tenyi resource pattern

```text
free Tenyi body
→ Monk
→ enable hand/GY Tenyi effect
→ assemble Level-8 or board-break resources
→ preserve Normal Summon for Swordsoul starter
```

This is a dependency pattern, not an exact script. The Teacher must re-evaluate after every legal candidate frame.

### 7.3 Restriction ordering

Ashuna's deck-summon effect imposes a Wyrm-only Special Summon restriction for the rest of the turn. Therefore:

- Dragite becomes unavailable afterward;
- existing Wyrm Synchro/Link lines remain possible;
- any planned non-Wyrm payoff must occur before Ashuna or be abandoned;
- plan state must be invalidated if the restriction conflicts with the active line.

The Swordsoul Token's non-Synchro Extra Deck restriction exists while the Token remains face-up. Do not attempt a Monk/Shaman line while the Token still imposes that restriction unless the environment supplies a legal route after the Token leaves.

## 8. Synchro payoff priorities

### 8.1 Level 8

| Payoff | Prefer when | Avoid / lower priority when |
|---|---|---|
| Chixiao | going first; search/negate bridge needed; Longyuan/Blackout/Summit access valuable | search targets exhausted; immediate non-destruction board break is more urgent |
| Baxia | going second; one or more high-value cards can be shuffled; Taia/Tenyi materials increase target count; revive line is valuable | no meaningful target; going-first search/interaction is needed more |
| Draco Berserker | opponent monster-effect body removal and battle pressure matter; search engine already secured | the effect must be negated rather than merely removing its body; follow-up search is missing |
| Dragite | Mo Ye is in GY; Spell/Trap activation negate is high value; no Ashuna Wyrm lock | WATER condition absent; Wyrm lock active; Chixiao/Baxia has higher immediate value |

### 8.2 Level 9

Chaofeng is a specialized line, usually via Baxia + Adhara. In the exact Salamangreat matchup, a LIGHT lock primarily affects Effect Veiler rather than the FIRE engine. Its score should therefore depend on:

- whether LIGHT hand-trap suppression still matters;
- whether Baxia has already produced value;
- whether consuming Adhara loses needed recursion;
- whether a stronger Level-10/interaction board is available.

It should not be an automatic first-turn goal.

### 8.3 Level 10

| Payoff | Prefer when |
|---|---|
| Qixing Longyuan | opponent's plan is rich in Special Summons and Spell/Trap activations; a later Wyrm Synchro can trigger its draw; burn matters; removing activation sources/bodies has high tempo value |
| Chengying | current/public future banishes can trigger field+GY removal; opponent grave recursion is valuable; destruction resilience and battle scaling matter; Qixing's non-negating removal is less useful |

The Teacher should recognize that neither is a generic omni-negate. Endboard evaluation must use their actual interaction semantics.

## 9. Search, send, and recovery priorities

### 9.1 Chixiao search/banish

Candidate preference families:

```text
need Level-10 side line + legal discard
→ Longyuan

need interaction and own face-up Wyrm will remain
→ Blackout

need revive/recovery and target exists
→ Sacred Summit

need flexible next starter/search chain
→ Emergence

need immediate Token extension and losing trap is justified
→ banish Blackout
```

The actual legal candidate domain determines available options.

### 9.2 Taia send

Useful send classes:

- Mo Ye for Summit/Baxia/Shaman revival;
- Ashuna/Vishuda/Adhara when their grave effects advance the current line;
- a Swordsoul/Wyrm needed as Chixiao negate or Chengying protection fuel;
- a duplicate future resource when no stronger setup exists.

Do not send a card merely because a static list ranks it first; confirm that its grave function and restrictions are currently valuable.

### 9.3 Baxia conversion

Prefer an own destruction target that is:

- spent;
- replaceable;
- no longer needed for interaction;
- beneficial to move off field;
- compatible with the revive target and zone count.

Typical candidates include Monk, a spent body, or a Token when destroying it improves the board. Preserve Chixiao/Qixing/Chengying or the only Blackout anchor unless the recovery payoff is clearly higher.

## 10. Interaction timing against the exact Salamangreat deck

This section is matchup policy, not hidden-hand prediction.

### 10.1 Public choke-point classes

Potential high-value public Salamangreat effects include:

- Salamangreat of Fire search;
- Gazelle's deck send;
- Miragestallio's deck summon and Link-material bounce;
- Balelynx's Sanctuary search when reincarnation access is otherwise absent;
- Sunlight Wolf's FIRE or Spell/Trap recovery;
- Promethean Princess's revive or grave removal trigger;
- Raging Phoenix's reincarnation search;
- Code of Soul's opponent-turn Link route;
- Roar/Rage when their current resolution materially breaks the Swordsoul line.

### 10.2 Timing model

For Ash, Veiler, Impermanence, Chixiao, Called by, Qixing, Dragite, and Blackout, compare:

```text
current effect resolution threat
opponent public investment
public replacement routes already visible
own interaction scarcity
whether effect/body is already answered
future public choke value
lethal/stabilization consequence
```

Examples:

- Negating Salamangreat of Fire is more valuable when it is the only public engine access and the opponent has invested the Normal Summon; less valuable if Gazelle/Circle/Mining access is already public.
- Negating Gazelle's send is high value when Roar/Rage/Spinny/Weasel setup is absent; less decisive when equivalent grave resources are already public.
- Qixing removing a newly Special Summoned body does not negate its trigger. Use it when removing the body/link material matters despite the trigger resolving.
- Dragite can negate a critical Spell/Trap activation only while a WATER monster, normally Mo Ye, is in GY; do not assume the condition.
- Blackout should remove a pair whose combined engine/interaction value exceeds the own-Wyrm cost and any destruction triggers the opponent may exploit.

## 11. Material selection

### 11.1 Generic priority

Use, when equivalent:

1. Swordsoul Token;
2. spent starter whose material trigger is valuable;
3. Tenyi whose grave effect is planned;
4. duplicate or recoverable body;
5. interaction monster only after its interaction value is exhausted.

Preserve:

- the only Longyuan discard;
- the only Taia banish resource;
- Adhara needed to recover a key banished Wyrm;
- Vishuda needed to remove a public threat;
- an unused hand trap when opponent's turn remains;
- a body required as Blackout anchor;
- the last Extra Deck copy needed for recovery.

### 11.2 Payoff-specific material considerations

- Baxia values distinct original Wyrm attributes; Taia + Token or Tenyi combinations may shuffle more cards than Mo Ye + Token.
- Chaofeng's useful attribute lock depends on Yang Zing material attributes; Baxia as LIGHT material is the key intended route.
- Dragite is non-Wyrm and conflicts with Ashuna's Wyrm lock.
- Shaman's revive creates a later Extra Deck effect-activation restriction; material/line scoring must price that cost.

## 12. Line-family catalog

These are partial-order strategic line families. Every step is conditional on the current legal candidate domain.

### SW-L01 — Mo Ye / Chixiao foundation

| Field | Definition |
|---|---|
| Strategic goal | Convert Mo Ye + legal reveal into search, draw, Level-8 interaction, and optional Level-10 follow-up |
| Required public resources | Mo Ye access; unused Normal Summon or legal Special Summon; legal Swordsoul/Wyrm reveal; zone for Token |
| Optional resources | Longyuan or searchable Longyuan + discard; Blackout; Summit; hand interaction |
| Primary starter | Mo Ye |
| Extenders | Longyuan; Tenyi bodies; Summit; Ecclesia access |
| Resource cost | Normal Summon; reveal information; Token/Synchro materials; possible Longyuan discard |
| Intermediate goals | `TOKEN_ACCESS`; `LEVEL8_ACCESS`; `CHIXIAO_ESTABLISHED`; `SEARCH_RESOLVED` |
| Preferred payoff | Chixiao plus Qixing/Chengying or Chixiao + Blackout/hand interaction |
| Follow-up retained | Taia/Summit/Emergence/Tenyi or an unused starter where possible |
| Vulnerabilities | Mo Ye negate/removal; Token denial; Chixiao search interruption; Longyuan discard loss |
| Recovery | Circle conversion; Longyuan side line; Tenyi/Monk line; stop on Chixiao/interaction |
| Stop conditions | No valuable extension; extending consumes all interaction/follow-up; current board already satisfies safe interaction goal |

### SW-L02 — Emergence / Taia baseline

| Field | Definition |
|---|---|
| Strategic goal | Produce a Level-8 Swordsoul line from Emergence without requiring Mo Ye access |
| Required public resources | Emergence; unused Normal Summon; Taia available in Deck; zone for Token |
| Primary starter | Emergence → Taia |
| Resource cost | Emergence becomes Taia's banish fuel; Normal Summon |
| Intermediate goals | `TAIA_FUEL_READY`; `TOKEN_ACCESS`; `LEVEL8_ACCESS`; `DECK_SEND_READY` |
| Preferred payoff | Chixiao + searched Blackout/Summit; Baxia going second; Mo Ye sent for recovery |
| Follow-up retained | Taia/Mo Ye in GY; searched interaction/revive |
| Vulnerabilities | Emergence negation; Taia negate/removal; grave banish denial |
| Recovery | Longyuan if present; Tenyi line; preserve interaction and stop |
| Stop conditions | Taia cannot legally create Token; no meaningful Level-8 candidate; continuation would expose more than it gains |

### SW-L03 — Ecclesia going-second access

| Field | Definition |
|---|---|
| Strategic goal | Preserve Normal Summon and turn Ecclesia into the correct Swordsoul starter while breaking/stabilizing |
| Required public resources | Opponent controls more monsters for free Ecclesia Special Summon, or a legal Normal Summon route; Main Phase |
| Primary starter | Ecclesia |
| Target choice | Mo Ye with reveal; Taia with banish fuel/board-break value |
| Preferred payoff | Baxia/Chixiao followed by retained Normal Summon or Longyuan line |
| Vulnerabilities | Ecclesia negate/removal; summoned starter negate |
| Recovery | Use preserved Normal Summon; Longyuan; Tenyi; generic board-break interaction |
| Stop conditions | Tributing Ecclesia would consume the only body without a supported target or worsen lethal exposure |

### SW-L04 — Longyuan independent Level-10 line

| Field | Definition |
|---|---|
| Strategic goal | Establish Qixing/Chengying without relying on Normal Summon starter resolution |
| Required public resources | Longyuan; another discardable Swordsoul/Wyrm; two monster zones |
| Optional resources | Future normal starter; later Wyrm Synchro for Qixing draw; banish enabler for Chengying |
| Resource cost | One discard; Longyuan + Token materials |
| Preferred payoff | Qixing against repeated summons/S/T; Chengying against board/GY and banish synergy |
| Follow-up retained | Normal Summon and best remaining starter if discard choice is sound |
| Vulnerabilities | Longyuan activation negate; zone denial; Token denial |
| Recovery | Use preserved Normal Summon; Tenyi; set interaction |
| Stop conditions | Only discard is the sole playable follow-up and Level-10 payoff does not stabilize enough |

### SW-L05 — Tenyi / Monk Level-8 line

| Field | Definition |
|---|---|
| Strategic goal | Use free Tenyi bodies and Monk to create Level-8 access, board breaking, or engine bridge while preserving Normal Summon |
| Required public resources | Tenyi Special Summon candidate; no Effect Monster controlled; compatible zone |
| Optional resources | second Tenyi; Ashuna GY effect; Adhara; Vishuda; normal Swordsoul starter |
| Resource cost | Monk copy; possible Ashuna Wyrm lock; Tenyi banishes |
| Intermediate goals | `MONK_ESTABLISHED`; `TENYI_GY_ONLINE`; `LEVEL8_ACCESS`; `NORMAL_SUMMON_PRESERVED` |
| Preferred payoff | Chixiao for engine; Baxia for board; Draco for pressure; Chaofeng setup only when justified |
| Vulnerabilities | interruption on Ashuna/Vishuda/Adhara; zone blocking; restriction conflict |
| Recovery | Normal starter remains; use Vishuda board break; Adhara recursion; stop on Monk + interaction if necessary |
| Stop conditions | Ashuna lock blocks superior required payoff; no second body; Extra Deck copy budget too costly |

### SW-L06 — Baxia board-break and revival

| Field | Definition |
|---|---|
| Strategic goal | Convert a Level-8 Synchro into non-destruction removal and revive a Level-4-or-lower starter |
| Required public resources | Legal Baxia Synchro; meaningful shuffle targets; optional expendable own card and revive target |
| Preferred materials | Taia/Token or distinct-attribute Wyrms when additional shuffle target is valuable |
| Resource cost | Baxia copy; own-card destruction for revive branch |
| Preferred payoff | Cleared engine cards plus Mo Ye/Taia/Ecclesia revival into another Synchro line |
| Vulnerabilities | Baxia negation/removal; target leaves; revive denial; zone loss |
| Recovery | Retain cleared tempo; normal starter; Longyuan; interaction-preserving stop |
| Stop conditions | No effective shuffle target; own destruction would consume critical interaction/follow-up |

### SW-L07 — Taia / Summit grind recovery

| Field | Definition |
|---|---|
| Strategic goal | Re-enter engine from grave resources without requiring the ideal opening |
| Required public resources | Taia + banish fuel or Summit + valid revive target |
| Optional resources | Adhara recovery; Chixiao second copy; Baxia revival |
| Resource cost | banished resource; Summit; Extra Deck copy |
| Preferred payoff | Chixiao/Baxia/Draco plus next-turn resource |
| Vulnerabilities | graveyard interaction; banish denial; exhausted Extra Deck copies |
| Recovery | Shaman late line; Longyuan; battle/interaction stop |
| Stop conditions | only banish target is indispensable; no remaining valuable payoff copy |

### SW-L08 — Chaofeng specialized line

| Field | Definition |
|---|---|
| Strategic goal | Convert Baxia + Adhara into a LIGHT-effect lock and Level-9 body |
| Required public resources | Baxia; Adhara Tuner; legal Level-9 Synchro; no conflicting restriction |
| Resource cost | Baxia and Adhara; foregone Baxia body/Adhara recursion |
| Matchup value | Mainly suppresses Effect Veiler in exact Salamangreat list; secondary recovery/battle effects |
| Recovery | Continue normal Swordsoul starter if preserved |
| Stop conditions | opponent's relevant LIGHT interaction window has passed; Qixing/Chengying/Chixiao line provides greater value |

### SW-L09 — Blackout interaction line

| Field | Definition |
|---|---|
| Strategic goal | Trade one expendable own Wyrm for two high-value opponent cards |
| Required public resources | Set/activatable Blackout; own face-up Wyrm; two legal opponent targets |
| Material selection | spent Monk/low-value Wyrm/replaceable body before critical payoff or follow-up anchor |
| Target selection | maximize combined engine interruption, interaction removal, and tempo; account for Princess/Raging destruction triggers |
| Vulnerabilities | target removal; own body removal; opponent benefiting from FIRE destruction |
| Recovery | preserve another interaction; exploit resulting banish/destruction synergies where applicable |
| Stop conditions | only own target is essential; target pair is low value; destruction would strengthen opponent more than it disrupts |

### SW-L10 — Lethal / battle conversion

| Field | Definition |
|---|---|
| Strategic goal | Convert cleared board and Level-8/10 pressure into deterministic lethal while preserving Main Phase 2 fallback |
| Required public resources | public ATK/DEF/LP and legal battle candidates sufficient for lethal or favorable removal |
| Preferred tools | Chengying scaling; Draco repeated attack; Qixing/Longyuan burn; Vishuda/Baxia removal |
| Vulnerabilities | known public interaction; battle position; overcommitting before damage |
| Recovery | take safe threat removal, enter Main Phase 2, establish interaction |
| Stop conditions | lethal is not proven; attack line sacrifices a higher-value stable position |

## 13. Interruption recovery rules

### 13.1 Starter interrupted

When Mo Ye, Taia, or Ecclesia is negated or removed:

1. reconcile the public board and effect-use state;
2. invalidate any line that still assumes the Token or starter body exists;
3. evaluate Circle if it can convert/dodge the interaction;
4. evaluate Longyuan independent Level-10 access;
5. evaluate preserved Tenyi/Normal Summon routes;
6. otherwise set/hold interaction and stop without fabricating a combo.

### 13.2 Extender interrupted

If Longyuan or Ashuna is stopped:

- retain the established Chixiao/Baxia/Monk value;
- do not spend the remaining starter merely to approximate the old endboard;
- choose between a smaller interaction board and a different line based on current complete domain;
- mark recovery provenance rather than claiming the original line continued.

### 13.3 Payoff unavailable

If a planned Extra Deck copy is absent or no longer legal:

- never infer a missing candidate;
- recompute by level and goal class;
- use Chixiao/Baxia/Draco/Dragite/Qixing/Chengying according to current candidates, restrictions, and copy budget;
- if no payoff improves the board, preserve resources and stop.

## 14. Scenario-family behavior

| Scenario | Expected architecture behavior |
|---|---|
| Ideal opening | Select SW-L01 or SW-L05 according to reveal/Tenyi resources; build interaction plus follow-up, not maximum summon count |
| Weak playable opening | Emergence → Taia baseline, Longyuan side line, or one Tenyi bridge; accept Chixiao + interaction |
| Brick-like opening | Preserve hand traps, set legal interaction, avoid meaningless body/material consumption; use explicit fallback provenance |
| Going first | Prioritize Chixiao, Qixing/Chengying, Blackout, and retained hand interaction |
| Going second | Prefer Ecclesia free access, Vishuda, Baxia/Yazi, Impermanence/Veiler, then stabilize |
| Starter interrupted | Reconcile and pivot through Circle, Longyuan, Tenyi, or safe stop |
| Extender interrupted | Keep achieved board and follow-up; do not continue queued materials/targets |
| Key Extra Deck payoff unavailable | Replan by current legal level/payoff family and copy budget |
| Board partially broken | Taia/Summit/Baxia/Shaman recovery, preserving remaining copies |
| Grind state | Recycle banished Wyrm with Adhara; use second Chixiao/Baxia/Draco deliberately |
| Lethal opportunity | Prove lethal from public state, order removal/attacks, preserve Main Phase 2 if lethal fails |
| Multiple opponent threats | Target vector balances engine source, interaction, recursion, and replacement availability |
| Continuation/material choice | Rank all legal selections; consume spent/recursive resources and preserve unique follow-up |

The shared combo/recovery report expands these into cross-deck stress tests.

## 15. Confidence recommendations

### High confidence

- Mo Ye + legal reveal into Chixiao with a clear search/payoff goal;
- Emergence → Taia baseline with confirmed grave fuel;
- Longyuan + clearly expendable discard into the correct Level 10;
- Vishuda/Baxia target choice with a dominant public threat;
- Blackout target/material tuple with a clear two-for-one and safe own cost;
- deterministic lethal.

### Medium confidence

- multiple valid Chixiao search branches with similar follow-up;
- recovery through Baxia/Shaman/Summit;
- specialized Chaofeng or Dragite choice;
- interaction timing where public replacement routes are ambiguous.

### Low/fallback

- no named line applies;
- target/material values are equal under known public facts;
- only generic safe utility distinguishes actions;
- bytewise key tie-break decides.

Only admitted high-confidence and separately validated medium-confidence decisions should be default future Behavior Cloning labels.

## 16. Source-derived facts, heuristics, and inferences

### SOURCE FACT

- exact counts/hashes and certified matchup come from OCGForge;
- card mechanics and restrictions come from the pinned CardScripts;
- the public Teacher must receive only the public observation and complete candidate domain;
- official coverage demonstrates real competitive use of Mo Ye/Chixiao/Longyuan/Qixing/Blackout, Tenyi/Monk, Circle conversion, and Taia/Baxia/Mo Ye recovery.

### PLAYER CONSENSUS / HEURISTIC

- Chixiao is the normal first-turn Level-8 bridge;
- Baxia is a high-value going-second/recovery payoff;
- Longyuan is an independent extender and Level-10 bridge;
- Heavenly Dragon Circle can convert interaction on a Swordsoul body;
- Tenyi bodies can preserve the Normal Summon and create Level-8 access;
- safe follow-up is often more valuable than one extra body.

### INFERENCE FOR OCGFORGE

- Qixing has unusually high matchup relevance because the exact Salamangreat list repeatedly Special Summons and activates engine Spells/Traps;
- Chaofeng is narrower than in generic Swordsoul guides because the exact opponent engine is predominantly FIRE, with Effect Veiler the main LIGHT interaction;
- Dragite is a deliberate but conditional anti-Spell/Trap option because Mo Ye supplies the practical WATER-in-GY condition and Ashuna can lock it out;
- three Taia, three Circle, two Baxia, and one Summit make interruption recovery a first-class profile concern;
- the Teacher should preserve Extra Deck copy budgets because the deck is intended to generate multi-turn trusted trajectories, not only opening-board labels.

## 17. Selected human strategy evidence

The evidence index contains full metadata and limitations. Particularly useful sources include:

- [Official 2023 Round 7 feature match](https://yugiohblog.konami.com/2023/ycs/round-7-feature-match-steven-gleason-vs-sathe-khaldi/) — Tenyi/Baxia line, interruption, Ecclesia/Taia recovery, Baxia revive, Mo Ye, Longyuan, Qixing, Chixiao, Blackout.
- [Official 2023 Top 4 feature match](https://yugiohblog.konami.com/2023/ycs/top-4-feature-match-stephen-silverman-team-back-for-seconds-vs-joseph-gold-kentucky-boyz/) — Emergence/Longyuan/Qixing, Taia/Baxia, Adhara recovery, Mo Ye revive, Chixiao/Blackout.
- [Official 2022 Round 10 feature match](https://yugiohblog.konami.com/2022/ycs/round-10-feature-match-jingyang-guo-vs-bryan-aguilar/) — Mo Ye/Longyuan/Chixiao/Qixing and Circle Tributing an exposed Chixiao.
- [Official 2022 Dragon Duel Finals](https://yugiohblog.konami.com/2022/championships/na-ygoc-2022/dragon-duel-finals-feature-match-aj-d-versus-connor-r/) — Mo Ye/Chixiao search adaptation and Qixing line.
- [Official 2022 Dragon Duel Round 4](https://yugiohblog.konami.com/2022/championships/na-ygoc-2022/dragon-duel-round-4-feature-match-aj-d-versus-connor-r/) — Ashuna/Monk/Adhara resource loop and Blackout/Qixing endboard.
- [YGOPRODeck Tenyi Swordsoul metagame snapshot](https://ygoprodeck.com/article/tcg-meta-snapshot-tenyi-swordsoul-may-2022-282839) — free Level-8 Tenyi route, Chixiao/Baxia/Draco role distinctions, Blackout, Circle, Dragite condition. Contains absent Halq/Baronne/Vessel material; only overlapping concepts are used.
- [YGOPRODeck rookie Tenyi Swordsoul primer](https://ygoprodeck.com/deck/rookie-s-tenyi-swordsoul-520131) — detailed Baxia/Adhara/Chaofeng dependency example. Format/list differs.
- [YGOPRODeck BODE metagame report](https://ygoprodeck.com/article/tcg-bode-metagame-tournament-report-weeks-3-4-229389) — Circle response to Veiler/Impermanence and Tenyi/Chaofeng context. Contains absent cards; used only for the overlapping interaction concept.
- [TCGplayer 2022 Nationals overview](https://www.tcgplayer.com/content/article/What-Decks-Won-Yu-Gi-Oh-Nationals-In-2022/353a5135-4baa-4328-8df3-057d1b0192ef/) — competitive Tenyi Swordsoul card-role context; lists differ.

## 18. Profile implementation boundary

A future `SwordsoulTenyiStrategyProfile` should own:

- exact deck/hash/matchup IDs;
- the role catalog above;
- SW-L01 through SW-L10 line and recovery definitions;
- search/send/reveal/discard/banish/material modifiers;
- Extra Deck copy budgets;
- public Salamangreat interaction map;
- integer preference tables;
- confidence rules.

It must not own:

- card legality;
- target/material enumeration;
- hidden opponent state;
- internal semantic keys;
- raw responses;
- candidate filtering;
- exact callback scripts.

## 19. Unresolved Swordsoul questions for implementation validation

1. Does the current public history expose every temporary restriction and relevant effect-use fact needed for Ashuna, Tokens, Shaman, Chixiao, Baxia, Qixing, and Chengying without policy-private engine queries?
2. Which public candidate fields distinguish Chixiao add-vs-banish and Blackout multi-selection continuations sufficiently for a generic evaluator?
3. Which engine-reachable fixtures best certify Circle's interaction-conversion branches under the locked matchup?
4. How frequently does Chaofeng outperform a simpler Chixiao/Qixing/Chengying plan against this exact Salamangreat profile?
5. What frozen integer margins separate safe Blackout two-for-one decisions from destruction lines that activate Princess/Raging recovery?
6. How should the scenario suite expose exhausted second-copy Chixiao/Baxia/Draco states without introducing a general hidden board-construction API?

These are acceptance-design questions, not permission to weaken the public boundary.
