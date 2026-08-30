# Teacher Combo Planning, Interruption Recovery, and Scenario Stress Tests

**Research date:** 2026-08-28  
**OCGForge checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Status:** non-normative domain model and future acceptance design

## 1. Purpose

This report answers the central representation question:

> How can the first OCGForge Teacher express Swordsoul Tenyi and Salamangreat strategy, retain multi-step intent, recover from interruption, and always choose a current legal public action without becoming an exact combo script or a second rules engine?

The recommendation is a **utility-guided partial-order line and recovery graph** evaluated from the current public frame.

```text
current PublicEnvironmentObservation
+
complete ordered EnvironmentActionCandidate[]
+
episode-local public-safe strategy state
+
immutable StrategyProfile
        ↓
reconcile current resources/restrictions/goals
        ↓
match active line and recovery intents to every supplied candidate
        ↓
score complete domain with exact deterministic vectors
        ↓
select exactly one public_action_key
```

No hypothetical CoreHost state fork is required. No future action is assumed legal before the environment supplies it.

## 2. Terminology

OCGForge already has an authoritative glossary for accepted project terms. The definitions below refine Teacher-research vocabulary locally; they do not create a competing normative glossary.

### Combo

A reusable family of coordinated plays that converts a class of resources into one or more strategic payoffs.

A Combo is broader than an exact ordered sequence. “Swordsoul starter into Level-8 bridge plus Level-10 interaction” is a combo family even when the starter, search target, materials, interruption, and final Level 10 differ.

### Line

One partial-order route through a Combo or Goal family.

A Line specifies dependencies, resources, desired intermediate states, and acceptable outcomes. It does not prescribe candidate indices or require every action to occur in one fixed order when independent steps may be reordered legally.

### Plan

The current episode-local instantiation of:

```text
selected strategic Goal
+
selected Line
+
current Line node/progress
+
resource and restriction snapshot
```

A Plan is advisory policy state. The current observation and candidate domain remain authoritative.

### Goal

A desired public strategic-state predicate, such as:

- establish interaction;
- establish recursion;
- preserve follow-up;
- remove an engine enabler;
- stabilize;
- recover engine;
- force interaction;
- reach lethal;
- stop with a safe board.

A Goal is not a specific card or action.

### Rule

A deterministic mapping from public facts and one supplied candidate to:

- applicability evidence;
- feature values;
- score components;
- explanation reasons.

A Rule must not return final authority before the complete domain is evaluated.

### Heuristic

An evidence-based strategic preference that is not rules truth. Examples include preserving the only starter or holding a negate for a higher-value public choke point.

### Utility

The explicit ordered integer/fixed-domain score vector used to compare current legal candidates. Utility is policy value, not environment reward or game-rule truth.

### Threat

A publicly observable opponent card, effect, board configuration, or imminent consequence with estimated adverse impact.

### Resource

A perspective-authorized capability that can be consumed, preserved, converted, recovered, or denied. Resources include cards, Normal Summon, Extra Deck copies, grave effects, linked zones, interaction, once-per-turn availability, and follow-up.

### Recovery

A deliberate transition from an invalidated, completed, or dominated Plan to another reachable Goal/Line based on the new public frame.

### Fallback

A deterministic selection layer used when no supported active-line or recovery edge sufficiently explains the current domain. Fallback still evaluates every candidate.

### StrategyProfile

An immutable deck-specific policy artifact containing roles, resources, Goals, Line graph, Recovery edges, matchup modifiers, and preference tables for one exact locked deck/version.

### TeacherCore

The generic public-state interpreter, state reconciler, full-domain feature/evaluation framework, deterministic resolver, and explanation machinery shared by profiles.

### PolicyArtifact

The immutable provenance identity for the exact Teacher behavior, including core, profile, preference, fallback, and tie-break configuration. It is not gameplay identity.

### DecisionExplanation

Derived policy provenance describing the selected Goal/Line/Rule, score, margin, confidence, invalidation, and fallback level. It must contain no hidden/private/internal information.

### Candidate intent

A profile-defined semantic purpose that may be matched to a candidate already supplied by the environment, such as:

- `ADVANCE_LEVEL8_ENGINE`;
- `ESTABLISH_REINCARNATION`;
- `REMOVE_HIGHEST_PUBLIC_THREAT`;
- `PRESERVE_UNIQUE_FOLLOW_UP`.

An intent is not a legal-action generator.

## 3. Combo representation alternatives

