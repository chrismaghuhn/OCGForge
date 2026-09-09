# OCGForge Fair RL Algorithm Benchmarking, Scaling, and Learning-Curve Evaluation

**Status:** Research recommendation / future Phase-7 design basis  
**Implementation authorization:** None  
**Research checkpoint:** `ea5b3ddf414987b451c44becf30619f1a0814189`  
**Related roadmap:** Issue #16  

This document records research into how OCGForge should later compare reinforcement-learning algorithms fairly when algorithms differ in early learning speed, data reuse, actor/learner scaling, wall-clock efficiency, hardware cost, stability, and large-budget performance.

It does **not** choose PPO, IMPALA/V-trace, R2D2, APPO, or any other method as the winner. It does not authorize neural-network implementation, self-play, league training, distributed actors, or cloud training.

The post-Phase-2 public policy boundary remains authoritative:

```text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
+ public_action_key
```

Internal `ActionCandidate.semantic_key`, internal decision identity, raw engine responses, raw message hashes, continuation IDs, hidden identities, CoreHost state, and restricted diagnostics remain outside the learner-facing boundary.

---

# 1. Executive recommendation

The general RL benchmarking problem has been solved in useful pieces, but no examined framework supplies the complete OCGForge chain:

```text
complete perspective-safe legal decisions
        ↓
trusted semantic trajectories
        ↓
algorithm-neutral training provenance
        ↓
frozen imperfect-information evaluation
        ↓
replay-backed benchmark evidence
        ↓
multi-axis statistical comparison
```

OCGForge should reuse ecosystem tooling for analysis, orchestration, statistics, and publication while retaining authority over gameplay semantics and benchmark evidence.

Recommended logical architecture:

```text
OCGForge semantic plane
────────────────────────────────────────
Pinned rules bundle
    ↓
EpisodicEnvironment V2
    ↓
trusted public trajectories + semantic replay

Training control plane
────────────────────────────────────────
Immutable TrainingExperimentSpec
    ↓
algorithm-specific runner adapter
    ↓
append-only training metrics
    ↓
immutable policy/training checkpoints

Evaluation evidence plane
────────────────────────────────────────
Frozen EvaluationBundle
    ↓
independent Evaluator
    ↓
per-game trusted evaluation trajectories
    ↓
immutable EvaluationResultBundle

Benchmark compilation plane
────────────────────────────────────────
EvaluationResultBundles
+ training metrics
+ resource provenance
    ↓
pinned statistical compiler
    ↓
learning curves / confidence intervals
sample efficiency / threshold times
scaling curves / Pareto frontiers

Publication plane
────────────────────────────────────────
Open RL Benchmark export
W&B / MLflow / Trackio export
Hugging Face model/benchmark cards
static CSV / Parquet / Markdown / figures
```

Primary conclusions:

1. Do not select one algorithm using one scalar leaderboard or one final budget.
2. Use accepted public actions as the primary environmental sample axis.
3. Separate algorithm quality from systems scalability.
4. Treat Open RL Benchmark as downstream analysis/export, not canonical authority.
5. Treat Hugging Face primarily as artifact publication/registry.
6. Pin or vendor the small rliable statistical surface if adopted.
7. Keep Ray Tune optional and subordinate to OCGForge-owned search budgets and identities.
8. Keep cold-start and BC-warm-start benchmarks separate.
9. Freeze evaluation opponents, job schedules, deck/seat/start partitions, seeds, and policy RNG schedules before confirmatory runs.
10. Use Pareto frontiers when different algorithms dominate different efficiency/strength axes.

---

# 2. Existing-system comparison

