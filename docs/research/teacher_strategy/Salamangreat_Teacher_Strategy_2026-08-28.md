# Salamangreat Teacher Strategy for the Exact OCGForge Locked Deck

**Research date:** 2026-08-28  
**OCGForge checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Deck:** `Salamangreat ML v1`  
**Deck SHA-256:** `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188`  
**Main / Extra:** 40 / 15  
**Status:** strategy research and future policy-profile design only

## 1. Scope and source discipline

This report models the exact locked OCGForge Salamangreat deck. It does not substitute an older WindBot list, a generic Soulburner structure-deck list, a Snake-Eye FIRE pile, or a current tournament deck.

Exact deck authority:

- [`fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json)
- [`tools/m3/locked_lists.py`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/tools/m3/locked_lists.py)
- [`docs/m3/CARD_COMPATIBILITY.md`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/docs/m3/CARD_COMPATIBILITY.md)
- OCGForge-pinned CardScripts commit `f337c87018ca723c1aded5143e616bb649555273`

Human strategy evidence often includes absent cards such as Lady Debug, Parallel eXceed, EM:P Meowmine, S:P Little Knight, Cyberse Wicckid, Worldsea Dragon Zealantis, Accesscode Talker, Splash Mage, Update Jammer, or newer side-deck tools. Only overlapping Salamangreat mechanics and line dependencies are used.

### 1.1 Exact-list warning

The locked Extra Deck contains only:

```text
Balelynx ×3
Sunlight Wolf ×3
Raging Phoenix ×2
Pyro Phoenix ×2
Heatleo ×1
Miragestallio ×2
Promethean Princess ×1
Hiita ×1
```

There is no generic Cyberse Link toolbox beyond these cards. The Teacher must preserve and reason about this exact copy budget.

## 2. Exact card list and strategic roles

Cards may have multiple roles. The role catalog should be centralized in an immutable StrategyProfile.

### 2.1 Main Deck — engine monsters

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Salamangreat of Fire | `11962031` | one-card starter; Normal/Special Summon searcher; Gazelle access; FIRE lock source; battle self-destruction resource | On summon, search a Level-4-or-lower Salamangreat other than itself, then track the rest-of-turn FIRE-only Special Summon restriction. The exact Extra Deck is all FIRE, but the restriction still belongs in state. Its GY battle effect can destroy the battling own Cyberse and may intentionally enable Raging Phoenix or other destruction synergies; use only when the resulting resource conversion is favorable. |
| 3 | Salamangreat Gazelle | `26889158` | primary engine access; conditional hand extender; deck-to-GY setup; trap/monster setup; recovery hub; starter | Special Summons when another Salamangreat monster is sent to GY under its exact trigger condition. On summon, sends one Salamangreat card from Deck to GY. Send choice determines extender, trap, Weasel, Jaguar, Falco, Foxy, or recovery line and must not be fixed globally. |
| 2 | Salamangreat Spinny | `52277807` | discard outlet; temporary ATK pressure; grave extender; Level-3 Xyz material; recursive engine body | Can discard itself to raise a Salamangreat's ATK, then revive from GY while another Salamangreat is controlled. Its leave-field handling is engine-authoritative; the Teacher reacts to the resulting public zone rather than reproducing rulings. Usually an efficient Gazelle send or Mining discard when Miragestallio access is desired. |
| 2 | Salamangreat Foxy | `94620082` | probabilistic Normal Summon starter; excavation-based access; grave extender; discard outlet; face-up Spell/Trap removal | The Teacher may know exact deck composition but not hidden top-deck order. Activating the top-three effect is deterministic policy behavior over stochastic engine state, not permission to inspect the deck. Its GY effect can discard a Salamangreat to revive and optionally destroy a face-up Spell/Trap, valuable going second or against public continuous cards. |
| 2 | Salamangreat Jack Jaguar | `56003780` | recursive extender; linked-zone body; Extra Deck recycle; Wolf trigger enabler; grind engine; piercing battle body | Returns another Salamangreat monster from GY to Deck/Extra Deck, then summons itself to a zone a Salamangreat Link points to. This is the primary copy-budget repair mechanism for Raging Phoenix, Sunlight Wolf, Balelynx, Miragestallio, or another engine piece. Preserve at least one live Jaguar route in grind states where possible. |
| 1 | Salamangreat Weasel | `57357130` | conditional hand extender; grave resource converter; opponent-field setup; draw; Princess trigger bridge; line recovery | Special Summons from hand when sufficient Salamangreat grave resources exist. When an eligible Salamangreat Extra/Ritual summon occurs while Weasel is in GY, it can return itself to the bottom of the Deck, summon another Salamangreat from own GY to the opponent's field, and draw. This can trigger Princess's GY removal and create a Raging Phoenix destruction/revival sequence. One-copy uniqueness is material. |
| 1 | Salamangreat Falco | `20618081` | Spell/Trap recursion; set-from-GY resource; self-revival; bounce/reuse; grind bridge | When sent to GY, can Set a Salamangreat Spell/Trap from GY. From GY, can return another face-up Salamangreat to hand/Extra Deck to summon itself. Rank the returned card by reuse value, copy budget, zone need, and whether its summon effect can be reused later. |
| 1 | Code of Soul | `74652966` | Link-enabled hand extender; alternate reincarnation enabler; immediate Link conversion; opponent-turn Link plan; Pyro Phoenix bridge | Can Special Summon while a Link is controlled. Its field effect supports a Salamangreat Link Summon that can use the same-name Link route, reducing dependence on Sanctuary; its GY quick effect during the opponent's Main Phase can enable a Link-3-or-higher Cyberse Link Summon. This is the primary opponent-turn Pyro/Raging planning resource in the exact list. |

### 2.2 Main Deck — defensive monsters

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Ash Blossom & Joyous Spring | `14558127` | defensive hand trap; FIRE resource; Hiita/Princess-compatible material or revive target after use | Against Swordsoul, can interact with Emergence, Circle search, Ecclesia/Ashuna/Yazi deck summons, Taia sends, and Chixiao searches. Preserve as interaction unless its FIRE body creates a higher-value recovery/lethal line after the relevant window. |
| 3 | Ghost Belle & Haunted Mansion | `73642296` | defensive graveyard hand trap; recursion denial; discard/material only after interaction window | Useful against public effects that add, Special Summon, or banish from GY under its exact legal response domain, including Swordsoul recovery effects where the engine offers the response. Do not hardcode a response to an effect family without a supplied legal chain candidate. |
| 3 | Effect Veiler | `97268402` | defensive monster negate; going-second board breaker; emergency material after interaction window | Strong against Mo Ye, Taia, Ecclesia, Chixiao, Baxia, Qixing, Chengying, and other public effect bodies depending on phase and replacement routes. Circle may convert a targeted Swordsoul body, so the timing evaluator must consider that public possibility without assuming hidden Circle. |

### 2.3 Main Deck — engine spells/traps

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Salamangreat Circle | `52155219` | archetype searcher; starter/extender access; reincarnated-Link protection | Default mode searches a Salamangreat monster. Its alternate mode protects a qualifying reincarnation-summoned Salamangreat Link from monster effects for the turn. Search and protection compete; evaluate current engine access, public interaction, and whether the protected payoff is strategically central. |
| 3 | Cynet Mining | `57160136` | broad Level-4-or-lower Cyberse search; discard converter; Gazelle trigger/setup enabler; consistency | Cost selection is strategic. Prefer a Salamangreat whose GY effect or later recovery is valuable—Spinny, Foxy, Jaguar, Weasel, Falco, or a recoverable trap setup—while preserving the only starter, hand interaction, or copy-critical card. It can search Gazelle, Of Fire, Spinny, Jaguar, Weasel, Falco, Code of Soul, or Foxy as legal. |
| 2 | Will of the Salamangreat | `64178424` | continuous extender; hand/GY revival; reincarnated-Link mass revival; recovery; material generation | One mode summons one Salamangreat from hand/GY. With a reincarnation-summoned Link, it can send itself and summon multiple Salamangreat monsters up to that Link Rating under the exact script. Rank the mode by current line, restrictions, zone capacity, and whether consuming Will loses longer-term value. |
| 1 | Salamangreat Sanctuary | `1295111` | Field Spell; primary reincarnation Link enabler; Raging/Pyro/Wolf effect unlock; battle utility | Balelynx searches the single copy. Sanctuary enables using a same-name Salamangreat Link as the entire material for another copy once per turn. Losing the only copy materially changes the line graph; Code of Soul is the alternate route. Do not treat Sanctuary as generic field decoration. |
| 1 | Salamangreat Charge | `83533296` | three-resource recycle; Extra Deck repair; negated revival; board removal under modified-ATK condition; grind recovery | One mode targets three FIRE monsters in GY/banished, returns two to Deck/Extra Deck, and revives one with effects negated and unable to attack. The target triple and revive choice are high-value continuation decisions. The alternate mode can destroy a field card when a face-up FIRE Ritual/Extra Deck monster's current ATK differs from its original ATK. |
| 1 | Called by the Grave | `24224830` | hand-trap protection; grave-effect denial; temporary same-name negate | Protects Of Fire/Gazelle/Stallio/Raging lines or stops a public Swordsoul grave effect. Its use must preserve higher-value later interaction when possible. |
| 3 | Infinite Impermanence | `10045474` | flexible monster negate; going-first set interaction; going-second board breaker; column interaction | Avoid redundant targeting with Veiler/Roar or a monster already answered by Rage/Princess. |
| 1 | Salamangreat Roar | `51339637` | archetype omni-negate; payoff interaction; recyclable trap; Gazelle/Falco/Wolf setup target | Requires control of a Salamangreat Link. When a Salamangreat Link is reincarnation summoned, Roar in GY can Set itself and is banished when it leaves after that set. Preserve exact zone/cycle state. Raging Phoenix commonly searches it when one broad negate is the preferred endboard. |
| 1 | Salamangreat Rage | `14934922` | flexible destruction; own Salamangreat conversion; multi-card removal with reincarnated Link; matchup board breaker | One mode sends a Salamangreat from hand/face-up field to GY to destroy one field card. The other targets a reincarnation-summoned Salamangreat Link and destroys opponent cards up to its Link Rating. Material/target selection must price Swordsoul destruction replacement and the value of the consumed own card. |

### 2.4 Extra Deck

| Count | Card | Passcode | Strategic roles | Exact-deck Teacher implications |
|---:|---|---:|---|---|
| 3 | Salamangreat Balelynx | `14812471` | Link-1 starter bridge; Sanctuary search; grave destruction protection; expendable/recyclable Link resource | Usually converts one engine body into Sanctuary access and a grave protection layer. Three copies support opening and grind, but repeated use still affects Jaguar/Charge copy decisions. |
| 3 | Salamangreat Sunlight Wolf | `87871125` | FIRE recursion; linked-zone payoff; reincarnation Spell/Trap recovery; grind engine; interaction recovery | A summon into its linked zone recovers a FIRE monster from GY, with a same-name summon/set restriction afterward. When reincarnation summoned, Wolf recovers a Salamangreat Spell/Trap. Placement and material choices must intentionally create linked-zone triggers. |
| 2 | Salamangreat Raging Phoenix | `57134592` | Link-4 payoff; reincarnation search; grave revival; ATK scaling; lethal pressure; Princess/Weasel bridge | When Link Summoned using another Raging Phoenix as material, searches any Salamangreat card. From GY, revives and gains ATK when another face-up FIRE monster controlled by its owner is destroyed by battle/effect. With only two copies, every reincarnation consumes the complete natural pair until Jaguar/Charge recycles one. |
| 2 | Salamangreat Pyro Phoenix | `31313405` | Link-4 board wipe; reincarnation payoff; opponent-Link setup; burn pressure; opponent-turn plan | Reincarnation summon can destroy the opponent's entire field. It can also Special Summon an opponent Link from their GY and punish opponent Link summons with damage. It is powerful but requires a valid same-name route and consumes both copies absent recycling. |
| 1 | Salamangreat Heatleo | `41463181` | Link-3 back-row removal; reincarnation ATK manipulation; board breaker; battle setup | On Link Summon, can shuffle an opponent Spell/Trap into Deck. Its reincarnation effect can change an opponent monster's ATK using an own Salamangreat's original ATK. One-copy specialized going-second resource. |
| 2 | Salamangreat Miragestallio | `87327776` | Rank-3 engine bridge; deck summon; Gazelle/Jaguar/Weasel/Falco access; Link-material bounce; restriction source | Detaches to Special Summon a Salamangreat from Deck in Defense Position, then prevents activation of non-FIRE monster effects for the rest of the turn. If Xyz Summoned and used as material for a Salamangreat Link, it can return a monster on field to hand. Track both the effect restriction and two-copy budget. |
| 1 | Promethean Princess, Bestower of Flames | `2772337` | Link-3 bridge; FIRE revival; field FIRE-only lock; grave disruption; self-revival; Raging trigger enabler | On field, revives a FIRE from GY and imposes FIRE-only Special Summons while it remains. In GY, when a monster is Special Summoned to the opponent's field, it can destroy one own face-up FIRE and one opponent monster, then revive. One copy is central; losing it requires a different profile mode. |
| 1 | Hiita the Fire Charmer, Ablaze | `48815792` | opponent-FIRE grave conversion; Link bridge; going-second value; destruction search | The exact Swordsoul deck places FIRE monsters such as Ash Blossom and Longyuan in GY, creating legitimate steal candidates when legal. Hiita can convert public opponent grave resources into Princess/Raging material. One-copy specialized bridge. |

## 3. Strategic identity of the exact deck

**PLAYER CONSENSUS / HEURISTIC.** Salamangreat is a recursive midrange Link strategy that turns small FIRE Cyberse bodies into repeated Link Summons, graveyard recursion, and recyclable interaction.

**INFERENCE FOR THE LOCKED LIST.** This list is deliberately modern and compact:

- Of Fire, Gazelle, Circle, and Mining create dense starter access;
- Spinny/Stallio provide Rank-3 extension;
- Jaguar, Falco, Wolf, Charge, and Will support multi-turn recovery;
- Weasel + Princess + Raging create a destruction/revival conversion loop;
- Code of Soul + Pyro Phoenix enable an opponent-turn or alternate reincarnation plan;
- nine monster hand traps plus three Impermanence provide a high interaction floor;
- the Extra Deck lacks generic finishers, so Salamangreat copy management is authoritative strategy rather than an optional optimization.

The Teacher should optimize recurring interaction and copy availability, not maximize Link count on turn one.

## 4. Strategic resource vector

The profile should expose at least:

```text
normal_summon_available
of_fire_access
gazelle_access
gazelle_special_trigger_available
gazelle_send_available
spinny_gy_extender_available
foxy_gy_extender_available
jaguar_recycle_available
weasel_hand_condition_available
weasel_gy_trigger_available
falco_set_recovery_available
code_of_soul_hand_extender_available
code_of_soul_gy_opponent_turn_available
sanctuary_available
alternate_reincarnation_access
linked_zone_available
sunlight_wolf_fire_recovery_available
sunlight_wolf_spell_trap_recovery_available
miragestallio_effect_available
stallio_non_fire_effect_lock_active
of_fire_fire_summon_lock_active
princess_fire_summon_lock_active
princess_gy_trigger_available
raging_reincarnation_search_available
raging_gy_revive_available
pyro_reincarnation_wipe_available
roar_available_and_recoverable
rage_available_and_recoverable
will_extension_available
charge_three_fire_target_set_available
extra_deck_copy_budget
remaining_hand_interaction
next_turn_gazelle_or_jaguar_follow_up
```

## 5. Copy-budget model

Copy counts are strategic resources:

| Extra Deck card | Copies | Budget implication |
|---|---:|---|
| Balelynx | 3 | Opening bridge, protection, and recurring material; avoid unnecessary third use before Jaguar/Charge recovery |
| Sunlight Wolf | 3 | Supports one reincarnation pair plus another grind copy; linked-zone placement determines value |
| Raging Phoenix | 2 | Natural reincarnation consumes both copies; immediately creates a recycle obligation for future Raging access |
| Pyro Phoenix | 2 | Same constraint as Raging; only pursue wipe when board value justifies exhausting pair |
| Heatleo | 1 | Specialized back-row/ATK tool; no redundant copy |
| Miragestallio | 2 | One opening and one grind/second push; Jaguar can recycle |
| Promethean Princess | 1 | Central bridge and grave interaction; if banished/unavailable, profile must switch modes |
| Hiita | 1 | Matchup-specific FIRE conversion; one-shot unless recycled by Charge/Jaguar if legal |

The Teacher must not use “remaining Extra Deck copies” inferred from raw engine state. It may derive own known copy usage from the exact locked deck and perspective-authorized public history/current observation.

## 6. Starter and search priorities

### 6.1 Of Fire

Default search intents:

```text
no Gazelle access
→ Gazelle

Gazelle available but no Level-3 extender / Stallio path
→ Spinny

reincarnation/Princess recovery branch needs one-copy card
→ Weasel or Code of Soul

grind/copy repair needed
→ Jack Jaguar

face-up Spell/Trap removal/recovery needed
→ Foxy
```

The actual candidate domain and public resources decide. Of Fire's FIRE-only Special Summon restriction should be recorded even though the exact Extra Deck is entirely FIRE.

### 6.2 Circle

Search mode priorities mirror engine needs but preserve the protection mode when:

- a reincarnation-summoned Raging/Pyro/Wolf is central;
- the opponent's known/public monster interaction would otherwise break the board;
- other starter access already exists.

### 6.3 Cynet Mining

Search:

- Of Fire or Gazelle for primary access;
- Spinny for Level-3 extension;
- Code of Soul for alternate/reaction Link plan;
- Jaguar for copy/grind repair;
- Weasel for Princess/Raging branch;
- Foxy/Falco for removal or recursion.

Discard preference, all else equal:

1. Spinny when its GY summon is live;
2. Foxy when its GY effect and a face-up S/T target are valuable;
3. Jaguar when linked-zone recycle can be established;
4. Weasel when its GY trigger line is active;
5. Falco when a Salamangreat S/T can be Set;
6. a recoverable Salamangreat Spell/Trap if Wolf/Falco access is real;
7. duplicates.

Preserve the only starter, only hand interaction needed for survival, only Sanctuary/Charge, and unique line resource when no recovery exists.

### 6.4 Gazelle send priorities

Gazelle's send is a strategic branch selector:

| Desired goal | Preferred send class |
|---|---|
| immediate Level-3 extension | Spinny |
| Raging/Princess conversion | Weasel |
| linked-zone recursion/copy repair | Jack Jaguar |
| Spell/Trap set recovery | Falco or Roar/Rage according to current recovery access |
| face-up S/T removal / extender | Foxy |
| direct interaction setup | Roar or Rage when Wolf/Falco/reincarnation can recover it |
| later opponent-turn Link | Code of Soul |

Do not send a trap merely because it is traditionally strong; confirm a legal recovery route.

## 7. Miragestallio decisions

### 7.1 Deck summon target

Potential intents:

- Jaguar for Wolf linked-zone recursion and Extra Deck recycle;
- Weasel for hand/field or later GY branch;
- Falco for S/T recovery;
- Of Fire for search if its summon trigger remains available and FIRE lock is accepted;
- Gazelle if its send remains unused;
- another Level-3/engine body according to active line.

### 7.2 Restriction pricing

After resolving Miragestallio's summon effect, non-FIRE monster effects cannot be activated for the rest of the turn under the pinned script. This can disable:

- Ghost Belle;
- Effect Veiler;
- other non-FIRE effect resources during the same turn.

Ash Blossom is FIRE and is not excluded by the FIRE condition. The Teacher must track the exact restriction and compare the extension against lost defensive options.

### 7.3 Link-material bounce

When an Xyz-summoned Miragestallio becomes material for a Salamangreat Link, its bounce can remove an opponent monster or, in rare cases, return an own reusable body. Rank targets by engine disruption, removal resistance, on-leave consequences, and whether destruction would have helped the opponent more.

## 8. Sanctuary and reincarnation Link mechanics

### 8.1 Reincarnation as a resource, not a ritual script

A Salamangreat Link Summon using a same-name Link as material unlocks effects on:

- Sunlight Wolf;
- Raging Phoenix;
- Pyro Phoenix;
- Heatleo;
- Roar/Rage-related state.

Sanctuary is the primary enabler; Code of Soul can provide an alternate route. The Teacher should track:

```text
reincarnation_access_source
same_name_copy_available
copy_recycle_plan
payoff_effect_still_unused
zone/material availability
post-summon follow_up
```

### 8.2 When not to reincarnate

Do not consume the second copy merely because the action is legal. Lower its score when:

- the corresponding payoff has no meaningful target;
- the first copy is needed as material for another better line;
- no Jaguar/Charge recovery exists and the duel is likely to continue;
- the second copy should be preserved for a later Pyro/Raging push;
- the opponent can answer the payoff and the profile lacks recovery.

## 9. Sunlight Wolf recursion

### 9.1 Linked-zone FIRE recovery

Preferred recoveries include:

- Gazelle for next-turn engine access;
- Ash Blossom after it has been used, when interaction is more valuable than engine body;
- Of Fire for a later Normal Summon;
- a FIRE engine monster required for active line.

The same-name summon/set restriction after recovery must be tracked from the exact script.

### 9.2 Reincarnation Spell/Trap recovery

Priorities:

1. Roar when broad negate is required;
2. Rage when multi-card removal or board breaking is required;
3. Circle when search/protection creates follow-up;
4. Will for extension/recovery;
5. Charge for copy repair/board removal;
6. Sanctuary if lost and still needed.

This is context-dependent. A one-card Roar is not always superior to Charge repairing both Raging copies in a long duel.

## 10. Princess, Weasel, and Raging conversion

### 10.1 Public dependency pattern

```text
Weasel in GY
+
eligible Salamangreat Extra/Ritual summon
+
another Salamangreat in own GY
→ summon that card to opponent field + draw
→ opponent-field Special Summon can trigger Princess in own GY
→ Princess destroys one own FIRE + the opponent monster and revives
→ destruction may enable Raging Phoenix's GY revival
```

Every arrow is conditional on current legal candidates and public state. This is not an exact queued script.

### 10.2 Own FIRE destruction target

Prefer an own FIRE that:

- is spent;
- is protected by Balelynx where preserving it creates additional value;
- triggers Raging Phoenix's revival;
- can be recovered by Wolf/Jaguar/Will/Charge;
- is less valuable on field than Princess's removal/revival.

Do not destroy the only live Roar/Rage anchor or irreplaceable follow-up without sufficient payoff.

### 10.3 Opponent-field target

The gifted Salamangreat is usually the deterministic Princess target if removing it restores card parity and enables the loop. If another opponent monster is a higher threat and the engine supplies that legal target, compare the public threat and resulting board rather than assuming the gifted card.

## 11. Code of Soul and Pyro Phoenix

### 11.1 Strategic role

Code of Soul reduces dependence on Sanctuary for a same-name Link route and can enable an opponent-turn Link-3-or-higher Cyberse summon from GY.

Pyro Phoenix's primary exact-deck payoff is a reincarnation-summon board wipe. A future Teacher should model:

- Code/Sanctuary reincarnation access;
- both Pyro copies remaining;
- opponent board value;
- own follow-up after the wipe;
- Roar/Rage/Princess interaction already available;
- whether an opponent-turn Link can occur under current public materials and restrictions.

### 11.2 No speculative opponent-turn script

The profile may prefer preserving Code in GY and materials for a future public chain window. It may not enqueue “summon Pyro Phoenix on opponent turn” before the engine supplies the legal Link candidate and materials.

## 12. Roar and Rage setup

### 12.1 Roar

Use/hold comparison:

```text
current effect would break engine/interaction or create lethal
vs
future public Swordsoul choke point likely has greater value
```

High-value public targets may include:

- Emergence/Circle/Ecclesia/Ashuna access when they are the only visible route;
- Chixiao search/negate setup;
- Baxia board break;
- Qixing/Chengying payoff;
- Blackout/Called by/Impermanence that would dismantle the current Salamangreat board.

Roar is broad interaction, but its recoverability depends on reincarnation/Wolf/Falco state.

### 12.2 Rage

Against Swordsoul, multi-target selection should price:

- Chixiao/Qixing/Dragite/Draco/Chengying current interaction;
- Blackout or another set/public Spell/Trap;
- Chengying's destruction replacement;
- whether destroying a Swordsoul body removes a Token/Synchro route;
- own card cost in single-removal mode;
- the value of keeping a reincarnated Link as trap anchor.

## 13. Interaction timing against the exact Swordsoul deck

### 13.1 Ash Blossom

Potential public response classes:

- Emergence and Circle searches;
- Ecclesia's Deck summon;
- Ashuna's Deck summon;
- Taia's Deck send;
- Chixiao's Deck add/banish;
- Yazi's Deck summon.

Prefer the effect that is engine-critical after considering already-public replacement routes and opponent investment. Do not automatically Ash the first search.

### 13.2 Veiler / Impermanence

Potential public targets:

- Mo Ye before Token generation;
- Taia before Token generation;
- Chixiao before search/negate value;
- Baxia before shuffle/revive;
- Qixing/Chengying/Draco/Dragite according to immediate threat.

Heavenly Dragon Circle can convert a targeted Wyrm body. The Teacher may account for Circle as a known public card or fixed-deck possibility only at a coarse policy level; it may not know whether Circle is hidden in hand.

### 13.3 Ghost Belle

Use only when the environment supplies a legal response to a graveyard-moving effect. Relevant categories may include Summit, Baxia/Shaman revival, or another effect that adds/summons/banishes from GY. Legality remains engine-owned.

### 13.4 Princess grave trigger

Swordsoul Special Summons—including Tokens and Synchros—can create a legal Princess trigger while Princess is in GY. Target selection should ask:

- can destroying the Token/body stop the current line;
- what own FIRE is expendable;
- will Balelynx protection preserve the own card while still allowing Princess to resolve;
- will destroying own FIRE revive Raging Phoenix;
- is a later summon a better choke point.

### 13.5 Balelynx protection

Use protection when destruction would otherwise remove a strategically central Salamangreat card and the grave copy is not more valuable for another future destruction. It does not answer Baxia's shuffle or Vishuda's return-to-hand effect.

## 14. Material selection

### 14.1 Generic preference

Use, when payoff is equivalent:

1. bodies whose once-per-turn effects are spent;
2. Spinny/Jaguar/Weasel/Falco resources that gain value in GY or through recycle;
3. extra Balelynx/Wolf copies with a clear Jaguar/Charge recovery plan;
4. Miragestallio after its summon effect, especially when its Link-material bounce is valuable.

Preserve:

- Gazelle until its send resolves;
- the only Weasel/Code/Falco branch resource;
- Jaguar needed to recycle exhausted Link/Xyz copies;
- a reincarnation-summoned Link needed for Roar/Rage/Will;
- Princess if its on-field revive or GY interaction is the active plan;
- the second Raging/Pyro copy until the payoff is justified;
- unused hand interaction before its window.

### 14.2 Link value is not sufficient

A Link-3 + one body can make a Link-4, but consuming Princess may remove its GY interaction or consuming Wolf may lose a FIRE/trap recovery. Material evaluation must include post-material grave effects and copy budget, not only total Link Rating.

### 14.3 Zone selection

Placement should preserve:

- Sunlight Wolf linked-zone triggers;
- an open zone for Jaguar/Weasel/Will;
- legal Princess/Raging/Pyro material layout;
- the ability to set/retain Roar/Rage;
- non-collision with existing Extra Monster Zone use.

No random zone selection is acceptable in Teacher v1.

## 15. Going-first goals and acceptable endboards

Preferred outcomes, depending on resources:

- reincarnation-summoned Raging Phoenix + Roar or Rage + Princess/Balelynx grave interaction + Gazelle/Jaguar follow-up;
- Sunlight Wolf + recovered Roar/Rage + Princess/Balelynx grave resources + Gazelle in hand;
- Raging Phoenix + multiple hand traps when trap access is unavailable;
- Wolf + one trap + hand interaction as a stable lower-resource endboard;
- Code of Soul/Pyro opponent-turn plan only when public materials, copies, and recovery are present.

Avoid treating “Raging + Wolf + Princess” as automatically superior if it exhausts both Raging copies, loses Jaguar, and has no interaction.

## 16. Going-second goals

1. force/negate Chixiao, Qixing, Dragite, or Blackout at the appropriate public choke point;
2. use Miragestallio bounce, Heatleo, Foxy, Hiita, Princess, Rage, and Pyro to dismantle the board;
3. preserve Gazelle/Jaguar follow-up after the first interaction;
4. convert opponent FIRE grave cards through Hiita when legal;
5. take deterministic lethal with boosted Raging/Pyro/Heatleo pressure;
6. otherwise establish Wolf/Raging + interaction rather than overextend into remaining Swordsoul resources.

## 17. Grind and recovery priorities

- recycle Raging/Pyro/Wolf/Balelynx/Stallio with Jaguar or Charge;
- recover Gazelle/Ash/Of Fire with Wolf;
- recover Roar/Rage/Will/Charge/Circle with reincarnated Wolf or Falco;
- use Falco to return a reusable on-field Salamangreat and restore a trap;
- preserve the one Princess and Code of Soul opponent-turn route;
- avoid exhausting Sanctuary and both same-name Link copies without a recycle plan;
- adapt Gazelle sends to the resource actually missing, not the opening combo template.

## 18. Line-family catalog

These are partial-order strategic families. The engine supplies every legal action and continuation.

### SAL-L01 — Of Fire / Gazelle foundation

| Field | Definition |
|---|---|
| Strategic goal | Convert one starter into Gazelle setup, Balelynx/Sanctuary, Level-3 or Link extension, and future interaction |
| Required public resources | Of Fire/Circle/Mining access; unused Normal Summon or legal Special Summon; Gazelle available; zones |
| Primary starter | Of Fire or searcher into Of Fire/Gazelle |
| Optional resources | Spinny; Jaguar; Weasel; Will; hand interaction |
| Resource cost | Normal Summon; possible Mining discard; FIRE lock after Of Fire |
| Intermediate goals | `GAZELLE_ACCESS`; `BAL ELYNX_ESTABLISHED`; `SANCTUARY_AVAILABLE`; `GY_RESOURCE_SELECTED` |
| Preferred payoff | progress into Stallio/Wolf/Princess/Raging or stop on Wolf + interaction |
| Follow-up retained | Gazelle/Jaguar/hand traps; recoverable Extra Deck copies |
| Vulnerabilities | Of Fire negate; Balelynx/Gazelle negate; graveyard denial; zone loss |
| Recovery | Spinny/Foxy/Will/Jaguar/Code; smaller Wolf line; interaction-preserving stop |
| Stop conditions | extending consumes all interaction/copy recovery; no meaningful payoff target; restriction conflict |

### SAL-L02 — Gazelle send branch

| Field | Definition |
|---|---|
| Strategic goal | Select the grave resource that creates the best reachable current or future line |
| Required public resources | Gazelle summon and unused send effect |
| Branches | Spinny extension; Weasel/Princess; Jaguar recycle; Falco/trap; Foxy removal; Roar/Rage setup; Code opponent-turn plan |
| Resource cost | one exact Deck copy moved to GY |
| Vulnerabilities | Ash/negate; graveyard banish; sending unique card without recovery |
| Recovery | use existing hand/field resources; mark missing branch and replan |
| Stop conditions | no candidate improves current/follow-up state; preserve Deck copy |

### SAL-L03 — Miragestallio bridge

| Field | Definition |
|---|---|
| Strategic goal | Turn two Level-3 bodies into a precise Deck summon and Link-material bounce |
| Required public resources | two legal Level-3 materials; Miragestallio copy; summon effect available |
| Optional targets | Jaguar, Weasel, Falco, Of Fire, Gazelle, another engine body |
| Resource cost | Xyz materials; Stallio non-FIRE-effect restriction; one copy |
| Preferred payoff | Wolf/Princess/Raging material plus high-value bounce |
| Vulnerabilities | effect negate; graveyard/copy disruption; restriction disables Belle/Veiler |
| Recovery | keep existing bodies; use Will/Code; stop on interaction |
| Stop conditions | non-FIRE effect lock costs more than summon; no valuable target/bounce; copy budget critical |

### SAL-L04 — Wolf / Jaguar recursion

| Field | Definition |
|---|---|
| Strategic goal | Repair Extra Deck copy budget, summon Jaguar to a linked zone, and recover a FIRE or Salamangreat S/T |
| Required public resources | Wolf with usable linked zone; Jaguar in GY; recycle target |
| Optional resources | reincarnation Wolf; Gazelle/Ash in GY; Roar/Rage/Charge/Will in GY |
| Resource cost | one GY Salamangreat returned; Jaguar effect; zone commitment |
| Preferred payoff | Gazelle/Ash recovery and restored Raging/Pyro/Stallio/Wolf copy |
| Vulnerabilities | linked-zone removal; graveyard negate; target leaves |
| Recovery | Charge/Falco/Will; alternate linked zone |
| Stop conditions | only recycle target is needed in GY; Jaguar body blocks a higher-value line |

### SAL-L05 — Princess / Raging reincarnation

| Field | Definition |
|---|---|
| Strategic goal | Convert FIRE Link material into Raging Phoenix, reincarnate it, search interaction, and retain Princess grave disruption |
| Required public resources | Princess/Raging access; both Raging copies; Sanctuary or Code reincarnation access; sufficient FIRE material |
| Optional resources | Weasel GY; Gazelle/Jaguar; Roar/Rage/Will/Charge search target |
| Resource cost | both Raging copies until recycle; Princess moves to GY; Link material |
| Preferred payoff | reincarnated Raging + selected Salamangreat card + Princess/Balelynx GY + follow-up |
| Vulnerabilities | Raging negate; Sanctuary removal; copy banish; graveyard denial |
| Recovery | Jaguar/Charge recycle; Wolf + trap; Pyro/Heatleo alternative only if legal/value-positive |
| Stop conditions | no useful search; no recycle path in a long game; smaller board preserves more interaction |

### SAL-L06 — Weasel / Princess destruction-revival loop

| Field | Definition |
|---|---|
| Strategic goal | Convert Weasel GY into draw, opponent-field body, Princess removal/revival, and optional Raging revival |
| Required public resources | Weasel in GY; eligible Salamangreat summon; another Salamangreat in GY; Princess in GY; legal targets |
| Resource cost | Weasel returns to Deck bottom; own FIRE destruction target; opponent receives temporary body |
| Preferred payoff | draw + opponent threat removal + Princess field + revived/boosted Raging |
| Vulnerabilities | graveyard negate; target removal; Princess/Raging already used; own target too valuable |
| Recovery | retain Raging/Wolf board; use Weasel later; generic interaction |
| Stop conditions | no expendable own FIRE; no valuable opponent target; gifted monster creates unacceptable risk |

### SAL-L07 — Code of Soul / Pyro Phoenix plan

| Field | Definition |
|---|---|
| Strategic goal | Preserve alternate reincarnation access and execute a high-value Pyro board wipe, including on opponent turn where legal |
| Required public resources | Code field/GY effect available; two Pyro copies; sufficient public Link materials; legal timing |
| Resource cost | both Pyro copies; Code banish/use; material and zone commitment |
| Preferred payoff | remove a developed Swordsoul board and retain follow-up/interaction |
| Vulnerabilities | Code/Pyro negate; opponent does not commit enough; copy banish; no post-wipe pressure |
| Recovery | Raging/Heatleo/Wolf plan; preserve Code if no valuable wipe |
| Stop conditions | opponent board value below copy/resource cost; legal Link candidate absent; smaller interaction line superior |

### SAL-L08 — Falco / Will / Charge recovery

| Field | Definition |
|---|---|
| Strategic goal | Rebuild after interruption or partial board break using spell/trap and FIRE-resource recursion |
| Required public resources | one of Falco, Will, or Charge with legal targets |
| Resource cost | discard/bounce/continuous spell/three-target selection depending branch |
| Preferred payoff | restore Wolf/Raging/Stallio copy, revive engine body, recover Roar/Rage, re-establish linked zone |
| Vulnerabilities | Ghost Belle/Called by/graveyard denial; poor target triple; zone capacity |
| Recovery | Jaguar/Wolf or hand starter; safe interaction stop |
| Stop conditions | recovery consumes the only future starter or returns the wrong copy set |

### SAL-L09 — Going-second FIRE conversion

| Field | Definition |
|---|---|
| Strategic goal | Break Swordsoul interaction using Stallio bounce, Heatleo, Hiita, Princess, Rage, or Pyro and establish a recurring board |
| Required public resources | legal board-break candidates and enough engine access after first interruption |
| Optional resources | public opponent Ash/Longyuan in GY for Hiita; Rage; Code/Pyro; Foxy face-up S/T target |
| Resource cost | specialized one-copy Extra Deck cards and interaction |
| Preferred payoff | remove Chixiao/Qixing/Blackout/Dragite pressure, then Wolf/Raging + follow-up |
| Vulnerabilities | Chixiao/Roar-equivalent negate; Blackout; Qixing; Chengying replacement; graveyard denial |
| Recovery | preserve Gazelle/Jaguar and hand traps; accept smaller board |
| Stop conditions | lethal not available and continued extension loses all recovery |

### SAL-L10 — Lethal conversion

| Field | Definition |
|---|---|
| Strategic goal | Convert Raging revival/ATK gain, Pyro wipe, Heatleo manipulation, Hiita material, and direct attacks into proven lethal |
| Required public resources | exact public LP/ATK/DEF and legal battle candidates |
| Resource cost | potential loss of recurring Links/materials |
| Preferred payoff | deterministic lethal; otherwise remove threat and build Main Phase 2 interaction |
| Vulnerabilities | known public battle/quick interaction; Sanctuary battle mode misvaluation; overcommitment |
| Recovery | Main Phase 2 Wolf/Raging + trap/follow-up |
| Stop conditions | lethal not provable; attack sequence sacrifices dominant recurring board |

## 19. Interruption recovery

### 19.1 Starter interrupted

If Of Fire, Gazelle, or Balelynx is stopped:

1. reconcile which effect was used and which card/zone remains;
2. invalidate line nodes that assume the search/send/Sanctuary resolved;
3. score Spinny, Foxy, Will, Jaguar, Weasel, Code, or Mining/Circle recovery from the actual current frame;
4. preserve hand traps and a smaller Wolf/interaction board when full Raging is no longer justified;
5. never enqueue the old Stallio/Princess/Raging selections.

### 19.2 Extender interrupted

If Spinny, Stallio, Jaguar, Will, or Princess is stopped:

- retain completed Gazelle/Balelynx/Wolf value;
- switch to copy-preserving recovery;
- do not consume both Raging/Pyro copies merely to approximate the original endboard;
- label the decision as recovery/medium confidence where appropriate.

### 19.3 Key payoff unavailable

If Princess, the second Raging/Pyro, Sanctuary, or a linked zone is unavailable:

- remove the incompatible line from reachable goals;
- use Wolf/Jaguar/Falco/Will/Charge or Heatleo/Stallio plans;
- preserve remaining copies;
- do not infer that an absent candidate should exist.

## 20. Scenario-family behavior

| Scenario | Expected architecture behavior |
|---|---|
| Ideal opening | Select Of Fire/Gazelle/Stallio into Wolf/Princess/Raging with trap and copy-recycle plan |
| Weak playable opening | Foxy or one searcher/extender into Balelynx/Wolf; preserve hand interaction |
| Brick-like opening | Set Impermanence/Roar/Rage if legal, hold hand traps, avoid consuming unique engine cards for no payoff |
| Going first | Prioritize Raging/Wolf + Roar/Rage + Princess/Balelynx GY + Gazelle/Jaguar follow-up |
| Going second | Use Veiler/Imperm/Rage/Stallio/Heatleo/Hiita/Princess/Pyro to break, then stabilize |
| Starter interrupted | Reconcile and pivot through Spinny/Foxy/Will/Jaguar/Code or safe stop |
| Extender interrupted | Keep achieved Wolf/Gazelle value; preserve Raging/Pyro/Princess copies |
| Key Extra Deck payoff unavailable | Replan around copy budget; use Wolf/Heatleo/Stallio and recovery |
| Board partially broken | Jaguar/Charge/Falco/Will/Wolf rebuild and restore trap/copies |
| Grind state | Recycle exact Extra Deck copies, recover Gazelle/traps, preserve one-copy Princess/Code |
| Lethal opportunity | Prove damage with Raging/Pyro/Heatleo; otherwise transition to Main Phase 2 interaction |
| Multiple opponent threats | Target active Synchro interaction, Blackout/public back row, recursion, and lethal source by vector |
| Continuation/material choice | Rank all legal Link/Xyz/material/target/triple choices; preserve unique and copy-critical resources |

## 21. Confidence recommendations

### High confidence

- Of Fire/Circle/Mining into Gazelle when it is the unique engine bridge;
- Gazelle send to Spinny for confirmed Stallio access;
- Raging reincarnation search with a dominant Roar/Rage/Will/Charge need;
- Jaguar recycle restoring an exhausted required Extra Deck copy;
- Princess target pair that stops a Swordsoul Token/payoff and safely converts own FIRE;
- deterministic Pyro wipe or lethal with all public conditions satisfied.

### Medium confidence

- Gazelle send among Weasel/Jaguar/Falco/trap branches;
- whether to consume both Raging copies now or preserve for grind;
- Code of Soul opponent-turn plan;
- Charge three-target selection with several comparable resources;
- hold-vs-use timing for Roar/Rage/Belle.

### Low/fallback

- no line definition matches the current restriction/copy state;
- Foxy excavation is the only uncertain starter route and no stronger policy distinction exists;
- equivalent material/zone candidates remain after all public features;
- canonical public-key tie-break decides.

Default future Behavior Cloning eligibility should follow the shared confidence policy: admitted high-confidence labels, validated medium-confidence labels, and exclusion of low/fallback decisions by default.

## 22. Source-derived facts, heuristics, and inferences

### SOURCE FACT

- exact counts/hash and certified matchup come from OCGForge;
- card mechanics, restrictions, triggers, and copy interactions come from the pinned CardScripts;
- official 2024 coverage demonstrates modern Gazelle/Weasel/Princess/Raging/Jaguar/Wolf/Charge and Code-of-Soul paths in competitive play;
- the Teacher must consume only the public observation and complete legal public candidate domain.

### PLAYER CONSENSUS / HEURISTIC

- Gazelle is the central grave-setup resource;
- Balelynx/Sanctuary opens reincarnation effects;
- Sunlight Wolf and Jaguar are the recurring midrange engine;
- Raging Phoenix converts reincarnation into searchable interaction;
- Promethean Princess and Weasel create a high-value destruction/revival route;
- preserving Extra Deck copies and follow-up often beats maximizing the first board.

### INFERENCE FOR OCGFORGE

- Raging/Pyro two-copy pairs make copy-budget state mandatory rather than optional;
- the exact list's three Wolf/Balelynx and two Stallio support longer episodes and recovery labels;
- Princess is especially relevant against Swordsoul Token/Synchro Special Summons;
- Hiita has real matchup-specific targets because Swordsoul uses Ash Blossom and Longyuan, both FIRE;
- Miragestallio's non-FIRE-effect restriction can suppress Ghost Belle/Veiler during the same turn and therefore must affect interaction valuation;
- a stable Wolf + trap + hand-interaction board may be a higher-quality Teacher label than a greedy Raging route with no recycle plan.

## 23. Selected human strategy evidence

The evidence index contains full metadata and limitations. Particularly useful sources include:

- [Official YCS Sydney 2024 coverage](https://www.yugioh-card.com/eu/it/ycs-sydney-2024-coverage/) — Gazelle sends, Princess/Raging construction, Weasel opponent-field summon and draw, Princess destruction/revival, Jaguar recycle, Wolf recovery. The featured list contains many absent generic Cyberse cards; only overlapping engine dependencies are used.
- [Official 2024 Round 11 feature match](https://yugiohblog.konami.com/2024/championships/round-11-feature-match-benjamin-rosen-vs-sean-washington/) — Gazelle/Weasel/Princess/Raging, Sanctuary reincarnation, Charge recovery, Stallio/Of Fire, Code of Soul fallback, and interaction under interruption.
- [TCGplayer: How To Build And Play Salamangreat With Legacy Of Destruction](https://www.tcgplayer.com/content/article/How-To-Build-And-Play-Salamangreat-With-Legacy-Of-Destruction/6e4847cd-40ec-4383-9aa2-d2eddb80d73e/) — modern Raging/Pyro/Princess/Code/Of Fire strategy concepts; list differs.
- [TCGplayer: How to Build Salamangreat with Soulburning Volcano](https://www.tcgplayer.com/content/article/How-to-Build-Salamangreat-with-Soulburning-Volcano/10128223-00bd-44a8-9f2c-667086a739fb/) — Of Fire/Gazelle/Stallio/Wolf/Raging and resource-management concepts; list differs.

## 24. Profile implementation boundary

A future `SalamangreatStrategyProfile` should own:

- exact deck/hash/matchup IDs;
- the role and copy-budget catalog above;
- SAL-L01 through SAL-L10 line/recovery definitions;
- search/send/discard/material/zone/recycle modifiers;
- reincarnation and linked-zone goal predicates;
- public Swordsoul interaction map;
- restriction/use facts;
- confidence and integer preference tables.

It must not own:

- Link/Xyz legality;
- target/material enumeration;
- hidden opponent cards or deck order;
- raw engine/client state;
- internal semantic keys;
- queued future selections;
- candidate filtering/truncation.

## 25. Unresolved Salamangreat questions for implementation validation

1. Does the current public frame/history expose enough information to distinguish all reincarnation-summon, same-name-copy, linked-zone, and temporary restriction facts without internal queries?
2. Which candidate metadata represents Charge's target triple and revive choice, Rage's two modes, and Sanctuary/Code same-name Link continuations?
3. How should the scenario corpus prove one-copy Princess unavailable/banished recovery without creating a general hidden board constructor?
4. What fixed integer copy-budget penalties prevent gratuitous second Raging/Pyro consumption while still allowing high-value wipes/lethal?
5. How should the Teacher represent Foxy's top-three activation confidence without inspecting hidden deck order or turning expected value into floating-point nondeterminism?
6. Which public fixtures demonstrate Miragestallio's non-FIRE-effect restriction suppressing Belle/Veiler choices?
7. When Princess can trigger on multiple Swordsoul Special Summons, which public choke-point map yields the best deterministic hold/use policy?
8. How often does Code of Soul/Pyro opponent-turn play outperform a simpler Roar/Rage endboard in this exact matchup?

These questions require fixtures and measurement, not privileged policy inputs.