| Representation | Strength | Failure mode in OCGForge | Decision |
|---|---|---|---|
| Exact action script | Simple replay of one ideal line; deterministic | Candidate order/identity changes; interruption removes expected action; duplicates legality; no graceful recovery | `REJECT` |
| Ordered rules/executors | Easy to add local behavior; transparent first match | Global order coupling; hidden priority; later rules never compared; grows into flags and callback queues | `REFERENCE_ONLY` / small local rules only |
| Finite state machine | Explicit lifecycle and transitions | State explosion across hands, chains, restrictions, target/material continuations; transitions often assume future legality | `ADAPT` only for bounded plan state |
| Behavior tree | Organizes going-first/second/recovery/battle modes | Selector order becomes implicit policy; does not solve complete candidate ranking | `ADAPT` as optional code organization |
| Full GOAP | Goals and recovery are natural | Requires a trustworthy transition model or state fork; an incomplete symbolic model becomes a second rules engine | `REJECT` for v1 execution |
| Dependency graph | Represents prerequisites/resources | Needs choice, scoring, recovery, and current-state reconciliation | `REUSE_CONCEPT` |
| Combo graph of exact actions | Visualizes branches | Still brittle and legality-assuming if nodes are engine actions | `REJECT` |
| Public-state Goal/Line graph | Represents desired states, dependencies, resources, and alternatives | Requires disciplined matching and explicit invalidation | `REUSE_CONCEPT` |
| Utility AI alone | Complete-domain ranking, explainability, deterministic tie-break | Myopic; can choose a locally strong action that breaks a multi-step plan | `ADAPT` as tactical core |
| Utility-guided partial-order Line + Recovery graph | Combines complete-domain ranking with intent, resources, and replanning | More design work and scenario evidence required | **Recommended** |

## 4. Why exact scripts fail

An exact script assumes something like:

```text
activate specific starter
→ choose expected search
→ choose expected target
→ select expected materials
→ summon expected payoff
→ set expected interaction
```

That is invalid as an OCGForge policy model because:

1. the opponent can interrupt any step;
2. legal target/material domains can differ after a chain resolves;
3. duplicate card copies can create different public identities;
4. a planned Extra Deck copy may be exhausted;
5. temporary restrictions may invalidate the planned payoff;
6. the correct search depends on the rest of the hand and public board;
7. continuation decisions are current semantic frames, not hidden callbacks;
8. exact scripts tempt the policy to reconstruct legality or use candidate indices;
9. recovery becomes an unmaintainable collection of special cases;
10. the resulting trajectory labels encode one narrow imitation ceiling.

The Teacher should preserve **strategic dependency**, not exact action order.

## 5. Recommended partial-order Line model

The following is conceptual and not a ratified schema.

```text
LineDefinition
- line_id
- strategy_profile_id
- strategic_goal_id
- source_basis[]
- applicability_predicates[]
- required_resources[]
- optional_resources[]
- forbidden_or_conflicting_restrictions[]
- intermediate_goal_nodes[]
- candidate_intent_edges[]
- expected_resource_deltas[]
- preserved_resource_preferences[]
- preferred_payoff_predicates[]
- acceptable_fallback_predicates[]
- vulnerability_tags[]
- recovery_edge_ids[]
- stop_conditions[]
- default_confidence_class
```

### 5.1 Public predicates only

A Line predicate may inspect only:

- fields in the acting perspective's current public observation;
- public metadata of current supplied candidates;
- perspective-authorized strategy memory;
- immutable exact deck/profile configuration;
- publicly visible/accepted history already represented safely.

It may not inspect:

- raw engine/core state;
- hidden opponent cards;
- internal semantic keys;
- private locators;
- candidate arrays from a hypothetical future state;
- secret-derived hashes.

### 5.2 Goal nodes

A Goal node describes evidence over public state, for example:

```text
Swordsoul:
- non_effect_tenyi_enabler_established
- level_8_engine_payoff_established
- level_10_interaction_established
- blackout_anchor_preserved

Salamangreat:
- gazelle_send_resolved
- sanctuary_or_code_reincarnation_available
- wolf_linked_zone_ready
- reincarnated_raging_established
- trap_and_copy_recovery_preserved
```

A node becomes achieved only after the environment accepts the relevant action and the next public state supports the predicate.

### 5.3 Candidate intent edges

An edge does not say “take candidate `2`.” It says:

```text
when current public state satisfies P
and one supplied candidate exhibits intent-compatible public features F
then that candidate advances Goal G
with resource cost vector C
and risk/recovery annotations R
```

Every supplied candidate still receives all generic evaluations. A candidate can match zero, one, or several intents.

### 5.4 Partial order

Two subgoals may be independent when rules and current candidates permit either order, for example:

- establish Tenyi grave access and preserve the Normal Summon;
- recover a FIRE resource with Wolf and recycle an Extra Deck copy with Jaguar.

The profile may express ordering constraints only where strategic or rule-derived evidence requires them. The engine remains responsible for actual availability.

## 6. Plan state and ownership

Conceptual state:

```text
EpisodeLocalStrategyState
- active_goal_id | NONE
- active_line_id | NONE
- active_line_node_id | NONE
- achieved_goal_ids[]
- public_resource_facts[]
- public_restriction_facts[]
- accepted_effect_use_facts[]
- known_public_threat_facts[]
- recovery_status
- last_accepted_environment_decision_index
- last_accepted_public_action_key
```

