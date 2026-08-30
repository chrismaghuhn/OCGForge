# OCGForge — Deterministic Teacher Research Evidence Index

**Research date:** 2026-08-28  
**Scope:** research / analysis / architecture recommendation only  
**Status:** non-normative research artifact; not an accepted gameplay or ML contract

## 1. Purpose

This file indexes the evidence used by the deterministic Teacher architecture and
locked-deck strategy research. It separates:

- OCGForge repository authority;
- locked matchup / rules identities;
- current external reference repositories inspected during the research;
- strategy / human-play references;
- research-only inference and recommendations.

The evidence indexed here does **not** authorize changes to gameplay semantics,
legal candidate domains, privacy contracts, replay identity, pinned rules inputs,
or ML/training behavior.

## 2. OCGForge research checkpoint

The live repository checkpoint inspected for the Teacher research was:

`ea5b3ddf414987b451c44becf30619f1a0814189`

This was the post-Episodic-Environment-V2 main checkpoint used as the repository
basis for the research. Repository state after that checkpoint is not implied by
this document.

Related roadmap issue inspected during the research:

`#16 — Roadmap: trusted trajectories, remote training, and self-play`

The roadmap sequence observed there places trusted trajectory work before a
future deterministic Teacher / evaluation phase.

## 3. OCGForge authority hierarchy used

The research treated the following classes of evidence in descending authority
for OCGForge semantics:

1. pinned rules inputs and versioned repository patches;
2. accepted/versioned OCGForge public contracts;
3. machine-readable acceptance evidence and executable tests;
4. accepted architecture decisions;
5. repository documentation and roadmap;
6. research reports and external reference implementations.

External implementations were never treated as OCGForge rules authority.

## 4. Locked matchup identities

Matchup:

`ocgforge.matchup.swordsoul_salamangreat.v1`

Format:

`TCG_ADVANCED_2026_05_18`

Duel mode:

`DUEL_MODE_MR5`

### Deck A

`ocgforge.swordsoul_tenyi.ml_v1`

Main Deck: 40  
Extra Deck: 15

SHA-256:

`8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7`

### Deck B

`ocgforge.salamangreat.ml_v1`

Main Deck: 40  
Extra Deck: 15

SHA-256:

`6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188`

The Teacher strategy research is intentionally tailored to these exact locked
lists. Generic online combo advice that depends on cards absent from the lists
must not be silently imported.

## 5. Pinned OCGForge rules inputs

The research preserved the historical certified rules identities:

ocgcore:

`9a0c558c2d686542f7914a6d529fd7aa57746aed`

CardScripts:

`f337c87018ca723c1aded5143e616bb649555273`

BabelCDB:

`89ad6837b0766a52984d8c715a7d5d4f8447946b`

Repository patchset:

`ocgforge.ocgcore.api_hardening.v1`

Canonical historical bundle ID referenced by the stable project pack:

`3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`

No external moving branch inspected for research replaces these pins.

## 6. OCGForge public contracts inspected

The Teacher design was constrained by the public environment contracts and the
project invariants. In particular:

- policy-facing state must be perspective-safe;
- legal candidate domains are complete for supported requests;
- the Teacher must not reconstruct or truncate legality;
- the Teacher selects exactly one current `public_action_key`;
- hidden identities and internal semantic keys are not policy inputs;
- candidate order / identity must not depend on pointer identity, wall time,
  PID, scheduling, or unordered iteration;
- unsupported or unsafe behavior fails closed.

Key repository documents inspected included the Episodic Environment V2,
public action identity, public environment observation, fixed matchup,
decision protocol, privacy, determinism/replay, architecture, roadmap,
and project invariant documents.

## 7. Phase-3A trajectory and provenance context

The Teacher research was conducted while the trusted-trajectory architecture was
being specified separately. The Teacher documents therefore treat exact policy
artifact provenance and per-decision explanatory metadata as future requirements
for trusted Teacher-generated data, not as already-implemented runtime behavior.

Research conclusions relevant to that boundary:

