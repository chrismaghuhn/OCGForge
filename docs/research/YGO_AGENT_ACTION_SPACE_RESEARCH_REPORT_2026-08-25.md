# OCGForge Research Report

## `ygo-agent max_options=24` versus OCGForge Complete Candidate Domains

**Inspection date:** 25 August 2026
**Task scope:** research, source-code audit, action-space analysis, and recommendation only
**Repository modifications:** none

---

# 1. Executive verdict

## Primary verdict: **F — Mixed by decision family**

`max_options=24` is **not merely observation padding** in the currently inspected `sbl1996/ygo-agent` code.

It begins as a sensible fixed-shape ML configuration:

```text
actions tensor width
scalar action-index range
number of policy logits
padding width
```

However, in all three registered ygoenv implementations inspected—

```text
ygoenv/ygopro
ygoenv/ygopro0
ygoenv/edopro
```

—the runtime option vector is explicitly resized to `max_options` before it is exposed to the model:

```cpp
if (n_options > max_options()) {
    legal_actions_.resize(max_options());
}
```

or, in the older string-option implementations:

```cpp
if (n_options > max_options()) {
    options_.resize(max_options());
}
```

That operation retains the insertion-order prefix and removes every option after the configured bound. It does not throw, paginate, dynamically grow, record an overflow status, or mark the duel unsupported.

The full answer is nevertheless **mixed by decision family**, rather than simply “ygo-agent caps every Yu-Gi-Oh! decision at 24”:

- Some direct engine menus are flattened one-to-one and then truncated if they exceed 24.
- Some combinatorial engine decisions are decomposed into multiple model decisions and may therefore represent more than 24 original engine responses without presenting more than 24 choices at once.
- Some families are supported only under narrow assumptions and throw otherwise.
- Some player decisions are reduced by adapter policy, such as disallowing unselection.
- Some are resolved automatically without model input, such as the inspected counter-allocation and card-ordering paths.
- EDOPro and modern YGOPro differ materially in how they represent combinatorial selections, even though both ultimately truncate the model-visible vector.

Therefore:

> **`max_options=24`** **is a performance-oriented fixed-shape parameter whose overflow policy leaks into game semantics. It is an implementation detail when the post-decomposition domain fits the bound, but a genuine semantic limitation when it does not.**

OCGForge’s supported-family contract is materially stronger because it prohibits this leakage: it requires complete semantic candidates, explicit continuations, semantic action keys, stale-action rejection, one exact terminal engine response, and fail-closed behavior when completeness is unproven.

That does **not** establish that OCGForge is globally superior. OCGForge currently has a narrower certified gameplay slice and no completed neural model adapter, while ygo-agent has a mature fixed-shape vectorized ML stack and trained-policy infrastructure. The stronger OCGForge conclusion is specifically about **supported-domain semantic completeness and auditability**.

---

# 2. Live repository state inspected

Static OCGForge project sources were used only for durable architecture and invariants. Current branch, PR, and acceptance status were taken from live GitHub, as required by the project update policy.

| RepositoryBranch/refExact commitStatus on 25 August 2026 |                                                  |                                            |                                     |
| -------------------------------------------------------- | ------------------------------------------------ | ------------------------------------------ | ----------------------------------- |
| `chrismaghuhn/OCGForge`                                  | `main`                                           | `588f02b4ef879fee999c921c114937a6a1e48557` | Current default branch              |
| `chrismaghuhn/OCGForge`                                  | `chris/m4-parallel-simulation-foundation`, PR #3 | `f9f10177ee48e244a491fb94ef63752ddf5ddfed` | Current relevant open draft M4 head |
| `sbl1996/ygo-agent`                                      | `main`                                           | `dbf5142d49aab2e6beb4150788d4fffec39ae3e5` | Current default branch              |

OCGForge `main` remains behind the active M4 implementation. The M4 audit therefore uses PR #3 head `f9f10177…`, not the older PR head from the previous report.

The inspected ygo-agent head is `dbf5142d…`; its current commit adds card/script preloading and `MSG_ANNOUNCE_CARD` support.

---

# 3. What exactly does `max_options` control?

## 3.1 End-to-end trace

The modern default training path can be traced as follows:

```text
scripts/cleanba.py
    Args.max_options = 24
        ↓
    ygoenv.make(... max_options=args.max_options)
        ↓
YGOPro-v1 registration
        ↓
ygoenv/ygopro/ygopro.h
    YGOProEnvFns::StateSpec
    YGOProEnvFns::ActionSpec
        ↓
engine-message handler builds legal_actions_
        ↓
YGOProEnvImpl::WriteState
    legal_actions_.resize(max_options)
        ↓
obs:actions_[24, 12]
        ↓
ActionEncoderV1
        ↓
one candidate embedding per row
        ↓
Actor dot-product scorer
        ↓
24 padded candidate-relative logits
        ↓
selected index
        ↓
callback_(index)
        ↓
ocgcore response
```

`cleanba.py` defaults to `YGOPro-v1`, sets `max_options=24`, and passes it directly into `ygoenv.make`.

Older or alternative training scripts use the same configuration idea. The inspected IMPALA and Torch PPO defaults use `YGOPro-v0` with `max_options=24`; repository search also found the same setting exposed by evaluation, battle, CleanBA variants, PPO variants, and other training entry points.

## 3.2 Source-use inventory

| File / symbolPurposeSemantic effect          |                                        |                                          |
| -------------------------------------------- | -------------------------------------- | ---------------------------------------- |
| `scripts/cleanba.py`, `Args.max_options`     | Training default                       | Sets width to 24                         |
| `scripts/cleanba.py`, `make_env`             | Python → C++ environment configuration | Passes width unchanged                   |
| `scripts/impala.py`, `scripts/torch/ppo.py`  | Alternative training defaults          | Also use 24                              |
| `ygopro/registration.py`                     | Registers `YGOPro-v1`                  | Selects modern implementation            |
| `ygopro0/registration.py`                    | Registers `YGOPro-v0`                  | Selects older implementation             |
| `edopro/registration.py`                     | Registers `EDOPro-v0`                  | Selects EDOPro implementation            |
| `YGOProEnvFns::DefaultConfig`                | Environment fallback                   | Default is 16 if not overridden          |
| `YGOProEnvFns::StateSpec`                    | Observation shape                      | `actions_` is `[max_options, 12]`        |
| `YGOProEnvFns::ActionSpec`                   | External action space                  | Scalar index `0..max_options-1`          |
| `RandomAI`                                   | Random baseline                        | Draw range is limited by `max_options`   |
| `YGOProEnvImpl::WriteState`                  | Model-facing state publication         | **Drops tail options**                   |
| `ActionEncoderV1`                            | Neural candidate encoding              | Encodes one row per retained option      |
| `Actor`                                      | Policy head                            | Produces one logit per retained row      |
| `EDOProEnvFns::StateSpec`                    | EDOPro observation shape               | `[max_options, 10 + 2×max_multi_select]` |
| `YGOPro0::WriteState` / `EDOPro::WriteState` | Older environment publication          | Same tail-dropping behavior              |

The modern C++ environment’s fallback configuration is 16, while training scripts normally override it to 24. `StateSpec` uses it as the first tensor dimension and `ActionSpec` uses it as the scalar action range.

The README’s current training example confirms the resulting physical shape:

```text
actions_: Box(..., (24, 12), uint8)
```

## 3.3 Important secondary effect: random baselines

`RandomAI` constructs its random distribution over:

```cpp
0 .. max_options - 1
```