### 6.1 State is participant- and episode-local

No state may leak across:

- episodes;
- seats;
- participant policy assignments;
- parallel environments;
- worker processes;
- rejected submissions.

### 6.2 Proposal does not commit state

Recommended semantic split:

```text
propose(current_frame, current_strategy_state)
→ selected_public_action_key
→ proposed_policy_state_delta
→ decision_explanation

on accepted environment transition:
→ reconcile next public frame
→ commit supported policy state delta

on rejected/stale/nonmember submission:
→ commit nothing
```

This preserves replayability and prevents plan progress from diverging from accepted gameplay.

### 6.3 Observation dominates memory

If memory and current observation conflict:

1. discard or expire the unsupported memory fact;
2. invalidate dependent line nodes;
3. record a public-safe invalidation reason;
4. enter recovery/replanning;
5. never synthesize the missing resource.

## 7. Plan invalidation

Recommended invalidation classes:

| Invalidation reason | Example | Required response |
|---|---|---|
| `STARTER_EFFECT_DID_NOT_RESOLVE` | Mo Ye or Gazelle is negated | Remove Token/send-dependent nodes; evaluate side lines |
| `EXPECTED_BODY_REMOVED` | opponent removes material before continuation | Recompute current material/zone graph |
| `RESOURCE_CONSUMED` | only Longyuan discard or Raging copy is spent | Re-evaluate lower-resource Goal |
| `RESTRICTION_ACTIVATED` | Ashuna Wyrm lock or Stallio non-FIRE effect lock | Invalidate conflicting payoffs/interactions |
| `ZONE_UNAVAILABLE` | linked zone or monster zone is occupied/removed | Replan placement/material route |
| `EXTRA_DECK_COPY_UNAVAILABLE` | second Raging/Pyro or Chixiao copy exhausted | Switch to copy-preserving recovery |
| `TARGET_NO_LONGER_PRESENT` | chain removes planned threat | Re-rank current legal targets |
| `PAYOFF_ALREADY_ANSWERED` | selected interaction becomes redundant | Preserve answer and choose another Goal |
| `LETHAL_NO_LONGER_PROVEN` | LP/body changes during chain | Switch to stabilization/Main Phase 2 plan |
| `PUBLIC_STATE_CONTRADICTION` | memory says resource exists, observation does not | Fail closed on memory; use observation |

Absence of an expected candidate in a complete current domain is sufficient to mark that **current edge** unavailable. It is not permission to fabricate the candidate or declare the environment wrong without independent evidence.

## 8. Recovery algorithm

```text
R1 validate actionable frame and nonempty complete domain
R2 interpret current public state
R3 reconcile strategy memory and expire scoped facts
R4 test active Goal/Line preconditions
R5 invalidate unsupported nodes/edges
R6 evaluate declared recovery edges
R7 evaluate alternative reachable Goals/Lines
R8 annotate every candidate with plan/recovery features
R9 add deck-specific and generic tactical utilities
R10 exact deterministic argmax over complete domain
R11 emit fallback/confidence/invalidation explanation
R12 commit state only after accepted transition
```

### 8.1 Recovery edge definition

Conceptual structure:

```text
RecoveryEdge
- recovery_edge_id
- from_line_or_goal
- invalidation_reason_classes[]
- required_public_predicates[]
- candidate_intents[]
- target_goal_or_line
- resource_preservation_priority[]
- stop_if_predicates[]
- confidence_cap
```

A recovery edge normally caps confidence at `MEDIUM_CONFIDENCE` until scenario evidence validates it.

### 8.2 Safe stop is a real recovery outcome

A competent midrange Teacher must sometimes stop extending:

- Chixiao + Blackout + hand trap may be safer than consuming all resources for another body;
- Wolf + Roar + Gazelle follow-up may be safer than exhausting both Raging copies;
- after an extender is negated, preserving the remaining starter may be superior to a weak forced line.

`STOP_WITH_SAFE_BOARD` should be an explicit Goal, not an accidental lack of matching rule.

## 9. Utility integration

### 9.1 Structured deterministic comparison

Recommended lexicographic order:

```text
1 hard survival / guaranteed lethal class
2 active Goal/Line progress or validated Recovery
3 immediate tactical necessity
4 interaction timing
5 target value
6 material/cost preservation
7 engine/follow-up resource vector
8 battle and Main Phase 2 value
9 profile preference
10 bytewise ascending public_action_key exact-equality tie-break
```

All dimensions use bounded checked integers or small canonical enums. No floating point is needed for Teacher v1.

### 9.2 Plan bonus is not absolute

A line-advancing candidate must still lose to:

- a candidate preventing immediate lethal;
- proven lethal;
- a mandatory high-value interaction;
- a candidate revealing that the current Plan is stale or dominated.

This avoids tunnel vision.

### 9.3 Tactically legal but strategically bad branches

Examples:

