- identical entity encoder;
- identical recurrence choice;
- similar parameter budget;
- identical rules/decks;
- identical terminal reward;
- equal environment-decision budget;
- equal wall-clock budget as a separate result;
- matched evaluation seeds and opponents;
- at least three learner seeds;
- identical checkpoint-selection protocol;
- zero trust-gate violations.

Report both:

```text
performance per environment decision
performance per wall-clock hour
```

This separates algorithmic sample efficiency from systems throughput.

---

# 29. Staged roadmap

## Stage A — Episodic environment and data contract

**Owning layer:** new environment/trajectory boundary above canonical simulation and below all model code.

**Prerequisites:** accepted decision, observation, replay, rules, and M4 worker contracts.

**Outputs:**

- reset configuration;
- step request/response;
- terminal and truncation semantics;
- perspective-safe transition output;
- versioned trajectory schema;
- deterministic trajectory replay validator;
- exact provenance.

**Acceptance gates:**

- `ENVIRONMENT_READY`;
- `DATA_TRUSTED`;
- cross-worker replay equivalence;
- zero candidate loss;
- zero privacy divergence;
- exact chosen-action replay.

**Privacy/determinism/replay implications:** this stage owns the stable bridge from gameplay semantics to training data. No model-specific tensor fields belong in its authoritative contract.

**Must not happen yet:** neural network, reward shaping, WindBot integration, self-play, checkpoint search.

## Stage B — Deterministic teacher baseline

**Owning layer:** policy/reference-agent layer.

**Prerequisites:** Stage A.

**Outputs:**

- full-domain ranking interface;
- deterministic teacher rule modules;
- confidence and abstention;
- teacher evaluation report;
- random-legal and deterministic teacher opponents.

**Acceptance gates:**

- `TEACHER_READY`;
- no raw CoreHost access;
- same input yields same ranking;
- exact candidate matching;
- zero teacher-generated legality.

**Must not happen yet:** describe teacher quality as rules correctness; use WindBot labels without mapping proof.

## Stage C — Neural imitation baseline

**Owning layer:** model adapter and supervised-learning layer.

**Prerequisites:** trusted trajectories and teacher labels.

**Outputs:**

- versioned tensor adapter;
- entity/set encoder;
- candidate scorer;
- BC checkpoints;
- feed-forward versus GRU ablation;
- inference benchmark.

**Acceptance gates:**

- tensor round-trip fixtures;
- complete-domain segmented softmax;
- held-out-seed metrics;
- zero mapping failures;
- deterministic evaluation profile;
- `TRAINING_READY`.

**Must not happen yet:** universal model, text encoder, league, search.

## Stage D — Controlled RL comparison

**Owning layer:** learner layer.

**Prerequisites:** accepted BC baseline and evaluation harness.

**Outputs:**

- PPO baseline;
- off-policy candidate-Q baseline;
- R2D2-style learner if recurrence wins;
- fixed-opponent evaluation;
- algorithm-tournament report.

**Acceptance gates:**

- RL improves over frozen BC under confidence intervals;
- zero trust errors;
- no partition collapse;
- equal-budget comparison;
- reproducible checkpoint selection.

**Must not happen yet:** declare the first successful algorithm globally best.

## Stage E — Snapshot self-play

**Owning layer:** opponent-pool manager and evaluation layer.

**Prerequisites:** independently useful specialists for both locked decks.

**Outputs:**

- immutable policy snapshots;
- opponent sampling manifest;
- promotion gates;
- payoff history;
- teacher-anchor matches.

**Acceptance gates:**

- no regression against anchors;
- promotion on paired evaluation;
- historical-pool robustness;
- zero policy-version ambiguity.

**Must not happen yet:** complex exploiters or population-based hyperparameter tuning.

## Stage F — League

**Owning layer:** population and empirical-game layer.

**Prerequisites:** measured cycles or forgetting and multiple promoted snapshots.

**Outputs:**

- payoff matrix;
- prioritized or mixture opponent selection;
- exploiters only where justified;
- average-policy or meta-strategy evaluation.

**Acceptance gates:**

- better worst-opponent or pool performance than simple snapshot sampling;
- bounded compute overhead;
- reproducible opponent mixtures;
- no degradation of environment semantics.

**Must not happen yet:** copy AlphaStar’s scale or complexity without evidence.

## Stage G — Search teacher

**Owning layer:** checkpoint/fork and planning layer.

**Prerequisites:** accepted checkpoint ADR, deterministic restore/fork, stable policy/value model.

**Outputs:**

- shallow search baseline;
- hidden-information-safe search configuration;
- search-generated ranking labels;
- policy-improvement comparison.

**Acceptance gates:**

- checkpoint/restore digest parity;
- paired-world noninterference;
- deterministic search under recorded seed/budget;
- teacher benefit under equal total compute.

**Must not happen yet:** deploy MCTS as default policy or use true hidden state as a determinization oracle.

## Stage H — Multi-deck generalization

**Owning layer:** model/adapter and curriculum layer.

**Prerequisites:** additional certified decks and stable specialists.

**Outputs:**

- shared entity/card encoder;
- public deck/archetype conditioning;
- specialist heads or adapters;
- distillation experiments;
- held-out matchup evaluation.

**Acceptance gates:**

- no catastrophic interference;
- specialists remain available as references;
- positive transfer under equal budget;
- explicit unknown-card behavior.

**Must not happen yet:** claim arbitrary-deck or full-card support.

---

# 30. Exact first neural experiment

## Scientific question

> Can a small deck-specific entity/set encoder with complete-candidate scoring reproduce a deterministic teacher well enough to play full held-out duels, without recurrence?

## Configuration

