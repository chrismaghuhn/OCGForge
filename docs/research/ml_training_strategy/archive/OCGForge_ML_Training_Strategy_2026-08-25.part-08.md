- evaluation against every retained opponent snapshot and teacher anchor.

## Promotion criterion

Promote only if:

- paired aggregate win-rate difference versus the incumbent has a lower 95% bound above zero;
- point estimate improves by at least 2 percentage points;
- no partition regresses by more than 3 points;
- worst-snapshot performance does not regress by more than 3 points;
- teacher-anchor performance remains stable;
- all integrity counters remain zero.

## Anti-collapse mechanisms

- historical-opponent floor;
- teacher-anchor floor;
- immutable incumbent;
- rollback to last promoted checkpoint;
- payoff-matrix reporting;
- no latest-only opponent distribution.

Do not introduce exploiters or a full league during this experiment.

---

# 33. Readiness gates before training

## ENVIRONMENT\_READY

All must pass:

- deterministic reset;
- deterministic step;
- explicit terminal and truncation semantics;
- complete candidates for every required player decision;
- perspective-safe observation;
- exact stale-action rejection;
- zero unsupported required decisions in the locked matchup;
- zero silent automatic player decisions;
- zero candidate truncation;
- deterministic semantic-action replay;
- worker-count independence;
- typed closed failures.

## DATA\_TRUSTED

All must pass:

- versioned trajectory schema;
- complete observation/request/candidate records;
- exact chosen semantic action key;
- acting-player perspective;
- rules, deck, seed, and code provenance;
- deterministic logical ordering;
- terminal/winner integrity;
- no raw pointers or omniscient fields;
- replay validator reproduces every sampled episode;
- compression round-trip reproduces logical records;
- zero gaps, duplicate steps, or orphan actions;
- generated evidence is not hand-edited.

Only then may a trajectory be marked:

```text
DATA_TRUSTED
```

## TEACHER\_READY

All must pass:

- teacher consumes only approved player-facing inputs;
- teacher never constructs legality;
- complete-domain ranking or explicit abstention;
- exact semantic-key matching;
- deterministic rule and tie ordering;
- versioned teacher identity;
- repeated-input reproducibility;
- coverage report by decision family;
- teacher-quality evaluation separated from legality.

## TRAINING\_READY

All must pass:

- versioned model adapter;
- complete-domain ragged batching;
- candidate masks affect only padding;
- checkpoint and optimizer provenance;
- deterministic evaluation harness;
- fixed and unseen seed corpora;
- policy/opponent version recording;
- recurrent reset and burn-in semantics where applicable;
- zero unsupported/error policy;
- model-in-loop throughput characterized.

## Additional later gates

### SELF\_PLAY\_READY

Requires two independently useful specialists, immutable snapshots, promotion evaluation, and opponent-version provenance.

### SEARCH\_READY

Requires deterministic checkpoint/restore/fork, hidden-information-safe belief sampling, and paired-world noninterference.

---

# 34. OCGForge-specific findings

## BLOCKER

### BLOCKER 1 — Training before a trusted episodic trajectory boundary

Without a replay-verifiable record of the exact observation, complete domain, chosen semantic action, provenance, and result, learner metrics cannot be audited.

### BLOCKER 2 — Any privileged policy, critic, teacher, or search input

Raw `CoreHost`, actual hidden deck order, hidden opponent cards, or pointer-derived identity would corrupt the scientific validity of the policy.

### BLOCKER 3 — Candidate truncation or fixed-vocabulary loss

The observed domain can reach 1,344 candidates. Any top-k or fixed-vocabulary truncation would change the legal decision problem.

### BLOCKER 4 — Ambiguous external-teacher mapping

A WindBot or replay action that does not map to exactly one semantic candidate must not become a label.

## MAJOR

### MAJOR 1 — Assuming recurrence without evidence

It adds sequence replay, hidden-state lifecycle, and inference complexity. It must beat the feed-forward baseline.

### MAJOR 2 — Latest-only self-play

This is likely to produce forgetting and cyclic overfitting.

### MAJOR 3 — Dense board-value reward

It is likely to encode deck-specific strategic errors and reward hacking.

### MAJOR 4 — Raw observation storage

At the measured canonical shape, million-decision datasets rapidly become hundreds of gigabytes.

### MAJOR 5 — No fresh Release model-in-loop scaling evidence

Simulation-only Release results are promising, but training-path throughput remains unmeasured.

## MINOR

### MINOR 1 — Separate decision-type heads

Potentially useful, but not needed before the shared candidate scorer is tested.

### MINOR 2 — Graph encoder

Plausible improvement for relation-heavy mechanics, but not required for the first experiment.

## NOTE

NFSP, PSRO, ReBeL, public-belief search, and card-text models are relevant long-term research directions. None is a prerequisite for the first useful policy.

---

# 35. What should not be built yet?

| Postponed capabilityWhy not nowEvidence/dependency required first |                                              |                                                  |
| ----------------------------------------------------------------- | -------------------------------------------- | ------------------------------------------------ |
| Giant universal Transformer                                       | No baseline showing set/GRU failure          | Multi-deck data and architecture ablation        |
| Full distributed actor fleet                                      | No model-in-loop scaling bottleneck yet      | Release scaling and queue/inference measurements |
| Search cluster                                                    | No checkpoint/fork contract                  | `SEARCH_READY`                                   |
| Full-card generalization                                          | Only one certified matchup                   | Additional locked deck slices                    |
| Learned hidden-card belief model                                  | High privacy and evaluation complexity       | Proven recurrence benefit and belief protocol    |
| LLM/card-text encoder                                             | Expensive and unnecessary for fixed pool     | ID/structured-feature generalization failure     |
| AlphaStar-style population                                        | Excessive complexity                         | Measured cycles and multiple promoted snapshots  |
| Large replay service                                              | Schema and access patterns are not stable    | Small trusted sequence-replay experiment         |
| Artificial combo macro-actions                                    | Would obscure the semantic decision contract | Separate model-layer option study                |
| Omniscient training critic                                        | Violates the player-safe architecture        | Should not be introduced                         |
| Global fixed action vocabulary                                    | Cannot preserve complete dynamic domains     | Should not be introduced                         |