and then takes the result modulo `actions.size()`. If the actual vector has more than `max_options` entries, the random agent also cannot choose any tail option.

Thus `max_options` is not merely a neural-network dimension. It constrains the inspected random baseline as well.

Human-mode selection is different: the human player is shown the actual internal vector and validates directly against `actions.size()`. Therefore the cap principally affects model-facing and random-agent paths rather than the underlying engine’s ability to emit larger menus.

## 3.4 Metadata off-by-one note

The inspected `info:num_options` state specification declares an apparent range ending at:

```text
max_options - 1
```

while `WriteState` can assign:

```text
num_options = max_options
```

when the vector contains exactly 24 retained options.

This appears to be an off-by-one inconsistency in the metadata specification. I found no evidence that the JAX policy relies on this bound—the model derives its action mask from padded action rows—but it is a **POTENTIAL_BUG**, not part of the central truncation conclusion.

---

# 4. Exact overflow behavior

## 4.1 Modern `ygoenv/ygopro`

**FACT**

In `YGOProEnvImpl::WriteState`, source range inspected around `ygopro.h:2320–2405`:

1. `n_options` is read from `legal_actions_.size()`.
2. The player observation is constructed.
3. The source explicitly notes that options cannot be shuffled because callback indices must remain stable.
4. If `n_options > max_options()`, the vector is resized.
5. `num_options` is recomputed from the shortened vector.
6. Only the retained prefix is encoded into `obs:actions_`.

There is:

- no exception;
- no warning;
- no overflow counter;
- no continuation created solely because of overflow;
- no random subset;
- no sorting before retention;
- no dynamic reallocation of the model tensor;
- no unsupported result;
- no second page.

The model sees only:

```text
legal_actions_[0:max_options]
```

## 4.2 `ygoenv/ygopro0`

The older environment performs the same operation on its string `options_` vector before action tensorization:

```cpp
if (n_options > max_options()) {
    options_.resize(max_options());
}
```

It then records the post-resize count and publishes only those rows.

## 4.3 `ygoenv/edopro`

The EDOPro implementation repeats the same prefix-resize behavior before calling `_set_obs_actions`.

## 4.4 Exact semantic consequence

For a direct one-row-per-engine-choice family, define:

```text
C_adapter = [c0, c1, ..., cN-1]
```

If:

```text
N > max_options
```

then the model receives:

```text
C_model = [c0, ..., cmax_options-1]
```

and:

```text
cmax_options ... cN-1
```

become unreachable.

This is a **prefix truncation**, not candidate sampling.

The retained choices remain callback-correct because the vector is not reordered. Completeness is what is lost.

---

# 5. Different action counts that must not be conflated

The comparison becomes clearer with five separate domains.

## 5.1 Raw engine items

Examples:

- cards listed in `MSG_SELECT_CARD`;
- activatable effects in `MSG_SELECT_CHAIN`;
- numbers in `MSG_ANNOUNCE_NUMBER`;
- cards that may be toggled in `MSG_SELECT_UNSELECT_CARD`.

Call this:

```text
I_engine
```

## 5.2 Exact legal engine responses

For a “choose two from ten” message, the ten raw items do not imply ten final responses. They imply:

(102)=45\binom{10}{2}=45

possible final subset responses.

Call this:

```text
C_engine
```

## 5.3 Adapter-local actions

The adapter may represent those 45 responses as:

- 45 flat combination options;
- ten first-card choices followed by nine second-card choices;
- a PICK/FINISH continuation;
- an automatic heuristic choice;
- an unsupported error.

Call the current local domain:

```text
C_step
```

## 5.4 Model-visible tensor rows

After fixed-width publication:

```text
C_model
```

contains only the action rows actually reachable by the network.

For ygo-agent:

∣Cmodel∣≤max\_options|C\_{\text{model}}| \leq \texttt{max\\_options}

at every model step.

## 5.5 Engine response emitted

The selected model index eventually becomes:

- an integer response;
- a byte response;
- an index vector;
- a combination response;
- a finish/cancel response.

Call this:

```text
R_engine
```

The important distinction is:

> A 45-response combinatorial choice may be represented losslessly using ten or fewer choices at each sequential model step. Conversely, a direct 25-option menu cannot be represented by a 24-row tensor unless it is explicitly decomposed or the width is increased.

---

# 6. Decision-family audit

The following classifications describe the modern `ygoenv/ygopro` path at commit `dbf5142d…`.

| Engine familyAdapter representation`C_model` versus `C_engine`Overflow / unsupported behavior |                                                                             |                                            |                                                                         |
| --------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- | ------------------------------------------ | ----------------------------------------------------------------------- |
| `MSG_SELECT_YESNO`                                                                            | Two direct rows                                                             | `LOSSLESS_REENCODING`                      | Always below 24                                                         |
| `MSG_SELECT_EFFECTYN`                                                                         | Activate / cancel                                                           | `LOSSLESS_REENCODING`                      | Always below 24                                                         |
| `MSG_SELECT_POSITION`                                                                         | One row per position                                                        | `LOSSLESS_REENCODING`                      | At most four ordinary positions                                         |
| `MSG_SELECT_OPTION`                                                                           | One row per option index                                                    | Equal until cap                            | `TRUNCATED` above cap                                                   |
| `MSG_SELECT_CHAIN`                                                                            | One row per chainable item plus optional cancel                             | Equal until cap                            | `TRUNCATED` above cap                                                   |
| `MSG_SELECT_IDLECMD`                                                                          | Concatenated summon, special summon, reposition, set, activate, phase menus | Equal until cap                            | `TRUNCATED` above cap                                                   |
| `MSG_SELECT_BATTLECMD`                                                                        | Activations, attackers, phase exits                                         | Equal until cap                            | `TRUNCATED` above cap                                                   |
| `MSG_SELECT_PLACE`                                                                            | One row per available zone                                                  | Equal only when requested count is one     | Count other than one throws; rows truncate above cap                    |
| `MSG_SELECT_DISFIELD`                                                                         | Same general zone representation                                            | Narrow supported case                      | Unsupported counts / overflow risks                                     |
| `MSG_ANNOUNCE_NUMBER`                                                                         | One row per supplied number                                                 | Adapter-restricted                         | Values outside implemented range throw                                  |
| `MSG_ANNOUNCE_ATTRIB`                                                                         | One row per attribute                                                       | Supported only for count one               | Other counts throw                                                      |
| `MSG_ANNOUNCE_CARD`                                                                           | One row per parsed explicit card code                                       | Equal for accepted opcode format until cap | Other opcode forms throw; rows truncate above cap                       |
| `MSG_SELECT_CARD`                                                                             | Sequential item selection with internal multi-select state                  | Potentially `LOSSLESS_DECOMPOSITION`       | `min=0` unsupported; cancel ignored; each local domain can truncate     |
| `MSG_SELECT_TRIBUTE`                                                                          | Sequential item selection                                                   | Lossless only under narrow assumptions     | Requires `min==max` and unit weights; otherwise throws                  |
| `MSG_SELECT_SUM`                                                                              | Enumerate valid combinations, then sequentialize by viable prefixes         | Potentially `LOSSLESS_DECOMPOSITION`       | Only one mode; limited mandatory selections; local domains can truncate |
| `MSG_SELECT_UNSELECT_CARD`                                                                    | Selectable items plus optional finish                                       | `FILTERED_BY_HEURISTIC`                    | Unselect choices removed; cancel ignored; remaining rows may truncate   |
| `MSG_SELECT_COUNTER`                                                                          | No model decision; deterministic allocation                                 | `FILTERED_BY_HEURISTIC`                    | More than two source cards throws                                       |
| `MSG_SORT_CARD`                                                                               | No meaningful model ordering                                                | Adapter fallback                           | Automatically submits fallback response                                 |