- Swordsoul can legally consume the only follow-up Wyrm as Longyuan discard for a marginal Level 10;
- Salamangreat can legally use both Raging copies when no search/recycle payoff exists;
- either deck can legally spend hand interaction as material before its critical window;
- Blackout/Rage can legally target low-value cards while leaving the engine source alive.

The complete-domain utility and resource vector penalize these without declaring them illegal.

## 10. Fallback semantics

```text
F0 named active Line edge
F1 explicit Recovery edge or replan
F2 deck-specific strategic utility
F3 generic tactical-safe utility
F4 canonical public-key equality tie-break
```

### 10.1 Requirements

- every level evaluates every candidate;
- `candidate[0]` is forbidden;
- candidate indices never become semantic authority;
- F4 is used only when all preceding score dimensions are equal;
- fallback level is derived provenance;
- equality-tie and fallback rates are quality metrics;
- low/fallback decisions are not default Behavior Cloning labels.

### 10.2 Generic tactical-safe utility

At F3, prefer candidates that:

- prevent immediate loss;
- do not throw away live interaction;
- preserve starters/follow-up;
- avoid consuming unique/copy-critical resources;
- improve board or card economy without relying on unknown hidden facts;
- avoid obviously losing battle choices;
- preserve legal Main Phase 2 options.

F3 remains a heuristic. It must not be mislabeled as a known combo decision.

## 11. Confidence and label quality

| Confidence | Evidence standard | Future default BC treatment |
|---|---|---|
| `HIGH_CONFIDENCE` | Named scenario/line; all public preconditions satisfied; clear strategic winner; no fallback; scenario accepted | Eligible after trajectory admission |
| `MEDIUM_CONFIDENCE` | Named recovery or deck-specific utility; plausible competing branch; validated only partially | Include only after review or with lower weight |
| `LOW_CONFIDENCE` | Generic utility is primary evidence; profile lacks exact case | Exclude by default |
| `FALLBACK` | F4 or explicit unknown-state completion | Exclude by default |

All decisions remain in canonical admitted trajectories for audit. A derived learner view may filter/weight them; it must not rewrite the source episode.

## 12. Scenario design principles

The scenario corpus should be:

- engine-reachable under the exact locked rules/decks;
- perspective-safe;
- built through accepted fixtures/replay mechanisms, not a new hidden runtime board constructor;
- frozen by scenario identity and exact setup actions;
- evaluated on complete candidate membership/order;
- paired across hidden worlds where privacy matters;
- explicit about expected Goal/Line class, not necessarily one exact candidate when multiple choices are strategically equivalent;
- capable of checking explanation, confidence, fallback, and state transition.

The scenarios below are research families. Future implementation must instantiate them as executable fixtures before claiming PASS.

# Part I — Swordsoul Tenyi scenario stress tests

## SW-S01 — Ideal Mo Ye opening

**Public setup family**

```text
going first
own hand includes:
- Mo Ye
- Longyuan
- at least one additional legal Swordsoul/Wyrm serving as reveal/discard resource
- at least one interaction or recoverable follow-up
```

**Expected Goal:** `ESTABLISH_INTERACTION + PRESERVE_FOLLOW_UP`.

**Expected Line:** SW-L01.

**Decision behavior:**

- prefer Mo Ye while a legal reveal exists;
- prefer Chixiao as first Level 8 unless current candidates/public state justify another payoff;
- because Longyuan is already in hand, Chixiao should normally prefer Blackout, Summit, Emergence, or another resource over redundant Longyuan;
- select Qixing versus Chengying from actual matchup state;
- preserve at least one meaningful next-turn resource or hand interaction.

**Recovery:** if Mo Ye is stopped, evaluate Circle, Longyuan side line, or Tenyi access before generic fallback.

**Confidence:** high when the selected endboard/resource goal is unique.

## SW-S02 — Weak but playable Emergence opening

**Public setup family**

```text
going first
own engine access is Emergence
no direct Mo Ye/Longyuan/Tenyi starter required
hand otherwise contains defensive cards
```

**Expected Goal:** `ESTABLISH_MINIMUM_INTERACTION`.

**Expected Line:** SW-L02.

**Decision behavior:**

- Emergence searches Taia when that is the engine-supplied legal route;
- Taia banishes Emergence from GY for Token access;
- Chixiao is preferred as the baseline payoff;
- Chixiao searches Blackout or recovery according to remaining hand/public state;
- Taia's send should create next-turn value, commonly Mo Ye or a live Tenyi resource.

**Stop condition:** Chixiao + interaction/follow-up is acceptable; no forced maximal extension.

**Confidence:** high for the baseline line; medium for send/search variants.

## SW-S03 — Brick-like defensive hand

**Public setup family**

```text
hand contains Longyuan but no other legal Swordsoul/Wyrm discard
remaining cards are hand traps/traps or non-starters
```

**Expected Goal:** `SURVIVE_AND_PRESERVE_ENGINE_TOPDECK`.

**Decision behavior:**