| System | Useful capability | Limitation for OCGForge | Recommended role |
| --- | --- | --- | --- |
| Open RL Benchmark | Complete learning curves, runtime/step axes, cross-library mappings, rliable analysis | W&B-centric scalar-history model; no legality/privacy/replay authority | Downstream analysis/export |
| rliable | IQM, bootstrap intervals, performance profiles, probability of improvement, optimality gap | Archived repository; no OCGForge-specific hierarchical sampling | Pin/vendor statistical core + OCGForge wrapper |
| Hugging Face Hub | Model repos, exact revisions, cards, collections, RL integration conventions | Does not execute/verify OCGForge semantic evaluation | Publication/discovery/registry |
| CleanRL | Transparent reference algorithms, seeded commands, reproducibility patterns | Not a neutral distributed control/evidence plane | Reference implementation methodology |
| SB3 / RL-Baselines3-Zoo | Baselines, evaluation scripts, Optuna tuning, Hub publication | Mostly Gym-style/single-machine assumptions | Pilot baseline/reference conventions |
| RLlib / Ray Tune | Actor/learner scaling, resource placement, HPO/search scheduling | Runtime topology can change data freshness/training semantics | Optional execution/HPO backend |
| Sample Factory | High-throughput actor/learner design, policy-lag/stale-data diagnostics | APPO/PPO-centered trajectory/runtime assumptions | Scaling instrumentation/reference |
| TorchRL / Tianshou / Acme / Reverb | Collectors, replay, recurrent sequence tooling, actor/learner building blocks | Their formats do not establish OCGForge legality/privacy/replay truth | Implementation substrate behind adapters |
| W&B / MLflow / Trackio | Dashboards, run comparison, artifacts, registry | Histories/aliases are mutable external state | Optional mirrors/UI |
| Procgen / ALE / Atari 100k | Fixed-budget and train/test benchmark discipline | Different environment/action semantics | Methodology only |
| OpenSpiel / PSRO / alpha-Rank | Cross-play/population evaluation/non-transitive methodology | Full-YGO exploitability is intractable; incompatible environment contract | Later self-play evaluation methodology |

---

# 3. Open RL Benchmark applicability

Open RL Benchmark is particularly useful because it compares complete run histories rather than only terminal scores. Its configurable mappings normalize library-specific keys such as environment name, experiment name, reward metric, x-axis, tags, and seeds.

It can provide:

- curves over environment steps;
- curves over runtime;
- aggregate plots/tables;
- rliable statistics;
- performance profiles;
- sample-efficiency views;
- wall-clock-efficiency views;
- CSV/Markdown/figure output.

However, its model is intentionally loose. It cannot prove that two same-named runs used the same rules bundle, reward semantics, candidate completeness, public observation semantics, privacy boundary, or frozen opponent schedule.

Its online source is normally W&B history. Complete history scans are preferable; sampled histories must never become canonical OCGForge evidence.

Recommended compatibility view:

```text
config.env_id
    = benchmark_environment_suite_id

config.exp_name
    = algorithm_implementation_id

config.algorithm_family_id
    = algorithm_family_id

config.seed
    = training_seed

config.training_experiment_spec_id
    = training_experiment_spec_id

config.evaluation_bundle_id
    = evaluation_bundle_id

global_step
    = env.accepted_public_actions_total

_runtime
    = time.training_elapsed_seconds

eval/frozen_pool_score
    = frozen evaluator score for exact checkpoint
```

Additional exported axes/series may include actor core-hours, learner GPU-hours, accepted actions/sec, queue/replay pressure, policy lag, forfeit rate, and invalid-evidence rate.

Hard boundary:

> Open RL Benchmark may visualize OCGForge evidence. It must not determine what gameplay occurred or whether the evidence is valid.

---

# 4. Hugging Face applicability

Useful Hugging Face capabilities include:

- versioned model repositories;
- exact revisions;
- model cards and structured metadata;
- RL-Baselines3-Zoo and Sample Factory publication conventions;
- datasets/collections;
- lightweight experiment tracking and Hub synchronization where useful.

Recommended identity relationship:

```text
PolicyArtifactId
    = OCGForge-owned content/manifest identity

HuggingFaceLocator
    = repo + exact revision + path

HuggingFaceLocator != PolicyArtifactId
```

Mutable aliases such as `main`, `latest`, or `best` are convenient human locators but cannot be canonical benchmark identity.

A future public policy card should be able to expose, where publishable:

```text
policy_artifact_id
training_checkpoint_id
training_experiment_spec_id
algorithm_family_id
algorithm_implementation_id
model_architecture_id
tensor_adapter_id
environment_semantic_id
rules_bundle_id
matchup_id
reward_adapter_id
input_dataset_id
initial_policy_artifact_id
evaluation_bundle_id
evaluation_result_bundle_id
runtime provenance summary
known limitations
supported deck/role
evaluation action-selection mode
```

