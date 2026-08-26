
Shaped rewards, auxiliary losses, and teacher rankings may accelerate learning. They must never become the main success metric.

Final evaluation should be based primarily on:

- duel outcomes;
- partitioned win rates;
- robustness against an opponent pool;
- zero unsupported/error rate;
- strategically useful secondary metrics such as duel length or lethal conversion.

## 6.4 Long combos

Long Yu-Gi-Oh! combo lines create delayed credit but do not justify changing engine actions into macro-actions.

Use:

- teacher demonstrations;
- behavior-cloned initialization;
- n-step returns;
- generalized advantage estimation;
- sequence replay;
- value bootstrapping;
- optional later model-layer options.

Do not replace semantic engine decisions with artificial “execute combo” actions. Any option or macro abstraction must remain a separately versioned model-layer construct that ultimately emits the original complete semantic decisions.

---

# 7. Pure RL from scratch

## 7.1 Why legality is not the main difficulty

OCGForge already solves one common RL problem: the model receives legal candidates and cannot submit an out-of-domain action if the environment validates the semantic key.

That removes invalid-action exploration, but it does not solve:

- strategic exploration;
- sparse terminal rewards;
- long horizons;
- opponent responses;
- combo discovery;
- hidden-information inference;
- non-stationary self-play;
- huge variation in decision semantics.

The live M4 workload averages roughly 617–618 player decisions per duel. Random exploration therefore faces an extremely long credit-assignment chain.

## 7.2 Assessment

**RECOMMENDATION**

Pure RL from random initialization should be retained only as a scientific control.

Its expected uses are:

- measuring the value of teacher bootstrapping;
- detecting whether the teacher traps the policy in a local optimum;
- validating that the environment can support learning at all;
- comparing exploration behavior.

It should not be the primary path to the first useful agent.

---

# 8. On-policy versus off-policy learning

## 8.1 Value of sample reuse

OCGForge simulation is CPU-side and full perspective-safe observation construction is expensive. Reusing experience is therefore strategically valuable.

Off-policy replay can provide:

- multiple learner updates per simulated decision;
- better GPU utilization;
- retention of rare successful combo trajectories;
- reuse of teacher demonstrations;
- recurrent sequence training;
- lower simulation demand per gradient update.

## 8.2 On-policy strengths

PPO is attractive as the first RL diagnostic because:

- candidate probabilities can be computed directly through a legal-domain softmax;
- implementation is comparatively simple;
- behavior and target policies remain close;
- no replay compatibility layer is required;
- failures are easier to attribute;
- a scalar shared value head fits naturally.