The direct chain, yes/no, option, and idle-menu handlers are visible in the same message-dispatch implementation.

The unselect handler explicitly says:

```text
unselect not allowed (no regrets)
```

skips the unselectable portion of the message, and does not turn `cancelable` into a model action. The immediately following card-selection branch also rejects `min == 0`.

The counter handler supports no more than two cards and deterministically consumes counters from the first card before the second; card sorting follows an automatic fallback path rather than exposing permutations. These are separate supported-slice or adapter-policy limitations, not consequences of `max_options` alone.

---

# 7. Direct overflow proof: a legal cancel can be dropped

A particularly clear source-level case is `MSG_SELECT_CHAIN`.

The handler:

1. reads every chainable engine entry;
2. appends one `LegalAction` per entry;
3. if the chain is not forced, appends `LegalAction::cancel()` **after** all activation choices;
4. maps cancel to engine response `-1`.

Consider a syntactically valid message with:

```text
24 chainable entries
forced = false
```

Before `WriteState`:

```text
24 activation candidates
+ 1 cancel candidate
= 25 local legal actions
```

After `resize(24)`:

```text
24 activation candidates
cancel removed
```

The network can no longer decline the chain, even though the engine message marked the chain as non-forced.

This is not a claim that the repository’s currently supported deck corpus has a committed fixture that reaches this exact state. It is a **source-proven behavior for a valid handler input shape**. No runtime fixture or guard demonstrates that such a state is impossible.

Classification:

```text
SEMANTIC_LIMITATION
```

For OCGForge, copying this behavior would be a **BLOCKER**.

---

# 8. Combinatorial decisions

## 8.1 Modern `ygopro`: sequentialization

Modern `ygoenv/ygopro` contains an internal multi-select state with:

- current selection index;
- selection mode;
- minimum and maximum counts;
- mandatory selections;
- remaining item specifications;
- valid combination prefixes;
- selected indices;
- terminal response construction.

For ordinary multi-card selection, it exposes one selectable item at a time and adds a finish action when completion is permitted. For sum selection, it first enumerates valid combinations, sorts their indices, then exposes distinct viable next indices. The engine remains waiting while these internal model decisions occur.

This means a large original response space need not become a large flat tensor.

Example:

```text
choose exactly 2 from 10
```

Original exact-response count:

(102)=45\binom{10}{2}=45

A sequential representation needs at most:

```text
10 first-card choices
9 second-card choices
```

at its respective steps. With `max_options=24`, all 45 subsets can therefore be represented without ever presenting 45 simultaneous logits—assuming the handler’s supported conditions, cancel semantics, and local candidate domains are otherwise satisfied.

This is a **sound different design** from flat combination enumeration.

## 8.2 Local domains remain capped

Sequentialization does not remove the overflow issue.

For:

```text
choose exactly 2 from 30
```

the first sequential step contains 30 item choices. `WriteState` retains only the first 24. Some valid subsets may become reachable after choosing an early retained card, but a subset consisting only of two initially hidden tail cards remains unreachable.

Therefore the modern sequential selector is:

```text
LOSSLESS only if every continuation-local domain fits max_options
and the decision is within the handler's supported semantics.
```

## 8.3 EDOPro: flat combination enumeration

The EDOPro environment handles `MSG_SELECT_CARD` differently.

It:

1. rejects or auto-handles cases where the minimum selection exceeds `max_multi_select`;
2. clamps the maximum selection count to `max_multi_select`;
3. enumerates every combination for each permitted cardinality;
4. creates one string option per full combination;
5. later passes the resulting option vector through the same `resize(max_options)` overflow path.

For “choose two from ten”:

```text
45 full combination options
```

are generated, but with `max_options=24`, only the first 24 are exposed.

Thus the same gameplay choice is:

- potentially losslessly sequentialized by modern `ygopro`;
- flat-enumerated and truncated by `edopro`.

This is one of the main reasons the correct overall verdict is **F — Mixed by decision family and implementation**.

## 8.4 `MSG_SELECT_SUM`

The modern implementation uses helpers that explicitly enumerate combinations and filter them by one of two permitted weights per card. Valid combinations are sorted and then passed into the prefix-based multi-select state.

The implementation supports only a restricted subset:

- one sum mode;
- no more than two mandatory items;
- specific weight semantics;
- no general proof in tests that every possible ocgcore sum form is losslessly represented.

The source shows a plausible lossless decomposition for its supported subset, not a general completeness theorem.

---

# 9. Does ygo-agent have an OCGForge-equivalent continuation?

## 9.1 Similarity

Yes, at a high level.

Modern ygo-agent can:

```text
receive one engine message
→ hold internal selection state
→ return multiple times to the model
→ collect partial choices
→ submit the final response later
```

The `next()` loop checks whether internal multi-select state is active and calls `handle_multi_select()` instead of advancing ordinary engine-message handling.

That is conceptually a continuation.

## 9.2 Important differences

| Continuation propertyygo-agentOCGForge |                                                                |                                                         |
| -------------------------------------- | -------------------------------------------------------------- | ------------------------------------------------------- |
| Model-visible continuation identity    | No explicit semantic continuation ID                           | Explicit `continuation_id`                              |
| Stale partial action rejection         | Index is relative to current vector; no versioned semantic key | Stale semantic key fails closed                         |
| Intermediate engine response           | Internal logic generally delays response                       | Contract and tests explicitly prohibit one              |
| Exact terminal response                | Constructed internally                                         | Required in terminal candidate/transition               |
| Complete next-choice guarantee         | No general guarantee; local vector can truncate                | Required for supported family                           |
| Cancel semantics                       | Frequently ignored or narrowed                                 | Explicit candidate when legally available               |
| Ordering canonicalization              | Family-specific                                                | Semantic identities and deterministic ordering required |
| Replay identity                        | Index and internal state                                       | Semantic key plus continuation state                    |
| Overflow behavior                      | Prefix truncation                                              | Must not truncate                                       |

OCGForge’s continuation contract states that ocgcore remains paused, intermediate actions do not submit a response or process the engine, stale identities fail closed, and only the terminal action constructs the exact response.

Current OCGForge tests verify:

- intermediate picks do not advance the engine;
- intermediate candidates contain no response bytes;
- continuation identity changes after a pick;
- a stale key is rejected as `InvalidSemanticKey`;
- the finish candidate carries the final response;
- terminal application produces exactly that response once.

The exhaustive continuation oracle also compares exposed picks and finish legality against independently calculated subset feasibility over multiple continuation kinds.

## 9.3 Learning-problem consequence

Different decompositions may be semantically lossless but still define different learning problems.

A flat 45-combination choice asks the model to compare 45 complete subsets at once.

A sequential 10→9 choice:

- lengthens the decision horizon;
- lets the second decision condition on the first;
- introduces multiple model timesteps;
- may create different exploration and credit-assignment behavior;
- may create duplicate sequences if unordered selections are not canonically ordered.

OCGForge’s continuation design deliberately treats this as a versioned model-facing semantic choice rather than an invisible tensor workaround.

---

# 10. Neural policy representation in ygo-agent

## 10.1 It is not a fixed global action vocabulary

**FACT**

The modern JAX model is candidate-relative.

Each `actions_` row contains features such as:

- referenced card/specification;
- card ID;
- engine message family;
- action kind;
- finish flag;
- effect description;
- phase;
- position;
- number;
- place;
- attribute.