Conclusion:

> Hugging Face is mainly an artifact publication/discovery/registry layer for OCGForge, not the benchmark authority.

---

# 5. Statistical evaluation

The top-level independent experimental unit is the **training run/seed**, not an individual evaluation game.

Conceptual score tensor:

```text
score[
    algorithm,
    training_seed,
    evaluation_stratum,
    checkpoint
]
```

An evaluation stratum should normally bind a frozen combination such as:

```text
opponent_policy_id
candidate deck/role
seat assignment
starting-player partition
evaluation action-selection mode
```

Multiple evaluation games inside one training seed/stratum estimate one checkpoint's playing strength. They must not be treated as thousands of independent RL training runs.

Recommended confirmatory output:

- mean frozen-evaluation score + confidence interval;
- raw per-training-seed scores and curves;
- win/draw/loss/failure/forfeit rates;
- median/IQR;
- IQM as a robust secondary aggregate initially, becoming more useful as the benchmark gains many matchup/evaluation strata;
- pairwise probability of improvement;
- performance profiles where multiple frozen tasks/strata exist;
- completion/failure rate across intended training runs.

The statistical compiler should own the hierarchy:

```text
resample training seeds
    ↓
optionally resample evaluation jobs within each stratum
    ↓
aggregate stratum scores
    ↓
apply frozen stratum weights
    ↓
compute mean / IQM / probability of improvement / profiles
```

Distinguish two claims:

1. Exact frozen-bundle score: deterministic fact against the exact immutable job bundle.
2. Distributional estimate: uncertainty about future jobs from a declared generator.

Do not mix the two estimands.

Seed-count guidance should be pilot-driven rather than ritualized. A practical initial convention is:

```text
3 seeds  = engineering smoke
5 seeds  = exploratory pilot
10 seeds = confirmatory starting point
>10      = use when pilot variance/effect size requires
```

The final count should be frozen before confirmatory results are inspected.

For threshold analyses, non-crossing runs are right-censored. Report fraction crossing, censor horizon, and a censor-aware summary rather than dropping slow learners.

Learning-curve integrals may include separately named linear-decision and log-decision AULC. Their grid, interpolation, initial value, horizon, and missing-checkpoint handling must be preregistered.

---

# 6. Learning-curve and multi-budget design

Define the primary new-environment-information unit as:

> One successfully accepted `public_action_key` against one complete current EpisodicEnvironment V2 public candidate domain.

Name:

```text
accepted_public_action
```

This includes policy-visible continuation decisions. It excludes:

- rejected stale/unknown actions;
- projection failures;
- policy retries;
- optimizer steps;
- PPO epoch reuse;
- R2D2 replay samples;
- raw core `process()` calls;
- automatic noninteractive messages.

Required comparison axes:

| Axis | Meaning | Use |
| --- | --- | --- |
| Accepted public actions | New policy/environment interactions | Primary sample efficiency |
| Completed episodes | Finished duels | Match/outcome throughput |
| Actor CPU core-hours | Simulation cost | CPU efficiency |
| Learner/inference GPU-hours | Accelerator cost | Learner/inference efficiency |
| Wall-clock time | End-to-end elapsed time | Operational speed |
| Learner action-equivalents | Total transitions processed by losses | Data reuse |
| Optimizer steps / FLOPs | Update work | Compute characterization |

No one axis replaces the others.

Use a common log-spaced accepted-action checkpoint grid, e.g. initially:

```text
100k
300k
1M
3M
10M
30M
100M
...
```

The exact grid must be calibrated by pilot data.

At every reporting checkpoint, run the same frozen `EvaluationBundle`. Learning curves should therefore plot **frozen evaluator strength** against accepted public actions and then project the same checkpoint evidence onto wall-clock and resource axes.

Record requested vs actual checkpoint budget for asynchronous systems:

```text
requested_budget
actual_checkpoint_budget
overshoot_actions
overshoot_fraction
```

Do not call the last finite checkpoint "asymptotic" without a separately defined plateau analysis. Prefer:

