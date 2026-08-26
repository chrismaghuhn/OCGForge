
Auxiliary targets must be derivable from legitimate player information.

## Priority ranking

### 1. Next decision family

Predict the next player-facing decision type or whether the episode terminates.

Benefits:

- decision-context representation;
- temporal structure;
- no hidden supervision.

### 2. Next visible event category

Predict categories such as:

- summon;
- activation;
- chain addition/resolution;
- zone movement;
- damage;
- phase transition.

### 3. Public LP delta and zone transitions

Predict:

- next LP change;
- visible entity zone movement;
- public hand-size change;
- chain-length change.

### 4. Terminal outcome/value

A shared outcome prediction head is already useful for policy learning.

### 5. Public board metrics

Predict counts or metrics exactly derivable from the current/next observation.

Reject:

- exact opponent hidden hand;
- exact deck order;
- hidden set-card identity;
- actual hidden-world state;
- privileged engine effect state.

---

# 19. Minimum trajectory contract

## 19.1 Logical trajectory record

The minimum conceptual record should contain:

| GroupRequired fields |                                                                             |
| -------------------- | --------------------------------------------------------------------------- |
| Schema               | trajectory schema ID and version                                            |
| Environment          | OCGForge commit, rules bundle, patchset, protocol versions                  |
| Matchup              | matchup ID, both deck hashes, seat and starting-player partition            |
| Job                  | master seed, job index/ID, episode ID                                       |
| Perspective          | acting player and observation perspective                                   |
| Policy               | model/checkpoint hash, model code commit, adapter version, inference config |
| Opponent             | opponent policy/checkpoint or teacher identity                              |
| State                | complete `PlayerObservation`                                                |
| Decision             | complete `DecisionRequest`                                                  |
| Domain               | complete, ordered `ActionCandidate` set                                     |
| Action               | chosen semantic action key and candidate index                              |
| Behavior data        | behavior log-probability or Q-values where applicable                       |
| Learning             | reward, discount, terminal, truncation, winner                              |
| Transition           | next observation/request/domain or exact successor reference                |
| Integrity            | observation, candidate-domain, action, and gameplay hashes                  |
| Recurrent            | sequence boundary and burn-in markers                                       |
| Provenance           | shard, generation run, writer version, validation status                    |

No raw engine pointer, opaque engine handle, process-local locator, or omniscient state belongs in the normal policy trajectory.

## 19.2 Deterministic action identity

The record must preserve both:

- the chosen semantic key;
- the complete candidate domain in which it was selected.

Storing only a candidate index is insufficient because the index has meaning only relative to the exact request and canonical candidate ordering.

## 19.3 Audit representation versus training representation

Use two distinct layers:

### Audit/logical layer

Defines exact semantics and can reconstruct:

- observation;
- candidates;
- action;
- reward;
- terminal result;
- provenance.

### Physical training shards

May use:

- compression;
- columnar encoding;
- repeated-static-feature dictionaries;
- event-prefix deduplication;
- integer packing;
- content-addressed records;
- deterministic sharding.

Physical compression may delta-encode cumulative history only if it reconstructs the exact logical observation. It must not silently redefine `PlayerObservation`.

## 19.4 Storage implications

The M4.3.4 historical sample averaged approximately 135.8 KiB of canonical observation data. At that shape:

| Decisions/observationsApproximate uncompressed canonical observation volume |          |
| --------------------------------------------------------------------------- | -------- |
| 100,000                                                                     | 12.6 GiB |
| 1 million                                                                   | 126 GiB  |
| 10 million                                                                  | 1.23 TiB |
| 100 million                                                                 | 12.3 TiB |

These are storage-shape estimates, not proposed trajectory sizes. They demonstrate that raw repeated canonical JSON is unsuitable as the sole physical training format.

## 19.5 Sharding and ordering

Recommended deterministic rules:

- shard by contiguous job-index ranges;
- sort records by job ID, player stream, and decision index;
- manifest every shard and hash;
- reject duplicates and gaps;
- never use worker completion order as semantic order;
- persist rules, deck, adapter, teacher, and model identities;
- make compression round-trip verification part of `DATA_TRUSTED`.

---

# 20. Replay-buffer architecture

## 20.1 Feed-forward stage

Use episode or transition replay for analysis and supervised learning.

## 20.2 Recurrent stage

Use sequence replay with:

- one player perspective per sequence;
- burn-in;
- learning unroll;
- terminal-aware boundaries;
- no crossing episode resets;
- policy and opponent version tags.

## 20.3 Prioritization

A later R2D2-style buffer should combine:

- TD-error priority;
- a uniform sampling floor;
- rare-decision stratification;
- teacher/demo sampling controls;
- recency control;
- long-term reservoir samples.

Importance-sampling correction is required where prioritized replay changes the training distribution.

## 20.4 Opponent-version awareness

Self-play replay should record:

- exact own behavior policy;
- exact opponent policy;
- opponent pool generation;
- sampling probability;
- matchup/deck identity.

Recommended retention:

- recent buffer for current learning;
- smaller reservoir of historically important episodes;
- teacher-anchor demonstrations;
- separately identifiable evaluation episodes that never enter training.

## 20.5 Stale representations

Store semantic source records, not only precomputed embeddings.

When the model adapter changes:

- retensorize under a declared compatible adapter;
- migrate with an explicit schema version;
- or reject the old data.

Do not silently reinterpret old candidate embeddings under new semantics.

---

# 21. Evaluation methodology

Evaluation must be independent of training reward.

## 21.1 Core protocol

