# OCGForge Research Task — ygo-agent Action-Space Completeness

Status: **research task only; no architecture decision or implementation authorization**.

Research date target: August 2026.

Primary repository:

- https://github.com/chrismaghuhn/OCGForge

Primary comparison repository:

- https://github.com/sbl1996/ygo-agent

Relevant engine references:

- https://github.com/edo9300/ygopro-core
- https://github.com/edo9300/edopro

## Purpose

Determine exactly what `sbl1996/ygo-agent` / `ygoenv` does when a Yu-Gi-Oh! player decision has more legal choices than its configured `max_options` value, and compare that behavior with OCGForge's requirement to preserve the complete semantic `ActionCandidate` domain.

The highest-value question is:

> Does `max_options=24` expose a fundamental semantic limitation in the strongest public Yu-Gi-Oh! ML environment, or is it merely an implementation/batching detail that OCGForge can learn from?

Answer with source evidence, not intuition.

## Scope

This is a:

- source-code audit;
- action-space architecture analysis;
- legal-candidate completeness analysis;
- model-interface comparison;
- determinism/replay analysis;
- failure-mode analysis.

This is **research only**.

Do not:

- modify OCGForge;
- modify ygo-agent;
- create branches or pull requests;
- add tests;
- start training;
- benchmark large workloads;
- assume `max_options=24` means truncation;
- assume OCGForge is stronger before proving the distinction.

## OCGForge invariants

Preserve this priority when evaluating consequences for OCGForge:

```text
correctness
→ determinism
→ information safety
→ complete legal decision representation
→ replay/auditability
→ maintainability
→ performance
→ ML scale
```

Important rules:

- never truncate a legal candidate domain merely to make tensor shapes convenient;
- never fabricate legal actions;
- fail closed when behavior or legality is unproven;
- agents consume `PlayerObservation + DecisionRequest + ActionCandidate`;
- legality remains environment-owned;
- models do not reconstruct legality;
- large combinatorial decisions may use lossless semantic continuations;
- continuation steps must preserve the complete legal next-choice domain;
- semantic identity must not depend on pointers, PID, wall time, scheduling, or unordered iteration.

Do not criticize ygo-agent simply for choosing different engineering trade-offs. Determine the semantic consequences.

## 1. Verify live repository state first

For both repositories record:

```text
repository
branch/ref
exact commit SHA
inspection date
```

### OCGForge

Inspect live:

- `main`;
- open M4-related pull requests;
- current `DecisionRequest`;
- current `ActionCandidate`;
- continuation implementation;
- M4 candidate counters;
- current acceptance/status documentation.

Do not use a stale PR head if a newer one exists.

### ygo-agent

Inspect the live default branch and search the complete tree for at least:

```text
max_options
n_options
num_options
options
legal_actions
action_mask
mask
truncate
overflow
candidate
select_card
select_sum
select_unselect
combinations
```

Trace `max_options` through:

```text
training configuration
→ Python environment creation
→ C++ ygoenv environment
→ observation/action-space construction
→ engine-message handling
→ model input/output
→ selected engine response
```

## 2. Determine exactly what `max_options` controls

Find every declaration, default, constructor parameter, tensor shape, and consumer of `max_options`.

For every material use report:

```text
path
class/function
purpose
input
output
semantic effect
```

Determine whether it controls any of the following:

- encoded candidate count;
- selectable candidate count;
- neural-network logits;
- observation width;
- padding;
- temporary storage;
- engine response construction;
- combinatorial enumeration;
- only one environment variant;
- only legacy code;
- only specific training scripts.

Compare relevant implementations where present, including `ygopro`, `ygopro0`, and `edopro`, and compare current PPO/IMPALA or other training paths.

## 3. Highest-priority branch: legal choices greater than `max_options`

Find the exact code path for:

```text
legal_candidate_count > max_options
```

Determine whether the implementation:

1. throws/fails;
2. truncates;
3. silently drops candidates;
4. selects a subset;
5. sorts and keeps the first N;
6. decomposes the decision into multiple model decisions;
7. dynamically grows the representation;
8. uses another overflow representation;
9. invokes fallback behavior;
10. marks the duel unsupported;
11. relies on the supported card/deck slice never exceeding the bound;
12. behaves differently by engine message family.