```text
terminal performance at budget B
best observed frozen-evaluation performance
plateau estimate under protocol P
```

---

# 7. Failure semantics in benchmarking

Distinguish three classes.

## Policy/algorithm failure

Examples:

- NaN action score;
- no action returned;
- invalid public action key;
- declared inference budget violation.

These normally become explicit policy forfeits and remain in performance statistics.

## Environment/evidence failure

Examples:

- incomplete legal domain;
- privacy violation;
- public/internal candidate divergence;
- semantic replay mismatch;
- worker-count-dependent gameplay;
- unsupported required decision.

These invalidate benchmark evidence and are not ordinary losses.

## Infrastructure failure

Examples:

- host termination;
- transport/storage interruption;
- unrelated process crash.

Retain provenance for the failed attempt. A deterministic semantic job may be retried, but duplicate/conflicting results must fail closed instead of being resolved by arrival order.

Report both intended-run completion and completion-conditional performance so crash-prone methods cannot look artificially strong.

---

# 8. Hardware/scaling benchmark

Algorithm quality and systems scalability are separate experiments.

## Quality benchmark

Question:

```text
How well does the algorithm learn under a frozen resource/evaluation protocol?
```

Hold fixed as appropriate:

- environment semantics;
- reward adapter;
- model capacity tier;
- evaluation bundle;
- final accepted-action horizon;
- hardware envelope;
- HPO budget;
- reference actor/learner topology.

## Scaling benchmark

Question:

```text
How does one frozen algorithm/configuration use increasing resources?
```

Hold algorithm/model/reward/environment/seed/evaluation fixed while varying declared topology.

Recommended layers:

### S0 — Environment-only scaling

```text
fixed semantic action traces
→ 1 / 4 / 16 / 32 / 64 actors
```

Measure throughput, memory, dispatch overhead, publication cost, while proving semantic equivalence.

### S1 — Frozen-policy inference scaling

```text
environment + frozen checkpoint + inference service
```

Measure inference throughput/latency, batch distribution, queue wait, candidate scoring throughput, GPU/CPU utilization, and end-to-end accepted actions/sec.

### S2 — Full actor/learner scaling

```text
actors + policy distribution + queues/replay + learner + evaluator
```

Measure throughput **and** learning quality.

Illustrative topology matrix:

```text
actors:   1, 4, 16, 32, 64, ...
learners: 1, 2, 4 where supported
```

Distinguish:

- strong scaling: fixed total work/optimization semantics;
- throughput/weak scaling: increasing aggregate work/resources;
- algorithm-native scaling: best topology within a fixed resource envelope.

Report speedup and efficiency plus quality at matched accepted-action and matched wall-clock budgets.

Important training-semantics point:

> Logical actor count, learner count, rollout/replay topology, synchronization mode, and policy-publication cadence may affect training data freshness and therefore belong in the training condition. Physical hostnames, PIDs, device serials, wall time, and scheduling order remain runtime provenance.

---

# 9. Hyperparameter and model-capacity fairness

Keep three budgets distinct.

## Algorithm-development budget

Includes implementation/debugging, pilots, architecture changes, and human analysis. It is difficult to equalize perfectly but must be disclosed. Freeze implementation revisions before confirmatory test results are exposed.

## Hyperparameter-search budget

Use a multidimensional cap:

```text
max trials/configurations
max accepted public actions
max actor CPU core-hours
max learner GPU device-hours
max elapsed search time
max concurrent resources
```

Search stops when the first declared cap is reached.

## Final-training budget

After configuration freeze:

- use new independent seeds;
- freeze final sample horizons;
- freeze evaluation bundle;
- prohibit final-test-driven retuning.

Report final-run cost and total research-inclusive cost separately.

Recommended tracks:

### Reference-config track

Published/official configurations, changed only where necessary for OCGForge interface/hardware integration.

### Equal-HPO track

Same search resource vector and validation protocol for every algorithm.

Do not merge the rankings.

Multi-fidelity pruning can bias against delayed learners. Grace periods must be expressed in accepted public actions, the same pruning policy should apply across algorithms, all pruned trials must remain in the record, and finalists should be rerun to full budget.

## Model-capacity fairness

The preferred controlled track is:

