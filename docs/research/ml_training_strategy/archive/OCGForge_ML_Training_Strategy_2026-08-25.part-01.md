# OCGForge Research Report

## Best ML Training Strategy for a Deterministic Yu-Gi-Oh! Environment

**Research date:** 25 August 2026
**Scope:** research, architecture, algorithm comparison, and roadmap recommendation only
**Repository changes:** none

---

## Executive decision

**RECOMMENDATION**

OCGForge should not begin with pure RL, latest-policy self-play, MCTS, a universal Transformer, or WindBot imitation.

The strongest practical progression is:

1. Establish a replay-verifiable episodic and trajectory boundary.
2. Build an OCGForge-native deterministic teacher that ranks the complete legal candidate set.
3. Train a small deck-specific entity/set encoder with a candidate-scoring policy by behavior cloning.
4. Test whether a GRU materially improves over the feed-forward policy.
5. Fine-tune with terminal-outcome RL.
6. Compare a simple on-policy method against a sample-reusing off-policy method under controlled conditions.
7. Introduce frozen-snapshot self-play.
8. Add a league only after cyclic behavior or forgetting is measured.
9. Add search only after deterministic checkpoint/fork support exists.
10. Progress toward multiple deck specialists and then a shared base model.

The supplied working hypothesis is therefore **substantially correct**, but it needs three important corrections:

- recurrence is an experimental ablation, not a mandatory starting architecture;
- WindBot should not be the primary trusted teacher;
- search and league training should enter later than the hypothesis might imply.

The most important post-M4 capability is **not a neural network**. It is a **versioned episodic trajectory boundary with deterministic replay validation**.

---

# 1. Evidence basis and live repository status

## 1.1 Exact live state inspected

**FACT**

The live `main` branch was inspected at:

```text
588f02b4ef879fee999c921c114937a6a1e48557
```

That commit is the documentation-foundation merge from 20 August 2026.

The current M4 implementation is ahead of `main` in open draft PR #3:

```text
PR:       #3 — M4: add parallel simulation foundation
Base:     588f02b4ef879fee999c921c114937a6a1e48557
Head:     ff58699e8726e128ff1e5a6fb7a57609c9f73c63
State:    open
Draft:    true
Merged:   false
```

The PR reports 27 commits and 39 changed files.

Therefore:

- repository-specific architecture claims in this report use PR head `ff58699…`;
- `main` must not be described as already containing M4;
- later M4.3 performance handoffs are treated as supplied historical evidence, not merged live-branch state.

That separation is required by the project’s update policy: live GitHub owns current status, while the static project pack owns stable architecture, accepted contracts, and historical evidence.

## 1.2 Actual M4 state

**FACT**

At PR head `ff58699…`, `docs/m4/M4_BASELINE.md` records:

```text
M4 BASELINE PASS — PERFORMANCE AUDIT READY
```

The live PR evidence includes:

- persistent worker processes;
- fresh duel state per job;
- deterministic job identity;
- semantic equivalence across 1, 2, 4, 8, 16, 32, and 64 workers;
- zero unsupported, automatic, truncated, core-error, worker-error, retry, or handshake counters;
- a measured peak of 148.221 interactive decisions/s at 16 workers in the baseline build;
- a maximum observed candidate-domain size of 1,344;
- 187,025 candidates across 39,519 decision requests, or approximately 4.73 candidates per request on average.

The M4 architecture uses persistent native workers, but each job receives a fresh private `CoreHost`/`OCG_Duel`. Worker scheduling, PID, and completion order do not define semantic job identity.

## 1.3 Later supplied performance evidence

**FACT — historical handoff, not live** **`main`**

M4.3.3’s supplied Release characterization used source commit:

```text
a61e3465b5903b21ad5eae17f45889e2a2fccef3
```

and measured 0.492809 games/s for 16 games on one worker under ordinary Release compilation. Debug and Release produced matching semantic gameplay results and all 9,908 observation hashes.

That characterization found:

- observation construction remained about 91.5% of worker time;
- canonical serialization represented 68.42% of observation time;
- observation hashing represented 15.60%;
- query decoding represented 9.08%;
- Release was 11.81× faster than the corresponding fresh Debug sample.

M4.3.4 then characterized approximately 135.8 KiB of canonical observation data per observation, dominated by cumulative visible-event history and entities. M4.3.5’s reserve-backed output experiment produced only about 0.41% serializer improvement and 0.18% worker improvement and was correctly rejected and reverted.

No verifiable M4.3.6 or completed direct-writer result was found in the live repository or supplied handoff sources. This report therefore makes no claim about such an experiment.

## 1.4 Immediate implication

**INFERENCE**

M4 has established a credible deterministic simulation foundation, but the repository does not yet expose the complete environment/data boundary required for training:

- no accepted reset/step episodic API;
- no versioned policy-trajectory contract;
- no model-facing tensor adapter;
- no trusted teacher interface;
- no checkpoint/fork contract;
- no replay buffer or evaluation harness for learned policies.

Those are explicitly listed as future architecture areas in the project roadmap rather than accepted current capabilities.

---

# 2. What learning problem is OCGForge solving?

## 2.1 Formal characterization

**FACT**

At the full-game level, the strongest useful formulation is:

> a two-player, zero-sum, partially observable stochastic game represented as an extensive-form imperfect-information game with chance events.

The stochasticity includes shuffled deck order and any rule-governed random effects. OCGForge can reproduce that stochasticity deterministically from seeds, but this does not turn the player’s learning problem into a fully observable MDP.

