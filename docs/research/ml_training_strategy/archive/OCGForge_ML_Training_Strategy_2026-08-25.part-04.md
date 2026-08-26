
## 12.2 Required capabilities before search

Search should not begin before OCGForge has:

- an authoritative checkpoint boundary;
- deterministic restore;
- deterministic fork/clone;
- bounded independent rollout instances;
- semantic checkpoint identity;
- player-safe root information;
- a defined hidden-world sampling method;
- an opponent rollout policy;
- a value or rollout evaluator;
- transposition-key rules;
- strict resource limits.

The static project roadmap already identifies checkpoint/fork/replay as a separate future architecture area that should receive an ADR before persistence semantics become long-lived.

## 12.3 Recommended role

Use search initially as:

```text
policy improvement operator
teacher label generator
hard-state analyzer
```

Do not make it the deployed policy by default.

ReBeL demonstrates that search in imperfect-information games can be combined with self-play learning, but it relies on public-belief-state reasoning and game-specific tractability that OCGForge does not currently possess. It is a long-term conceptual reference, not a near-term implementation prescription. ([arXiv](https://arxiv.org/abs/2007.13544 "https://arxiv.org/abs/2007.13544"))

## 12.4 Search trust gate

A search teacher must pass paired-world information-safety tests:

- construct two authoritative worlds with identical player observations but different hidden state;
- run the teacher with the same public inputs and search seed;
- require indistinguishable label distributions unless the algorithm explicitly samples beliefs from legitimate public information;
- reject actual-hidden-state-conditioned labels.

---

# 13. Self-play and leagues

## 13.1 Why latest-policy mirror self-play is insufficient

The initial matchup is asymmetric:

```text
Swordsoul Tenyi ML v1
versus
Salamangreat ML v1
```

The first self-play system should therefore use two deck specialists, not one policy mirrored against itself.

Training only against the latest opponent creates:

- non-stationarity;
- tactical overfitting;
- catastrophic forgetting;
- cyclic strategies;
- exploitation of one counterpart’s quirks;
- loss of performance against older policies or teachers.

PSRO research explicitly identifies overfitting to concurrently learned policies and motivates training approximate responses against mixtures of policies. ([arXiv](https://arxiv.org/abs/1711.00832 "https://arxiv.org/abs/1711.00832"))

## 13.2 Recommended progression

### First

Train against a frozen deterministic opponent or teacher.

### Then

Introduce a small pool of frozen opponent snapshots.

### Then

Use candidate-versus-pool evaluation and promotion.

### Only later

Introduce a structured league with prioritized opponents or exploiters.

AlphaStar’s population and league mechanisms are evidence that population training can address strategic diversity and forgetting in a complex game, but its scale and architecture are not evidence that OCGForge needs an AlphaStar-sized league initially. ([Nature](https://www.nature.com/articles/s41586-019-1724-z "https://www.nature.com/articles/s41586-019-1724-z"))

## 13.3 When a league becomes justified

Begin a league only when at least one of these is measured:

- A beats B, B beats C, and C beats A under stable confidence intervals.
- A new snapshot improves against the latest opponent but regresses materially against earlier snapshots.
- Training repeatedly collapses to a narrow exploitable line.
- At least three promoted policy generations exist for each deck.
- A payoff matrix contains meaningful strategic diversity.

Before that point, a small snapshot pool is sufficient.

## 13.4 Imperfect-information algorithms

### CFR, CFR+, and Deep CFR

These require repeated information-set/game-tree traversal. Yu-Gi-Oh!’s full tree, card pool, candidate structure, and horizon make exact traversal unrealistic. Deep CFR avoids tabular storage through function approximation, but still approximates iterative CFR traversal and was demonstrated in much more structurally tractable poker games. ([Proceedings of Machine Learning Research](https://proceedings.mlr.press/v97/brown19b.html "https://proceedings.mlr.press/v97/brown19b.html"))

**Decision:** do not implement first.

### NFSP

NFSP combines best-response learning with an average policy and demonstrated useful convergence behavior in poker-like imperfect-information settings. ([arXiv](https://arxiv.org/abs/1603.01121 "https://arxiv.org/abs/1603.01121"))

Transferable idea:

- retain an average/behavioral policy or historical mixture;
- avoid optimizing only against the latest opponent.

**Decision:** useful conceptual input to a later league, not the first learner.

### PSRO

Transferable ideas:

- maintain a population;
- estimate a restricted empirical payoff matrix;
- train responses against mixtures;
- use the mixture for robust evaluation.

**Decision:** strong later league architecture.

### ReBeL

Transferable ideas:

- public-belief reasoning;
- policy/value learning plus information-set search.

**Decision:** very long-term; not justified before checkpoint/fork and belief-state research.

---

# 14. Curriculum design

Curriculum learning should vary the distribution of trusted states and episodes, not alter rules or remove legal actions.

## 14.1 Recommended curriculum

| CurriculumData sourcePurposeGraduation requirement |                                              |                                              |                                                     |
| -------------------------------------------------- | -------------------------------------------- | -------------------------------------------- | --------------------------------------------------- |
| 0 — Adapter corpus                                 | Trusted recorded observations and decisions  | Validate tensorization and candidate scoring | 100% round-trip and mapping correctness             |
| 1 — Opening slices                                 | Early decisions from complete legal episodes | Learn initial combo choices                  | High-confidence teacher top-1 ≥80%, top-3 ≥95%      |
| 2 — Tactical subsequences                          | Trusted episode subsequences                 | Target interaction and continuation choices  | No full-game regression beyond 3 percentage points  |
| 3 — Full fixed matchup                             | Complete Swordsoul–Salamangreat duels        | End-to-end policy learning                   | Lower 95% win-rate bound clearly above random legal |
| 4 — Seat/start partitions                          | All four canonical partitions                | Remove seat and starting-player bias         | No partition more than 5 points below aggregate     |
| 5 — Multiple certified decks                       | Additional locked deck pairs                 | Multi-task transfer                          | Positive transfer without specialist collapse       |
| 6 — Wider archetype set                            | Broader certified distribution               | Generalization                               | Held-out deck/matchup protocol                      |

## 14.2 Important restriction

The existing fixture-loading helpers are test/conformance facilities, not an approved general board-construction API. They must not quietly become a training state editor or hidden-information bypass.

Therefore, early tactical curricula should preferably use:

- slices from full trusted episodes;
- replayed checkpoints once checkpoint support is accepted;
- separately reviewed scenario contracts.

They should not directly mutate arbitrary ocgcore state.

## 14.3 Transfer risk

A curriculum may produce policies that solve simplified openings but fail after:

- unusual interruptions;
- suboptimal earlier decisions;
- opponent deviations;
- different starting players;
- rare continuation shapes.

Every curriculum stage must therefore include evaluation on held-out full duels. Success on the curriculum distribution alone is insufficient.

---

# 15. Opponent modeling

## 15.1 Initial recommendation

Do not build an explicit opponent model for the first agent.

Use:

- known public matchup/deck configuration where contractually available;
- current visible state;
- cumulative visible history;
- optional GRU memory.

This allows implicit opponent modeling without another learned subsystem.

## 15.2 Later options

In order of increasing complexity:

1. recurrent implicit opponent model;
2. public opponent-deck or archetype embedding;
3. opponent-policy-family embedding for analysis or league conditioning;
4. prediction of next visible opponent action;
5. explicit belief over hidden cards.

The first two are reasonable when expanding beyond the fixed matchup.

An explicit hidden-card belief model should remain internal and probabilistic. It must not modify authoritative observations or be trained from privileged hidden-card labels under the normal policy-training contract.

---

# 16. Card representation and multi-deck generalization

## 16.1 Initial fixed-matchup model

Raw card IDs are acceptable for the first two-deck model because:

- the pool is fixed;
- the purpose is to validate the learning interface;
- zero-shot card generalization is not the initial scientific question.

Use:

- learned card-ID embedding;
- structured static card properties;
- a defined unknown-card embedding;
- zone and visibility context.

## 16.2 Limits of ID-only models

An ID-only model cannot infer useful behavior for a new card without training examples. It also tends to memorize deck-specific lines.

Introduce stronger structure only when moving toward additional decks:

- card type;
- attribute and race;
- level/rank/link;
- ATK/DEF;
- spell/trap subtype;
- targeting or activation tags where reliably derivable;
- script-derived effect categories under a separately versioned extractor.

## 16.3 Text models

Card text embeddings should enter only after a controlled comparison shows that:

- structured features cannot generalize to held-out cards;
- enough certified multi-deck data exists;
- text-version and localization provenance can be pinned;
- the additional inference cost is justified.

An LLM card encoder is not a post-M4 priority.

---

# 17. First policy/value network architectures

## 17.1 Architecture comparison

| BaselineApproximate parameter rangeInference costPOMDP handlingCandidate handlingExpected role |        |              |                            |                        |                       |
| ---------------------------------------------------------------------------------------------- | ------ | ------------ | -------------------------- | ---------------------- | --------------------- |
| A — Aggregated MLP                                                                             | 0.1–1M | Very low     | None                       | Candidate MLP possible | Sanity baseline       |
| B — Entity/set encoder + scorer                                                                | 1–5M   | Low–moderate | History only through input | Excellent              | **First real model**  |
| C — Entity/set encoder + GRU + scorer                                                          | 2–8M   | Moderate     | Stronger                   | Excellent              | First memory ablation |
| D — Entity Transformer + candidate cross-attention                                             | 5–30M  | High         | Context-window dependent   | Excellent              | Later                 |

## 17.2 Minimum architecture worth trying

**RECOMMENDATION: Baseline B**

A practical design is:

```text
global/decision encoder
entity token encoder
permutation-aware entity pooling
ordered event-history encoder
candidate encoder
joint observation-candidate MLP or bilinear scorer
```

Suggested hidden width: 192–256.

This is large enough to represent the fixed matchup but small enough to:

- run on CPU actors if necessary;
- batch efficiently;
- debug;
- ablate;
- train cheaply;
- avoid premature Transformer complexity.

## 17.3 Policy and value heads

Use a shared encoder with:

- one scalar logit per legal candidate;
- one scalar perspective-conditioned value estimate.

The value head should predict expected terminal outcome from the current player’s information state. It may include a pooled summary of the current candidate set because the legal domain is legitimate player-facing information.

For an off-policy Q learner, replace the policy logit with candidate-relative (Q(o,d,c\_i)) values while preserving the same candidate encoder.

## 17.4 Separate action-type heads

Do not add separate heads initially.

They can help if:

- decision-type imbalance is severe;
- one shared scorer underfits numerically distinct decision families;
- amount assignment or announcement choices need specialized output structure.

The first model should establish whether shared semantic candidate scoring is already sufficient.

---

# 18. Auxiliary tasks