`ActionEncoderV1` embeds those categorical fields.

The observation encoder constructs:

- card embeddings;
- global-state embeddings;
- action-history embeddings;
- one embedding for each current action row;
- a pooled summary of current action embeddings;
- a shared state embedding.

The standard actor then computes:

```python
logits = einsum("bc,bnc->bn", f_state, f_actions)
```

and masks padded rows.

Conceptually, this is:

score(state,current\_optioni)\text{score}(state, current\\_option\_i)

rather than:

```text
one permanent logit for "activate card X"
one permanent logit for "select zone Y"
...
```

## 10.2 Fixed width around a candidate scorer

The accurate description is:

```text
candidate-relative scorer
+
fixed maximum batch width
+
padding mask
+
environment-side overflow truncation
```

The candidate scorer itself is an architectural strength. The problematic part is that the physical width is allowed to redefine the legal gameplay domain.

## 10.3 Candidate-set interaction

The standard actor independently scores each candidate against the shared state embedding. The state embedding also contains a pooled mean of valid candidate embeddings, giving the model an O(N)O(N) summary of the current option set.

An optional FiLM actor applies an encoder layer across candidate embeddings before producing logits, allowing richer candidate interaction at higher cost.

This is useful precedent for OCGForge:

- independent candidate scoring is a strong baseline;
- a cheap pooled set summary can introduce context without quadratic attention;
- full candidate-set attention should remain optional.

## 10.4 Duplicate candidate-feature issue

`MSG_SELECT_OPTION` creates candidates from effect descriptions and sends the chosen vector index back to the engine. If two engine options carry the same description and card identity, their encoded rows can be identical even though the callback indices represent different engine responses.

The standard candidate scorer has no candidate-position embedding, so identical rows receive identical logits.

OCGForge explicitly tests that duplicate option payloads do not collapse semantic identities: two options with the same payload must still have distinct semantic keys.

Classification for ygo-agent:

```text
AUDITABILITY_LIMITATION
and potentially a POLICY_REPRESENTATION_LIMITATION
```

This is separate from `max_options`.

---

# 11. Why ygo-agent likely chose a fixed width

## 11.1 Proven motivations in the architecture

The fixed shape fits the project’s use of:

- EnvPool;
- large numbers of parallel environments;
- JAX/XLA;
- static accelerator shapes;
- dense GPU batches;
- TFLite inference;
- compact byte tensors;
- simple scalar action indices.

The README reports approximately 1,000 environment steps per second on a laptop GPU in its example configuration, demonstrating that throughput is a central design goal.

## 11.2 Correct classification

The **fixed tensor width itself** is:

```text
SOUND_DIFFERENT_DESIGN
+
PERFORMANCE_TRADEOFF
```

The **silent prefix resize when the semantic domain exceeds it** is:

```text
SEMANTIC_LIMITATION
```

Those should not be conflated.

A static 24-row tensor is perfectly sound if one of the following is proven:

- all supported local domains contain at most 24 actions;
- every larger decision is losslessly decomposed into local domains no larger than 24;
- overflow fails closed;
- a wider/bucketed representation is selected before semantics are lost.

The inspected repository proves none of those as a general invariant.

---

# 12. Supported-deck restrictions do not prove the bound

The ygo-agent README presents an explicit supported-deck/card corpus and states that training currently uses cards from those deck assets. The source tree also contains a separate `assets/deck/unsupported` directory.

This is a legitimate supported-slice strategy.

However, I found no:

- invariant proving all supported local action domains are at most 24;
- candidate-count census for every supported deck;
- overflow exception;
- test at 25 options;
- documented statement that truncation is semantically safe;
- per-family maximum proof.

The practical absence of observed overflow in a particular training corpus would not establish impossibility.

Classification:

```text
SUPPORTED_SLICE_ASSUMPTION
+
EVIDENCE GAP
```

Card-support restrictions and action-representation restrictions must remain separate:

- A card may be unsupported because its message family is not implemented.
- A fully supported card may still generate more than 24 direct options.
- A combinatorial family may be implemented but lose choices because a continuation-local domain exceeds 24.
- A message may fit 24 while still being reduced for another reason, such as ignored unselection.

---

# 13. Legality and hidden-information ownership

## 13.1 ygo-agent

The base option lists normally originate from engine messages. The model does not independently reconstruct legality.

The adapter then owns the effective model-facing transformation:

```text
engine message
→ LegalAction/options vector
→ optional decomposition/filter/fallback
→ max_options prefix
→ model
```

No evidence was found that the overflow prefix is selected using hidden strategic information. It is insertion-order truncation, not a privileged “keep the best 24” heuristic.

That distinction matters:

```text
not hidden-information filtering
but still legal-domain reduction
```

Other adapter paths do apply non-engine policy:

- unselection is prohibited by adapter choice;
- counter allocation is deterministic;
- card sorting is automatically resolved;
- several legal message forms throw as unsupported.

## 13.2 OCGForge

OCGForge’s policy boundary is explicit:

```text
PlayerObservation
+
DecisionRequest
+
complete ActionCandidate domain
```

Raw `CoreHost` state is not an acceptable model input, and candidate references are checked separately against the perspective-safe observation.

The engine and protocol adapter own legality. The future model adapter may tensorize candidates but may not reconstruct or reduce them.

---

# 14. Determinism and ordering

## 14.1 ygo-agent ordering

The source explicitly avoids shuffling because the selected model index must remain valid for the callback.

Ordering is generally derived from:

- engine list order;
- fixed category concatenation order;
- vector insertion order;
- sorted integer prefix indices in parts of multi-select handling.

This is sufficient for callback correctness within the current decision.

## 14.2 What it does not establish

There is no explicit, versioned semantic identity equivalent to:

```text
activate:card=...:effect=...:source=...:decision=...
```

Therefore:

> “option 7” means “the seventh row in this exact adapter-produced vector,” not a stable semantic action independent of implementation details.

Under the exact same engine state, commit, card data, and deterministic message ordering, the index is likely reproducible. But it is not protected against:

- engine ordering changes;
- adapter category-order changes;
- feature-encoding migrations;
- inserted new options;
- altered card enumeration;
- different environment implementation.

## 14.3 OCGForge ordering

OCGForge action identity uses `semantic_key`, and candidate validation rejects:

- empty keys;
- duplicate keys;
- terminal candidates without exact response bytes;
- intermediate candidates with response bytes.

Selection by an unknown or stale key fails closed.

This is materially stronger for replay and auditability.

---

# 15. Replay consequences

## 15.1 Is `action = 7` sufficient in ygo-agent?

No.

To interpret it, a replay or trajectory reader also needs at least:

- exact environment implementation;
- exact environment commit;
- exact rules/card/script inputs;
- exact state;
- exact ordered option vector;
- exact callback semantics;
- relevant internal multi-select state.

The training code records observations alongside actions, so a particular in-memory rollout may retain the action tensor that gives index 7 local meaning. That is not the same as a versioned semantic replay contract.

Furthermore:

- two rows may have identical candidate features;
- a later implementation may order them differently;
- an action tensor may not expose every callback-relevant discriminator;
- options 25+ may never have been recorded.

## 15.2 Engine replay versus ML trajectory

ygo-agent contains YGOPro replay-writing wrappers that record actual engine responses. Such a `.yrp` response stream can support engine replay.

That does not make:

```text
training action index = 7
```

a standalone semantic identity.

## 15.3 OCGForge requirement

OCGForge intends to retain:

```text
complete candidate domain
+
chosen semantic action key
+
rules/deck/protocol provenance
```

