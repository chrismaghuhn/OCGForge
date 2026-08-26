
The existing candidate type already includes semantic action keys, locators, continuation information, action kinds, and final-response semantics.

The semantic key itself should be retained for identity and replay, but it should not be the model’s only feature. An opaque embedding of the complete key would generalize poorly to unseen candidates.

## 3.4 PICK and FINISH

PICK, FINISH, CANCEL, and continuation-local decisions should be scored as ordinary candidates within the current request.

The model does not need to know how to construct the final ocgcore response. It needs to know:

- what has already been selected;
- which candidates remain legal;
- whether FINISH is legal now;
- the semantic implications of selecting or finishing.

The environment remains responsible for preserving the paused engine, rejecting stale identities, and submitting exactly one final response.

## 3.5 Scaling and batching

The live M4 baseline observed:

- mean candidate count: approximately 4.73;
- maximum candidate count: 1,344.

That distribution favors an (O(|C|)) candidate scorer:

- normal requests remain cheap;
- large candidate domains remain complete;
- inference can be bucketed by candidate count;
- ragged batches can use flat candidate arrays plus per-request offsets;
- segmented softmax avoids a global padded vocabulary.

Full candidate-to-candidate self-attention would become quadratic at the 1,344-candidate extreme. It should not be the first architecture unless measurements show that candidate interactions cannot be represented through the observation and continuation context.

## 3.6 Generalization

Candidate scoring naturally accepts a candidate that was not present during training. Whether it scores that candidate well depends on representation:

- card-ID-only embeddings provide little genuine zero-shot generalization;
- semantic candidate fields plus structured card properties provide a reasonable unknown-card path;
- card text or script-derived features may improve later generalization but are not required for the fixed matchup.

Candidate scoring therefore solves the **architectural ability to represent new legal actions**, but not automatically the **strategic understanding of unseen cards**.

---

# 4. Observation representation

## 4.1 Contract versus adapter

**RECOMMENDATION**

Keep `PlayerObservation` as the environment contract. Add a separate versioned model-facing adapter.

The adapter may:

- tokenize entities;
- normalize bounded integer fields;
- create embeddings;
- construct masks and ragged offsets;
- deduplicate repeated static card features;
- encode cumulative event history compactly;
- bucket samples;
- produce tensors.

It must not:

- change visibility;
- infer hidden identities;
- remove authoritative fields without a declared adapter decision;
- reorder semantic sequences;
- reconstruct legality;
- become a second game-state authority.

## 4.2 Recommended initial representation

Use a hybrid representation with four components.

### Global and match features

Examples:

- LP;
- turn and phase;
- active player;
- starting-player partition;
- player identity and perspective;
- known public matchup/deck identity where authorized;
- current decision type;
- chain length;
- hand and zone counts.

### Entity/card tokens

Each visible entity token should contain:

- card ID or unknown-card ID;
- owner/controller;
- zone;
- sequence or semantically meaningful position;
- face-up/down and battle position;
- static card properties;
- current visible ATK/DEF/level/rank/link information;
- relation references or relation-derived features.

### Ordered sequences

These need order-aware encoding:

- chain order;
- visible event chronology;
- semantically ordered continuation selections;
- any ordered-zone information that is legitimately visible.

### Unordered or partially ordered sets

These should not depend on incidental enumeration:

- hand entities where order has no semantics;
- graveyard/banished collections except where sequence is contractually meaningful;
- candidate domains;
- relation sets.

