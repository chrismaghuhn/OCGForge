# OCGForge Deterministic Teacher Architecture

**Date:** 2026-08-28  
**OCGForge checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Matchup:** `ocgforge.matchup.swordsoul_salamangreat.v1`  
**Status:** research/design recommendation only; non-normative

## 1. Executive decision

OCGForge should **not** use a WindBot-like ordered executor architecture as the authoritative top-level Teacher policy.

OCGForge should use a deterministic hybrid composed of:

- complete-domain utility evaluation;
- explicit public-state strategic goals;
- deck-specific partial-order line graphs;
- explicit recovery edges;
- generic target, material/cost, interaction, and battle evaluators;
- bounded episode-local strategy state reconciled against every current observation;
- exact integer/lexicographic resolution;
- one immutable deck `StrategyProfile` per versioned policy artifact;
- derived, privacy-safe decision explanations.

The Teacher must remain a policy over the same boundary as future learned models:

```text
PublicEnvironmentObservation
+
complete ordered EnvironmentActionCandidate[]
        ↓
DeterministicTeacherPolicy
        ↓
exactly one current public_action_key
```

It must not become a second rules engine, a hidden-state client mirror, an exact combo macro player, or an implicit candidate filter.

## 2. Research basis and authority

### 2.1 Live OCGForge state

**SOURCE FACT.** The live `origin/main` checkpoint inspected on 2026-08-28 was:

```text
ea5b3ddf414987b451c44becf30619f1a0814189
```