For a player facing a fixed opponent policy, the player’s problem can be reduced to a POMDP. Once both agents learn or the opponent is sampled from a pool, the correct abstraction is a partially observable stochastic game rather than an ordinary stationary POMDP.

## 2.2 Three state concepts must remain separate

### True engine state

This is the authoritative omniscient state owned by ocgcore and `CoreHost`:

- exact deck order;
- all hidden card identities;
- engine objects;
- Lua state;
- private effect state;
- pending engine messages;
- RNG state.

It is not a permitted policy, value, teacher, or search-model input.

### Player information state

This is everything legitimately available to one player:

- current `PlayerObservation`;
- current `DecisionRequest`;
- complete legal `ActionCandidate` domain;
- public and private-to-that-player visible history;
- publicly known experiment metadata where contractually authorized.

`ygo.player_observation.v1` already defines the intended perspective-safe boundary, including zones, entities, relationships, chain state, match and decision context, visible events, canonical bytes, and an observation hash.

### Model-internal belief or memory

A model may internally infer:

- whether a hand trap is likely;
- which searched cards remain unplayed;
- likely opponent combo lines;
- opponent style or policy family;
- probabilities over remaining hidden cards.

That state is neither authoritative nor guaranteed correct. It belongs inside the model and must never be written back into `PlayerObservation` as a fact.

## 2.3 Consequences

A valid value function estimates:

```text
V(player information state)
```

not:

```text
V(omniscient engine state)
```

A valid policy estimates:

```text
π(candidate_i | PlayerObservation, DecisionRequest, complete candidates)
```

A centralized or asymmetric critic that consumes hidden engine state would conflict with OCGForge’s information-safety boundary. Even if used only during training, it would introduce privileged supervision and weaken auditability.

Partial observability also affects replay. A transition is not adequately identified by a small board snapshot plus action. For recurrent or history-sensitive learning, replay must preserve player perspective, ordering, sequence boundaries, policy versions, and enough history or burn-in to reconstruct the model’s information state.

---

# 3. Action-space architecture

This is the clearest architectural decision in the report.

## 3.1 Required policy form

**RECOMMENDATION**

The policy should compute:

[
s\_i = f\_\theta(o, d, c\_i)
]

for every legal candidate (c\_i), followed by a segmented softmax over exactly the candidate domain belonging to that request:

# [ \pi(c\_i\mid o,d,C)

\frac{\exp(s\_i)}
{\sum\_{c\_j\in C}\exp(s\_j)}
]

where:

- (o) is `PlayerObservation`;
- (d) is `DecisionRequest`;
- (C) is the complete legal candidate set;
- (c\_i) is one semantic `ActionCandidate`.

Logits should not exist for nonexistent or illegal actions. Padding masks may be used only to batch ragged candidate sets; padding must never remove, replace, or invent a legal candidate.

This matches the project’s public protocol: supported requests expose complete semantic candidates, large combinatorial decisions use PICK/FINISH-style continuations, and the model must not independently reconstruct legality.

## 3.2 Comparison of action architectures

| ApproachSemantic compatibilityVariable-domain scalingUnseen actionsOCGForge decision |                              |                                   |                               |                                     |
| ------------------------------------------------------------------------------------ | ---------------------------- | --------------------------------- | ----------------------------- | ----------------------------------- |
| Global fixed vocabulary                                                              | Poor                         | Constant output, huge sparse head | Poor                          | Reject                              |
| Masked fixed vocabulary                                                              | Better, but vocabulary-bound | Large sparse head                 | Poor                          | Reject as primary architecture      |
| Candidate scoring                                                                    | Excellent                    | Linear in legal candidate count   | Good with structured features | **Use first**                       |
| Candidate embeddings                                                                 | Excellent as part of scoring | Linear                            | Better generalization         | **Use**                             |
| Pointer-network selection                                                            | Strong                       | Linear or attention-based         | Good                          | Compatible, but unnecessary first   |
| Candidate cross-attention                                                            | Strong                       | Potentially expensive             | Strong                        | Later architecture                  |
| Autoregressive selection                                                             | Useful for sequences         | Sequential inference              | Good                          | Only at model layer where justified |
| Hierarchical action heads                                                            | Can reduce complexity        | Depends on hierarchy              | Mixed                         | Optional later                      |
| Continuation-aware scoring                                                           | Excellent                    | Matches protocol decomposition    | Strong                        | **Required**                        |

Pointer Networks were explicitly designed for variable output dictionaries whose valid classes depend on input length. Deep Sets and Set Transformers provide permutation-aware handling of variable sets. These ideas support candidate-relative scoring, but do not require a large pointer or Transformer architecture for the first model. ([arXiv](https://arxiv.org/abs/1506.03134 "https://arxiv.org/abs/1506.03134"))

Large-action-space research also supports learning structured action representations instead of treating actions as unrelated opaque indices. ([Proceedings of Machine Learning Research](https://proceedings.mlr.press/v97/tennenholtz19a.html "https://proceedings.mlr.press/v97/tennenholtz19a.html"))

## 3.3 Candidate encoding

The first candidate encoder should derive features from semantic fields such as:

- `ActionKind`;
- decision/message family;
- source and target player;
- source and target zone;
- sequence and position;
- referenced visible entity/card;
- amount or declared value;
- continuation identifier and stage;
- `submits_engine_response`;
- PICK, FINISH, CANCEL, and assignment semantics;
- phase and chain context;
- whether the action references the currently encoded entity set.