- do not pretend Longyuan is activatable;
- set or retain legal interaction according to phase and public threats;
- avoid Normal Summoning a hand trap merely to create a low-value body unless battle/survival demands it;
- select F2/F3 utility with low confidence if no named line exists.

**Fallback label:** low/fallback, not default BC data.

## SW-S04 — Going-first Tenyi before Normal Summon

**Public setup family**

```text
hand includes Ashuna + Adhara or Vishuda
plus Mo Ye/Taia/Emergence access
```

**Expected Goal:** `PRESERVE_NORMAL_SUMMON + ESTABLISH_TENYI_RESOURCES`.

**Expected Line:** SW-L05 followed by SW-L01 or SW-L02.

**Decision behavior:**

- use a free Tenyi body and Monk when zone/restriction state supports it;
- compare Ashuna's Wyrm lock against any desired Dragite line;
- retain the Normal Summon and avoid blocking Monk's linked zone;
- choose Chixiao/Baxia/Draco based on search/board needs.

**Recovery:** if Ashuna is stopped, use the preserved Normal Summon rather than forcing the old Tenyi node.

## SW-S05 — Going second with Ecclesia and Vishuda

**Public setup family**

```text
opponent controls more monsters
own hand includes Ecclesia and Vishuda or another Tenyi
opponent public board includes at least one active Salamangreat Link/interaction source
```

**Expected Goal:** `FORCE_INTERACTION + REMOVE_ENGINE + STABILIZE`.

**Expected Lines:** SW-L03, SW-L05, SW-L06.

**Decision behavior:**

- prefer Ecclesia's free Special Summon when legal to preserve Normal Summon;
- evaluate Vishuda bounce before consuming it as material;
- choose Taia/Baxia when multiple public cards can be shuffled and deck send/revive is valuable;
- choose Mo Ye/Chixiao when search/negate is needed more;
- preserve a starter after the first Salamangreat interaction.

## SW-S06 — Mo Ye targeted by Veiler/Impermanence with Circle available

**Public setup family**

```text
Mo Ye effect is on chain or Mo Ye is targeted by a public negate
Heavenly Dragon Circle is legal
Shthana or another useful Wyrm remains searchable
```

**Expected Goal:** `CONVERT_INTERRUPTION`.

**Recovery edge:** Circle Tributes Mo Ye as cost, searches a Wyrm, and preserves any Mo Ye effect that can still resolve under engine rules.

**Decision behavior:**

- compare Circle conversion against holding Circle for later;
- if a non-effect Token remains, a searched Shthana may legally Special Summon and restore Level-8 access;
- re-evaluate the new continuation rather than enqueueing Chixiao/materials in advance.

**Confidence:** high only when all legal candidates and public outcome conditions are present.

## SW-S07 — Longyuan extender interrupted after Chixiao

**Public setup family**

```text
Chixiao/search has resolved
Longyuan activation or Token generation is stopped
Blackout or hand interaction remains
```

**Expected Goal:** `STOP_WITH_SAFE_BOARD`.

**Decision behavior:**

- invalidate Level-10 node;
- retain Chixiao + Blackout/hand interaction;
- avoid spending the remaining starter/interaction on a weak substitute;
- mark `STARTER_OR_EXTENDER_DID_NOT_RESOLVE` recovery.

**Confidence:** medium; high after dedicated fixture validation.

## SW-S08 — Planned Level-8 payoff unavailable

**Public setup family**

```text
second Chixiao copy exhausted or absent
current complete domain offers Baxia, Draco Berserker, and/or Dragite according to restrictions
```

**Expected Goal:** current board-dependent substitution, not candidate fabrication.

**Decision behavior:**

- Baxia for meaningful non-destruction board break/revive;
- Draco for monster-body removal and battle pressure;
- Dragite for Spell/Trap negate only with WATER in GY and no Wyrm lock;
- preserve remaining copy budgets.

**Confidence:** medium.

## SW-S09 — Board partially broken with Taia/Summit recovery

**Public setup family**

```text
Mo Ye or another Level-4 starter is in GY
Taia has legal banish fuel and/or Summit has a legal target
opponent retains one or more public threats
```

**Expected Goal:** `RECOVER_ENGINE + STABILIZE`.

**Decision behavior:**

- compare Taia Token line against Summit revival;
- prefer Baxia if shuffle and revival create two-stage recovery;
- preserve only remaining Chixiao/Blackout/hand interaction where possible;
- do not assume a revived Mo Ye can trigger without a legal reveal.

## SW-S10 — Grind state with Adhara recursion

**Public setup family**

```text
Adhara is in hand/GY with a face-up non-effect monster condition
multiple Wyrms are banished, such as Vishuda, Ashuna, or Longyuan
```

**Expected Goal:** `RESTORE_BEST_FOLLOW_UP_RESOURCE`.

**Decision behavior:**

- recover Vishuda if public removal is needed;
- recover Longyuan if a legal discard and Level-10 payoff remain;
- recover Ashuna if a Wyrm-locked extension is useful;
- avoid choosing by card ID order.