A small Deep Sets-style encoder is sufficient for the first experiment. Attention can be introduced only if interaction modeling is measurably deficient. Deep Sets gives an appropriate inductive bias for permutation-invariant entity collections, while Set Transformer adds richer interactions at greater cost. ([NeurIPS Proceedings](https://proceedings.neurips.cc/paper_files/paper/2017/hash/f22e4747da1aa27e363d86d40ff442fe-Abstract.html "https://proceedings.neurips.cc/paper_files/paper/2017/hash/f22e4747da1aa27e363d86d40ff442fe-Abstract.html"))

## 4.3 Graph representations

A graph model is plausible because `PlayerObservation` contains relationships, materials, chain links, and zone/entity references. It is not the minimum useful architecture.

Reasons to defer a GNN:

- the first fixed matchup has a constrained card pool;
- many important relations can be encoded directly as typed features;
- graph batching adds engineering cost;
- the M4 sample reportedly contained no relationship objects in one serialization workload, so relation-heavy modeling has not yet been shown to dominate.

A GNN becomes justified if targeted ablations show failures on:

- linked-zone reasoning;
- Xyz/material relationships;
- target relationships;
- chain dependency reasoning;
- multi-entity board interactions.

## 4.4 Textual card information

Do not build a Yu-Gi-Oh! language model initially.

For the fixed two-deck slice:

- raw card ID embeddings are adequate for memorizing the supported pool;
- structured card properties improve inductive bias;
- a stable unknown-card embedding provides a defined failure mode.

Text embeddings become relevant only after a multi-deck experiment shows that ID-plus-structure cannot transfer to newly introduced cards.

CardScripts may later produce versioned structured metadata, but such metadata must remain a model feature source. It must never become a second legality engine.

---

# 5. Does the policy need memory?

## 5.1 Theoretical answer

A memoryless policy over a compact current-board snapshot is generally insufficient for Yu-Gi-Oh!, because public history can reveal information not recoverable from the present board alone.

Examples include:

- which cards were searched;
- which effects were already used;
- what was revealed earlier;
- which cards were returned or shuffled;
- opponent action tendencies;
- what resources have been expended;
- whether a line is consistent with a hidden interaction card;
- which once-per-turn effects were observed.

However, OCGForge’s current observation workload contains cumulative visible event history.

**INFERENCE**

A sufficiently expressive feed-forward network over the full current observation and cumulative history may already approximate a useful information-state policy. This means recurrence is not automatically necessary for the first agent.

## 5.2 Architecture comparison

| Memory designStrengthCost/riskRecommendation |                                       |                                           |                           |
| -------------------------------------------- | ------------------------------------- | ----------------------------------------- | ------------------------- |
| Feed-forward current observation             | Cheapest and easiest to audit         | May miss history-dependent inference      | **First baseline**        |
| Frame stacking                               | Simple                                | Highly redundant with cumulative history  | Do not prioritize         |
| GRU                                          | Compact recurrent baseline            | Sequence replay and hidden-state handling | **First memory ablation** |
| LSTM                                         | Strong conventional memory            | More parameters/state                     | Second recurrent option   |
| Transformer-XL memory                        | Long-context capacity                 | High cost and complexity                  | Later                     |
| Recurrent state-space model                  | Efficient long sequences in principle | Less established in this domain           | Research later            |
| Explicit belief model                        | Interpretable probability state       | High design and leakage risk              | Defer                     |

Recurrent model-free RL can be a strong POMDP baseline when architecture and training details are handled carefully, but current research does not establish that a Transformer is universally superior for POMDPs. Recent work has in fact reported limitations of ordinary Transformers and strong recurrent alternatives. ([Proceedings of Machine Learning Research](https://proceedings.mlr.press/v162/ni22a.html "https://proceedings.mlr.press/v162/ni22a.html"))

## 5.3 Exact recommendation

Run a matched ablation:

```text
same entity encoder
same candidate scorer
same parameter budget range
same teacher dataset
same optimization
feed-forward versus GRU
```

Promote the GRU only if it provides a statistically meaningful improvement in full-duel evaluation, not merely better teacher-label accuracy.

## 5.4 Recurrent lifecycle

A recurrent policy state should:

- be maintained separately for each player perspective;
- reset at duel start;
- reset on environment reset;
- reset when policy/checkpoint identity changes unless migration is explicitly supported;
- never be shared between the two players;
- advance at a precisely specified observation cadence;
- be treated as model state, not engine state.

For the first implementation, update recurrent state once per player decision observation. Events between decisions are already represented through the next `PlayerObservation`.

## 5.5 Trajectory handling

Recurrent trajectories need:

- episode and player-stream boundaries;
- sequence indices;
- burn-in prefixes;
- learning-unroll boundaries;
- behavior-policy version;
- optional stored behavior hidden state as non-authoritative diagnostic data.

The preferred learner behavior is to recompute the current hidden state from burn-in using the current network. R2D2 specifically identifies recurrent-state staleness, representational drift, and parameter lag as important replay issues. ([ICLR](https://iclr.cc/virtual/2019/poster/648 "https://iclr.cc/virtual/2019/poster/648"))

Stored floating-point hidden states should not become replay authority. They may vary across device implementations and checkpoint versions.

---

# 6. Reward design and combo credit assignment

## 6.1 Primary reward

**RECOMMENDATION**

Use terminal outcome as the authoritative objective:

```text
win   +1
draw   0
loss  -1
```

This directly represents the duel objective, is deck-neutral, and avoids teaching an incorrect proxy strategy.

## 6.2 Reward comparison

| RewardAdvantagesMain failure modesDecision |                                          |                                                 |                        |
| ------------------------------------------ | ---------------------------------------- | ----------------------------------------------- | ---------------------- |
| Terminal only                              | Objective-faithful, simple, reproducible | Sparse and delayed                              | **Primary**            |
| LP differential                            | Dense and intuitive                      | Rewards temporary damage over winning lines     | Reject as primary      |
| Card advantage                             | Helps material reasoning                 | Misvalues graveyard resources and combo setup   | Reject as primary      |
| Board-value heuristic                      | More strategic                           | Highly deck-specific and exploitable            | Teacher feature only   |
| Tempo/disruption                           | Potentially useful                       | Difficult to define without hindsight           | Teacher/auxiliary only |
| Potential-based shaping                    | Can preserve policy under assumptions    | Hard to define safely in a POMDP/self-play game | Optional experiment    |
| Learned reward model                       | Flexible                                 | Objective drift, hidden bias, weak auditability | Defer                  |

The classical guarantee for potential-based reward shaping is tied to a specific potential-difference form. Arbitrary dense shaping does not preserve the optimal policy and can create reward-hacking behavior. ([dblp](https://dblp.org/pid/n/AndrewYNg "https://dblp.org/pid/n/AndrewYNg"))

Even potential-based shaping should not be assumed safe without checking:

- whether the potential is computed only from legitimate player information;
- whether the relevant game/POMDP assumptions hold;
- whether self-play equilibrium behavior is preserved;
- whether the potential creates deck-specific asymmetries.

## 6.3 Training aid versus evaluation objective