Show the exact branch/loop/code that proves the behavior.

Do not infer this from variable names or README prose.

## 4. Distinguish different action counts

Do not conflate:

```text
raw ocgcore selectable items
legal combinations
model-visible actions
engine responses
continuation/model substeps
```

For each relevant message family determine which quantity `max_options` bounds.

At minimum inspect where supported:

```text
MSG_SELECT_CARD
MSG_SELECT_UNSELECT_CARD
MSG_SELECT_SUM
MSG_SELECT_TRIBUTE
MSG_SELECT_COUNTER
MSG_SELECT_OPTION
MSG_SELECT_CHAIN
MSG_SELECT_PLACE
MSG_SELECT_POSITION
MSG_SELECT_IDLECMD
MSG_SELECT_BATTLECMD
```

## 5. Combinatorial decisions

Inspect how ygo-agent handles:

- choose k cards from n;
- weighted/sum-constrained subsets;
- tribute selections;
- counter assignment;
- select/unselect protocols;
- ordered selections.

Follow helpers such as `combinations(...)`, `combinations_with_weight(...)`, and related functions to their consumers.

Determine whether ygo-agent:

- enumerates complete combinations;
- exposes one item at a time;
- converts one engine decision into multiple model decisions;
- caps combination count;
- heuristically reduces combinations;
- rejects overflow.

A lossless multi-step decomposition is **not** candidate truncation.

## 6. Semantic completeness model

Define:

```text
C_engine = all legal engine responses/choices for the player decision
C_model  = all choices actually exposed to the model
```

For each investigated family determine whether:

```text
C_model == C_engine
```

or classify the transformation as:

```text
LOSSLESS_DECOMPOSITION
LOSSLESS_REENCODING
FILTERED_BY_RULE
FILTERED_BY_HEURISTIC
TRUNCATED
UNSUPPORTED
UNKNOWN
```

Do not call a reduced representation defective when it is proven lossless.

## 7. Investigate OCGForge's large-domain evidence

OCGForge M4 evidence has reported a candidate maximum on the order of:

```text
candidate_max = 1344
```

Verify the live/current evidence before using the number.

Determine:

- which engine message/decision family produced the maximum;
- whether it was one `DecisionRequest`;
- whether these were truly distinct semantic candidates;
- whether continuations were involved;
- whether the count is before or after continuation decomposition;
- whether a semantically comparable state can occur in ygo-agent.

Do **not** conclude anything from `24 < 1344` until both quantities are shown to mean the same thing.

If the exact 1,344 case cannot be reconstructed from committed evidence, say so and report the strongest available evidence.

## 8. Neural policy representation

Determine whether ygo-agent is conceptually using:

```text
fixed 24-logit action head
```

or:

```text
candidate-relative scorer padded to 24
```

or another design.

Inspect:

- observation tensors;
- option/candidate tensors;
- masks;
- candidate embeddings;
- output logits;
- padding behavior;
- invalid-action masking;
- overflow behavior.

If candidate-relative scoring is used, document the state representation, candidate representation, joint scoring function, mask semantics, and padding/overflow behavior.

Compare this with the proposed OCGForge form:

```text
score(PlayerObservation, DecisionRequest, ActionCandidate_i)
```

for every legal candidate.

## 9. Batching trade-off

If ygo-agent intentionally uses a fixed maximum width, determine why.

Potential motivations include:

- fixed-shape EnvPool tensors;
- JAX/XLA compilation;
- GPU batching;
- memory allocation;
- network simplicity;
- empirical supported-deck limits.

Distinguish an **ML batching constraint** from a **game-semantics constraint**.

Evaluate how OCGForge could preserve complete domains efficiently using techniques such as:

```text
flat candidate tensor + request offsets
segmented softmax
ragged batching
candidate-count bucketing
padding only inside a batch
optional chunking for extreme domains
```

Do not recommend candidate truncation.

## 10. Extreme-domain computational cost

For a domain near 1,344 candidates, evaluate the cost of:

```text
h = encode_observation(observation)          # once
candidate_i = encode_candidate(candidate_i) # N times
score_i = scorer(h, candidate_i)             # N times
```

Estimate implications for:

- inference latency;
- candidate encoder cost;
- memory;
- batching;
- padding waste;
- segmented softmax.

