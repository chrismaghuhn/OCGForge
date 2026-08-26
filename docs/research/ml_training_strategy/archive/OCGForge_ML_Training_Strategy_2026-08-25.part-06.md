- model-adapter validation;
- a small supervised experiment.

### Small RL

Current evidence is plausibly sufficient for:

- a 1M–10M decision PPO or replay experiment;
- a controlled representation ablation.

A model-in-loop benchmark is still required before committing the budget.

### Large self-play

A 100M-decision experiment is not yet operationally characterized. Before such a run, measure:

- Release scaling;
- actual model inference;
- trajectory compression;
- queue overhead;
- replay-buffer bandwidth;
- policy-refresh cost.

### Large league training

Not justified by current evidence.

---

# 25. Kaggle viability

## 25.1 Practical role

Kaggle is practical for:

- behavior cloning;
- offline trajectory analysis;
- model-adapter tests;
- small learner experiments;
- checkpoint evaluation;
- possibly limited online RL.

It should not define OCGForge’s environment architecture.

Kaggle’s official notebook documentation currently describes 12-hour CPU/GPU sessions, 20 GB of auto-saved working storage, four CPU cores for CPU and GPU notebooks, and T4×2 configurations among the available accelerators. ([Kaggle](https://www.kaggle.com/docs/notebooks "https://www.kaggle.com/docs/notebooks"))

Kaggle also publishes the Docker definitions for its CPU and GPU notebook images, which is useful for reproducing the user-space environment. ([GitHub](https://github.com/kaggle/docker-python "https://github.com/kaggle/docker-python"))

As of 25 August 2026, Kaggle has announced that its P100 option will retire on 15 September 2026, with T4×2 and L4 identified as replacements. Experiment manifests should therefore record the actual accelerator rather than assume a stable “Kaggle GPU.” ([Kaggle](https://www.kaggle.com/discussions/product-announcements/735239 "https://www.kaggle.com/discussions/product-announcements/735239"))

## 25.2 Constraints

- Four CPU cores limit local ocgcore actor throughput.
- Twelve-hour sessions require resumable shards/checkpoints.
- Twenty GB of persistent working storage is insufficient for large raw canonical-JSON datasets.
- Online actor/learner orchestration is less convenient than offline BC.
- GPU type may vary across experiments.

## 25.3 Required Linux/Kaggle gate

The eventual workflow should be:

1. clone the exact OCGForge commit;
2. fetch the pinned rules bundle;
3. build the same Linux Release worker;
4. run a deterministic acceptance corpus;
5. compare with Windows Release;
6. only then run training.

Cross-platform equivalence should include:

- terminal result;
- winner;
- gameplay semantic hash;
- exact candidate domains;
- semantic actions;
- PlayerObservation hashes where the canonical contract is platform-portable.

Raw trace hashes that intentionally include compiler/build identity may differ. Build/provenance hashes must not be compared as gameplay hashes.

---

# 26. Strategy comparison

## 26.1 Required recommendation matrix

| StrategyInitial strengthSample efficiencyComputeEngineeringPOMDP fitCandidate-space fitLong-term ceilingRecommendation |                      |                |                       |                   |                           |             |             |                      |
| ---------------------------------------------------------------------------------------------------------------------- | -------------------- | -------------- | --------------------- | ----------------- | ------------------------- | ----------- | ----------- | -------------------- |
| A — Pure RL from scratch                                                                                               | Very low             | Poor           | Very high             | Medium            | Conditional               | Excellent   | High        | Control only         |
| B — Heuristic teacher → BC → RL                                                                                        | High                 | Excellent      | Moderate              | Medium            | Good with memory ablation | Excellent   | High        | **Use as bootstrap** |
| C — WindBot imitation → RL                                                                                             | Medium–high          | Good           | Moderate              | High mapping cost | Unproven                  | Conditional | Medium–high | Secondary only       |
| D — Search teacher → policy/value                                                                                      | High if search works | Good per label | Very high             | Very high         | Difficult                 | Excellent   | Very high   | Defer                |
| E — Self-play from scratch                                                                                             | Very low             | Poor           | Very high             | High              | Conditional               | Excellent   | Very high   | Reject as first path |
| F — Heuristic bootstrap → snapshot self-play → league                                                                  | High                 | Excellent      | Moderate–high         | High but staged   | Strong                    | Excellent   | Very high   | **Primary strategy** |
| G — Teacher + off-policy recurrent RL                                                                                  | High                 | Excellent      | Efficient after setup | Very high         | Excellent                 | Excellent   | Very high   | Later core learner   |

## 26.2 Full 1–5 scoring

Scoring rules:

- 5 is best for compatibility, efficiency, strength, fit, ceiling, scalability, reproducibility, and human-data independence.
- complexity 5 means hardest;
- risk 5 means highest risk.

| StrategyEnv. compatibilityComplexitySample efficiencyCompute efficiencyPOMDP fitEarly strengthCeilingMulti-deckReproducibilityHuman-data independenceRisk |                 |   |   |   |   |   |   |   |   |   |   |
| --------------------------------------------------------------------------------------------------------------------------------------------------------- | --------------- | - | - | - | - | - | - | - | - | - | - |
| A Pure RL                                                                                                                                                 | 5               | 3 | 1 | 1 | 3 | 1 | 4 | 3 | 4 | 5 | 5 |
| B Native teacher → BC → RL                                                                                                                                | 5               | 3 | 5 | 4 | 4 | 5 | 5 | 4 | 5 | 5 | 2 |
| C WindBot → RL                                                                                                                                            | 3               | 4 | 4 | 4 | 3 | 4 | 4 | 3 | 2 | 5 | 4 |
| D Search teacher                                                                                                                                          | 2 now / 4 later | 5 | 4 | 1 | 3 | 4 | 5 | 4 | 4 | 5 | 5 |
| E Self-play from scratch                                                                                                                                  | 5               | 4 | 1 | 1 | 3 | 1 | 5 | 4 | 3 | 5 | 5 |
| F Native bootstrap → snapshot pool → league                                                                                                               | 5               | 4 | 5 | 4 | 4 | 5 | 5 | 5 | 4 | 5 | 2 |
| G Teacher + off-policy recurrent RL                                                                                                                       | 5               | 5 | 5 | 5 | 5 | 5 | 5 | 5 | 3 | 5 | 3 |

Strategy F is the best complete progression. Strategy B is its bootstrap portion, and Strategy G is a likely later learner architecture.

---

# 27. Algorithm comparison

PPO is the cleanest first diagnostic; R2D2-style learning is the most important sample-reuse comparison; IMPALA is a later systems-scale candidate. DQfD/R2D3 ideas are relevant after trusted teacher trajectories exist. Agent57, MuZero, discrete SAC, and full Rainbow stacks introduce too many simultaneous variables for the first tournament. Agent57 itself combines a family of exploration policies and substantial distributed machinery, making it unsuitable as an initial controlled baseline. ([Proceedings of Machine Learning Research](https://proceedings.mlr.press/v119/badia20a.html "https://proceedings.mlr.press/v119/badia20a.html"))

| AlgorithmWhy it might fitWhy it might failRequired environment featuresExperiment priority |                                                         |                                            |                                                      |                              |
| ------------------------------------------------------------------------------------------ | ------------------------------------------------------- | ------------------------------------------ | ---------------------------------------------------- | ---------------------------- |
| Behavior-cloned candidate scorer                                                           | Validates representation cheaply; strong initialization | Covariate shift and teacher ceiling        | `DATA_TRUSTED`, `TEACHER_READY`                      | **P0**                       |
| PPO                                                                                        | Simple legal-domain policy; stable diagnostic           | Weak sample reuse                          | Episodic rollout API, log-probs, value targets       | **P1**                       |
| R2D2-style candidate-Q                                                                     | Reuses expensive sequences; handles memory              | High replay/recurrent complexity           | Sequence replay, burn-in, full next candidate domain | **P1 controlled comparison** |
| Feed-forward off-policy candidate-Q                                                        | Isolates replay from recurrence                         | May underperform in POMDP                  | Transition replay, target network                    | P1 if GRU loses              |
| IMPALA/V-trace                                                                             | Good asynchronous actor/learner scaling                 | Premature without actor-scale pressure     | Versioned behavior policies and lag tracking         | P2                           |
| DQfD                                                                                       | Combines demos and Q-learning                           | Fixed-action assumptions need adaptation   | Trusted demo buffer, candidate-Q scorer              | P2                           |
| R2D3                                                                                       | Strong conceptual fit for demos, POMDP, sparse reward   | Very high systems complexity               | R2D2 stack plus demo replay                          | P2–P3                        |
| QR-DQN/Rainbow components                                                                  | Better value distributions/stability may help           | Confounds first algorithm comparison       | Mature off-policy baseline                           | P3                           |
| Discrete SAC variant                                                                       | Entropy-based exploration                               | No clear advantage; variable-domain tuning | Stable candidate-policy critic                       | P3–P4                        |
| NFSP                                                                                       | Average-policy concept addresses non-stationarity       | Additional networks/buffers                | Mature self-play population                          | P3 league                    |
| PSRO                                                                                       | Robust population responses and payoff matrix           | Expensive response-oracle loop             | Stable policies and evaluation matrix                | P3 league                    |
| CFR+/Deep CFR                                                                              | Imperfect-information theory                            | Tree traversal is impractical              | Information-set traversal                            | Do not prioritize            |
| ReBeL-like search                                                                          | Search and equilibrium learning                         | Belief/search architecture absent          | Public belief, fork/restore, value model             | Long-term only               |

---

# 28. Controlled algorithm tournament

Once the environment exists, OCGForge should run a controlled tournament rather than selecting an algorithm by reputation.

Required controls:

- identical `PlayerObservation`;
- identical candidate encoding;