## SW-S11 — Proven lethal opportunity

**Public setup family**

```text
Qixing/Longyuan burn, Chengying scaling, Draco repeated attack, or Baxia/Vishuda removal creates a public lethal line
```

**Expected Goal:** `REACH_LETHAL`.

**Decision behavior:**

- calculate exact public damage and attack order;
- remove blockers before committing attacks;
- use Qixing/Longyuan burn only as represented by actual legal effects;
- preserve Main Phase 2 stabilization when lethal ceases to be proven.

**Confidence:** high for exact deterministic lethal.

## SW-S12 — Multiple Salamangreat threats

**Public setup family**

```text
opponent public board/history identifies:
- Raging Phoenix or Sunlight Wolf
- Sanctuary
- a searched/known Roar or Rage or another public interaction
```

**Expected Goal:** `REMOVE_ENGINE_AND_INTERACTION`.

**Decision behavior:**

- target vector distinguishes current negate/removal, recursion source, reincarnation enabler, and copy replacement;
- Blackout ranks the own Wyrm cost and opponent target pair together;
- account for Balelynx destruction protection and Raging/Princess destruction triggers;
- no hidden set-card identity is inferred unless public history legitimately preserves it.

## SW-S13 — Continuation/material selection

**Public setup family**

```text
complete continuation domain contains several legal Synchro materials or Blackout target tuples
```

**Expected Goal:** `PRESERVE_UNIQUE_RESOURCE`.

**Decision behavior:**

- for Baxia, prefer distinct Wyrm attributes when extra shuffle value matters;
- use spent/recursive/duplicate resources before unique starter/follow-up;
- for Blackout, choose expendable own Wyrm plus highest-value opponent pair;
- evaluate every legal continuation and return its public key.

# Part II — Salamangreat scenario stress tests

## SAL-S01 — Ideal Of Fire / Gazelle opening

**Public setup family**

```text
going first
own hand includes Of Fire or a searcher
plus at least one hand interaction or recoverable extender
```

**Expected Goal:** `ESTABLISH_RECURRING_INTERACTION + PRESERVE_COPY_RECOVERY`.

**Expected Lines:** SAL-L01, SAL-L02, SAL-L03, SAL-L05.

**Decision behavior:**

- Of Fire obtains Gazelle when needed;
- Balelynx obtains Sanctuary;
- Gazelle sends Spinny, Weasel, Jaguar, or trap according to reachable payoff;
- Stallio/Wolf/Princess/Raging route is selected only if resources/copies support it;
- reincarnated Raging searches Roar/Rage/Will/Charge according to current needs;
- preserve Jaguar/Charge recovery after using both Raging copies.

**Confidence:** high for dominant search/send branches; medium where several grave setups are comparable.

## SAL-S02 — Weak one-body opening

**Public setup family**

```text
Foxy or another single Salamangreat body
no confirmed Gazelle/Spinny extender
multiple hand interactions remain
```

**Expected Goal:** `ESTABLISH_MINIMUM_ENGINE + HOLD_INTERACTION`.

**Decision behavior:**

- activate Foxy's excavation without consulting hidden top-deck order when strategically appropriate;
- if it misses, convert the body to Balelynx/Sanctuary only when that preserves future value;
- do not consume unique Code/Weasel or both Raging copies without a route;
- accept Balelynx/hand-trap control or explicit low-confidence fallback.

## SAL-S03 — Brick-like defensive hand

**Public setup family**

```text
hand contains hand traps/Impermanence and no viable Salamangreat starter or extender
```

**Expected Goal:** `SURVIVE_AND_WAIT_FOR_ENGINE_ACCESS`.

**Decision behavior:**

- set legal Impermanence or other interaction;
- preserve hand traps for high-value Swordsoul effects;
- do not Normal Summon a hand trap for a meaningless Link unless survival/battle demands it;
- label generic fallback as low/fallback.

## SAL-S04 — Going-first Raging vs Wolf stop decision

**Public setup family**

```text
Gazelle/Spinny/Stallio line has produced enough material
both Raging copies remain
Wolf + trap + follow-up is also reachable
```

**Expected Goal:** choose between `ESTABLISH_RAGING_INTERACTION` and `STOP_WITH_SAFE_WOLF_BOARD`.

**Decision behavior:**

- take Raging when the reincarnation search and Princess/Weasel/copy-recycle plan justify both copies;
- stop on Wolf + Roar/Rage + hand interaction when Raging would exhaust recovery for marginal gain;
- emit the score/resource explanation rather than hide this as executor order.

## SAL-S05 — Going second into Chixiao/Qixing/Blackout

**Public setup family**

```text
opponent board has Chixiao and Qixing or Chengying
public history identifies Blackout or another interaction
own hand has starter plus one or more Veiler/Impermanence/Rage/Circle/Mining resources
```