PPO still consumes fresh trajectories for every update and is therefore a weak long-term choice if simulation remains the dominant cost. PPO’s original design explicitly trades a relatively simple clipped objective for multiple minibatch epochs over recently collected on-policy data. ([arXiv](https://arxiv.org/abs/1707.06347 "https://arxiv.org/abs/1707.06347"))

## 8.3 Off-policy strengths and complications

An R2D2-style learner offers:

- recurrent sequence replay;
- prioritized sample reuse;
- n-step value learning;
- distributed actor compatibility;
- a natural response to expensive simulation.

For OCGForge, its fixed action-value vector must be replaced by:

[
Q(o,d,c\_i)
]

for each complete next-state candidate domain. The Bellman target takes the maximum or policy-weighted value over all candidates returned by the next request. No candidate may be sampled away merely to reduce compute.

Complications include:

- recurrent-state staleness;
- behavior-policy lag;
- changing self-play opponents;
- stale model adapters;
- old candidate schemas;
- overrepresentation of obsolete strategies;
- Q-value overestimation over large candidate sets.

R2D2 directly studies recurrent replay, parameter lag, and stale recurrent states, making it the most relevant off-policy family for a serious OCGForge comparison. ([ICLR](https://iclr.cc/virtual/2019/poster/648 "https://iclr.cc/virtual/2019/poster/648"))

## 8.4 IMPALA

IMPALA separates actors from a learner and uses V-trace to correct actor-policy lag. This is useful when actor throughput is already large enough for asynchronous policy lag to become a real systems problem. ([Proceedings of Machine Learning Research](https://proceedings.mlr.press/v80/espeholt18a.html "https://proceedings.mlr.press/v80/espeholt18a.html"))

OCGForge should not begin there. Current priorities are:

- validating the episodic contract;
- proving candidate tensorization;
- producing trustworthy data;
- comparing sample reuse.

IMPALA becomes valuable after a measured actor fleet is starving the learner or synchronous barriers are materially reducing throughput.

## 8.5 Recommendation

The first controlled RL comparison should be:

1. PPO as the low-complexity on-policy reference.
2. R2D2-style recurrent candidate-Q learning as the sample-reuse reference.

However, the representation must be controlled:

- if the GRU ablation wins, both PPO and R2D2 should use the same GRU encoder;
- if the feed-forward policy wins, compare PPO against a feed-forward off-policy candidate-Q baseline before attributing differences to R2D2’s recurrence.

IMPALA/V-trace should be the third experiment, conditional on demonstrated policy-lag or scaling pressure.

---

# 9. WindBot and heuristic teachers

## 9.1 What WindBot is useful for

**FACT**

Current WindBot contains dedicated Swordsoul and Salamangreat executor implementations.

Its architecture uses large deck-specific rule executors, ordered callbacks, internal mutable state, fallback behavior, and random choices. The executor base owns duel-facing objects and initializes a random generator, while the two deck implementations contain extensive archetype-specific logic and state.

This makes WindBot valuable as:

1. an external benchmark opponent;
2. a source of archetype heuristic ideas;
3. a curriculum opponent;
4. a behavioral baseline;
5. a possible secondary label source after exact mapping.

## 9.2 What WindBot must not be

WindBot must not be treated as:

- a rules authority;
- proof that an action is legal;
- a perspective-safety authority;
- an unchanged deterministic OCGForge teacher;
- a source of labels that bypasses the OCGForge candidate domain.

Its current state interface has not been proven equivalent to `PlayerObservation + ActionCandidate`. Its random and fallback behavior also requires explicit normalization before labels can be reproducible.

## 9.3 Exact label mapping gate

A WindBot label may be admitted only when all of the following hold:

1. OCGForge and WindBot are operating under explicitly compatible rules, cards, decks, and visible state.
2. WindBot’s selected action can be mapped to exactly one OCGForge semantic candidate.
3. The mapped candidate is present in the complete candidate domain.
4. No hidden information outside `PlayerObservation` influenced the label.
5. Repeating the same input and teacher configuration yields the same mapped label.
6. Zero-match and multi-match cases are recorded as abstentions, not guessed mappings.

The mapping status should be explicit:

```text
LEGALITY_TRUSTED
TEACHER_LABEL_AVAILABLE
TEACHER_LABEL_MATCHED
TEACHER_CONFIDENCE
```

A legal but strategically poor WindBot choice remains legal. Teacher strength and legality are separate properties.

---

# 10. OCGForge-native deterministic teacher

## 10.1 Minimum useful architecture

**RECOMMENDATION**

The first trusted teacher should consume exactly:

```text
PlayerObservation
DecisionRequest
complete ActionCandidate domain
public experiment configuration
```

It should contain:

- generic Yu-Gi-Oh! heuristics;
- deck/archetype rules;
- combo-line state machines;
- threat and interaction assessment;
- target selection;
- deterministic tie handling;
- confidence and abstention.

It should not construct candidate legality.

## 10.2 Teacher output

The primary output should be a ranking of the complete candidate domain:

```text
candidate semantic key
relative score
rank or tie group
confidence class
rule provenance
abstention reason when applicable
```

The teacher may derive a top-1 choice for gameplay, but a complete ranking is more useful because it supports:

- listwise or pairwise supervision;
- measuring near-miss decisions;
- learning from tied strategic alternatives;
- policy distillation;
- later teacher comparisons;
- confidence calibration.

Top-k should be a derived view of the full ranking, not a reduced legal domain.

## 10.3 Determinism

Determinism requires:

- no wall-clock input;
- no random tie-breaking unless an explicit teacher seed is recorded;
- stable rule ordering;
- semantic-key tie-breaks;
- versioned rule modules;
- exact teacher configuration identity;
- same input producing byte-equivalent ranking output.

Where multiple actions are strategically tied, the teacher should preferably emit a tie group or abstain rather than teach an arbitrary key as strategically unique.

## 10.4 Abstention

Recommended abstention reasons include:

```text
NO_RULE_MATCH
AMBIGUOUS_TOP_RANK
LOW_CONFIDENCE
UNSUPPORTED_HEURISTIC_SEMANTICS
CONFLICTING_RULES
```

Abstention means “no trustworthy teacher preference.” It does not mean that the underlying legal decision is unsupported.

---

# 11. Behavior cloning and imitation learning

## 11.1 Role of BC

**RECOMMENDATION**

Behavior cloning should be an initialization method and representation test, not the final training method.

It offers:

- early non-random play;
- combo-line exposure;
- straightforward candidate-scorer validation;
- stable supervised debugging;
- low GPU cost;
- a way to verify data and batching before RL.

## 11.2 Main risks

### Covariate shift

A cloned policy reaches states the teacher corpus may not cover. Errors compound because an early wrong action changes every later state.

DAgger addresses this by collecting states from the learned policy and requesting expert labels on those induced states. Its original motivation is precisely that sequential imitation data is not i.i.d. and that the learner changes its own future state distribution. ([Proceedings of Machine Learning Research](https://proceedings.mlr.press/v15/ross11a "https://proceedings.mlr.press/v15/ross11a"))

For OCGForge, DAgger is viable only when the teacher safely labels arbitrary player-observation/candidate pairs and can abstain.

### Label bias

A deterministic teacher may encode:

- fixed combo preferences;
- brittle target priorities;
- deck-specific assumptions;
- weak opponent models;
- poor risk management.

RL must therefore be allowed to exceed the teacher.

### Dataset imbalance

Pass/decline/obvious continuation actions may dominate. Training should report accuracy by:

- decision family;
- candidate count bucket;
- action kind;
- combo stage;
- teacher confidence;
- game phase;
- rare candidate types.

A high global top-1 accuracy can conceal complete failure on strategically decisive decisions.

## 11.3 Data-source priority

Recommended order:

1. OCGForge-native deterministic teacher.
2. Strong self-play snapshots.
3. Search-generated labels after checkpoint/fork exists.
4. Exactly mapped WindBot labels.
5. Future human replay data.

Human replays are optional augmentation, not a prerequisite.

Demonstration-augmented RL methods such as DQfD and R2D3 show that even relatively small demonstration sets can improve early performance or exploration, but they do not establish that OCGForge should immediately implement their full distributed systems. ([AAAI Veröffentlichungen](https://ojs.aaai.org/index.php/AAAI/article/view/11757 "https://ojs.aaai.org/index.php/AAAI/article/view/11757"))

---

# 12. Search as a teacher

## 12.1 Why naïve MCTS is a poor initial fit

Yu-Gi-Oh! combines:

- hidden hands and deck order;
- chance;
- opponent responses;
- hundreds of decisions per duel;
- large combinatorial candidate domains;
- long setup sequences;
- state-dependent tactical traps;
- expensive observation construction.

Naively determinizing the true hidden state and running ordinary MCTS risks:

- strategy fusion;
- conditioning decisions on information the player does not know;
- severe rollout-policy bias;
- different hidden worlds producing improperly merged nodes;
- enormous expansion cost.

Information-set MCTS and re-determinizing methods were developed specifically because ordinary perfect-information MCTS can leak hidden information or reason inconsistently in imperfect-information games. ([Pure York](https://pure.york.ac.uk/portal/en/publications/information-set-monte-carlo-tree-search/ "https://pure.york.ac.uk/portal/en/publications/information-set-monte-carlo-tree-search/"))