Every policy evaluation should report:

- total win, loss, and draw rates;
- 95% confidence intervals;
- normal and mirrored seat partitions;
- both starting-player partitions;
- fixed regression seeds;
- unseen seeds;
- deterministic teacher baseline;
- random-legal baseline;
- WindBot benchmark where compatible;
- frozen policy snapshots;
- average and quantile duel length;
- candidate-count distribution;
- unsupported/error/truncation rate.

All unsupported, automatic-player-decision, candidate-truncation, core-error, and replay-divergence counts must be zero.

## 21.2 Paired comparison

When comparing two versions, run them on matched:

- seed;
- deck role;
- seat;
- starting player;
- opponent;
- environment identity.

Use a paired confidence interval for the win-rate difference rather than comparing two unrelated samples.

## 21.3 Exploitability proxy

Exact exploitability is not currently tractable.

Useful proxies include:

- train a bounded best response against a frozen policy;
- evaluate against a diverse historical pool;
- construct a restricted payoff matrix;
- report cyclic dominance;
- report worst-opponent win rate;
- compare average-policy and latest-policy performance.

Do not label such a proxy “true exploitability.”

## 21.4 Multi-deck evaluation

Future evaluations should separate:

- seen own deck and seen opponent;
- seen own deck and unseen opponent;
- unseen own deck after adaptation;
- unseen matchup;
- specialist versus shared-base policy;
- zero-shot versus fine-tuned performance.

---

# 22. Reproducibility requirements

Three reproducibility levels should be distinguished.

## Level 1 — Gameplay reproducibility

This is mandatory.

Given identical:

- canonical rules;
- deck identities;
- seed/configuration;
- semantic action sequence;

the system must reproduce:

- candidate domains;
- player observations;
- terminal result;
- gameplay hashes;
- semantic action identities.

This is independent of worker count or completion order.

## Level 2 — Data reproducibility

Also mandatory.

The same validated generation inputs must produce logically identical trajectories. Physical compressed files may differ only where the physical encoding contract explicitly permits it; logical decoded records and semantic hashes must match.

## Level 3 — Training reproducibility

Require statistical reproducibility, not necessarily bit-identical CUDA updates.

Record:

- environment and model code commits;
- complete model config;
- optimizer and scheduler;
- all seeds;
- worker count;
- batching order/config;
- mixed-precision settings;
- deterministic-kernel settings;
- trajectory manifests;
- checkpoint hashes;
- opponent pool;
- total environment decisions;
- learner updates;
- wall-clock and compute allocation.

For scientific comparisons, run at least three independent training seeds where budget permits and report the distribution rather than only the best checkpoint.

Bit-identical GPU training may be an optional diagnostic target. It should not be confused with deterministic gameplay.

---

# 23. Compute architecture

## 23.1 Recommended topology

Actor/learner separation is appropriate:

- CPU workers own independent ocgcore simulations.
- Actors obtain legal candidates and perspective-safe observations.
- Inference is local or batched through a policy service.
- Validated trajectories enter a queue or replay store.
- A GPU learner updates a versioned policy.
- Actors refresh policy versions at controlled boundaries.

For the first experiments, pin a policy version for an entire episode or rollout. This simplifies provenance and avoids mid-duel policy ambiguity.

## 23.2 Synchronous first

Use synchronous collection initially because it provides:

- exact policy-version boundaries;
- simpler PPO semantics;
- easier attribution of performance;
- reproducible rollout batches;
- no actor-lag correction.

Move to asynchronous actors only when measurement shows:

- learner starvation;
- actor barrier idle time;
- enough simulation throughput to justify lag;
- stable policy-version tracking.

## 23.3 Inference batching

The candidate architecture supports ragged batching:

```text
flat observations/entities
flat candidates
request offsets
segmented candidate softmax
```

A small first model may run economically on actor CPUs. GPU inference becomes useful when:

- enough actors can batch requests;
- centralized inference latency does not stall simulations;
- candidate padding waste is controlled through bucketing.

---

# 24. M4 throughput and experiment scale

## 24.1 Live PR baseline

At the live M4 PR head:

| WorkersDecisions/s100k decisions1M10M100M |        |          |         |        |         |
| ----------------------------------------- | ------ | -------- | ------- | ------ | ------- |
| 1                                         | 17.19  | 1.62 h   | 16.16 h | 6.73 d | 67.35 d |
| 16                                        | 148.22 | 11.2 min | 1.87 h  | 18.7 h | 7.81 d  |

These are baseline-build simulation measurements, not model-in-loop training throughput.

## 24.2 Supplied Release characterization

**INFERENCE**

Using M4.3.3’s 0.492809 games/s and approximately 618 player decisions per game gives roughly 305 environment decisions/s on one Release worker.

That implies simulation-only orders of magnitude of approximately:

| ScaleOne Release worker |             |
| ----------------------- | ----------- |
| 100k decisions          | 5.5 minutes |
| 1M                      | 0.91 hours  |
| 10M                     | 9.1 hours   |
| 100M                    | 3.8 days    |

This is not a throughput promise. It excludes:

- neural inference;
- trajectory serialization;
- replay writes;
- actor/learner coordination;
- checkpoint refresh;
- GPU stalls;
- a fresh Release worker-scaling matrix.

M4.3.3 explicitly states that the earlier scaling saturation point became stale under Release and that a new Release scaling matrix had not been run.

## 24.3 Sufficiency assessment

### Teacher generation and BC

Current evidence appears sufficient for:

- 100k–1M teacher-labeled decisions;