This matches the post-Phase-2 checkpoint recorded in [Issue #16](https://github.com/chrismaghuhn/OCGForge/issues/16).

The relevant accepted public boundaries on this checkpoint are:

- [`docs/contracts/episodic-environment-v2.md`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/docs/contracts/episodic-environment-v2.md);
- [`docs/contracts/public-environment-observation-v1.md`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/docs/contracts/public-environment-observation-v1.md);
- [`docs/contracts/public-action-identity-v1.md`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/docs/contracts/public-action-identity-v1.md);
- the decision protocol and PlayerObservation contracts referenced by those public adapters.

These contracts require the public frame to preserve the complete candidate domain and map exactly one selected current public key back to the corresponding internal candidate. Projection failure fails the frame; privacy is never obtained by dropping, truncating, reordering, or defaulting candidates.

### 2.2 Locked rules and decks

**SOURCE FACT.** The Teacher target is the exact locked M3/M3.5 matchup:

| Item | Identity |
|---|---|
| Matchup | `ocgforge.matchup.swordsoul_salamangreat.v1` |
| Rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |
| Swordsoul deck hash | `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` |
| Salamangreat deck hash | `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` |
| Main / Extra | 40 / 15 for each deck |

The exact deck source is [`fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/fixtures/decks/ocgforge.matchup.swordsoul_salamangreat.v1.json), with human-readable locked lists in [`tools/m3/locked_lists.py`](https://github.com/chrismaghuhn/OCGForge/blob/ea5b3ddf414987b451c44becf30619f1a0814189/tools/m3/locked_lists.py).

### 2.3 Phase-3 provenance context

**SOURCE FACT / VOLATILE CONTEXT.** The open Phase-3A PR inspected during research was [PR #20](https://github.com/chrismaghuhn/OCGForge/pull/20), head `9496eba06bc6906f6084c5bd0207209efabe8f63`. It proposes immutable policy artifacts, participant assignments, and policy-local RNG provenance.

**PROPOSED OCGFORGE DESIGN.** Teacher implementation should reuse the accepted provenance owner after Phase 3A is finalized. This report does not create a competing gameplay identity or trajectory schema. Teacher-specific reason/confidence/plan metadata should be optional derived policy provenance.

## 3. Non-negotiable architecture invariants

The following are design gates, not preferences.

### 3.1 Public boundary only

The Teacher may consume:

- the acting perspective's `PublicEnvironmentObservation`;
- the complete ordered `EnvironmentActionCandidate[]` domain for that frame;
- immutable Teacher policy configuration and deck profile;
- bounded strategy memory derived only from prior perspective-authorized frames, accepted public actions, and public history.

It may not consume:

- `CoreHost`;
- raw ocgcore queries or messages;
- `DecisionRequest.decision_id` or internal `ActionCandidate.semantic_key`;
- opponent hand contents or hidden deck order;
- face-down identity not visible to the perspective;
- private locators, pointers, cache keys, or secret-derived hashes;
- restricted diagnostics;
- a second omniscient client mirror.

### 3.2 Complete-domain evaluation

For every supported actionable frame with a nonempty candidate domain:

```text
input candidate count
=
candidates inspected by TeacherCore
=
candidates participating in deterministic resolution
```

A line graph, goal, target rule, material preference, or interaction heuristic may contribute features or score components. It may not remove a candidate from consideration.

### 3.3 Engine owns legality

The Teacher never asks, “Is this action legal?” It asks only, “Given that the environment supplied this legal candidate, how desirable is it?”

The StrategyProfile must not independently enumerate:

- legal summon materials;
- legal targets;
- legal zones;
- legal chain responses;
- legal subsets/sequences;
- activation conditions;
- engine responses.

For continuations, the environment exposes the legal intermediate semantic actions. The Teacher ranks those legal continuations; it does not rebuild the original combinatorial response.

### 3.4 Fail closed on architectural uncertainty

If implementation cannot prove that a required candidate feature can be derived from the public frame, it must not query an internal fallback source. The supported choices are:

1. use a coarser public heuristic;
2. mark the decision low-confidence/fallback;
3. propose a separately reviewed public-contract extension;
4. fail the unsupported Teacher policy path during development.

Silently consulting internal state is a privacy BLOCKER.

### 3.5 Determinism

Under identical:

- observation;
- complete ordered candidate domain;
- episode-local strategy state;
- Teacher artifact/configuration;

selection and decision explanation must be byte-stable independent of:

- pointer addresses;
- unordered iteration;
- wall time;
- PID;
- thread scheduling;
- process completion order;
- random-device state;
- platform floating-point drift.

The first Teacher should use no policy randomness.

## 4. Architecture alternatives

The following table is the result of the comparison, not a preselected outcome.

| Architecture | Deterministic | Maintainable | Combo-capable | Recovery | Explainable | Requires state fork | Recommended |
|---|---|---|---|---|---|---|---|
| Ordered executors | Yes if ordering is explicit | Low to medium; order coupling grows quickly | Medium for rehearsed lines | Low unless many ad hoc flags are added | Medium; first-match reason is visible but global priority interactions are opaque | No | **No as top-level policy**; retain small local predicates only |
| Utility AI | Yes with exact integer scores and canonical tie-break | High when features are centralized | Medium alone | Medium; can re-rank current state but lacks multi-step intent | High | No | **Yes as tactical candidate core** |
| Behavior tree | Yes with deterministic child order | Medium | Medium | Medium | Medium; priority can be hidden in tree shape | No | **Reference/adapt for mode organization only** |
| GOAP | Conceptually yes | Low to medium without a validated state model | High | High in theory | High | Usually requires trustworthy transition model/fork for this domain | **Reject for v1 execution**; bounded goal selection only |
| Combo graph | Yes | High if declarative and versioned | High | High with explicit invalidation and recovery edges | High | No when it describes public-state dependencies rather than simulating legality | **Yes as deck knowledge** |
| Hybrid | Yes | High with strict ownership | High | High | High | No for v1 | **Recommended** |

### 4.1 Ordered rule/executor system

**Finding.** Ordered executors are attractive for a first prototype because each rule is easy to write. They become brittle when:

- adding one high-priority rule changes unrelated later behavior;
- target/material choices are queued for future callbacks;
- the action expected by a combo rule is absent after interruption;
- resource flags have unclear reset ownership;
- the first matching rule becomes an implicit candidate filter;
- fallback becomes “first legal item.”

**Decision.** Use ordered rules only as bounded feature contributors, such as “this candidate advances active goal `ESTABLISH_CHIXIAO`” or “this material consumes the only known starter.” No rule may return the final candidate before all candidates are scored.

### 4.2 Utility AI

**Finding.** Utility evaluation fits OCGForge's complete variable candidate domain and is naturally explainable. It generalizes target, material, interaction, and battle decisions. It cannot by itself preserve multi-step intent or distinguish “locally valuable but line-breaking” actions.

**Decision.** Make utility evaluation the generic tactical core, augmented by line/goal state.

### 4.3 GOAP-like planning

**Finding.** Full GOAP requires a trustworthy transition model. An incomplete symbolic Yu-Gi-Oh! transition model would become a second legality/rules engine; an engine-backed model requires a proven perspective-safe state fork that OCGForge does not currently claim.

**Decision.** Do not execute forward search or arbitrary GOAP in Teacher v1. It is acceptable to select among explicit goals and declarative line nodes using current public-state predicates. That is bounded goal control, not hypothetical state simulation.

### 4.4 Behavior tree

**Finding.** A behavior tree can organize high-level modes such as going first, going second, recovery, battle, and grind. It does not solve candidate ranking, and selector order can reproduce executor opacity.

**Decision.** A shallow mode tree is optional implementation organization, never the semantic decision owner. The final policy still scores the complete domain.

### 4.5 Combo / line graph

**Finding.** A graph can encode requirements, goals, resources, vulnerabilities, and recovery without prescribing one exact engine response sequence. It remains useful when candidates appear in different orders or a line is interrupted.

**Decision.** Each StrategyProfile should own a versioned partial-order line graph plus recovery edges. Graph nodes identify public-state goals and candidate intents, not internal engine actions.

## 5. Recommended conceptual architecture

```text
DeterministicTeacherPolicy
│
├── PolicyBoundaryAdapter
│   ├── receives one immutable actionable public frame
│   ├── validates profile/artifact/frame compatibility
│   └── returns one public_action_key
│
├── TeacherCore
│   ├── PublicStateInterpreter
│   ├── StrategyStateReconciler
│   ├── GoalAndLineController
│   ├── CandidateFeatureExtractor
│   ├── TacticalEvaluator
│   ├── TargetEvaluator
│   ├── MaterialAndCostEvaluator
│   ├── InteractionTimingEvaluator
│   ├── BattleEvaluator
│   └── DeterministicResolver
│
├── StrategyProfile
│   ├── exact locked-deck identity
│   ├── CardRoleCatalog
│   ├── ResourceModel
│   ├── GoalCatalog
│   ├── PartialOrderLineGraph
│   ├── RecoveryEdges
│   ├── InteractionMap
│   └── VersionedIntegerPreferences
│
├── EpisodeLocalStrategyState
│   ├── active goal / line / node
│   ├── achieved public goals
│   ├── perspective-authorized resource ledger
│   ├── public restrictions / effect-use ledger
│   ├── known public threats
│   └── recovery/fallback status
│
└── DecisionExplanationEmitter
    └── derived policy provenance only
```

## 6. Component ownership and contracts

| Component | Responsibility | Inputs | Outputs | State owner | Generic vs deck-specific | Privacy | Determinism / replay |
|---|---|---|---|---|---|---|---|
| `PolicyBoundaryAdapter` | Enforce exact public frame and return one current key | Public observation, complete ordered domain, artifact/profile IDs | Selected `public_action_key`, optional explanation | Stateless | Generic | Rejects internal/restricted inputs | Frame-local validation; selected key is public semantic identity |
| `PublicStateInterpreter` | Derive policy facts from perspective-safe fields | Current observation | `InterpretedPublicState` | Stateless per frame | Generic with profile role lookup | No extra query path | Canonical iteration and explicit sorting |
| `StrategyStateReconciler` | Reconcile memory with current facts; invalidate stale plan | Interpreted state, prior accepted strategy state | Reconciled state, invalidation reasons | Episode-local | Generic | Drops identity after knowledge-destroying transitions; memory cannot override observation | Explicit transition table and reset rules |
| `GoalAndLineController` | Select/retain goal and identify reachable line edges | Reconciled state, profile graph | Active goals, per-candidate plan features | Episode-local plan IDs only | Generic controller, deck-specific graph | Public predicates only | Stable goal priority and graph ordering |
| `CandidateFeatureExtractor` | Inspect every supplied candidate once | Complete domain, interpreted state | Feature record for every candidate | Stateless | Generic; profile roles add labels | Uses only public candidate metadata | Candidate count/order audit |
| `TacticalEvaluator` | Score immediate board/resource consequences | Candidate features, state | Integer tactical components | Stateless | Generic | No hidden threat lookup | Checked integer arithmetic |
| `TargetEvaluator` | Rank legal target candidates | Target candidate metadata, public threats | Target score components | Stateless | Generic dimensions plus profile modifiers | Hidden identities never inferred | Stable threat tier ordering |
| `MaterialAndCostEvaluator` | Rank legal materials, tributes, discards, banishes, costs | Candidate metadata, resource ledger | Preservation/cost vector | Stateless | Generic preservation rules plus profile roles | Own/perspective-authorized resources only | Stable role/value order |
| `InteractionTimingEvaluator` | Decide activate-now vs hold | Current chain/history, public opponent investment, known own interaction | Timing score components | Stateless plus accepted effect-use ledger | Generic engine; profile choke-point map | Public events/cards only | No probabilistic RNG in v1 |
| `BattleEvaluator` | Avoid obvious battle mistakes and recognize lethal | Board, battle candidates, public history | Lethal/safety/damage/post-battle components | Stateless | Mostly generic | Known/public interaction only | Exact integer LP/ATK/DEF calculations where exposed |
| `DeterministicResolver` | Choose deterministic argmax over all candidates | Complete scored candidate vector | One selected key, margin, tie reason | Stateless | Generic | Public keys only | Lexicographic integer comparison; bytewise key tie-break |
| `DecisionExplanationEmitter` | Produce audit/label metadata | Selected/scored records, plan state | Derived explanation | Stateless | Generic schema; profile IDs | Must not add non-observed information | Excluded from gameplay identity; exact artifact attribution |
| `StrategyProfile` | Immutable deck knowledge | Policy artifact | Roles, goals, graphs, modifiers | Immutable | Deck-specific | No hidden runtime data | Content-addressed/versioned artifact |
| `EpisodeLocalStrategyState` | Preserve bounded intent and use/restriction facts | Accepted transitions only | Next policy state | Per episode and participant assignment | Generic schema | Perspective-isolated | Reset/isolation and accepted-transition commit gates |

## 7. Decision lifecycle

The recommended lifecycle is:

```text
1. receive actionable public frame
2. validate artifact/profile/frame compatibility
3. interpret current perspective-safe state
4. reconcile prior accepted strategy state
5. invalidate any plan whose public preconditions no longer hold
6. identify current strategic goals and reachable line/recovery edges
7. derive features for every candidate exactly once
8. score every candidate with generic and profile components
9. deterministic argmax across the complete domain
10. return exactly one current public_action_key
11. emit optional derived explanation
12. commit strategy-state transition only after environment acceptance
```

### 7.1 Proposal vs accepted transition

A policy proposal must not mutate durable strategy state merely because a key was returned. A rejected `step()` or stale submission cannot advance the Teacher's plan.

Implementation should therefore distinguish conceptually:

```text
propose(frame, strategy_state)
    → selected_key + proposed_state_delta + explanation

commit_accepted(accepted_transition, proposed_state_delta)
    → next_strategy_state
```

The exact API may differ, but the invariant is mandatory: only accepted public semantic actions advance strategy memory.

### 7.2 Observation is authoritative over memory

If memory says a resource remains available but the current observation does not support that fact, the Teacher must:

1. invalidate the conflicting plan/resource fact;
2. record a public-safe invalidation reason;
3. re-enter recovery or fallback;
4. never synthesize the missing resource.

## 8. Strategic state model

Recommended bounded state:

```text
EpisodeLocalStrategyStateV1
- participant_policy_assignment_id
- episode_instance_token owned by runtime, not exposed as gameplay identity
- active_goal_id | NONE
- active_line_id | NONE
- active_line_node_id | NONE
- achieved_goal_ids[] in canonical profile order
- public_restriction_facts[]
- effect_use_facts[]
- resource_facts[]
- known_public_threat_facts[]
- recovery_state
- last_accepted_public_action_key
- last_accepted_environment_decision_index
```

This is conceptual, not a ratified schema.

### 8.1 What may be remembered

- own cards and resources legitimately visible to the acting perspective;
- publicly revealed opponent cards;
- public actions and events;
- effects the Teacher itself selected and that were accepted;
- public turn/phase/chain state;
- deterministic profile and line IDs;
- restrictions derivable from accepted actions/public history.

### 8.2 What may not be remembered

- opponent hidden card identity;
- hidden deck order;
- face-down identity after it is no longer visible;
- raw engine object identity;
- hidden physical-card identity across a shuffle or other knowledge-destroying transition;
- internal candidate or decision IDs;
- guessed exact opponent hand contents.

### 8.3 Once-per-turn and restriction memory

The Teacher needs to reason about once-per-turn usage and temporary restrictions such as:

- Swordsoul Token non-Synchro Extra Deck restriction;
- Ashuna's Wyrm-only Special Summon restriction;
- Salamangreat of Fire's FIRE-only Special Summon restriction;
- Miragestallio's non-FIRE monster-effect restriction;
- Promethean Princess's FIRE-only Special Summon restriction;
- already-used recursion, search, trap, and interaction effects.

The preferred source is current observation plus perspective-safe event history. Policy-local memory may fill only genuinely public temporal gaps and must have explicit turn/phase/episode reset semantics. It must never query engine effect flags.

## 9. StrategyProfile boundary

### 9.1 What belongs in generic TeacherCore

- public-state decoding;
- complete-domain iteration;
- deterministic tie resolution;
- threat dimensions;
- target ranking framework;
- material/cost preservation framework;
- interaction timing framework;
- battle/lethal baseline;
- generic resource dimensions;
- plan invalidation/recovery mechanics;
- fallback hierarchy;
- explanation generation;
- state reset/isolation;
- provenance binding.

### 9.2 What belongs in deck-specific StrategyProfile

- exact deck identity/hash and supported matchup identity;
- centralized card-role catalog;
- deck-specific resources and restrictions;
- goals and acceptable endboards;
- partial-order line families;
- resource requirements/costs/preservation preferences;
- search, send, discard, banish, and material modifiers;
- deck-specific choke points and interaction mappings;
- Extra Deck copy budgets;
- recovery edges;
- versioned integer preferences.

### 9.3 Avoiding hardcoded-card-ID sprawl

Card identities are unavoidable in a deck-specific policy. They must be centralized in the immutable `CardRoleCatalog`, not scattered through generic code or hundreds of opaque `if card_id == ...` branches.

Recommended conceptual profile organization:

```text
StrategyProfile
├── profile metadata
├── exact deck manifest reference
├── CardRoleCatalog
├── ResourceDefinition[]
├── GoalDefinition[]
├── LineDefinition[]
├── RecoveryEdge[]
├── InteractionPattern[]
├── TargetModifier[]
├── MaterialModifier[]
└── IntegerPreferenceTable
```

A profile is a policy artifact, not a rules artifact. It may say “preserve Jack Jaguar for recursion” but never “Jack Jaguar is legal to summon here.”

## 10. Combo, line, plan, and goal model

The authoritative terminology is detailed in the combo/recovery report. Architecturally:

- a **Combo** is a family of coordinated plays producing a strategic payoff;
- a **Line** is one partial-order route through that family;
- a **Plan** is the current episode-local selection of a goal and line;
- a **Goal** is a desired predicate over the current/future public strategic state;
- a **Recovery** is a transition to another reachable goal/line after invalidation;
- a **Fallback** is a deterministic decision layer used when no supported plan edge applies.

### 10.1 Line nodes describe outcomes, not exact engine commands

Good line node:

```text
Goal: establish a level-8 Swordsoul Synchro search/interaction body
Preconditions: token-access starter + legal Synchro candidate supplied
Consumes: normal summon or starter access; token route
Preserves: at least one Longyuan discard candidate where possible
Progress evidence: public field/GY and accepted action history
Recovery: Longyuan side line, Tenyi line, or interaction-preserving stop
```

Rejected exact script:

```text
activate candidate index 2
select card index 0
select material indices 1 and 4
choose option 1
```

The latter is brittle, index-authoritative, and fails under interruption or domain reordering.

## 11. Candidate evaluation and deterministic resolution

### 11.1 Every candidate gets a structured score

Recommended conceptual result:

```text
CandidateEvaluationV1
- public_action_key
- candidate_kind
- matched_goal_ids[]
- matched_line_edge_ids[]
- hard_policy_class
- plan_progress_score
- tactical_urgency_score
- target_score
- material_cost_score
- interaction_timing_score
- battle_score
- resource_preservation_vector
- profile_preference_score
- confidence_evidence
- explanation_reason_ids[]
```

This is derived policy data, not public gameplay state.

### 11.2 Prefer lexicographic bands over one opaque scalar

A single “board score” obscures tradeoffs and makes one numerical weight silently dominate unrelated behavior. Recommended deterministic comparison:

```text
1. hard policy class
2. active-plan / recovery progress
3. immediate tactical necessity
4. lethal / survival class
5. interaction timing value
6. target value
7. material/cost preservation
8. future engine/follow-up resource vector
9. profile preference
10. bytewise ascending public_action_key equality tie-break
```

Within each band, use bounded signed integers and explicit canonical dimension order. Use checked arithmetic; overflow is an error during development, not wraparound.

### 11.3 No floating point in Teacher v1

Floating-point utility is unnecessary for the first deterministic Teacher and introduces cross-platform comparison hazards. Use integers or explicitly specified fixed-point values only.

If a later Teacher uses floating point, it requires a new policy artifact and a documented deterministic inference contract. That is not recommended for v1.

### 11.4 Final tie-break

Research recommends bytewise ascending `public_action_key` only after all strategic score dimensions are exactly equal.

The public key must never be the primary strategy signal. The evaluator should report equality-tie frequency; a high rate indicates under-specified heuristics and should not be hidden by the tie-break.

Candidate index/order is never selected or persisted as semantic action authority.

## 12. Resource model

The Teacher should maintain a vector, not one board scalar.

### 12.1 Generic resources

| Resource | Meaning | Typical preservation question |
|---|---|---|
| Survival / lethal margin | Immediate win/loss exposure | Does holding interaction prevent lethal? Can current line guarantee lethal from public state? |
| Card economy | Accessible own cards and repeatable value | Does this discard consume the only follow-up card? |
| Tempo | Current board/action pressure | Does bounce/removal gain a turn even without card advantage? |
| Interaction capacity | Available negates/removal/protection | Is this interaction needed now or more valuable against a later public choke point? |
| Engine access | Starters/searchers/recursion routes | Does material selection consume the only route back into engine? |
| Normal Summon | Used/available strategic resource | Can a special-summon line preserve it? |
| Extra Deck access | Legal current access plus remaining copies | Does this route exhaust the only recovery copy? |
| Graveyard resource | Useful accessible grave cards | Is the grave card spent, recursive, protected, or required as cost? |
| Banished resource | Recoverable vs permanently spent cards | Can Adhara/Charge reclaim it? Does banishing advance Chengying? |
| Once-per-turn availability | Remaining effect budget | Has the public effect already been used? |
| Follow-up | Next-turn starter/recursion | Does the endboard retain Gazelle, Taia, Summit, Jaguar, or a searchable trap? |
| Zone capacity | Main/Extra Monster and S/T zones | Will material/placement block the next intended line? |
| Restriction state | Active summon/effect restrictions | Does the candidate conflict with FIRE/Wyrm/Synchro-only constraints? |
| Battle pressure | Damage, attackers, safe sequence | Is a body worth preserving for Main Phase 2 or next turn? |

### 12.2 Deck-specific resources

Swordsoul Tenyi adds:

- revealable Swordsoul/Wyrm card without consuming it;
- Longyuan discard quality;
- Taia banish fuel;
- non-effect-monster state for Tenyi abilities;
- token access and token occupancy;
- Wyrm-only and Synchro-only restrictions;
- Level 8 / 9 / 10 route availability;
- banished Wyrm recovery;
- Blackout expendable own Wyrm;
- WATER-in-GY condition for Dragite;
- Extra Deck copy budget for Chixiao/Baxia/Monk/Draco Berserker.

Salamangreat adds:

- Gazelle access and send target;
- reincarnation access through Sanctuary or Code of Soul;
- linked-zone access;
- graveyard body loop;
- Raging/Pyro/Sunlight Wolf/Balelynx copy budget;
- FIRE-only and non-FIRE-effect restrictions;
- Roar/Rage availability and recoverability;
- Jaguar recycle target quality;
- Weasel opponent-field setup availability;
- Princess grave trigger setup;
- Code of Soul opponent-turn Link availability;
- Charge three-target recycle state;
- publicly modified FIRE Extra Deck ATK for Charge's destruction mode.

## 13. Target selection architecture

Target selection is not a separate legality layer. Every supplied target candidate receives generic and profile-specific features.

### 13.1 Generic target dimensions

- immediate lethal threat;
- active engine enabler;
- interaction source;
- recursion/value generator;
- floodgate or restriction source;
- opponent resource investment already committed;
- replacement availability visible from public state;
- removal resistance and likely effectiveness of the chosen effect;
- battle relevance;
- future combo relevance;
- whether the target is already negated/spent;
- whether another selected/chain effect already reserves it for removal or negation.

### 13.2 Threat tiers

Recommended deterministic conceptual tiers:

```text
T0: must answer to prevent public-state lethal or policy failure
T1: active engine-critical payoff / floodgate / broad interaction
T2: recursion engine or high-investment threat
T3: meaningful tempo or battle threat
T4: low-value/spent body
T5: unknown face-down identity; score only by legal public facts
```

A face-down card must never receive a score based on its hidden identity. It may be valued as an unknown set resource, zone occupancy, or publicly revealed known card only where the observation legitimately preserves that knowledge.

### 13.3 Matchup-specific modifiers

Profiles may add public-state modifiers, for example:

- Swordsoul prioritizes a publicly active Gazelle, Miragestallio, Sunlight Wolf, Raging Phoenix, Sanctuary, Roar, or Rage according to current plan and replacement availability;
- Salamangreat prioritizes Chixiao, Qixing, Chengying, Dragite, Baxia, Blackout's visible setup, or a board-breaking Tenyi according to current threat and recoverability.

These are modifiers, not unconditional “always target card X” rules.

## 14. Material and cost selection

The environment supplies legal continuation choices. The Teacher ranks them using a preservation model.

### 14.1 Generic material/cost dimensions

Prefer, where strategically equivalent:

1. already-spent once-per-turn bodies;
2. recursive resources that become useful in GY/banished state;
3. duplicate engine pieces;
4. low-value bodies or tokens;
5. resources whose consumption advances another public goal.

Preserve, unless the payoff justifies consumption:

1. the only starter;
2. the only extender after an exposed choke point;
3. live interaction;
4. unique follow-up;
5. required reveal/discard/banish fuel;
6. the last necessary Extra Deck copy;
7. a body required for a known recovery edge.

### 14.2 Selection families

The same framework applies to:

- Synchro material;
- Link material;
- Xyz material decisions where exposed by continuations;
- tribute/release;
- discard;
- banish cost;
- destroy-own-card cost;
- return-to-deck/recycle choices;
- option selection affecting cost/payoff.

### 14.3 No staged hidden callback queue

The Teacher must not choose an activation now and secretly enqueue expected future target/material indices. Every continuation frame is independently evaluated against the current public observation and complete legal continuation domain, while the active plan provides context.

## 15. Interaction timing

### 15.1 Activate now vs hold

Recommended generic components:

- `resolution_threat`: consequence if the current public effect resolves;
- `engine_criticality`: starter, extender, bridge, payoff, recursion, protection, or filler;
- `public_investment`: normal summon, discard, material, Extra Deck, or prior effect resources already committed;
- `replacement_availability`: publicly known alternative routes;
- `interaction_scarcity`: number and breadth of own remaining answers;
- `future_choke_value`: known higher-value public line point likely reachable from observed resources;
- `chain_effectiveness`: whether the candidate actually answers the current effect/source under public metadata;
- `lethal_risk`: immediate survival impact;
- `hold_cost`: probability-free deterministic downside of waiting based on current public board/history.

The first Teacher should not maintain a probabilistic hidden-hand belief state. A profile may use deterministic public archetype indicators and exact pre-declared matchup knowledge only when that knowledge is explicitly fair policy configuration for both participants.

### 15.2 Public opponent modeling boundary

Allowed:

- observed cards and archetype tags;
- public actions;
- public graveyard/banished/field;
- public reveals;
- perspective-safe event history;
- exact fixed-matchup identity if explicitly configured as public experiment context.

Forbidden:

- exact hidden hand reconstruction;
- face-down identity from engine state;
- hidden deck order;
- secret-derived hashes;
- persistent hidden locators;
- querying opponent-private observation;
- treating legal candidate absence/presence as a secret oracle beyond what the acting player is entitled to know under the existing public contract.

## 16. Battle evaluation

Teacher v1 needs a strong baseline, not exhaustive combat search.

### 16.1 Minimum generic capabilities

- recognize deterministic lethal from the current public board and legal battle candidates;
- avoid attacking a stronger known monster without a justified trigger/goal;
- order attacks to remove known interaction/protection before direct damage;
- account for piercing and repeated-attack effects where candidate metadata/public state supports it;
- preserve an attacker when its post-battle engine value exceeds marginal damage;
- use battle to trigger known own effects such as Shthana or Of Fire only when the public cost/benefit is favorable;
- preserve Main Phase 2 follow-up;
- avoid exposing all pressure to a known public battle interaction where another ordering is safer.

### 16.2 No hidden trap inference

An unknown set card can increase generic exposure/risk, but the Teacher may not act as though it knows the exact trap.

## 17. Strategic goals and endboards

The Teacher should reason through explicit goals, not monster count.

Generic goal families:

- `ESTABLISH_INTERACTION`;
- `ESTABLISH_RECURSION`;
- `PRESERVE_FOLLOW_UP`;
- `REMOVE_ENGINE_ENABLER`;
- `FORCE_INTERACTION`;
- `STABILIZE_BOARD`;
- `RECOVER_ENGINE`;
- `REACH_LETHAL`;
- `DENY_GRAVEYARD_LOOP`;
- `CONVERT_SPENT_RESOURCES`;
- `STOP_WITH_SAFE_BOARD`.

Deck-specific profiles instantiate these goals with exact line predicates.

A preferred Swordsoul endboard is not “maximum Synchros.” In the locked list it may be Chixiao plus Qixing or Chengying with Blackout/hand interaction and retained follow-up, while a lower-resource Chixiao + Blackout or single Level 10 + interaction may be the correct stop.

A preferred Salamangreat endboard is not “maximum Links.” It may be a reincarnated Raging Phoenix or Sunlight Wolf with Roar/Rage, Princess/Balelynx grave interaction, Gazelle/Jaguar follow-up, and preserved Extra Deck copies. A smaller Wolf + trap board can dominate a greedier line that exhausts recursion.

## 18. Fallback hierarchy

The Teacher must always return one legal key for a supported actionable nonempty domain.

Recommended hierarchy:

```text
F0 known active line edge
↓
F1 explicit recovery edge / replan
↓
F2 deck-specific strategic utility
↓
F3 generic tactical-safe utility
↓
F4 bytewise canonical public_action_key equality tie-break
```

Properties:

- all levels evaluate the complete domain;
- no level uses undocumented `candidate[0]`;
- fallback level is emitted in derived provenance;
- F4 is a deterministic total-order completion mechanism, not game strategy;
- fallback frequency is a core quality metric;
- F3/F4 decisions are not high-quality imitation labels by default.

## 19. Confidence and future label eligibility

Recommended classes:

| Class | Meaning | Default future BC eligibility |
|---|---|---|
| `HIGH_CONFIDENCE` | Named line/interaction/scenario with satisfied public preconditions, unique strategic winner, and no fallback | Eligible after trajectory admission and scenario-gate pass |
| `MEDIUM_CONFIDENCE` | Deck-specific recovery/utility decision with evidence but multiple plausible branches or small margin | Eligible only after validation or with reduced weight |
| `LOW_CONFIDENCE` | Generic utility dominates; profile lacks direct support | Exclude by default |
| `FALLBACK` | Canonical safety/tie completion or unknown decision pattern | Exclude by default |

Confidence is policy provenance, not environment truth and not a reward.

A future dataset may preserve all decisions for audit while deriving a training view that filters or weights them. Canonical trajectories must not be rewritten to hide weak labels.

## 20. Decision explanation and provenance

Recommended derived record:

```text
TeacherDecisionExplanation
- teacher_policy_artifact_id
- teacher_core_component_version
- strategy_profile_artifact_id
- active_goal_id | NONE
- active_line_id | NONE
- active_line_node_id | NONE
- matched_rule_ids[]
- selected_public_action_key
- selected_score_vector
- runner_up_score_vector | NONE
- utility_margin
- confidence_class
- fallback_level
- plan_invalidation_reason_ids[]
- explanation_schema_id
```

Optional full candidate score vectors may be retained as a derived debug artifact, but they can be large. The canonical trusted trajectory need only bind the exact policy artifact/assignment; explanation storage should remain separable.

Privacy requirements:

- no opponent hidden identity;
- no internal semantic keys;
- no raw engine messages/responses;
- no private locators or secret hashes;
- no facts absent from the acting policy input and immutable public policy configuration;
- paired public-equivalent worlds must produce identical decisions and explanations.

Identity requirements:

- changing core logic, profile graph, role table, preferences, tie-break, or fallback behavior creates a new immutable policy artifact;
- explanation bytes do not change public gameplay trajectory identity;
- policy provenance may change collection-record identity according to the accepted Phase-3 contract.

## 21. Versioning recommendation

Do not ratify names prematurely, but the artifact must identify at least:

- Teacher core implementation/config version;
- exact StrategyProfile content identity;
- exact locked deck identity/hash;
- supported matchup identity;
- score-layout and integer-preference identity;
- line/recovery graph identity;
- target/material/interaction/battle policy identity;
- fallback policy identity;
- final tie-break identity;
- explanation schema identity;
- policy RNG contract (`NONE` for deterministic v1).

A profile file edited in place without a new artifact identity is unacceptable for trusted trajectory generation.

## 22. Evaluation design

### 22.1 Opponents and schedules

Future evaluation should include:

- `RandomLegal` under a fixed policy-RNG schedule;
- Swordsoul Teacher vs Salamangreat Teacher;
- both seat assignments;
- both starting-player partitions;
- frozen deterministic environment seed schedule;
- profile self/mirror tests only where an exact certified mirror environment exists;
- previous frozen Teacher versions as regression opponents.

### 22.2 Required metrics

- win / draw / loss;
- terminal, interrupted, and failed episode counts;
- average and quantile episode length;
- decisions by family;
- candidate-domain-size distribution;
- fallback frequency by level;
- confidence distribution;
- known-line and recovery-edge coverage;
- target/material/battle scenario pass rates;
- illegal output count = 0;
- stale/nonmember output count = 0;
- privacy violation count = 0;
- candidate truncation/filter count = 0;
- same-process determinism failures = 0;
- cross-process determinism failures = 0;
- state leakage/reset failures = 0.

Beating RandomLegal is a smoke test, not a strength claim.

### 22.3 Frozen baseline identity

A baseline report should bind:

- OCGForge commit and build provenance;
- rules bundle and deck hashes;
- environment contract IDs;
- Teacher policy artifacts;
- evaluator version;
- seed schedule identity;
- seat/start partition schedule;
- scenario corpus identity;
- semantic gameplay results separately from build/provenance hashes.

## 23. Teacher-generated trajectory quality

Useful Teacher data should cover:

- more than one opening line family;
- going first and second;
- weak/brick-like hands;
- starter and extender interruption;
- target and material continuations;
- chain timing;
- battle and Main Phase 2;
- board breaking;
- recovery and grind;
- explicit confidence/fallback provenance.

A giant exact script would create a narrow imitation ceiling. The hybrid mitigates this by selecting line families from current resource state and re-ranking every legal candidate, producing legitimate variation without policy randomness.

A later stochastic Teacher variant could add controlled diversity only with explicit policy RNG provenance and a new artifact. It is not required for v1.

## 24. Privacy and determinism findings

### BLOCKER

**B-01 — Any CoreHost/hidden/private input dependency.**  
A Teacher requiring raw engine state, opponent-private observations, hidden identity, internal semantic keys, private locators, or secret-derived hashes is architecturally invalid.

**B-02 — Candidate reconstruction, filtering, truncation, or default selection.**  
The complete supplied domain is authoritative. A profile may score candidates, never rebuild or reduce legality. `candidate[0]` fallback is forbidden.

**B-03 — Independent legality or an exact script that assumes unavailable future actions.**  
A profile cannot prove future legal actions without engine authority. Exact queued scripts and unproven hypothetical state models fail closed.

**B-04 — Persistent hidden physical-card identity or secret opponent model.**  
Strategy memory must respect knowledge-destroying transitions and may not track hidden cards through pointers/locators/inference or reconstruct exact hidden hands.

**B-05 — Nondeterministic authoritative selection.**  
Wall time, process RNG, unordered iteration, random shuffles, floating drift, and scheduling-dependent ties are unacceptable.

**B-06 — Explanation/provenance leakage or identity contamination.**  
Derived explanation must not expose restricted data or alter gameplay identity.

### MAJOR

**M-01 — WindBot-style first-match executor ownership.**  
Useful as a pattern source, but priority coupling and early return are too brittle for OCGForge's complete-domain policy.

**M-02 — Reference executor/deck drift.**  
Current WindBot Swordsoul and Salamangreat executors do not match the exact locked lists; blindly importing their lines would reference absent cards and obsolete plans.

**M-03 — Opaque mutable flags with unclear lifecycle.**  
Turn/chain/episode state must have explicit ownership, reset, acceptance, and reconciliation semantics.

**M-04 — Missing recovery as a first-class concept.**  
A line representation without invalidation and recovery edges will degrade into scripts plus ad hoc exceptions.

**M-05 — One arbitrary board scalar.**  
It obscures follow-up, interaction, restriction, and copy-budget tradeoffs. Use ordered dimensions/resource vectors.

**M-06 — Missing immutable policy/profile provenance.**  
Trusted trajectories cannot attribute labels if strategy rules, graphs, weights, tie-break, and fallback change under one ID.

**M-07 — Treating fallback labels as equally trusted.**  
Low-confidence/fallback decisions should remain auditable but be excluded or down-weighted in future derived training views.

**M-08 — Weak evaluation design.**  
One seed or only RandomLegal cannot establish competence, determinism, recovery, or matchup quality.

### MINOR

**N-01 — Equality tie-break dependence.** Track tie frequency and expand heuristics if canonical ties are common.  
**N-02 — Integer overflow.** Use bounded checked arithmetic and frozen score ranges.  
**N-03 — Source/list staleness.** Every human/WindBot claim must declare absent cards and format mismatch.  
**N-04 — Generic/profile duplication.** Keep threat/material/timing concepts centralized.  
**N-05 — Explanation volume.** Full candidate scores are useful but should remain optional derived diagnostics.

### NOTE

Future search-assisted teaching may become valuable after a checkpoint/fork ADR, deterministic restore evidence, and perspective-safety proof. Probabilistic belief modeling and broader decks should remain later work.

## 25. Future acceptance gates

### T-G01 — Public boundary only

**PASS:** static dependency review and runtime instrumentation show the Teacher receives only the actionable public frame, immutable policy/profile artifacts, and accepted policy-local state. No CoreHost, raw message/query, opponent-private, internal-key, private-locator, or restricted-diagnostic access exists.

### T-G02 — Complete candidate-domain consumption

**PASS:** for every frozen test frame, the number and ordered key digest of input candidates equal the number and ordered key digest of candidate evaluation records; every candidate is evaluated exactly once.

### T-G03 — Exactly one current public key selected

**PASS:** every supported actionable nonempty frame returns exactly one `public_action_key` that occurs exactly once in that current domain. Empty/terminal frames do not invoke the Teacher. Stale and nonmember keys are rejected by the environment.

### T-G04 — Zero hidden-information dependency

**PASS:** paired worlds with byte-identical acting public frames and policy state but different opponent-hidden state produce byte-identical selected keys and explanations.

### T-G05 — Deterministic repeatability

**PASS:** repeated evaluation of the same frozen corpus in one process produces identical selected keys, score vectors, fallback levels, and explanation bytes.

### T-G06 — Cross-process determinism

**PASS:** independent processes/build runs on the certified platform, with varied PID/scheduling/worker count, produce identical semantic Teacher outputs for the same corpus. Cross-platform claims require separately demonstrated exact equivalence.

### T-G07 — Strategy-state reset and isolation

**PASS:** episode reset clears all non-artifact state; interleaved episodes and participants do not affect one another; rejected actions do not advance state; turn/phase/reset fixtures prove effect-use and restriction expiry.

### T-G08 — Exact Teacher artifact provenance

**PASS:** every accepted decision resolves through participant assignment to one immutable Teacher artifact identifying core, profile, deck, graph, preferences, fallback, and tie-break. Artifact bytes/content digest are immutable.

### T-G09 — Swordsoul line coverage

**PASS:** a frozen, engine-reachable scenario corpus covers the named Swordsoul ideal, weak, brick-like, going-first, going-second, Tenyi, Longyuan side, Taia recovery, board-break, grind, and lethal families; the Teacher selects an accepted expected goal/line family or explicitly justified recovery/fallback.

### T-G10 — Salamangreat line coverage

**PASS:** a frozen, engine-reachable corpus covers Of Fire/Gazelle, Spinny/Stallio, Wolf recursion, Raging reincarnation, Princess/Weasel, Code/Pyro, weak line, going-second, grind, and lethal families with the same expected-goal/recovery rule.

### T-G11 — Interruption recovery coverage

**PASS:** fixtures where starter, extender, material, target, or payoff becomes unavailable invalidate stale plan state before the next choice and select a legal recovery/stop action. No missing expected action is replaced by a guessed candidate.

### T-G12 — Target-selection coverage

**PASS:** multi-threat public scenarios for both profiles select the expected threat class and emit target rationale; paired hidden variants remain identical.

### T-G13 — Material-selection coverage

**PASS:** legal continuation corpora demonstrate preservation of unique starter/extender/interaction/follow-up and consumption of spent/recursive/duplicate resources unless a higher-priority payoff explicitly justifies otherwise.

### T-G14 — Battle baseline

**PASS:** frozen scenarios cover deterministic lethal, safe attack ordering, removing a threat by battle, avoiding obvious losing attacks, preserving post-battle resources, and Main Phase 2 follow-up.

### T-G15 — Going-first / going-second coverage

**PASS:** both profiles are evaluated on balanced starting-player/seat partitions and demonstrate distinct opening, board-breaking, stabilization, and follow-up goals.

### T-G16 — Fallback determinism

**PASS:** every supported nonempty corpus frame produces one legal key with an explicit fallback level; no code path uses first-item selection; F3/F4 decisions are labeled low/fallback.

### T-G17 — No candidate truncation/filtering

**PASS:** before/after candidate count, membership, order, and public-domain digest are identical; profile/line code exposes no filtering API.

### T-G18 — Explainability/provenance safety

**PASS:** explanation contains only approved policy-artifact, public-frame-derived, public-key, score, goal/line/rule, confidence, and fallback fields; paired public-equivalent worlds yield identical explanation; no explanation field participates in gameplay identity.

### T-G19 — Trusted trajectory compatibility

**PASS:** admitted trajectories bind exact policy assignment/artifact and complete public frames; optional Teacher explanation is derived data; rejected, failed, interrupted, quarantined, corrupted, or replay-divergent records are not silently admitted as trusted labels.

### T-G20 — Frozen evaluation baseline

**PASS:** a versioned evaluation bundle fixes rules/decks, Teacher artifacts, seed/seat/start schedule, scenario corpus, evaluator, and metrics. Illegal/privacy/truncation/determinism failures are all zero. Strength claims report confidence intervals/partitions and do not rely solely on RandomLegal.

## 26. Relationship to Phase 3

Teacher implementation should wait until the required trusted policy/trajectory provenance owner is accepted. This research does not block Phase 3A unless implementation discovers that current public frames cannot express a required **public** candidate feature.

Any such deficiency must be reported as a separate contract question, not patched through internal Teacher access.

Recommended Phase-3 compatibility:

- deterministic Teacher is a policy kind/artifact;
- no policy RNG for v1;
- participant assignment attributes every accepted decision;
- canonical trajectory stores exact public frame/domain/key;
- Teacher explanation is optional derived sidecar/provenance;
- changing explanation alone does not change gameplay identity;
- changing decision behavior creates a new policy artifact.

## 27. Explicit answers to the required decisions

### 1. Should OCGForge use a WindBot-like executor architecture?

**No as the top-level policy.** Reuse small concepts—ordered threat tiers, resource memory, target/material preferences—but evaluate the complete domain and select through one deterministic resolver.

### 2. Should combos be scripts, graphs, goals, utilities, or a hybrid?

**Hybrid.** Deck knowledge should be partial-order line/recovery graphs over public-state goals. Current candidates are ranked by deterministic utility. Exact queued scripts are rejected.

### 3. What belongs in generic `TeacherCore`?

Public interpretation, state reconciliation, complete-domain feature extraction, goal/line control mechanics, target/material/timing/battle frameworks, generic resources, fallback, deterministic resolution, explanation, and provenance binding.

### 4. What belongs in deck-specific `StrategyProfile`?

Exact deck identity, card roles, deck resources/restrictions, goals/endboards, line/recovery graphs, search/discard/material/target modifiers, interaction map, Extra Deck copy budget, and versioned integer preferences.

### 5. How should Swordsoul strategy be represented?

As line families around Mo Ye/Chixiao, Longyuan Level-10 access, Ecclesia access, Tenyi/Monk setup, Taia/Summit recovery, Baxia/Yazi board breaking, interaction preservation, and grind/lethal goals. Each line carries resources, restrictions, vulnerabilities, recovery, and stop conditions.

### 6. How should Salamangreat strategy be represented?

As line families around Of Fire/Gazelle access, Spinny/Miragestallio, Balelynx/Sanctuary, Wolf/Jaguar/Falco recursion, Princess/Weasel/Raging conversion, Code/Pyro opponent-turn pressure, Roar/Rage setup, copy-budget preservation, grind, and lethal.

### 7. How should interruption recovery work?

Reconcile every new public frame, invalidate unsatisfied line preconditions, score explicit recovery edges, then deck utility, generic utility, and canonical fallback. Never assume the next scripted action exists.

### 8. How should targets and materials be selected?

Rank every legal continuation candidate using generic threat/resource dimensions plus profile modifiers. Preserve unique starters/extenders/interaction/follow-up and prefer spent/recursive/duplicate resources where payoff is equivalent.

### 9. How should interaction timing work?

Use public threat criticality, opponent investment, visible replacement routes, own interaction scarcity, future choke value, chain effectiveness, and lethal risk. No hidden-hand oracle or probabilistic belief state in v1.

### 10. How should fallback decisions work?

Known line → recovery → deck-specific utility → generic tactical-safe utility → bytewise public-key equality tie-break. Every fallback is explicit and provenance-labeled.

### 11. Should confidence/reason metadata be persisted?

**Yes as derived policy provenance**, bound to the exact Teacher artifact and excluded from gameplay identity. It must be privacy-safe and optional for canonical trajectory storage.

### 12. Which decisions should later be eligible for Behavior Cloning?

Admitted `HIGH_CONFIDENCE` decisions by default; validated `MEDIUM_CONFIDENCE` optionally/down-weighted. `LOW_CONFIDENCE` and `FALLBACK` excluded by default. No rejected/failed/quarantined/replay-divergent data.

### 13. What acceptance gates are required?

T-G01 through T-G20 above, with zero tolerance for illegal output, hidden dependency, candidate loss, and determinism failure.

### 14. What should be borrowed from WindBot?

Concepts: deck-specific executors as strategy modules, explicit resource/use memory, threat tiers, target/material preference lists, line prerequisites, restrictions, turn resets, fallback-line awareness, and detailed interaction heuristics.

### 15. What must be rejected from WindBot?

Mutable omniscient client state, `ClientCard` identity, first-item/card/option/position fallbacks, callback selection queues, executor order as final authority, raw card-ID sprawl, opaque flags, process/global randomness, candidate index authority, exact stale combo scripts, and non-versioned behavior.

## 28. Unresolved implementation research questions

1. Does the accepted public observation/history expose enough information to reconstruct all required temporary restrictions and once-per-turn facts without policy-only assumptions?
2. Which public candidate metadata fields are available for every target/material/option/continuation family, and are they sufficient for generic feature extraction?
3. Is exact opponent locked-deck identity explicitly part of fair policy configuration for both participants, or should v1 use only observed archetype indicators?
4. What exact derived-envelope owner will carry Teacher explanation after Phase 3A/3B is ratified?
5. How will engine-reachable interruption/scenario fixtures be generated without introducing a general hidden board-construction runtime API?
6. Which score ranges and confidence thresholds survive empirical evaluation without excessive equality ties?
7. Should full candidate utility vectors be persisted, sampled, or regenerated from artifact + frame for audit?
8. What minimum win-rate/performance threshold defines “competent” beyond the semantic gates? This should be frozen only after a baseline run, not guessed in research.

These questions do not justify using private state or starting ML early.