---

# 36. Direct answers

## 1. What training strategy should OCGForge use first?

A deterministic OCGForge-native teacher should generate complete-domain rankings for trusted trajectories; a small deck-specific candidate scorer should be behavior-cloned; terminal-outcome RL should then improve it against frozen opponents before snapshot self-play begins.

## 2. Should the first neural agent be trained by BC, RL, or both?

**Both, in that order.**

BC should initialize and validate the representation. RL should then test and exceed the teacher rather than treating imitation as the final objective.

## 3. Should WindBot be used?

**Yes, but only as a benchmark, heuristic source, curriculum opponent, and secondary exactly mapped label source.**

It should not be the rules authority or primary trusted OCGForge teacher.

## 4. Should the policy be recurrent?

**Not by default.**

Start feed-forward because `PlayerObservation` includes cumulative visible history. Run a matched GRU ablation. Adopt recurrence only if it improves held-out full-duel results.

## 5. Should the action architecture use candidate scoring?

**Yes.**

Score every complete legal candidate with a shared observation-candidate function. Use segmented softmax or candidate-relative Q-values. Never truncate.

## 6. Which RL algorithms deserve the first controlled comparison?

**PPO and an R2D2-style sample-reusing candidate-Q learner.**

Use the same encoder and recurrence choice. If recurrence does not win, compare PPO against a feed-forward off-policy candidate-Q baseline first. IMPALA/V-trace is the next scaling candidate.

## 7. When should self-play begin?

After:

- trusted data and training gates pass;
- BC produces a useful policy;
- RL measurably improves over BC against a frozen opponent;
- both deck specialists exist;
- snapshot evaluation is operational.

## 8. When should a league begin?

Only after a snapshot pool shows measured cyclic dominance, forgetting, or strategic overfitting and at least three promoted generations per deck exist.

## 9. When, if ever, should search/MCTS enter?

After deterministic checkpoint/restore/fork exists and a policy/value baseline is stable. Search should first be a teacher or policy-improvement operator, not the deployed policy. Naïve perfect-information MCTS should not be used.

## 10. Should the first agent be deck-specific or universal?

**Deck-specific.**

Train one Swordsoul specialist and one Salamangreat specialist. Progress later to a shared encoder with specialist heads/adapters, followed by distillation if evidence supports it.

## 11. What are the exact readiness gates before training?

The mandatory gates are:

```text
ENVIRONMENT_READY
DATA_TRUSTED
TEACHER_READY
TRAINING_READY
```

Each requires deterministic episodic semantics, complete candidates, perspective-safe observations, exact replay/provenance, trusted teacher mapping, versioned tensorization, checkpoint provenance, and zero unsupported/truncation/privacy errors.

## 12. Is Kaggle practical for the initial experiments?

**Yes for BC, offline analysis, and a small learner.**

It is only conditionally practical for online RL because notebook sessions currently provide four CPU cores, limited persistent storage, and bounded session duration. A Linux/Windows semantic-equivalence gate must precede use.

## 13. What is the single biggest training risk?

The largest risk is **training on data that looks structurally valid but is not perspective-safe, complete, provenance-bound, and deterministically replayable**.

A powerful algorithm trained on corrupted or privileged trajectories would produce scientifically meaningless strength.

## 14. What is the single highest-value next implementation milestone after M4?

A **versioned, replay-verifiable episodic reset/step and trajectory contract**.

It unlocks trusted teacher generation, BC, RL comparison, evaluation, self-play, and later checkpoint/search work without weakening engine legality or privacy.

---

# 37. Primary recommendation

```text
Environment
↓
Data
↓
Teacher/bootstrap
↓
First neural policy
↓
RL
↓
Self-play
↓
League/search/generalization
```

**Environment → Data:** the episodic environment must first expose deterministic reset/step, complete candidates, perspective-safe observations, and terminal semantics; otherwise trajectory records have no stable meaning.

**Data → Teacher/bootstrap:** teacher labels are useful only after the exact observation, request, legal domain, chosen key, and provenance can be replayed and audited.

**Teacher/bootstrap → First neural policy:** supervised candidate scoring requires a deterministic, confidence-aware label source that consumes the same legal player-facing interface as the model.

**First neural policy → RL:** RL should begin from a functioning representation and policy so that the experiment measures policy improvement rather than random combo discovery and data-path defects.

**RL → Self-play:** self-play should begin only after stationary-opponent RL demonstrates real improvement and both deck specialists are independently viable.

**Self-play → League/search/generalization:** a league is justified by measured cycles or forgetting; search requires deterministic fork/restore; broader generalization requires additional certified decks and stable specialists.

> If the OCGForge team could implement only one post-M4 capability before touching ML algorithms, it should be **a replay-verifiable episodic reset/step and trajectory contract** because **every teacher label, model update, RL comparison, and self-play result is scientifically meaningless unless the exact perspective-safe observation, complete candidate domain, chosen semantic action, provenance, and terminal result can be reproduced and audited**.