> Matched shared observation/candidate/recurrent backbone capacity with algorithm-native heads and complete compute disclosure.

Freeze a capacity tier around:

```text
public observation encoder
candidate encoder
shared recurrent backbone
hidden-state size
embedding dimensions
shared-backbone parameter envelope
inference-FLOP envelope
```

Allow algorithm-native policy/value/Q/distributional/target/auxiliary heads.

Report:

```text
shared parameters
online-head parameters
target-network parameters
auxiliary parameters
total stored parameters
inference FLOPs
training FLOPs estimate
peak VRAM
checkpoint size
```

Candidate scoring cost depends on domain size, so latency/FLOPs should be reported at representative candidate-count percentiles rather than only at one fixed size.

---

# 10. Recurrent/POMDP fairness

The primary OCGForge RL comparison should be recurrent because the game is partially observable and history-dependent.

Standardize:

- hidden-state initialization;
- episode-boundary reset;
- terminal masking;
- truncation handling;
- no hidden state across unrelated episodes;
- no privileged engine state.

Allow algorithm-specific:

- unroll length;
- burn-in length;
- stored vs recomputed recurrent state;
- BPTT length.

Charge burn-in/recomputation as learner work. Replay age, recurrent-state staleness, and policy-generation lag are material metrics for recurrent off-policy methods.

---

# 11. Cold start vs BC warm start

These are separate benchmark families.

## Cold start

```text
frozen architecture
+ deterministic initialization contract
→ RL from random initialization
```

Measures exploration, representation learning, optimization stability, and sample efficiency without a policy prior.

## BC warm start

```text
same frozen BC policy artifact
+ same transfer manifest
→ algorithm-specific RL fine-tuning
```

Measures improvement of an existing policy and resistance to forgetting.

Warm-start contract should bind:

```text
behavior_cloning_dataset_id
initial_policy_artifact_id
model_architecture_id
exact transferred tensors
exact newly initialized tensors
initialization rule for native heads
optimizer-state policy
normalization-state policy
recurrent-state policy
step-zero evaluation result
```

Default direction: transfer common encoders/recurrent backbone, initialize missing algorithm-native heads through a frozen rule, and start fresh optimizer/scheduler/replay unless a separately named experiment explicitly transfers them.

Never combine cold and warm runs into one ranking.

---

# 12. Frozen opponent/evaluation design

Initial frozen evaluation pool should eventually contain immutable representatives such as:

```text
RandomLegal
one or more deterministic deck-specific teachers
frozen BC checkpoints
later: frozen historical RL checkpoints
```

Every opponent should have explicit artifact/source identity, action-selection contract, policy RNG contract, supported role/deck, and version.

A future versioned `EvaluationBundle` should bind:

```text
environment semantic ID
rules bundle ID
matchup ID
candidate policy deck/role
opponent pool manifest
opponent mixture weights
seat assignments
starting-player partitions
environment seed schedule
candidate policy RNG schedule
opponent RNG schedule
evaluation action-selection mode
score adapter
inference limits
failure/forfeit rules
job ordering
```

All algorithms/seeds receive the same logical evaluation jobs. Job identity must be independent of worker, PID, host, completion order, or wall time.

For a two-player duel, a conventional benchmark score may be:

```text
win  = 1.0
draw = 0.5
loss = 0.0
```

Policy forfeits count as losses. Training reward is not independent playing-strength evidence.

Freeze deterministic vs stochastic evaluation action selection. If stochastic sampling is part of evaluation, bind the exact policy RNG schedule.

Keep at least:

```text
development opponent pool
HPO validation pool
final hidden test pool
```

Training-time self-play win rate is not comparable across independently evolving populations.

---

# 13. Later self-play evaluation

This research does not authorize self-play implementation, but it recommends the later evaluation extension:

```text
immutable historical snapshot archive
common cross-play tournament
shared external anchor opponents
empirical payoff matrix
population-level evaluation
fixed-compute approximate best response
historical-forgetting metrics
```

Store the full payoff/cross-play matrix before reducing it to scalar ratings.

Exact exploitability is unrealistic for full Yu-Gi-Oh!. A bounded-compute approximate best response can instead measure discovered vulnerability, but it must be reported as a lower bound/proxy with its own compute budget and provenance.