Do not assume the full observation encoder is rerun once per candidate.

Also compare O(N) independent candidate scoring against O(N²) candidate-set self-attention. State whether OCGForge should prefer O(N) scoring initially unless evidence shows cross-candidate attention is necessary.

## 11. Continuation architecture comparison

Compare ygo-agent's treatment of compound selections with OCGForge adapter-local continuations.

Determine whether ygo-agent has an equivalent concept and answer:

- is it model-visible?;
- is the engine kept paused?;
- does only the terminal subdecision emit the engine response?;
- can stale partial selections be rejected?;
- can continuation-local domains exceed `max_options`?;
- is legality recomputed or reconstructed during partial selection?;
- does the decomposition change the learning problem?

Classify differences as lossless sequential representation versus legal-domain reduction.

## 12. Supported-slice limitations versus action-space limitations

ygo-agent documents restricted card/deck support.

Determine whether large domains are avoided because:

- supported decks rarely exceed the cap;
- unsupported cards are excluded;
- message families are excluded;
- deck construction is constrained;
- candidate generation is reduced.

Keep **card-support limitations** and **action-representation limitations** separate.

## 13. Information safety, legality ownership, determinism, and replay

Determine who owns legality in ygo-agent:

- engine;
- environment adapter;
- model;
- heuristic filter.

Check whether any action reduction uses information not available to the acting player.

Determine how model-visible option ordering is derived and whether the same logical decision gives stable semantic meaning to option index N.

Check for dependencies on:

- pointer/object identity;
- unordered containers;
- incidental insertion order;
- card sequence;
- explicit semantic sorting.

For trajectories, determine whether storing only:

```text
action = 7
```

is sufficient to reconstruct the exact semantic action. If not, identify the required candidate/option context.

Compare with OCGForge's intended replay identity:

```text
complete candidate domain
+
chosen semantic action key
```

## 14. Required source-level case studies

Trace at least three real/supported cases end-to-end:

### Case A — small domain

Approximately 2–5 legal choices.

Trace:

```text
engine
→ environment candidate representation
→ tensor/mask
→ model action
→ engine response
```

### Case B — near `max_options`

Approximately 20–24 choices.

Trace the same path.

### Case C — exceeding `max_options`

A supported state with >24 legal choices if one can be produced or proven from source/tests.

This case is mandatory when evidence exists.

If no supported case can exceed the configured limit, distinguish:

```text
NOT OBSERVED
```

from:

```text
PROVEN IMPOSSIBLE UNDER SUPPORTED SLICE
```

## 15. Tests and evidence gaps

Inspect ygo-agent tests for behavior at:

```text
max_options
max_options + 1
large combinatorial domain
```

If no test exists, classify that as an evidence gap, not automatically a bug.

For OCGForge identify evidence/tests for:

- no candidate truncation;
- continuation completeness;
- stable semantic action identity;
- large candidate domains.

## 16. Required comparison table

Produce:

| Property | ygo-agent / ygoenv | OCGForge | Consequence |
|---|---|---|---|
| Legality owner | | | |
| Candidate representation | | | |
| Fixed maximum | | | |
| Overflow behavior | | | |
| Combinatorial handling | | | |
| Continuations | | | |
| Complete-domain guarantee | | | |
| Candidate identity | | | |
| Candidate ordering | | | |
| Replay identity | | | |
| Hidden-info boundary | | | |
| Batching model | | | |
| Failure behavior | | | |
| Largest proven domain | | | |

Use `PROVEN`, `INFERRED`, and `UNKNOWN` where appropriate.

## 17. Classify differences carefully

For every important ygo-agent behavior classify it as one of:

```text
SOUND_DIFFERENT_DESIGN
PERFORMANCE_TRADEOFF
SUPPORTED_SLICE_ASSUMPTION
SEMANTIC_LIMITATION
AUDITABILITY_LIMITATION
POTENTIAL_BUG
UNKNOWN
```

A fixed tensor width of 24 is not a semantic defect if larger decisions are losslessly decomposed.

It becomes a semantic limitation only when legal choices become unrepresentable, silently removed, or semantically ambiguous.

## 18. What should OCGForge copy or avoid?

Rank useful ideas from ygo-agent:

