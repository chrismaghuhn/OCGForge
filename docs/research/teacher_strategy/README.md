# Deterministic Teacher Architecture and Strategy Mining

**Research date:** 2026-08-28  
**OCGForge source checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Roadmap owner:** [Issue #16](https://github.com/chrismaghuhn/OCGForge/issues/16)  
**Scope:** research, domain modeling, architecture comparison, strategy mining, and future acceptance design only

> [!IMPORTANT]
> These documents are non-normative research. They do not implement or ratify a production Teacher, change gameplay semantics, alter the locked decks, start ML, or replace any accepted OCGForge contract. Exact proposed names, score layouts, policy IDs, line IDs, and metadata schemas require a later implementation/contract review.

## Research question

What architecture should OCGForge use for its first deterministic heuristic Teacher so that it can play the exact certified Swordsoul Tenyi vs Salamangreat matchup competently, recover from interruptions and non-ideal hands, generate useful trusted trajectories, and remain maintainable without becoming a brittle collection of exact action scripts?

The required policy boundary remains:

```text
PublicEnvironmentObservation
+
complete ordered EnvironmentActionCandidate[]
        ↓
Deterministic Teacher
        ↓
exactly one current public_action_key
```

The Teacher must not consume `CoreHost`, raw ocgcore state, opponent-private observations, internal semantic keys, private locators, secret-derived hashes, or independently reconstructed legality. The complete candidate domain remains authoritative and must never be filtered or truncated.

## Executive conclusion

The research does **not** recommend a WindBot-style ordered executor architecture as OCGForge's top-level policy.

The recommended conceptual design is a deterministic hybrid:

```text
DeterministicTeacherPolicy
├── PolicyBoundaryAdapter
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
├── StrategyProfile
│   ├── CardRoleCatalog
│   ├── ResourceModel
│   ├── GoalCatalog
│   ├── PartialOrderLineGraph
│   ├── RecoveryEdges
│   ├── InteractionMap
│   └── VersionedIntegerPreferences
├── EpisodeLocalStrategyState
└── DecisionExplanationEmitter
```

The decisive properties are:

1. every candidate in the complete public domain is evaluated exactly once;
2. engine legality is never duplicated;
3. combo knowledge is represented as public-state goals, partial-order line graphs, resource requirements, and recovery edges—not queued exact engine actions;
4. each actionable frame is evaluated afresh, so interruption invalidates a plan rather than corrupting an action script;
5. exact integer/lexicographic scoring and a public-key equality tie-break produce deterministic selection;
6. bounded episode-local strategy state is reconciled against each current observation and is updated only after accepted transitions;
7. deck-specific knowledge is isolated in immutable `StrategyProfile` artifacts while target, material, interaction, battle, fallback, and audit logic remain generic where possible;
8. decision reason/confidence/fallback metadata is derived policy provenance and must not become gameplay identity or leak information;
9. low-confidence and canonical-fallback decisions should be preserved for audit but excluded from future Behavior Cloning by default;
10. future search-assisted Teachers remain out of scope until checkpoint/fork semantics are trustworthy and perspective-safe.

## Locked matchup inspected

| Item | Exact identity |
|---|---|
| Matchup | `ocgforge.matchup.swordsoul_salamangreat.v1` |
| Rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |
| Swordsoul Tenyi deck | 40 Main / 15 Extra |
| Swordsoul deck SHA-256 | `8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7` |
| Salamangreat deck | 40 Main / 15 Extra |
| Salamangreat deck SHA-256 | `6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188` |

No generic online deck list is treated as a substitute for these exact files.

## Documents

1. [Deterministic Teacher architecture](OCGForge_Deterministic_Teacher_Architecture_2026-08-28.md)
   - architecture alternatives and recommendation;
   - owning layers and invariants;
   - generic `TeacherCore` vs deck-specific `StrategyProfile`;
   - target, material, interaction, battle, resource, fallback, determinism, provenance, evaluation, and acceptance gates;
   - explicit answers to the fifteen required design decisions.

2. [WindBot strategy mining](WindBot_Strategy_Mining_2026-08-28.md)
   - current repository pins;
   - `Executor`, `GameAI`, `DefaultExecutor`, `AIUtil`, Swordsoul, Salamangreat, Albaz, and Labrynth findings;
   - reusable concepts classified as `REUSE_CONCEPT`, `ADAPT`, `REFERENCE_ONLY`, or `REJECT`;
   - explicit anti-pattern register.

3. [Swordsoul Tenyi Teacher strategy](Swordsoul_Tenyi_Teacher_Strategy_2026-08-28.md)
   - exact locked card list and per-card roles;
   - strategic goals, resources, search/discard/material priorities, interaction timing, battle, endboards, line families, interruption recovery, and scenario families.

4. [Salamangreat Teacher strategy](Salamangreat_Teacher_Strategy_2026-08-28.md)
   - exact locked card list and per-card roles;
   - Gazelle/Spinny/Weasel/Jaguar/Falco loops;
   - Sanctuary/reincarnation mechanics;
   - Wolf, Miragestallio, Princess, Raging Phoenix, Pyro Phoenix, Roar/Rage, recovery, interaction, and scenario families.

5. [Combo planning and recovery](Teacher_Combo_Planning_and_Recovery_2026-08-28.md)
   - precise domain terminology;
   - exact-script, rule, state-machine, behavior-tree, GOAP, graph, and hybrid comparison;
   - partial-order line-graph schema;
   - plan invalidation and recovery procedure;
   - required concrete scenario stress tests for both decks;
   - fallback and label-quality policy.

6. [Research evidence index](Teacher_Research_Evidence_Index_2026-08-28.md)
   - OCGForge live sources and in-flight Phase-3A context;
   - current external repository pins and OCGForge rules-authority pins;
   - exact CardScripts inspected;
   - official tournament coverage, competitive primers, claim use, confidence, and known limitations.

## What this research adds beyond prior OCGForge work

The 2026-08-25 EDOPro/WindBot capability-mining report established that WindBot is useful as a source of handcrafted game knowledge but unsuitable as OCGForge's policy boundary. This research adds:

- an exact first-Teacher architecture;
- a complete-domain deterministic resolver model;
- a generic/deck-specific ownership split;
- a deck-neutral resource taxonomy;
- partial-order line and recovery representation;
- exact locked-deck role catalogs;
- exact Swordsoul and Salamangreat line families;
- interruption, target, material, timing, battle, and fallback models;
- confidence and future label-eligibility semantics;
- a twenty-gate future acceptance matrix;
- explicit compatibility requirements for trusted trajectory/policy provenance.

## Authority boundaries

Use this order when interpreting the documents:

1. OCGForge's pinned rules bundle owns rules truth for the certified matchup.
2. Accepted OCGForge contracts own the public observation/action/environment semantics.
3. Executable acceptance evidence owns certification claims.
4. Current external repository heads are read-only research references.
5. Human primers and tournament reports provide strategy evidence, not legality.
6. Proposed architecture and inferred heuristics remain non-normative until implemented and accepted.

Current external heads must never silently replace OCGForge's pinned ocgcore, CardScripts, BabelCDB, patchset, or locked decks.

## Source-label convention

The reports distinguish:

- **SOURCE FACT** — directly supported by an inspected repository, contract, script, or official report;
- **PLAYER CONSENSUS / HEURISTIC** — competitive guidance that may be format-, list-, or matchup-dependent;
- **INFERENCE** — a reasoned consequence of the exact deck and inspected mechanics;
- **PROPOSED OCGFORGE DESIGN** — a future architecture recommendation.

## Scope exclusions

No production code was designed in implementation detail or changed. In particular, this research does not start or implement:

- Behavior Cloning;
- PPO, IMPALA, R2D2, APPO, or another RL algorithm;
- self-play or league training;
- neural networks or model adapters;
- MCTS, CFR, GOAP execution, or arbitrary state search;
- distributed actors;
- checkpoint/fork persistence;
- new legality logic;
- new public observation or candidate semantics.

## Relationship to Phase 3

This work should not block Phase 3A or 3B. The only Phase-3-facing requirement is that future trusted trajectory records can attribute decisions to an exact immutable deterministic-heuristic `PolicyArtifact` and can carry optional derived Teacher decision provenance without changing public gameplay identity.

The in-flight Phase-3A PR was inspected only to avoid designing a competing provenance owner. This research does not amend or ratify that PR.