Elo/Glicko may remain useful for matchmaking/operations but cannot be the sole scientific result in a potentially non-transitive game.

Potential forgetting metrics:

```text
mean score versus historical pool
worst-decile historical score
minimum score against critical anchors
regression from prior snapshot
count of historical opponents with material decline
```

---

# 14. Experiment identity and provenance direction

Do not conflate gameplay semantic identity, training-condition identity, execution provenance, and storage identity.

Recommended conceptual planes:

```text
Gameplay semantic plane
───────────────────────
environment_semantic_id
episode_semantic_id
public_semantic_decision_id
public_gameplay_trajectory_id

Training condition plane
────────────────────────
training_experiment_spec_id
training_condition_id

Execution provenance plane
──────────────────────────
training_attempt_id
runtime_provenance_id

Artifact/evaluation plane
─────────────────────────
policy_artifact_id
training_checkpoint_id
evaluation_bundle_id
evaluation_result_bundle_id
derived_analysis_id
```

A future `TrainingExperimentSpec` should fully materialize defaults and include semantically material training conditions such as:

```text
environment_semantic_id
observation/action/tensor adapter IDs
reward_adapter_id
input_dataset_id
model architecture/capacity tier
initial policy artifact
algorithm family/implementation revision
fully expanded algorithm config
optimizer/scheduler/numerical config
logical actor count
logical learner count
rollout/replay topology
training opponent/curriculum
training seed derivation/root seed
final budget
checkpoint schedule
```

A separate hardware envelope may describe promised resources. Runtime provenance records the actual compiler/build/container/packages/OS/CPU/GPU/driver/provider/host topology without altering gameplay semantic identity.

A deployable `PolicyArtifact` and a resumable `TrainingCheckpoint` are distinct concepts. The latter may additionally own optimizer, scheduler, scaler, learner RNG, replay state/manifest, cumulative counters, and generation information.

The `EvaluationBundle` normally belongs to evaluation result identity rather than policy identity unless evaluation directly affects early stopping, model selection, promotion, curriculum, or HPO.

---

# 15. Exact metrics direction

A future metric event should include at least:

```text
metric_schema_id
training_attempt_id
source_component_id
component_generation
event_sequence_index
metric_name
metric_kind
unit
value / histogram
accepted_public_actions_total
learner_optimizer_steps_total
monotonic_elapsed_time
policy_generation_index where relevant
```

Wall-clock timestamps may be diagnostic but never semantic identity.

## Environment/integrity

```text
env.accepted_public_actions_total
env.decision_frames_total
env.original_engine_requests_total
env.continuation_actions_total
env.final_engine_responses_total

env.episodes_started_total
env.episodes_completed_total
env.episodes_truncated_total

env.wins_total
env.draws_total
env.losses_total
env.policy_forfeits_total

env.candidate_count
env.candidate_scores_computed_total

env.action_rejections_total
env.stale_action_rejections_total
env.unknown_action_rejections_total
env.public_action_collision_total

env.unsupported_required_decisions_total
env.candidate_truncations_total
env.public_projection_failures_total
env.public_internal_domain_mismatches_total
env.privacy_failures_total
env.replay_mismatches_total
env.semantic_divergences_total
```

Semantic/privacy/replay correctness failure counters must remain zero for valid benchmark evidence.

## Learner/data reuse

```text
learner.unique_public_actions_admitted_total
learner.input_action_equivalents_total
learner.loss_action_equivalents_total
learner.burn_in_action_equivalents_total
learner.sequences_sampled_total
learner.minibatches_total
learner.optimizer_steps_total
learner.target_updates_total
```

Derived reuse ratio:

```text
learner.loss_action_equivalents_total
/
learner.unique_public_actions_admitted_total
```

This is essential because replay-heavy and multi-epoch methods must not turn reused transitions into fake new environment samples.

## Replay/distributed