| ItemSpecification  |                                                                                 |
| ------------------ | ------------------------------------------------------------------------------- |
| Own deck           | Swordsoul Tenyi ML v1                                                           |
| Opponent           | Deterministic Salamangreat ML v1 native teacher                                 |
| Environment        | Locked M3/M3.5 rules and decks                                                  |
| Perspective        | Swordsoul player only                                                           |
| Observation        | Globals, entities, zones, ordered visible history, decision context             |
| Candidate encoding | Action kind, source/target semantics, card/entity features, continuation fields |
| Network            | Deep Sets-style entity encoder, 256 hidden width, joint candidate MLP           |
| Parameter budget   | 1–3 million                                                                     |
| Recurrence         | None                                                                            |
| Value head         | None in the primary supervised test                                             |
| Teacher            | OCGForge-native deterministic complete-domain ranker                            |
| Loss               | Listwise cross-entropy, confidence-weighted; optional pairwise tie loss         |
| Split              | By duel root seed, never by individual transition                               |

## Data volume

Generate:

- 3,072 training duels;
- 384 validation duels;
- 384 held-out supervised-test duels.

All sets should be balanced across seat and starting-player partitions. Teacher-abstained decisions remain recorded but are excluded from top-1 imitation loss.

This produces roughly one million Swordsoul decision labels, depending on the exact decision split.

## Evaluation

Supervised metrics:

- top-1 accuracy;
- top-3 accuracy;
- negative log-likelihood;
- calibration by teacher confidence;
- accuracy by decision type;
- accuracy by candidate-count bucket;
- exact action-key mapping failures.

Gameplay metrics:

- learned Swordsoul versus random-legal Salamangreat;
- learned Swordsoul versus deterministic Salamangreat teacher;
- native Swordsoul teacher versus the same opponent;
- 1,024 fixed evaluation seeds and 1,024 unseen seeds;
- all four seat/start partitions.

## Acceptance thresholds

- zero privacy, replay, mapping, or candidate-domain failures;
- at least 80% top-1 accuracy on high-confidence teacher decisions;
- at least 95% top-3 accuracy on high-confidence decisions;
- lower 95% win-rate bound at least 20 percentage points above random legal;
- gameplay win rate no more than 5 percentage points below the native Swordsoul teacher against the same opponent;
- no partition more than 8 points below the aggregate.

The strategic thresholds are initial experimental gates, not permanent project contracts.

## Compute budget

- no more than eight CPU-worker-hours for trusted data generation;
- no more than four GPU-hours on one ordinary training GPU;
- no more than 20 epochs;
- early stop after five epochs without validation-NLL improvement.

## Stop conditions

Stop immediately on:

- nonzero trajectory replay divergence;
- any hidden-information mismatch;
- any missing or truncated candidate;
- any chosen action absent from its domain;
- unresolved teacher nondeterminism.

---

# 31. Exact first RL experiment

## Scientific question

> Does terminal-outcome RL improve a behavior-cloned Swordsoul candidate scorer against a fixed opponent without sacrificing deterministic or partitioned performance?

## Configuration

| ItemSpecification |                                                           |
| ----------------- | --------------------------------------------------------- |
| Algorithm         | PPO                                                       |
| Initialization    | Accepted BC checkpoint                                    |
| Control           | Same PPO from random initialization                       |
| Opponent          | Frozen deterministic Salamangreat teacher                 |
| Network           | Same feed-forward entity/set encoder and candidate scorer |
| Value             | Shared scalar value head                                  |
| Reward            | Win +1, draw 0, loss −1                                   |
| Discount          | 1.0                                                       |
| GAE               | λ = 0.95                                                  |
| PPO clip          | 0.2 initial setting                                       |
| Actors            | 8 synchronous actors                                      |
| Rollout           | 256 Swordsoul decisions per actor                         |
| Batch             | 2,048 acting-player decisions before update               |
| Candidate policy  | Segmented softmax over complete legal set                 |
| Recurrence        | None                                                      |
| Replay            | None; strictly recent PPO rollout data                    |
| Maximum budget    | 2 million Swordsoul decisions                             |

These hyperparameters are starting experimental settings, not accepted project defaults.

## Evaluation

Every 250,000 decisions:

- 1,024 fixed seeds;
- 1,024 unseen seeds;
- all seat/start partitions;
- compare against frozen BC;
- compare against random-initialized PPO control;
- retain teacher and random-legal references.

## Success criterion

The RL policy passes if:

- the lower 95% bound on paired win-rate improvement over frozen BC exceeds 3 percentage points;
- no seat/start partition regresses by more than 3 points;
- teacher-anchor performance remains within 5 points of the BC baseline;
- all trust counters remain zero.

## Stop criteria

Stop when:

- four consecutive evaluations show no improvement;
- 2 million decisions are consumed;
- any trust gate fails;
- policy entropy collapses while win rate does not improve;
- one partition deteriorates by more than 8 points.

This experiment tests whether RL adds value. It is not intended to produce the final strongest agent.

---

# 32. Exact first self-play experiment

Self-play is justified only after both deck specialists show RL improvement over their supervised baselines.

## Policies

Maintain separate:

- Swordsoul specialist;
- Salamangreat specialist.

## Snapshot policy

- create an immutable candidate snapshot every 250,000 own-deck decisions;
- evaluate before promotion;
- retain at most eight promoted snapshots per deck initially;
- never overwrite checkpoint identities.

## Opponent sampling

For each training episode:

- 50% latest promoted opponent;
- 30% uniform historical promoted snapshot;
- 20% deterministic teacher anchor.

## Evaluation corpus

For each promotion candidate:

- 1,024 fixed seeds;
- 1,024 unseen seeds;
- every seat/start partition;
- matched comparison against the current incumbent;