Replay identity must remain independent of process scheduling, pointers, and incidental enumeration.

---

# 16. The OCGForge `candidate_max = 1344` case

## 16.1 What is proven by the counting code?

At current M4 PR head `f9f10177…`, `canonical_simulation.cpp` does the following for every current request:

1. validates `request.candidates`;
2. adds `request.candidates.size()` to the total;
3. updates `candidate_max` with that size;
4. repeats the same process for every continuation-local request produced before the engine resumes.

Therefore the counter means:

> The maximum size of one model-facing `DecisionRequest.candidates` vector observed by the simulation policy, after protocol transformation into the current request representation.

It is not:

- total legal subsets across an entire continuation;
- cumulative candidate count for one duel;
- raw ocgcore selectable-item count;
- number of all possible future action sequences.

It is a single current request domain.

Because validation occurs before counting, each counted request must have:

- at least one candidate;
- nonempty semantic keys;
- no duplicate semantic keys;
- exact response bytes for terminal actions;
- no response bytes for intermediate actions.

## 16.2 Current committed evidence

The current M4 baseline document records:

```text
candidate_max   = 1344
candidate_sets  = 39519
candidate_total = 187025
```

It also records zero `truncated` counters in the historical rows.

However, its current status is:

```text
M4 BASELINE ACCEPTANCE PENDING
```

with the reason:

```text
acceptance evidence lacks a repository-backed run identity
```

## 16.3 What cannot be reconstructed

The committed aggregate does not identify:

- the job ID containing the 1,344 request;
- engine message family;
- raw message hash;
- decision kind;
- continuation kind and step;
- observation hash;
- full semantic-key set;
- whether it was the initial request or a continuation-local request;
- exact seed and action prefix reaching it.

No committed per-request witness for the 1,344 case was found.

The current decoder also explicitly fails closed on `MSG_ANNOUNCE_CARD`, so the count cannot simply be attributed to a giant card-announcement vocabulary without evidence. The fail-closed test asserts that `MSG_ANNOUNCE_CARD` is unsupported.

## 16.4 Strongest accurate statement

**FACT**

OCGForge has a committed historical aggregate measurement of a validated, single-request candidate count of 1,344.

**UNKNOWN**

The exact gameplay decision that produced it cannot be reconstructed from current committed evidence.

**FACT**

OCGForge has a directly reconstructable unit test with 128 ordering items producing 129 current candidates without truncation.

Therefore the evidence ladder is:

```text
129 candidates:
    explicit source-level test witness

1344 candidates:
    committed historical aggregate,
    current acceptance pending,
    no per-request witness
```

---

# 17. Are 24 and 1,344 semantically comparable?

Not directly.

| NumberActual meaning              |                                                                         |
| --------------------------------- | ----------------------------------------------------------------------- |
| `24` in normal ygo-agent training | Configured maximum number of simultaneous model-visible option rows     |
| `>24` internal ygo-agent vector   | Possible pre-tensor adapter domain, later prefix-truncated              |
| `1,344` in OCGForge M4            | Historical maximum of one validated `DecisionRequest.candidates` vector |
| `129` in OCGForge test            | Explicitly reconstructed complete ordering continuation domain          |

The correct comparison is not:

```text
24 < 1344
```

It is:

```text
For the same gameplay decision:

1. What exact engine responses are legal?
2. Does the adapter use a lossless re-encoding or decomposition?
3. What choices can the model actually select?
4. Is overflow explicit?
5. Can the chosen semantic response be replayed?
```

A ygo-agent modern selection may represent 45 legal subsets using at most ten simultaneous rows. That is not inferior to a 45-row flat domain.

A direct 25-option chain menu reduced to 24 is semantically incomplete.

---

# 18. Controlled source-level case studies

## Case A — Small domain: `MSG_SELECT_YESNO`

### Engine

```text
Yes
No
```

### ygo-agent adapter

The handler creates:

```text
candidate 0: activate/yes
candidate 1: cancel/no
```

The callback maps index 0 to response `1` and index 1 to response `0`.

### Tensor and policy

Two action rows are encoded, padded to 24, and scored by the candidate-relative actor.

### Classification

```text
C_model == C_engine
LOSSLESS_REENCODING
```

---

## Case B — Near the boundary: 23 chainable effects plus cancel

This is a source-level constructed handler case, not a committed duel fixture.

### Engine message

```text
chainable entries = 23
forced = false
```

### Adapter domain

```text
23 activation options
+ cancel
= 24
```

### Publication

`24 > max_options` is false, so all options remain.

### Policy

The network receives 24 candidate-relative rows and can choose any of them.

### Classification

```text
C_model == C_adapter == C_engine
at this current direct decision
```

Subject to the separate caveat that two candidates with identical encoded features may be indistinguishable to the standard scorer.

---

## Case C — Exceeding the boundary: 24 chainable effects plus cancel

This is the exact overflow case described earlier.

### Engine message

```text
chainable entries = 24
forced = false
```

### Adapter domain before publication

```text
24 activations
+ cancel
= 25
```

### Publication

```text
resize(24)
```

removes the final candidate, which is cancel.

### Model-visible result

```text
24 activations
no cancel
```

### Classification

```text
C_model ⊂ C_engine
TRUNCATED
SEMANTIC_LIMITATION
```

---

## Case D — More than 24 original legal responses, but losslessly decomposable

### Gameplay choice

```text
choose exactly 2 cards from 10
```

### Original responses

```text
45 legal subsets
```

### Modern `ygopro`

Sequential model decisions:

```text
choose first item from up to 10
choose second item from up to 9
submit final subset response
```

This can represent 45 original responses while never exposing more than 10 choices at one step.

### EDOPro

Enumerates all 45 combinations as flat options, then truncates the vector to 24.

### Classification

```text
modern ygopro:
    potentially LOSSLESS_DECOMPOSITION

edopro:
    LEGAL_DOMAIN_REDUCTION with max_options=24
```

---

# 19. Tests and evidence coverage

## 19.1 ygo-agent

A recursive source-tree and code search did not find a test specifically covering:

```text
n_options == max_options
n_options == max_options + 1
very large n_options
overflow status
prefix retention
cancel-after-boundary behavior
```

No assertion was found before `resize`, and no overflow metric is published.

This is an **evidence gap**, not by itself proof of a defect beyond the source-proven truncation behavior.

I also did not find a committed candidate-count census proving that every supported deck and message family remains within 24.

## 19.2 OCGForge

Current source tests cover several relevant properties.

### Fail-closed and non-truncation

`decision_fail_closed_test.cpp` verifies:

- unsupported families produce a structured error;
- duplicate option payloads remain distinct semantic actions;
- a 128-item ordering decision exposes 129 candidates rather than being truncated;
- malformed payloads do not produce plausible fallback decisions.

### Continuation lifecycle

`continuation_core_test.cpp` verifies:

- adapter-local picks;
- no response or engine advance during intermediate steps;
- evolving semantic identity;
- stale-key rejection;
- exact final response.

### Continuation completeness

`continuation_oracle_test.cpp` exhaustively explores small continuation state spaces and independently checks:

- legal next picks;
- finish legality;
- reachable subset states;
- terminal response construction.

### M4 integrity

M4 report and schema tests require explicit `truncated` counters and prevent an acceptance pass without durable evidence. The current baseline remains pending rather than silently claiming acceptance.

## 19.3 Remaining OCGForge gap

There is no committed test or trace witness reconstructing the historical 1,344-candidate request.

That is the most important evidence gap exposed by this comparison.

---

# 20. Direct contract comparison