```text
replay.items_inserted_total
replay.items_sampled_total
replay.items_evicted_total
replay.current_action_equivalents
replay.sample_age_actions
replay.sample_age_seconds
replay.priority

distributed.behavior_policy_generation
distributed.learner_policy_generation
distributed.policy_lag_updates
distributed.policy_lag_seconds
distributed.actor_queue_depth
distributed.actor_queue_age_seconds
distributed.learner_queue_depth
distributed.learner_queue_age_seconds
distributed.stale_sequences_discarded_total
distributed.network_bytes_sent_total
distributed.network_bytes_received_total
```

## Compute/time

```text
time.training_elapsed_seconds
time.training_active_seconds
time.evaluation_seconds
time.checkpoint_seconds
time.trajectory_validation_seconds

cpu.actor_allocated_core_seconds
cpu.actor_process_seconds
cpu.learner_process_seconds
cpu.evaluator_process_seconds

gpu.learner_allocated_device_seconds
gpu.inference_allocated_device_seconds
gpu.learner_active_seconds
gpu.inference_active_seconds
gpu.utilization_percent
gpu.memory_bytes_peak

memory.actor_rss_bytes_peak
memory.learner_rss_bytes_peak
memory.evaluator_rss_bytes_peak
```

Store latency histograms rather than only averages for environment actions, policy inference, learner updates, queue waits, policy distribution, validation, publication, and evaluation games.

Algorithm-specific losses/diagnostics should be namespaced instead of forced into misleading common fields.

---

# 16. What OCGForge can reuse

Infrastructure/methodology worth adopting later:

- Open RL Benchmark analysis and curve/report tooling;
- pinned rliable aggregate/statistical functions;
- optional Ray Tune/RLlib execution and HPO;
- optional W&B/MLflow/Trackio dashboards;
- Hugging Face exact-revision publication and model cards;
- CleanRL/RL-Baselines3-Zoo reproducibility/reference patterns;
- Sample Factory policy-lag/throughput instrumentation;
- TorchRL/Acme/Tianshou/Reverb components behind adapters;
- Procgen/ALE/Atari fixed-budget methodology;
- OpenSpiel/PSRO cross-play methodology in the later self-play phase.

Formats worth exporting to:

```text
W&B-compatible scalar histories
Open RL Benchmark inputs
Hugging Face model/eval metadata
MLflow runs
CSV / Parquet analysis tables
static Markdown / HTML / figures
```

These remain exports, never canonical gameplay contracts.

---

# 17. What OCGForge must own

OCGForge must continue to own:

- public environment semantics;
- complete legal public candidate domains;
- trusted trajectories;
- reward-adapter identity;
- training condition identity;
- checkpoint/policy artifact identity;
- frozen evaluation bundle identity;
- per-game evaluation evidence;
- semantic replay;
- result-bundle identity;
- benchmark/statistical protocol;
- promotion evidence.

Future authoritative data classes should remain distinct:

## Canonical trajectory data

Public observation/domain/action/closure plus exact semantic and policy provenance required to replay/attribute gameplay.

## Training-run metrics

Losses, throughput, resource use, policy lag, queue/replay state, numerical health, checkpoint references.

## Benchmark results

Frozen evaluation bundle + per-game results + trusted trajectory/evidence references + exact policy/opponent identities.

## Derived analytics

Interpolation, smoothing, confidence intervals, IQM, AULC, probability of improvement, threshold times, Pareto frontiers, ratings, plots.

Derived analytics must be regenerable from immutable inputs.

---

# 18. Risks

## BLOCKER

- External tracker becomes authoritative.
- Internal semantic keys/raw CoreHost state reach the learner.
- Candidate truncation or fixed global action vocabulary.
- Replay samples counted as new environment decisions.
- Moving self-play/training return used as cross-algorithm strength.
- Final test opponent pool influences HPO/config selection.
- Semantic divergence ignored in scaling work.
- Privacy/environment failure scored as an ordinary loss.
- Different reward/rules/evaluation/matchup semantics aggregated as one condition.

## MAJOR

- Only one final training budget.
- Too few independent training seeds.
- Evaluation games treated as independent training runs.
- Unequal/undisclosed HPO.
- Early-stopping policy systematically removes delayed learners.
- Exactly identical architecture disables algorithm-native requirements.
- Unbounded native architecture advantages conflate scale with algorithm.
- Policy lag/replay age/stale discards omitted.
- Wall-clock results lack hardware provenance.
- Cold and warm starts combined.
- Mutable checkpoint aliases used as identity.
- Archived statistical dependency used unpinned.
- Failed/pruned runs omitted.