```text
P0 — should influence first model adapter
P1 — likely useful
P2 — later optimization
P3 — interesting only
```

Potential areas:

- compact option encoding;
- vectorized environment layout;
- masks;
- history encoding;
- JAX-friendly batching;
- actor/learner integration;
- inference batching.

Separately list what OCGForge should **not** copy, but only when proven by source evidence. Possible examples include hard truncation, ambiguous option indexing, non-fail-closed overflow, hidden legality reconstruction, or fixed tensor constraints leaking into gameplay semantics.

Do not recommend importing code without a separate license/semantics/maintenance review.

## 19. OCGForge findings severity

Classify consequences for OCGForge as:

**BLOCKER** — would violate legal-action completeness, information safety, semantic determinism, or replay integrity.

**MAJOR** — could materially degrade learning correctness, supported-domain fidelity, generalization, or batching architecture.

**MINOR** — useful improvement with limited strategic impact.

**NOTE** — interesting implementation difference.

## 20. Evidence discipline

Every material technical claim must cite:

```text
repository
exact commit
path
class/function
relevant code behavior
```

Clearly distinguish:

```text
FACT
INFERENCE
RECOMMENDATION
```

Do not state:

> ygo-agent truncates legal actions to 24

unless source/tests prove it.

Do not state:

> OCGForge supports arbitrary candidate counts

merely because a domain of 1,344 was observed.

Limit every claim to the contracts and evidence actually proven.

## 21. Required primary verdict

Conclude with exactly one primary category:

### A — Equivalent semantics

`max_options` is only a physical representation/batching parameter and ygo-agent losslessly preserves complete legal choices.

### B — Lossless but differently decomposed

ygo-agent exposes a bounded model-visible domain but losslessly decomposes larger engine choices into multiple decisions.

### C — Supported-slice bounded

The supported card/deck slice effectively stays within the configured bound, but no general completeness guarantee exists.

### D — Legal-domain reduction

ygo-agent can encounter larger legal domains and removes/suppresses choices before model selection.

### E — Fail-closed overflow

Larger domains are rejected rather than silently reduced.

### F — Mixed by decision family

Different message families use materially different behavior.

### G — Insufficient evidence

The exact behavior cannot be proven from source/tests.

If behavior differs by environment implementation, provide the best overall category plus per-implementation categories.

## 22. Required direct answers

End by answering:

1. What exactly does `max_options=24` control?
2. Can ygo-agent represent more than 24 legal choices?
3. If yes, how?
4. If no, what happens to choice 25+?
5. Is any legal candidate silently dropped?
6. Is large-action handling lossless?
7. Does behavior differ by decision family?
8. Does behavior differ between environment implementations?
9. What is the largest candidate domain proven in ygo-agent?
10. What is the largest candidate domain proven in OCGForge?
11. Are those counts semantically comparable?
12. Does ygo-agent use candidate scoring or a fixed action vocabulary?
13. Is option ordering semantically stable?
14. Can a ygo-agent trajectory reconstruct the exact semantic action?
15. Does OCGForge's continuation architecture solve a problem ygo-agent handles differently?
16. What should OCGForge copy from ygo-agent?
17. What should OCGForge explicitly avoid copying?
18. Is OCGForge's complete-domain architecture materially stronger, or merely different?
19. What is the largest unresolved evidence gap?
20. What single experiment/test should OCGForge run next because of this comparison?

## 23. Required OCGForge model-adapter recommendation

Give one specific recommendation for the first model-facing action adapter and evaluate it against what ygo-agent actually does.

The candidate architecture to assess is:

```text
ObservationEncoder once
        ↓
shared observation embedding

CandidateEncoder × N
        ↓
N candidate embeddings

shared scorer
        ↓
N logits

segmented softmax over exactly N legal candidates
```

Gameplay semantics must not impose a hard global N cap.

Operational batching may use:

```text
ragged arrays
request offsets
candidate-count buckets
padding inside a batch only
optional chunking for extreme N
```

Do not recommend candidate truncation.

## Stop condition

STOP after the research report.

Do not:

- modify either repository;
- implement ragged batching;
- implement a candidate scorer;
- add tests;
- open issues or PRs;
- begin ML training.

The next step is a separate OCGForge architecture decision based on the evidence.