| Propertyygo-agent / ygoenvOCGForgeConsequence   |                                                                                                                  |                                                                                               |                                                                                |
| ----------------------------------------------- | ---------------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Legality owner                                  | **PROVEN:** engine emits base lists; adapter creates effective option domain and sometimes filters/auto-resolves | **PROVEN:** engine + protocol adapter; model must not reconstruct legality                    | Both keep legality outside model, but OCGForge has stronger completeness rules |
| Candidate representation                        | `LegalAction` or string option encoded into compact byte row                                                     | Typed `ActionCandidate` with semantic key, source/target fields, continuation, exact response | OCGForge is more explicit and audit-friendly                                   |
| Fixed maximum                                   | **PROVEN:** configurable `max_options`; normally 24 in training                                                  | No gameplay-semantic global candidate maximum in current contract                             | OCGForge tensor adapter must not add one                                       |
| Overflow behavior                               | **PROVEN:** silent insertion-order prefix resize                                                                 | Supported domains must not truncate; unrepresentable behavior fails closed                    | Material semantic difference                                                   |
| Combinatorial handling                          | Mixed: sequential modern selector, flat EDOPro combinations, restricted handlers                                 | Adapter-local semantic continuations                                                          | Both can decompose, but guarantees differ                                      |
| Continuations                                   | Internal state; no semantic stale-action key                                                                     | Explicit continuation state and semantic identity                                             | OCGForge stronger for replay and stale rejection                               |
| Complete-domain guarantee                       | No general guarantee found                                                                                       | Required for each supported family                                                            | OCGForge materially stronger contractually                                     |
| Candidate identity                              | Local vector index plus encoded row                                                                              | Versioned semantic key                                                                        | Index alone is weaker                                                          |
| Candidate ordering                              | Engine/insertion/category order                                                                                  | Explicit semantic/canonical ordering requirement                                              | OCGForge more robust across implementations                                    |
| Replay identity                                 | `action=N` needs exact option vector and implementation                                                          | Candidate domain plus chosen semantic key                                                     | OCGForge intended replay is self-describing                                    |
| Hidden-info boundary                            | Ad hoc environment observation projection; not certified by this audit                                           | Explicit `PlayerObservation` contract                                                         | OCGForge boundary is clearer                                                   |
| Batching model                                  | Dense static `[B, max_options, F]`                                                                               | Future adapter not yet implemented                                                            | ygo-agent is currently more mature operationally                               |
| Failure behavior                                | Mixed: truncate, throw, auto-resolve, fallback                                                                   | Fail closed                                                                                   | Different research priorities                                                  |
| Largest model-visible domain                    | **PROVEN:** 24 under normal configuration                                                                        | **Historical aggregate:** 1,344; **explicit test:** 129                                       | Counts have different evidence and semantics                                   |
| Largest pre-truncation supported runtime domain | **UNKNOWN**                                                                                                      | Exact 1,344 family **UNKNOWN**                                                                | Both need better per-request witnesses                                         |

---

# 21. Classification of important ygo-agent choices

| BehaviorClassificationReason                     |                                                      |                                                                  |
| ------------------------------------------------ | ---------------------------------------------------- | ---------------------------------------------------------------- |
| Candidate-relative action rows                   | `SOUND_DIFFERENT_DESIGN`                             | Avoids a giant global vocabulary                                 |
| Static `max_options` tensor width                | `PERFORMANCE_TRADEOFF`                               | Efficient for EnvPool/JAX/TFLite                                 |
| Padding and invalid-row masking                  | `SOUND_DIFFERENT_DESIGN`                             | Standard fixed-shape batching technique                          |
| Shared state–candidate dot-product scoring       | `SOUND_DIFFERENT_DESIGN`                             | Natural variable-option policy within the bound                  |
| Candidate-set pooled summary                     | `SOUND_DIFFERENT_DESIGN`                             | Adds set context in O(N)O(N)                                     |
| Prefix `resize(max_options)`                     | `SEMANTIC_LIMITATION`                                | Removes reachable direct legal actions                           |
| No overflow flag/test found                      | `UNKNOWN` / evidence gap                             | Intent and supported-corpus frequency are not documented         |
| Sequential multi-select                          | `SOUND_DIFFERENT_DESIGN`                             | Can losslessly encode large response spaces                      |
| Sequential local domain still capped             | `SEMANTIC_LIMITATION`                                | Decomposition is not sufficient when local branching exceeds cap |
| EDOPro flat combination enumeration              | `SUPPORTED_SLICE_ASSUMPTION`                         | Works for small combination sets                                 |
| EDOPro combination truncation                    | `SEMANTIC_LIMITATION`                                | Removes full legal subset responses                              |
| Ignoring unselect choices                        | `SEMANTIC_LIMITATION`                                | Deliberately reduces engine-provided interaction                 |
| Counter auto-allocation                          | `SUPPORTED_SLICE_ASSUMPTION` / `SEMANTIC_LIMITATION` | Selects one allocation instead of exposing all                   |
| Sort-card fallback                               | `SUPPORTED_SLICE_ASSUMPTION`                         | Keeps games running without model ordering                       |
| Index-only action identity                       | `AUDITABILITY_LIMITATION`                            | Requires exact external context to interpret                     |
| Duplicate option rows with same features         | `AUDITABILITY_LIMITATION`                            | Distinct responses may be policy-indistinguishable               |
| `num_options` apparent upper-bound inconsistency | `POTENTIAL_BUG`                                      | Count can equal the declared maximum width                       |

---

# 22. What OCGForge should learn from ygo-agent

## P0 — Should influence the first model adapter

### Candidate-relative policy

Copy the concept:

```text
encode each current legal candidate
→ shared scorer
→ one logit per current candidate
```

Do not use a permanent global action vocabulary.

### Encode the observation once

ygo-agent constructs one shared state representation and combines it with each candidate embedding. OCGForge should do the same.

### Compact typed candidate features

Candidate tensors should encode:

- action kind;
- decision kind;
- source/target entity references;
- card IDs or unknown IDs;
- zone and position;
- effect/option identity;
- continuation step;
- amount;
- finish/cancel semantics.

### Padding masks

Padding is useful and safe when it represents only absent batch slots, never omitted legal actions.

### Cheap candidate-set summary

A mean, sum, max, or Deep-Sets-style aggregate of all current candidate embeddings can give every score information about the alternative set while remaining O(N)O(N).

## P1 — Likely useful

- Candidate-count bucketing.
- Static JAX shapes per bucket.
- Separate player action histories.
- Recurrent policy support.
- Actor/learner separation.
- Vectorized CPU environments.
- Compact byte or integer transport formats.

## P2 — Later optimization

- FiLM conditioning of candidate embeddings.
- Candidate-set attention for small domains.
- TFLite or other deployment formats.
- Specialized action-feature encoders by message family.

## P3 — Interesting only for later scale

- Full EnvPool-like native vectorization.
- Large distributed training topology.
- Multiple model-serving backends.

These are conceptual lessons only. This report does not recommend importing source code; license compatibility, dependency fit, and long-term maintenance would require a separate review.

---

# 23. What OCGForge should explicitly not copy

The following exclusions are supported by inspected source behavior.

1. **Do not call** **`resize(max_options)`** **on the authoritative candidate vector.**
2. **Do not let a global tensor width define gameplay semantics.**
3. **Do not store only** **`action_index`** **in a trajectory.**
4. **Do not remove cancel, finish, unselect, or tail candidates merely because they are inconvenient to encode.**
5. **Do not auto-resolve a required player choice to keep a duel running.**
6. **Do not enumerate a combinatorial response space flatly and then keep only the first N combinations.**
7. **Do not allow duplicate semantic actions to collapse to indistinguishable candidate identities.**
8. **Do not rely on insertion order as the durable replay identity.**
9. **Do not treat a supported-deck list as proof that candidate overflow cannot occur.**
10. **Do not omit an overflow counter, explicit error, or acceptance test.**