## MINOR

- Smoothed curves shown without raw points.
- Only average latency/utilization stored.
- Public model-card/registry omissions.
- Energy metrics omitted where later cost studies might benefit.

---

# 19. Proposed future Phase-7 roadmap

This is a future sequence only. Earlier Phase-3/4/5/6 prerequisites remain mandatory.

## Phase 7A — Benchmark protocol and identities

Define training-condition/evaluation/result/statistical protocol terminology and canonical identities. No algorithm winner.

## Phase 7B — Frozen evaluator result bundles

Build immutable shared opponent/job bundles and replay-backed per-game result evidence.

## Phase 7C — Statistical compiler

Deterministic compiler over immutable results with raw seed curves, confidence intervals, probability of improvement, performance profiles, threshold analyses, and Pareto views.

## Phase 7D — Variance/hardware pilot

Measure variance, checkpoint range, recurrent batching, evaluation precision, and hardware bottlenecks. Explicitly nonconfirmatory.

## Phase 7E — HPO protocol freeze

Freeze reference/equal-HPO tracks, search budgets, validation pool, pruning rules, final seed count, and primary endpoints before final test exposure.

## Phase 7F — Cold-start benchmark

Run confirmatory recurrent algorithms from matched random initialization conditions.

## Phase 7G — BC-warm-start benchmark

Run separately from the same frozen BC policy artifact under an explicit transfer manifest.

## Phase 7H — Scaling benchmark

Run S0 environment-only, S1 frozen inference, and S2 full actor/learner scaling with matched semantic evidence and explicit resource provenance.

## Phase 7I — External exports

Only after OCGForge bundles are sufficient by themselves, export to Open RL Benchmark/W&B, Hugging Face, MLflow/Trackio, CSV/Parquet, and static reports.

## Later — Self-play evaluation extension

Only after the controlled frozen-opponent benchmark works: snapshot archives, cross-play, historical pools, approximate best responses, forgetting metrics, and league promotion evidence.

---

# Final adoption boundary

| Category | Recommendation |
| --- | --- |
| Infrastructure worth adopting | Open RL Benchmark analysis, pinned rliable functions, optional Ray/RLlib/Tune execution, optional trackers, Hugging Face publication |
| Methodology worth copying | Full learning curves, fixed budgets, robust aggregate statistics, train/test separation, policy-lag diagnostics, transparent configs, cross-play |
| Formats worth exporting to | W&B/Open-RL-Benchmark histories, HF cards/eval metadata, MLflow, CSV/Parquet/static reports |
| OCGForge must own | Gameplay/public candidate truth, trusted trajectories, training/evaluation identities, replay evidence, result bundles, statistical protocol |

Central architectural decision:

> OCGForge should adopt the ecosystem's analysis, orchestration, statistics, and publication capabilities, but it must continue to own every artifact that determines what gameplay occurred, what information the policy saw, what actions were legal, what training condition was executed, and what evidence supports a benchmark result.

---

# Primary external references

- Open RL Benchmark: https://github.com/openrlbenchmark/openrlbenchmark
- Open RL Benchmark paper: https://arxiv.org/abs/2402.03046
- rliable: https://github.com/google-research/rliable
- Hugging Face Hub RL-Baselines3-Zoo: https://huggingface.co/docs/hub/rl-baselines3-zoo
- Hugging Face evaluation results: https://huggingface.co/docs/hub/eval-results
- CleanRL: https://github.com/vwxyzjn/cleanrl
- RLlib scaling: https://docs.ray.io/en/latest/rllib/scaling-guide.html
- Ray Tune concepts: https://docs.ray.io/en/latest/tune/key-concepts.html
- Sample Factory policy lag: https://www.samplefactory.dev/07-advanced-topics/policy-lag/
- TorchRL: https://docs.pytorch.org/rl/main/
- OpenSpiel: https://openspiel.readthedocs.io/en/latest/intro.html

This research document is a design basis only. Proposed future schema names and phase labels do not become accepted contracts merely because they appear here.