**Expected Goal:** `FORCE_INTERACTION + BREAK_BOARD + RETAIN_GAZELLE`.

**Decision behavior:**

- use Veiler/Impermanence on the payoff whose active effect blocks the intended route;
- preserve a second starter after Chixiao/Blackout;
- use Stallio bounce/Heatleo/Hiita/Princess/Pyro according to legal candidates;
- account for Chengying destruction replacement and Qixing non-negating removal;
- stabilize if lethal is not proven.

## SAL-S06 — Of Fire negated, Gazelle in hand

**Public setup family**

```text
Of Fire summon effect is negated
Gazelle is already in hand
Of Fire can be used as Link material
```

**Expected Goal:** `RECOVER_PRIMARY_ENGINE`.

**Decision behavior:**

- Link Of Fire to Balelynx when legal;
- sending Of Fire to GY can create Gazelle's public trigger;
- use Gazelle send to rebuild according to current resources;
- do not assume the original search succeeded.

**Confidence:** high after exact fixture validation.

## SAL-S07 — Miragestallio extender interrupted

**Public setup family**

```text
Gazelle/Spinny established Miragestallio
its Deck summon is negated or target access is lost
remaining bodies can still form a smaller Link line
```

**Expected Goal:** `PRESERVE_ACHIEVED_VALUE`.

**Decision behavior:**

- invalidate expected Jaguar/Weasel/Falco target node;
- use Stallio as Salamangreat Link material if the bounce and Wolf route are still valuable;
- preserve hand interaction affected by Stallio's non-FIRE-effect restriction;
- stop on Wolf/interaction rather than forcing Raging.

## SAL-S08 — Princess or second Raging unavailable

**Public setup family**

```text
Princess is banished/unavailable or one Raging copy is no longer in Extra Deck and no recycle has resolved
```

**Expected Goal:** `SWITCH_TO_COPY_SAFE_RECOVERY`.

**Decision behavior:**

- disable SAL-L05/SAL-L06 edges whose preconditions fail;
- use Wolf/Jaguar/Falco/Will/Charge/Stallio/Heatleo plans;
- preserve remaining Raging/Pyro copies;
- never infer the missing Extra Deck candidate.

## SAL-S09 — Board partially broken, Balelynx/Princess/Jaguar resources remain

**Public setup family**

```text
Wolf/Raging is threatened or destroyed
Balelynx protection and/or Princess GY trigger is available
Jaguar can recycle an Extra Deck monster
```

**Expected Goal:** `CONVERT_DESTRUCTION + REBUILD`.

**Decision behavior:**

- use Balelynx protection only if preserving the target exceeds saving the grave protection for later;
- trigger Princess at the most valuable public Swordsoul Special Summon;
- recycle the copy required for the next line with Jaguar;
- recover Gazelle/trap through Wolf/Falco when legal.

## SAL-S10 — Charge grind continuation

**Public setup family**

```text
at least three legal FIRE monsters are in GY/banished
examples include Raging, Wolf, Stallio, Balelynx, Gazelle, Princess, or Ash Blossom
```

**Expected Goal:** `REPAIR_COPY_BUDGET + REVIVE_USEFUL_BODY`.

**Decision behavior:**

- choose a target triple that restores the most critical Extra Deck copies;
- revive the body whose negated/no-attack state still advances zones/material/follow-up;
- do not shuffle away the only live grave trigger or Jaguar target;
- use alternate Charge destruction mode only when current ATK modification condition and target value are public.

## SAL-S11 — Proven lethal through Raging/Pyro

**Public setup family**

```text
Princess/other effect destroyed own FIRE
Raging can revive with an ATK increase
or Pyro/Heatleo clears/manipulates the public board
```

**Expected Goal:** `REACH_LETHAL`.

**Decision behavior:**

- calculate exact public damage;
- order wipe/removal/revival before attacks;
- retain Main Phase 2 Wolf/Roar/Rage plan if lethal becomes unavailable;
- avoid Of Fire's self-destruction battle effect unless it increases net lethal or protected recovery.

## SAL-S12 — Multiple Swordsoul threats and interaction timing

**Public setup family**

```text
opponent public state includes multiple of:
- Chixiao
- Qixing or Dragite
- Chengying
- known Blackout
- live grave recovery
```

**Expected Goal:** `DENY_HIGHEST_IMPACT_RESOLUTION`.

**Decision behavior:**

- Roar/Veiler/Impermanence/Ash/Belle timing uses threat, public investment, replacement, and interaction scarcity;
- Rage/Princess target vectors account for Chengying protection and current Token/material route;
- hold Princess through a low-value summon when a later Token/Level-10 summon is a stronger public choke point;
- no hidden hand is reconstructed.

## SAL-S13 — Continuation/material and zone choice

**Public setup family**

```text
complete domain contains multiple legal Link materials, linked zones, Rage target sets, or Charge target triples
```

**Expected Goal:** `PRESERVE_COPY_AND_RECURSION`.