- Teacher identity must be immutable/versioned;
- deck profile identity must be immutable/versioned;
- policy-local deterministic state must reset between episodes;
- explanation/confidence metadata is policy provenance, not gameplay identity;
- fallback/low-confidence labels should remain distinguishable;
- Teacher-generated trajectories are not automatically trusted merely because a
  legal action was selected.

## 8. External repository pins inspected

The following repositories were independently fetched during the research.
These are research-reference heads only.

### ProjectIgnis / windbot

Repository:

`https://github.com/ProjectIgnis/windbot`

Default branch inspected:

`master`

Commit:

`b33bf21aa164e8a3dd42707934d92118bdd1a253`

Inspection date:

`2026-08-28`

Use:

- historical/default executor architecture;
- card selection and target/material helper patterns;
- deck-specific executor examples;
- anti-pattern comparison.

### ProjectIgnis / WindBot-Ignite

Repository:

`https://github.com/ProjectIgnis/WindBot-Ignite`

Default branch inspected:

`master`

Commit:

`b7e144a1948826ee1005437121153443a5851fc5`

Inspection date:

`2026-08-28`

Use:

- current executor organization;
- mature deck executor examples;
- current card-selection heuristics;
- Salamangreat support/reference behavior where available.

### EDOPro

Repository:

`https://github.com/edo9300/edopro`

Default branch inspected:

`master`

Commit:

`3ca1a76140c5a27d806b01262ea3a2e041e0f0e7`

Inspection date:

`2026-08-28`

Use:

- client/bot integration context;
- evidence for why client-side state models are not OCGForge policy authority.

### ygopro-core

Repository:

`https://github.com/edo9300/ygopro-core`

Default branch inspected:

`master`

Commit:

`9a0c558c2d686542f7914a6d529fd7aa57746aed`

Inspection date:

`2026-08-28`

Use:

- engine/reference context only;
- does not authorize Teacher access below EpisodicEnvironment V2.

### ProjectIgnis / CardScripts

Repository:

`https://github.com/ProjectIgnis/CardScripts`

Default branch inspected:

`master`

Commit:

`f337c87018ca723c1aded5143e616bb649555273`

Inspection date:

`2026-08-28`

Use:

- exact card behavior/rules reference;
- locked-deck card role/line analysis.

### ProjectIgnis / BabelCDB

Repository:

`https://github.com/ProjectIgnis/BabelCDB`

Default branch inspected:

`master`

Commit:

`89ad6837b0766a52984d8c715a7d5d4f8447946b`

Inspection date:

`2026-08-28`

Use:

- card metadata / current database context;
- not a replacement for OCGForge's pinned bundle authority.

## 9. WindBot files / concepts sampled

The WindBot analysis focused on recurring architecture rather than copying one
executor.

Patterns examined included:

- ordered `AddExecutor(...)` registration;
- `DefaultExecutor` generic tactical helpers;
- deck-specific activation/summon rules;
- target selection;
- material selection;
- chain response logic;
- battle-phase decisions;
- mutable deck-specific state / flags;
- per-card ID dispatch;
- resource-preservation heuristics.

Representative mature executor families were sampled where useful to separate
patterns that generalize from deck-specific exact scripts.

The research classified WindBot ideas into:

- `REUSE_CONCEPT`;
- `ADAPT`;
- `REFERENCE_ONLY`;
- `REJECT`.

The detailed classification lives in `OCGForge_WindBot_Strategy_Mining.md`.

## 10. Human / strategy evidence classes

Human strategy sources were used as heuristic/strategic evidence rather than as
rules authority.

Evidence classes included:

- archetype combo/strategy primers;
- tournament/deck explanations;
- public card/rule references;
- established combo guides;
- community consensus on choke points and resource preservation.

Important discipline used throughout:

- source facts were separated from player heuristics;
- player heuristics were separated from OCGForge design inference;
- generic deck advice was rejected when it depended on cards absent from the
  locked OCGForge deck list;
- exact card behavior was cross-checked against the locked list/rules context
  instead of relying only on informal combo descriptions.

