# OCGForge RL Benchmarking Research — Project Interpretation

**Status:** Reviewed research interpretation  
**Research report:** `OCGForge_RL_Benchmarking_Research_2026-08-28.md`  
**Repository checkpoint reviewed:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Decision:** GO as a future Phase-7 design basis; no implementation authorization.

## Review result

```text
BLOCKER  0
MAJOR    3
MINOR    2
```

The research supports the existing OCGForge direction. It does not justify starting RL early and it does not identify a winning algorithm.

The most important result is methodological:

> A fair OCGForge RL comparison must distinguish early learning, sample efficiency, data reuse, compute efficiency, wall-clock efficiency, scaling behavior, stability, and large-budget strength instead of comparing one final win rate.

## Accepted direction

The future benchmark should preserve this authority order:

```text
OCGForge trusted gameplay/evaluation evidence
        ↓
OCGForge benchmark/result bundles
        ↓
OCGForge statistical compiler
        ↓
optional external analysis/publication adapters
```

External tools may improve execution, analysis, visualization, or publication, but they do not own gameplay semantics or benchmark truth.

## MAJOR 1 — Do not make IQM the only or immediate primary score

The research correctly recommends rliable-style robust statistics, including IQM, probability of improvement, confidence intervals, and performance profiles.

For the initial OCGForge fixed-matchup benchmark, however, the project should not immediately replace interpretable duel outcomes with one cross-task aggregate.

Initial primary reporting should emphasize:

```text
mean frozen-evaluation score
95% confidence interval
raw per-training-seed results
win / draw / loss / forfeit / invalid-evidence rates
```

with these as complementary analyses:

```text
IQM
median / IQR
probability of improvement
performance profiles
```

As OCGForge expands to many certified matchups, deck roles, and evaluation strata, IQM becomes increasingly useful as a primary cross-task aggregate.

The independent statistical unit remains the training run/seed, not each individual evaluation duel.

## MAJOR 2 — Keep gameplay semantics separate from training-condition identity

Training topology can materially change learning:

```text
logical actor count
logical learner count
rollout length
replay policy
sync/async mode
policy publication cadence
```

These belong in a future `TrainingExperimentSpec` / `TrainingConditionId`.

They must never alter the meaning of gameplay-semantic identities.

Required conceptual separation:

```text
Gameplay semantic plane
-----------------------
environment_semantic_id
episode_semantic_id
public_semantic_decision_id
public_gameplay_trajectory_id

Training condition plane
------------------------
training_experiment_spec_id
training_condition_id

Execution provenance plane
--------------------------
training_attempt_id
runtime_provenance_id
```

For example, the same deterministic duel executed through a 16-actor or 32-actor collection topology must still have the same gameplay semantics when the exact public action sequence is the same.

## MAJOR 3 — Integrate Open RL Benchmark only after OCGForge result bundles exist

Open RL Benchmark is a useful analysis/export target, not the first implementation layer.

Preferred order:

```text
immutable OCGForge benchmark/result bundles
        ↓
OCGForge statistical compiler
        ↓
canonical CSV/Parquet/static output
        ↓
Open RL Benchmark / W&B adapter
```

The project must remain able to reproduce all benchmark conclusions with W&B, Hugging Face, MLflow, Trackio, Open RL Benchmark, or any other external service completely absent.

No sampled tracker history may become canonical benchmark evidence.

## Particularly strong research decisions

### Accepted public action as the sample axis

The primary sample-efficiency counter should be one successfully accepted public action against one complete public candidate domain.

This avoids falsely crediting replay-heavy algorithms with extra environment experience.

Track separately:

```text
env.accepted_public_actions_total
learner.loss_action_equivalents_total
learner.reuse_ratio
```

### Algorithm quality and scaling are separate experiments

Quality benchmark:

```text
Which algorithm learns better under a frozen benchmark condition?
```

Scaling benchmark:

```text
How does one frozen algorithm/configuration use increasing resources?
```

Do not let a systems-throughput advantage silently become an algorithm-quality claim.

### Cold start and BC warm start stay separate

```text
random initialization → RL
```

and

```text
same BC policy artifact → RL fine-tuning
```

answer different scientific questions and need separate HPO, tables, curves, and conclusions.

### Frozen evaluation replaces training return as cross-algorithm strength evidence

Independent self-play populations are moving and algorithm-specific opponent distributions.

All compared checkpoints must eventually be judged against a shared frozen evaluation bundle with exact opponents, roles, decks, seeds, starting-player/seat partitions, and policy RNG rules.

### Pareto frontiers are preferable to forced scalar winners

A plausible benchmark result may be:

```text
fastest early learner       Algorithm A
best sample efficiency      Algorithm B
best wall-clock scaling     Algorithm C
highest large-budget score  Algorithm B
best stability              Algorithm A
```

If no method dominates all declared axes, report the non-dominated set rather than inventing one universal winner.

## MINOR 1 — Hugging Face is broader than a passive registry, but remains non-authoritative

Hugging Face can provide artifact storage, exact revisions, cards, collections, lightweight tracking, and other execution/publication features.

That broader capability does not change the OCGForge boundary: Hugging Face is replaceable infrastructure and must not own `PolicyArtifactId`, trusted evaluation execution, or benchmark identity.

## MINOR 2 — V2 documentation status drift should be cleaned separately

At the research checkpoint, some V2 documentation still used pre-implementation wording despite PR #18 having merged.

This is documentation drift, not an architecture blocker. Do not pull unrelated cleanup into Phase 3A or future benchmark-contract work unless separately scoped.

## Relationship to current roadmap

This research belongs to future Phase 7.

Current sequence remains:

```text
Phase 3A
Trusted Trajectory Core Contracts
        ↓
Phase 3B
Persistence + Admission
        ↓
Phase 4
Teacher + Frozen Evaluation
        ↓
Phase 5
Candidate-scoring Model Adapter
        ↓
Phase 6
Behavior Cloning
        ↓
Phase 7
Controlled RL Benchmark
```

No PPO/IMPALA/R2D2/APPO implementation should begin because this research exists.

## Proposed future Phase-7 decomposition

```text
7A Benchmark protocol + identities
7B Frozen evaluator result bundles
7C Statistical compiler
7D Variance/hardware pilot
7E HPO protocol freeze
7F Cold-start benchmark
7G BC-warm-start benchmark
7H Scaling benchmark
7I External analysis/publication exports
Later: self-play population evaluation
```

Exact phase names are research guidance, not accepted contracts.

## Final project boundary

Adopt from the ecosystem:

- analysis and learning-curve tooling;
- robust statistical methods;
- optional HPO/distributed execution;
- policy-lag/scaling instrumentation;
- artifact publication/registry conventions;
- cross-play methodology later.

OCGForge continues to own:

- what gameplay occurred;
- what information the policy was allowed to see;
- the complete legal candidate domain;
- which public action was selected;
- trusted trajectory/replay evidence;
- training-condition identity;
- frozen evaluation jobs;
- benchmark result bundles;
- the statistical protocol used for project decisions.

This interpretation is intentionally non-normative. Future contracts must ratify exact IDs, canonical bytes, field ownership, and acceptance gates separately.