**Decision behavior:**

- preserve Gazelle before its send, Jaguar before needed recycle, and one-copy Weasel/Code/Falco when their line remains active;
- use spent bodies and materials with useful grave effects;
- place a summon to enable Wolf/Jaguar while preserving future zones;
- evaluate every legal tuple and use public key only as final equality tie-break.

# Part III — Cross-cutting adversarial scenarios

## 13.1 Candidate domain contains a strategically bad legal branch

**Test:** include at least one legal candidate that consumes the sole follow-up or all same-name Extra Deck copies for a smaller immediate gain.

**PASS expectation:** candidate remains in evaluation records but loses through resource/plan dimensions. Domain membership/order is unchanged.

## 13.2 Active line becomes unavailable midway

**Test:** interrupt or remove the exact body/resource expected by the active line.

**PASS expectation:** stale node invalidated before next selection; recovery/fallback provenance emitted; no queued target/material choice survives automatically.

## 13.3 Same public frame, different hidden opponent hand

**Test:** paired worlds differ only in opponent-private cards.

**PASS expectation:** identical selected key, score vector, plan transition, confidence, and explanation.

## 13.4 Rejected action submission

**Test:** policy proposes a key, environment rejects a stale/nonmember submission in a controlled negative fixture.

**PASS expectation:** no strategy-state delta commits; next valid proposal from unchanged frame/state is identical.

## 13.5 Candidate order is semantically meaningful but public-key values differ

**Test:** use real canonical environment order and candidates whose strategic scores tie.

**PASS expectation:** environment order is preserved in records; final equality tie is the frozen bytewise public-key rule, not index authority.

## 13.6 Parallel episode isolation

**Test:** interleave two episodes with different active Plans in one worker/process.

**PASS expectation:** decisions equal isolated executions; no cross-episode flags or copy budgets leak.

## 13.7 Knowledge-destroying transition

**Test:** public history includes a shuffle/randomization that removes legitimate hidden physical identity.

**PASS expectation:** strategy memory does not retain pointer/locator/card identity through the transition. Only contract-authorized aggregate/public knowledge remains.

## 14. Evaluation outputs per scenario

Each future scenario result should report:

```text
scenario_id
rules_bundle_id
locked_deck_ids
teacher_policy_artifact_id
strategy_profile_id
initial public frame identity
environment decision indices exercised
complete candidate count/digest per frame
selected public_action_key per frame
active goal/line/recovery IDs
plan invalidation reasons
confidence/fallback level
expected outcome class
actual outcome class
privacy paired-world result
determinism repeat result
```

This is test/evaluation provenance, not a new gameplay contract.

## 15. Acceptance implications

The scenario corpus directly supports:

- T-G09 Swordsoul line coverage;
- T-G10 Salamangreat line coverage;
- T-G11 interruption recovery;
- T-G12 target selection;
- T-G13 material selection;
- T-G14 battle baseline;
- T-G15 going-first/going-second coverage;
- T-G16 fallback determinism;
- T-G18 explanation safety;
- T-G20 frozen evaluation baseline.

A scenario document alone is not evidence. PASS requires executable engine-reachable fixtures and exact recorded results.

## 16. Final representation decision

### Exact scripts

**Rejected.** They are brittle, legality-assuming, interruption-hostile, and narrow as training labels.

### Ordered executors

**Rejected as top-level authority.** Small ordered rule tables may contribute named score components, but no rule returns before complete-domain evaluation.

### State machine / behavior tree

**Adapted only for bounded mode and strategy-state organization.** They do not own candidate resolution.

### GOAP / forward planning

**Rejected for Teacher v1.** OCGForge lacks a proven arbitrary perspective-safe state-fork boundary, and a custom symbolic transition model would duplicate legality.

### Graphs and utilities

**Accepted as the hybrid:**

```text
public-state Goal graph
+
partial-order Line dependencies
+
explicit Recovery edges
+
complete-domain deterministic utility
+
canonical final equality tie-break
```

This representation retains deck expertise while allowing every new public frame to correct the Plan.

## 17. Unresolved research questions

1. Which exact public candidate fields are stable enough to match candidate intents without card-ID-specific code in `TeacherCore`?
2. Which temporary restriction/use facts are fully reconstructible from current public observation/history, and which need a separately reviewed public extension?
3. How should scenario expected outcomes express strategic equivalence when multiple candidate keys are equally acceptable for different copies/placements?
4. What confidence cap should apply after each recovery class?
5. How should active Plan state be serialized for policy audit without becoming authoritative trajectory state or learner input?
6. What is the smallest engine-reachable fixture mechanism that covers all 26 deck scenarios without creating a hidden runtime board-construction API?
7. How should long-grind copy-budget scenarios be generated efficiently while preserving exact replay and privacy evidence?
8. At what equality-tie/fallback rate should a profile be considered under-specified and fail quality acceptance?

None of these questions require a state fork, hidden input, candidate filtering, or early ML implementation.