## 11. Locked Swordsoul Tenyi evidence model

The Swordsoul document maps the locked deck around line families rather than one
fragile script.

Evidence-backed strategic concepts examined include:

- Mo Ye as a core Swordsoul starter;
- Taia as a graveyard-dependent starter/recovery piece;
- Longyuan as Level-10 Synchro conversion;
- Swordsoul Emergence as access/conversion;
- Incredible Ecclesia as conditional starter/access;
- Tenyi names as extension / board-breaking / recovery resources;
- Chixiao as central Level-8 payoff/search/interaction;
- Level-10 Synchro payoff selection;
- Blackout setup and resource cost;
- Wyrm-resource preservation;
- going-second conversion;
- interruption-aware recovery.

The research does not claim that every discovered theoretical archetype combo is
available in the locked list.

## 12. Locked Salamangreat evidence model

The Salamangreat document maps the locked deck around resource-loop and
reincarnation-Link goals.

Evidence-backed strategic concepts examined include:

- Gazelle access;
- Spinny conversion;
- Balelynx into Sanctuary access;
- Salamangreat Sanctuary / reincarnation Link mechanics;
- Sunlight Wolf recursion;
- Miragestallio sequencing;
- Roar/Rage interaction setup;
- graveyard-loop preservation;
- extra-deck material choices;
- rebuilding after interruption;
- going-second removal/conversion lines.

Again, generic online lines requiring cards outside the exact locked list are not
silently treated as valid Teacher lines.

## 13. Deterministic Teacher architecture conclusion

The research recommends a hybrid future architecture:

- generic `TeacherCore`
  - read-only candidate normalization/classification;
  - tactical evaluators;
  - plan tracking;
  - full-domain candidate ranking;
  - deterministic resolver;
  - explanation/provenance builder.
- immutable `StrategyProfile`
  - Swordsoul Tenyi;
  - Salamangreat.

Recommended strategy representation:

- goal graph;
- partial-order line families;
- recovery transitions;
- deterministic lexicographic/integer utility;
- bounded public-history state;
- exact canonical tie-break.

Rejected as authoritative Teacher architecture:

- raw WindBot executor copying;
- omniscient state;
- exact brittle scripts without recovery;
- independent legality calculation;
- candidate truncation/filtering;
- hidden-state inference as authoritative input;
- stochastic or wall-clock-dependent tie-breaking.

## 14. Determinism / privacy evidence requirements proposed

Future Teacher implementation should prove at minimum:

- public-boundary-only policy input;
- N/N candidate evaluation;
- exactly one current `public_action_key`;
- paired-world hidden-state invariance;
- same-process and cross-process repeatability;
- episode-state reset/isolation;
- immutable Teacher/Profile artifact identity;
- deterministic recovery from interruption;
- target/material safety;
- deterministic fallback;
- explanation metadata free of hidden/internal data;
- trusted-trajectory compatibility once the trajectory contracts are accepted.

These are research recommendations, not existing PASS claims.

## 15. Companion research documents

This evidence index belongs with:

1. `OCGForge_Teacher_Research_README.md`
2. `OCGForge_Teacher_Architecture.md`
3. `OCGForge_WindBot_Strategy_Mining.md`
4. `OCGForge_Swordsoul_Tenyi_Teacher_Strategy.md`
5. `OCGForge_Salamangreat_Teacher_Strategy.md`
6. `OCGForge_Teacher_Combo_Planning_and_Recovery.md`
7. `OCGForge_Teacher_Research_Evidence_Index.md`

Together these seven Markdown files form the complete downloadable Teacher
research pack.

## 16. Non-claims

This research pack does **not**:

- implement a Teacher;
- modify gameplay semantics;
- modify the locked decks;
- change rules inputs;
- ratify trajectory contracts;
- implement Behavior Cloning;
- implement PPO / IMPALA / R2D2 / APPO;
- implement self-play;
- implement search / MCTS / CFR;
- claim arbitrary-deck Yu-Gi-Oh! support;
- claim competitive playing strength.

All implementation and acceptance work remains future work.