These exclusions follow directly from OCGForge’s existing fail-closed and complete-candidate invariants.

---

# 24. Extreme-domain computational cost

## 24.1 Recommended computation

For one request:

```text
h = ObservationEncoder(observation)          # once

e_i = CandidateEncoder(candidate_i)          # N times

logit_i = Scorer(h, e_i)                     # N times

policy = segmented_softmax(logits)           # exactly N legal candidates
```

The observation encoder must not be rerun 1,344 times.

## 24.2 Memory estimate for 1,344 candidates

For candidate embedding width 128:

| RepresentationApproximate memory |          |
| -------------------------------- | -------- |
| 1,344 × 128 × FP16               | 336 KiB  |
| 1,344 × 128 × FP32               | 672 KiB  |
| 1,344 FP32 logits                | 5.25 KiB |

For width 256, candidate embeddings are approximately:

- 672 KiB in FP16;
- 1.31 MiB in FP32.

These are small compared with ordinary GPU activation budgets.

## 24.3 Compute estimate

A modest candidate encoder such as:

```text
64 input features
→ 128 hidden
→ 128 output
```

requires roughly:

64×128+128×128=24,57664 \times 128 + 128 \times 128 = 24{,}576

multiply-accumulate operations per candidate.

For 1,344 candidates:

24,576×1,344≈33 million MACs24{,}576 \times 1{,}344 \approx 33\,\text{million MACs}

That is practical on a modern GPU and still manageable for occasional CPU inference. The extreme count is not a justification for semantic truncation.

## 24.4 Padding waste

The historical M4 aggregate has:

187,02539,519≈4.73\frac{187{,}025}{39{,}519} \approx 4.73

candidates per request on average, while the maximum was 1,344.

Padding every request globally to 1,344 would waste more than 99% of candidate slots for a typical request.

Therefore OCGForge should use:

- ragged flat arrays;
- request offsets;
- candidate-count buckets;
- batch-local padding;
- optional exact chunking for extremes.

---

# 25. Candidate interactions

## 25.1 Independent scoring

An independent scorer conditioned on the observation and decision context:

si=f(h,ei)s\_i=f(h,e\_i)

is the correct first baseline.

Its complexity is:

O(N)O(N)

## 25.2 Set-summary enhancement

A stronger still-linear form is:

g=pool⁡{ϕ(ej)}g = \operatorname{pool}\\{\phi(e\_j)\\} si=f(h,g,ei)s\_i=f(h,g,e\_i)

This lets the model reason about:

- whether an action is uniquely available;
- relative candidate types;
- current domain composition;
- how many finish/cancel alternatives exist;
- whether several targets are strategically similar.

This resembles ygo-agent’s inclusion of a pooled valid-action summary in its state representation.

## 25.3 Full candidate attention

At N=1,344N=1{,}344:

N2=1,806,336N^2 = 1{,}806{,}336

candidate pairs.

With eight attention heads, an FP16 attention-score tensor alone is roughly:

```text
1,806,336 × 8 × 2 bytes
≈ 27.6 MiB
```

per sample and layer, before gradients and other activations.

The QK⊤QK^\top compute at width 128 is on the order of hundreds of millions of operations.

Therefore:

**RECOMMENDATION**

Start with O(N)O(N) scoring plus an O(N)O(N) candidate-set summary. Introduce full candidate-to-candidate attention only after a controlled experiment shows that independent scoring is strategically insufficient.

---

# 26. Specific recommendation for OCGForge’s first action adapter

## Owning layer

```text
future model-facing tensor adapter
```

It must sit above:

```text
PlayerObservation
DecisionRequest
ActionCandidate
```

and below the neural policy.

It must not modify the protocol-layer candidate vector.

## Recommended interface

```text
PlayerObservation batch
        ↓
ObservationEncoder once per request
        ↓
H[B, D]

flat ActionCandidate tensor
        ↓
CandidateEncoder once per candidate
        ↓
E[T, D]

request_offsets[B + 1]
or segment_id[T]
        ↓
shared scorer
        ↓
logits[T]

segmented softmax
        ↓
one probability distribution per request
```

where:

```text
B = number of requests
T = total candidates across the batch
N_b = exact candidate count for request b
```

and:

```text
T = Σ N_b
```

## Required semantics

- `N_b` is not capped by gameplay semantics.
- Every `ActionCandidate` receives exactly one tensor row.
- Padding may exist only within a physical batch bucket.
- Padding rows are never entered into the candidate domain.
- The model returns a local index plus the associated semantic key.
- The environment verifies that key against the unchanged request.
- Trajectories retain the complete candidate domain and chosen key.
- Tensorization failure rejects the sample or request; it never drops candidates.

## Operational strategies

Use:

```text
ragged flat arrays
candidate-count bucketing
batch-local static padding
segmented softmax
optional exact chunking
```

For JAX/XLA, compile several candidate-count buckets rather than one global shape. A possible initial set could be:

```text
1–8
9–16
17–32
33–64
65–128
129–256
257–512
513–1024
1025–2048
```

Those exact boundaries are not contractual and should be chosen from a measured candidate-count histogram.

For an extreme domain, chunk candidate encoding if necessary, but compute the exact logits and exact global softmax normalization. Chunking must be a physical execution detail, not a top-k filter.

## Comparison with ygo-agent

OCGForge should copy:

```text
candidate row encoding
shared state embedding
shared scorer
padding masks
static compiled buckets
```

OCGForge should replace:

```text
global max_options
+
authoritative vector resize
```

with:

```text
ragged complete domain
+
batch-local physical shape
+
fail-closed tensorization
```

---

# 27. OCGForge findings classification

## BLOCKER — Global model-width truncation

Any OCGForge tensor adapter that removes candidate 25+ to fit a neural shape would violate the complete-domain contract.

## BLOCKER — Index-only trajectory identity

A trajectory containing only:

```text
action = 7
```

without the complete candidate domain and semantic key cannot satisfy OCGForge replay requirements.

## BLOCKER — Automatic resolution of required player decisions

Counter allocation, ordering, cancel, or unselection must not be automatically reduced unless the removed responses are proven semantically equivalent.

## BLOCKER — Duplicate semantic candidates collapsing in the tensor adapter

Two distinct `ActionCandidate` keys may share most features, but the adapter must preserve a discriminator that allows distinct policy scores when their game meaning differs.

## MAJOR — Global padding to the largest observed domain

Padding every request to 1,344 would make batching inefficient and would encourage pressure to reduce the domain later.

## MAJOR — Treating the historical 1,344 aggregate as a complete witness

The exact message family and action set are currently unavailable. Architecture should not be optimized around an unexplained maximum without a reproducible request artifact.

## MAJOR — Failing to test continuation-local overflow

No-truncation gates must cover both initial engine requests and every continuation-local request.

## MINOR — No per-family candidate-count distribution

M4 records totals and maxima, but a future model adapter needs histograms by:

- decision family;
- continuation kind;
- continuation step;
- deck;
- phase;
- candidate feature shape.

## NOTE — ygo-agent demonstrates that candidate scoring and fixed-shape acceleration are compatible

The choice is not between:

```text
complete semantics
```

and:

```text
efficient batching
```

Ragged logical domains can be transformed into efficient bucketed physical batches.

---

# 28. Final verdict by implementation

## Overall

# **F — Mixed by decision family**

## `ygoenv/ygopro` / `YGOPro-v1`

# **F — Mixed by decision family**

- direct options truncate;
- several combinatorial choices are sequentialized;
- local continuation domains still truncate;
- some legal forms throw;
- some actions are filtered or auto-resolved.

## `ygoenv/ygopro0` / `YGOPro-v0`

# **F — Mixed by decision family**

- same fixed-width prefix truncation;
- older string-option encoding;
- older privacy and action-feature behavior;
- internal multi-step handling for some families.

## `ygoenv/edopro` / `EDOPro-v0`

# **F — Mixed by decision family**

- same prefix truncation;
- flat combination enumeration for important multi-card decisions;
- separate `max_multi_select` bound;
- unsupported or automatic handling for other cases.

---

# 29. Direct answers

## 1. What exactly does `max_options=24` control?

It controls:

- the first dimension of the model-facing action tensor;
- the scalar external action-index range;
- the maximum number of policy logits;
- padding width;
- the inspected random baseline’s sampling range;
- and, through `resize(max_options)`, the number of options retained for model selection.

It is therefore both an ML batching parameter and an effective semantic cap.

## 2. Can ygo-agent represent more than 24 legal choices?

With `max_options=24`, it cannot expose more than 24 simultaneous model-visible choices.

It can nevertheless represent more than 24 original engine responses when a decision is losslessly sequentialized into several local steps whose domains each fit within 24.

The configuration can also be increased before environment construction, producing a wider fixed tensor.

## 3. If yes, how?

Through internal multi-select sequentialization:

```text
large response space
→ choose one item
→ update internal selection state
→ choose another item or finish
→ submit one final response
```

Modern `ygopro` uses this for several card, tribute, and sum selections.

## 4. If no, what exactly happens to choice 25+?

For a direct or continuation-local domain containing more than 24 current options:

```text
vector.resize(24)
```

removes choice 25 and every later choice.

They are not encoded, masked, paginated, or recoverable by the model.

## 5. Is any legal candidate silently dropped?

Yes.

The direct `MSG_SELECT_CHAIN` code can build 24 activation choices followed by a legal cancel choice. With `max_options=24`, the cancel candidate is the tail element and is silently removed.

A committed supported-duel fixture reaching this exact case was not found, but the source behavior is unambiguous.

## 6. Is large-action handling lossless?

Not generally.

It is potentially lossless for supported sequentialized families when every local domain fits the cap and all engine semantics—such as cancel—are preserved.

It is not lossless for direct overflow, EDOPro flat-combination overflow, ignored unselect choices, automatic counter allocation, or other adapter reductions.

## 7. Does behavior differ by decision family?

Yes.

It includes:

- direct one-to-one menus;
- sequential decompositions;
- flat combination enumeration;
- unsupported exceptions;
- heuristic filtering;
- automatic fallback responses;
- silent truncation.

## 8. Does behavior differ between `ygopro` and `edopro`?

Yes.

Both truncate to `max_options`, but modern `ygopro` sequentializes several combinatorial choices, while `edopro` flatly enumerates full combinations up to `max_multi_select` and then truncates the resulting option vector.

## 9. What is the largest candidate domain proven in ygo-agent?

Three different answers are necessary:

- Largest model-visible domain under the normal configuration: **24**.
- Source-proven pre-truncation domain: **greater than 24**; a 25-option chain case is directly constructible from the handler.
- Largest domain observed in a committed supported duel or candidate census: **unknown**.

## 10. What is the largest candidate domain proven in OCGForge?

- Largest explicitly reconstructable unit-test domain: **129 candidates**.
- Largest committed historical M4 aggregate: **1,344 candidates in one validated request**.
- The current M4 baseline carrying 1,344 is marked **acceptance pending**, and the exact request cannot be reconstructed.

## 11. Are those counts semantically comparable?

No.

`24` is ygo-agent’s configured post-transformation model width.

`1,344` is OCGForge’s recorded size of one protocol-level `DecisionRequest.candidates` vector before any ML tensor adapter.

The same gameplay decision must be traced before comparing counts.

## 12. Does ygo-agent use candidate scoring or a fixed action vocabulary?

Candidate scoring.

Its model encodes each current action row, combines it with a shared state representation, and emits one logit per current row. It is not a fixed global Yu-Gi-Oh! action vocabulary.

## 13. Is option ordering semantically stable?

It is callback-stable within the current request because vector order is preserved and not shuffled.

It is not a versioned semantic identity. Index 7 can change meaning if engine ordering, adapter ordering, or candidate construction changes.

## 14. Can a ygo-agent trajectory reconstruct the exact semantic action?

An `action=7` value alone cannot.

The exact ordered candidate tensor, environment version, state, internal continuation state, and callback semantics are required. No semantic action-key contract equivalent to OCGForge’s was found.

## 15. Does OCGForge’s continuation architecture solve a problem ygo-agent handles differently?

Yes.

Both can split one engine decision across several model decisions, but OCGForge formalizes:

- continuation identity;
- complete next-choice domains;
- stale-action rejection;
- no intermediate engine response;
- exact terminal response;
- replay semantics.

ygo-agent’s continuation is internal and still subject to `max_options` truncation.

## 16. What should OCGForge copy from ygo-agent?

Primarily:

- candidate-relative action encoding;
- shared observation encoding;
- one shared scorer;
- padding masks;
- compact candidate features;
- O(N)O(N) candidate-set pooling;
- static candidate-count buckets for JAX/GPU efficiency.

## 17. What should OCGForge explicitly avoid copying?

- authoritative vector resize;
- global semantic action cap;
- index-only replay identity;
- ignored cancel/unselect actions;
- automatic player-choice resolution;
- flat combination enumeration followed by truncation;
- silent overflow;
- candidate feature collisions without a semantic discriminator.

## 18. Is OCGForge’s complete-domain architecture materially stronger, or merely different?

It is materially stronger **for supported decision families** in:

- legal-domain completeness;
- fail-closed behavior;
- semantic identity;
- continuation auditability;
- stale-action rejection;
- replay integrity.

It is not globally stronger as an ML system: ygo-agent currently has the more mature batching, training, inference, and deployment stack, while OCGForge’s model-facing adapter remains future work.

## 19. What is the biggest unresolved evidence gap?

The exact OCGForge request that produced `candidate_max=1344` is not committed as a replayable witness, and ygo-agent has no committed supported-corpus candidate census showing how often its pre-truncation domain exceeds 24.

The most important single missing artifact is a reproducible per-request large-domain witness.

## 20. What single experiment or test should OCGForge run next?

Run a **deterministic candidate-domain witness test** over the canonical fixed matchup.

For every request, record:

```text
job ID
seed
engine step
raw message hash
DecisionRequest kind
continuation kind and step
candidate count
ordered semantic-key digest
observation hash
chosen semantic key
```

Retain the maximum request as a committed replay fixture.

The test should then tensorize and replay boundary cases including:

```text
N = 24
N = 25
N = 129
the reconstructed historical maximum
```

and assert:

```text
input candidate count
=
tensor row count
=
policy logit count
=
replayed candidate count
```

with zero dropped, fabricated, or reordered semantic candidates.

> **Final answer:** `max_options=24` exposes a real semantic limitation in ygo-agent whenever a post-decomposition legal domain exceeds the configured width. It is not merely padding. The model architecture itself—candidate-relative scoring with fixed-shape batching—is worth learning from; the fundamental problem is allowing the fixed physical width to truncate the legal gameplay domain.
