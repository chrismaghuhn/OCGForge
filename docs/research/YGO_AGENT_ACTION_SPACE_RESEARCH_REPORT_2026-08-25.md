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

> **`max_options=24` is a performance-oriented fixed-shape parameter whose overflow policy leaks into game semantics. It is an implementation detail when the post-decomposition domain fits the bound, but a genuine semantic limitation when it does not.**

OCGForge’s supported-family contract is materially stronger because it prohibits this leakage: it requires complete semantic candidates, explicit continuations, semantic action keys, stale-action rejection, one exact terminal engine response, and fail-closed behavior when completeness is unproven.

That does **not** establish that OCGForge is globally superior. OCGForge currently has a narrower certified gameplay slice and no completed neural model adapter, while ygo-agent has a mature fixed-shape vectorized ML stack and trained-policy infrastructure. The stronger OCGForge conclusion is specifically about **supported-domain semantic completeness and auditability**.

---

# 2. Live repository state inspected

Static OCGForge project sources were used only for durable architecture and invariants. Current branch, PR, and acceptance status were taken from live GitHub, as required by the project update policy.

| Repository | Branch/ref | Exact commit | Status on 25 August 2026 |
|---|---|---|---|
| `chrismaghuhn/OCGForge` | `main` | `588f02b4ef879fee999c921c114937a6a1e48557` | Current default branch at inspection time |
| `chrismaghuhn/OCGForge` | `chris/m4-parallel-simulation-foundation`, PR #3 | `f9f10177ee48e244a491fb94ef63752ddf5ddfed` | Relevant open draft M4 head at inspection time |
| `sbl1996/ygo-agent` | `main` | `dbf5142d49aab2e6beb4150788d4fffec39ae3e5` | Current default branch at inspection time |

OCGForge `main` was behind the active M4 implementation when this report was produced. This section is a dated research snapshot and must not be used as current repository status after 25 August 2026.

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

| File / symbol | Purpose | Semantic effect |
|---|---|---|
| `scripts/cleanba.py`, `Args.max_options` | Training default | Sets width to 24 |
| `scripts/cleanba.py`, `make_env` | Python → C++ environment configuration | Passes width unchanged |
| `scripts/impala.py`, `scripts/torch/ppo.py` | Alternative training defaults | Also use 24 |
| `ygopro/registration.py` | Registers `YGOPro-v1` | Selects modern implementation |
| `ygopro0/registration.py` | Registers `YGOPro-v0` | Selects older implementation |
| `edopro/registration.py` | Registers `EDOPro-v0` | Selects EDOPro implementation |
| `YGOProEnvFns::DefaultConfig` | Environment fallback | Default is 16 if not overridden |
| `YGOProEnvFns::StateSpec` | Observation shape | `actions_` is `[max_options, 12]` |
| `YGOProEnvFns::ActionSpec` | External action space | Scalar index `0..max_options-1` |
| `RandomAI` | Random baseline | Draw range limited by `max_options` |
| `YGOProEnvImpl::WriteState` | Model-facing state publication | **Drops tail options** |
| `ActionEncoderV1` | Neural candidate encoding | Encodes one row per retained option |
| `Actor` | Policy head | Produces one logit per retained row |
| `EDOProEnvFns::StateSpec` | EDOPro observation shape | `[max_options, 10 + 2×max_multi_select]` |
| `YGOPro0::WriteState` / `EDOPro::WriteState` | Older environment publication | Same tail-dropping behavior |

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

This appears to be an off-by-one inconsistency in the metadata specification. No evidence was found that the JAX policy relies on this bound; it is a **POTENTIAL_BUG**, not part of the central truncation conclusion.

---

# 4. Exact overflow behavior

## 4.1 Modern `ygoenv/ygopro`

**FACT**

In `YGOProEnvImpl::WriteState`:

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

and every later candidate becomes unreachable.

This is **prefix truncation**, not candidate sampling. The retained choices remain callback-correct because the vector is not reordered. Completeness is what is lost.

---

# 5. Different action counts that must not be conflated

The comparison requires five separate domains:

```text
I_engine = raw engine-listed items
C_engine = exact legal engine responses
C_step   = adapter-local action domain for the current model step
C_model  = model-visible tensor rows
R_engine = emitted engine response
```

A 45-response combinatorial choice may be represented losslessly using ten or fewer choices at each sequential model step. Conversely, a direct 25-option menu cannot be represented by a 24-row tensor unless it is explicitly decomposed or the width is increased.

---

# 6. Decision-family audit

The following classifications describe the modern `ygoenv/ygopro` path at commit `dbf5142d…`.

| Engine family | Adapter representation | `C_model` vs `C_engine` | Overflow / unsupported behavior |
|---|---|---|---|
| `MSG_SELECT_YESNO` | Two direct rows | `LOSSLESS_REENCODING` | Always below 24 |
| `MSG_SELECT_EFFECTYN` | Activate / cancel | `LOSSLESS_REENCODING` | Always below 24 |
| `MSG_SELECT_POSITION` | One row per position | `LOSSLESS_REENCODING` | At most four ordinary positions |
| `MSG_SELECT_OPTION` | One row per option index | Equal until cap | `TRUNCATED` above cap |
| `MSG_SELECT_CHAIN` | One row per chainable item plus optional cancel | Equal until cap | `TRUNCATED` above cap |
| `MSG_SELECT_IDLECMD` | Concatenated summon, special summon, reposition, set, activate, phase menus | Equal until cap | `TRUNCATED` above cap |
| `MSG_SELECT_BATTLECMD` | Activations, attackers, phase exits | Equal until cap | `TRUNCATED` above cap |
| `MSG_SELECT_PLACE` | One row per available zone | Equal only when requested count is one | Other counts throw; rows truncate above cap |
| `MSG_SELECT_DISFIELD` | Same general zone representation | Narrow supported case | Unsupported counts / overflow risks |
| `MSG_ANNOUNCE_NUMBER` | One row per supplied number | Adapter-restricted | Values outside implemented range throw |
| `MSG_ANNOUNCE_ATTRIB` | One row per attribute | Supported only for count one | Other counts throw |
| `MSG_ANNOUNCE_CARD` | One row per parsed explicit card code | Equal for accepted opcode format until cap | Other opcode forms throw; rows truncate above cap |
| `MSG_SELECT_CARD` | Sequential item selection with internal multi-select state | Potentially `LOSSLESS_DECOMPOSITION` | `min=0` unsupported; cancel ignored; each local domain can truncate |
| `MSG_SELECT_TRIBUTE` | Sequential item selection | Lossless only under narrow assumptions | Requires `min==max` and unit weights; otherwise throws |
| `MSG_SELECT_SUM` | Enumerate valid combinations, then sequentialize by viable prefixes | Potentially `LOSSLESS_DECOMPOSITION` | Restricted mode; local domains can truncate |
| `MSG_SELECT_UNSELECT_CARD` | Selectable items plus optional finish | `FILTERED_BY_HEURISTIC` | Unselect choices removed; cancel ignored; remaining rows may truncate |
| `MSG_SELECT_COUNTER` | No model decision; deterministic allocation | `FILTERED_BY_HEURISTIC` | More than two source cards throws |
| `MSG_SORT_CARD` | No meaningful model ordering | Adapter fallback | Automatically submits fallback response |

These are separate supported-slice or adapter-policy limitations, not consequences of `max_options` alone.

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

This is a source-proven behavior for a valid handler input shape. No committed supported-duel fixture reaching this exact state was found, and no runtime fixture or guard demonstrates that such a state is impossible.

Classification:

```text
SEMANTIC_LIMITATION
```

For OCGForge, copying this behavior would be a **BLOCKER**.

---

# 8. Combinatorial decisions

## 8.1 Modern `ygopro`: sequentialization

Modern `ygoenv/ygopro` contains an internal multi-select state with current selection index, selection mode, minimum and maximum counts, mandatory selections, remaining item specifications, valid combination prefixes, selected indices, and terminal response construction.

For ordinary multi-card selection, it exposes one selectable item at a time and adds a finish action when completion is permitted. For sum selection, it first enumerates valid combinations, sorts their indices, then exposes distinct viable next indices. The engine remains waiting while these internal model decisions occur.

Example:

```text
choose exactly 2 from 10
```

The original response space contains 45 legal subsets, but a sequential representation needs at most ten first-card choices and nine second-card choices at the respective steps. With `max_options=24`, all 45 subsets can therefore be represented without presenting 45 simultaneous logits, assuming the handler’s supported conditions and cancel semantics are otherwise preserved.

This is a **sound different design** from flat combination enumeration.

## 8.2 Local domains remain capped

Sequentialization does not remove the overflow issue. For:

```text
choose exactly 2 from 30
```

the first sequential step contains 30 item choices. `WriteState` retains only the first 24. Therefore the sequential selector is lossless only if every continuation-local domain fits `max_options` and the decision is otherwise within the handler’s supported semantics.

## 8.3 EDOPro: flat combination enumeration

The EDOPro environment handles important multi-card selection differently: it enumerates full combinations and later passes the resulting vector through the same `resize(max_options)` overflow path.

For “choose two from ten”, 45 full combination options are generated; with `max_options=24`, only the first 24 are exposed.

Thus the same gameplay choice is potentially losslessly sequentialized by modern `ygopro`, but flat-enumerated and truncated by `edopro`.

This is a principal reason for verdict **F — Mixed by decision family and implementation**.

---

# 9. Does ygo-agent have an OCGForge-equivalent continuation?

At a high level, yes:

```text
receive one engine message
→ hold internal selection state
→ return multiple times to the model
→ collect partial choices
→ submit the final response later
```

The important differences are contractual:

| Continuation property | ygo-agent | OCGForge |
|---|---|---|
| Model-visible continuation identity | No explicit semantic continuation ID | Explicit `continuation_id` |
| Stale partial action rejection | Index relative to current vector | Stale semantic key fails closed |
| Intermediate engine response | Internal logic generally delays response | Contract/tests prohibit it |
| Exact terminal response | Constructed internally | Required in terminal candidate/transition |
| Complete next-choice guarantee | No general guarantee; local vector can truncate | Required for supported family |
| Cancel semantics | Frequently ignored or narrowed | Explicit candidate when legal |
| Replay identity | Index and internal state | Semantic key plus continuation state |
| Overflow behavior | Prefix truncation | Must not truncate |

Different decompositions may be semantically lossless but still define different learning problems. OCGForge deliberately treats continuation semantics as a versioned model-facing choice rather than an invisible tensor workaround.

---

# 10. Neural policy representation in ygo-agent

## 10.1 It is not a fixed global action vocabulary

**FACT**

The modern JAX model is candidate-relative.

Each `actions_` row contains features such as referenced card/specification, card ID, engine message family, action kind, finish flag, effect description, phase, position, number, place, and attribute.

`ActionEncoderV1` embeds those categorical fields. The observation encoder constructs a shared state embedding and one embedding for each current action row. The standard actor then computes:

```python
logits = einsum("bc,bnc->bn", f_state, f_actions)
```

and masks padded rows.

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

The candidate scorer itself is an architectural strength. The problematic part is that physical width is allowed to redefine the legal gameplay domain.

## 10.2 Candidate-set interaction

The state embedding also contains a pooled summary of valid candidate embeddings, giving the model an O(N) summary of the current option set. An optional FiLM actor allows richer candidate interaction at higher cost.

This is useful precedent for OCGForge:

- independent candidate scoring is a strong baseline;
- a cheap pooled set summary can introduce context without quadratic attention;
- full candidate-set attention should remain optional.

## 10.3 Duplicate candidate-feature issue

Distinct engine responses can in some cases produce identical encoded candidate rows. The standard scorer has no durable semantic candidate identity equivalent to OCGForge’s semantic key.

Classification:

```text
AUDITABILITY_LIMITATION
and potentially a POLICY_REPRESENTATION_LIMITATION
```

This is separate from `max_options`.

---

# 11. Why ygo-agent likely chose a fixed width

The fixed shape fits EnvPool, large numbers of parallel environments, JAX/XLA, static accelerator shapes, dense GPU batches, TFLite inference, compact byte tensors, and simple scalar action indices.

The correct classification is:

```text
fixed tensor width:
SOUND_DIFFERENT_DESIGN + PERFORMANCE_TRADEOFF

silent prefix resize on semantic overflow:
SEMANTIC_LIMITATION
```

A static 24-row tensor would be sound if all supported local domains were proven to fit, every larger decision were losslessly decomposed, overflow failed closed, or a wider/bucketed representation were selected before semantics were lost. The inspected repository proves none of those as a general invariant.

---

# 12. Supported-deck restrictions do not prove the bound

The repository has a legitimate supported-slice strategy, but no invariant was found proving all supported local action domains are at most 24; no candidate-count census, overflow exception, test at 25 options, documented semantic-safety claim, or per-family maximum proof was found.

Classification:

```text
SUPPORTED_SLICE_ASSUMPTION
+
EVIDENCE GAP
```

Card-support restrictions and action-representation restrictions must remain separate.

---

# 13. Legality and hidden-information ownership

The base option lists normally originate from engine messages. The model does not independently reconstruct legality. The adapter then owns the effective transformation:

```text
engine message
→ LegalAction/options vector
→ optional decomposition/filter/fallback
→ max_options prefix
→ model
```

The overflow prefix is insertion-order truncation, not a privileged hidden-information heuristic. Other adapter paths do apply policy reductions such as prohibiting unselection, deterministic counter allocation, card-sorting fallback, and unsupported exceptions.

OCGForge’s policy boundary remains:

```text
PlayerObservation
+
DecisionRequest
+
complete ActionCandidate domain
```

The future model adapter may tensorize candidates but may not reconstruct or reduce them.

---

# 14. Determinism and ordering

In ygo-agent, option order is callback-stable within a current request and generally derives from engine order, fixed category concatenation, vector insertion order, and some sorted integer prefixes. This is sufficient for callback correctness but is not a versioned semantic identity.

“Option 7” therefore means the seventh row in this exact adapter-produced vector, not a stable semantic action independent of implementation details.

OCGForge uses `semantic_key` and rejects empty or duplicate keys, terminal candidates without exact response bytes, intermediate candidates with response bytes, and unknown/stale keys. This is materially stronger for replay and auditability.

---

# 15. Replay consequences

An `action=7` value alone cannot reconstruct the exact semantic choice in ygo-agent. A reader also needs the exact environment implementation and commit, rules/card/script inputs, state, ordered option vector, callback semantics, and relevant internal continuation state.

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

At the inspected M4 head, `canonical_simulation.cpp` validates each current request and counts `request.candidates.size()` for both initial and continuation-local requests. Therefore `candidate_max` means the largest size of one model-facing `DecisionRequest.candidates` vector observed by the simulation policy after protocol transformation.

The historical aggregate records:

```text
candidate_max   = 1344
candidate_sets  = 39519
candidate_total = 187025
```

with zero historical `truncated` counters.

However, no committed per-request witness was found identifying the job ID, message family, raw message hash, decision kind, continuation kind/step, observation hash, full semantic-key set, or exact action prefix producing the 1,344 request.

The strongest accurate statement is:

**FACT:** OCGForge has a committed historical aggregate measurement of a validated single-request candidate count of 1,344.

**UNKNOWN:** the exact gameplay decision that produced it cannot be reconstructed from the inspected committed evidence.

**FACT:** OCGForge has a directly reconstructable unit test with 128 ordering items producing 129 current candidates without truncation.

Evidence ladder:

```text
129 candidates:
    explicit source-level test witness

1344 candidates:
    committed historical aggregate,
    no per-request witness in the inspected snapshot
```

---

# 17. Are 24 and 1,344 semantically comparable?

Not directly.

| Number | Actual meaning |
|---|---|
| `24` in normal ygo-agent training | Configured maximum simultaneous model-visible option rows |
| `>24` internal ygo-agent vector | Possible pre-tensor adapter domain, later prefix-truncated |
| `1,344` in OCGForge M4 | Historical maximum of one validated `DecisionRequest.candidates` vector |
| `129` in OCGForge test | Explicitly reconstructed complete ordering continuation domain |

The correct comparison is not `24 < 1344`; it is whether the same gameplay decision is losslessly represented, whether overflow is explicit, and whether the chosen semantic response can be replayed.

---

# 18. Controlled source-level case studies

## Case A — Small domain: `MSG_SELECT_YESNO`

Two direct candidates, padded to 24 and scored candidate-relatively.

Classification:

```text
C_model == C_engine
LOSSLESS_REENCODING
```

## Case B — Near boundary: 23 chainable effects plus cancel

```text
23 activations + cancel = 24
```

All options remain model-visible.

## Case C — Exceeding boundary: 24 chainable effects plus cancel

```text
24 activations + cancel = 25
resize(24)
→ cancel removed
```

Classification:

```text
C_model ⊂ C_engine
TRUNCATED
SEMANTIC_LIMITATION
```

## Case D — More than 24 original responses, losslessly decomposable

```text
choose exactly 2 cards from 10
→ 45 legal subsets
```

Modern `ygopro` can represent this sequentially with at most 10 then 9 simultaneous choices. EDOPro flatly enumerates 45 combinations and truncates to 24.

---

# 19. Tests and evidence coverage

## ygo-agent

No focused test was found covering:

```text
n_options == max_options
n_options == max_options + 1
very large n_options
overflow status
prefix retention
cancel-after-boundary behavior
```

No candidate-count census proving every supported deck/message family remains within 24 was found.

This is an evidence gap; the source-proven truncation itself remains unambiguous.

## OCGForge

Relevant tests cover fail-closed unsupported behavior, duplicate option payloads remaining distinct semantic actions, a 128-item ordering decision exposing 129 candidates, continuation lifecycle, stale-key rejection, exact terminal response, and continuation completeness via independent oracle logic.

The major remaining gap exposed by this comparison is the absence of a committed replayable witness for the historical 1,344-candidate request.

---

# 20. Direct contract comparison

| Property | ygo-agent / ygoenv | OCGForge | Consequence |
|---|---|---|---|
| Legality owner | Engine base lists; adapter may filter/auto-resolve | Engine + protocol adapter | OCGForge has stronger completeness rules |
| Candidate representation | `LegalAction`/string option → compact row | Typed `ActionCandidate` + semantic key | OCGForge more explicit/auditable |
| Fixed maximum | Configurable `max_options`, normally 24 | No gameplay-semantic global cap | Future adapter must not add one |
| Overflow behavior | Silent insertion-order prefix resize | Supported domains must not truncate | Material semantic difference |
| Combinatorial handling | Mixed sequential/flat/restricted | Adapter-local semantic continuations | Both can decompose; guarantees differ |
| Continuation identity | Internal state/index | Explicit semantic identity | OCGForge stronger for stale/replay checks |
| Complete-domain guarantee | No general guarantee found | Required for supported families | OCGForge materially stronger contractually |
| Candidate identity | Local index + encoded row | Semantic key | Index alone weaker |
| Replay identity | Needs exact option vector/implementation | Domain + chosen semantic key | OCGForge intended replay self-describing |
| Batching model | Dense static `[B,max_options,F]` | Future adapter not yet implemented | ygo-agent more mature operationally |
| Failure behavior | Mixed truncate/throw/auto/fallback | Fail closed | Different research priorities |
| Largest model-visible domain | 24 under normal config | Historical 1,344; explicit test 129 | Counts have different semantics/evidence |

---

# 21. Classification of important ygo-agent choices

| Behavior | Classification | Reason |
|---|---|---|
| Candidate-relative action rows | `SOUND_DIFFERENT_DESIGN` | Avoids giant global vocabulary |
| Static `max_options` width | `PERFORMANCE_TRADEOFF` | Efficient for EnvPool/JAX/TFLite |
| Padding/masking | `SOUND_DIFFERENT_DESIGN` | Standard fixed-shape batching |
| Shared state–candidate scoring | `SOUND_DIFFERENT_DESIGN` | Natural variable-option policy within bound |
| Candidate-set pooled summary | `SOUND_DIFFERENT_DESIGN` | Adds O(N) set context |
| Prefix `resize(max_options)` | `SEMANTIC_LIMITATION` | Removes reachable legal actions |
| No overflow flag/test | Evidence gap | Intent/frequency undocumented |
| Sequential multi-select | `SOUND_DIFFERENT_DESIGN` | Can losslessly encode large response spaces |
| Local sequential domain still capped | `SEMANTIC_LIMITATION` | Decomposition insufficient if local branching exceeds cap |
| EDOPro flat combination truncation | `SEMANTIC_LIMITATION` | Removes full legal subset responses |
| Ignored unselect choices | `SEMANTIC_LIMITATION` | Reduces engine-provided interaction |
| Counter auto-allocation | `SUPPORTED_SLICE_ASSUMPTION` / `SEMANTIC_LIMITATION` | Selects one allocation instead of exposing all |
| Sort-card fallback | `SUPPORTED_SLICE_ASSUMPTION` | Keeps games running without model ordering |
| Index-only action identity | `AUDITABILITY_LIMITATION` | Requires exact external context |
| Duplicate feature rows | `AUDITABILITY_LIMITATION` | Distinct responses may be policy-indistinguishable |
| `num_options` upper-bound mismatch | `POTENTIAL_BUG` | Count can equal declared maximum width |

---

# 22. What OCGForge should learn from ygo-agent

## P0 — First model adapter

Copy the concepts of:

```text
candidate-relative action encoding
shared observation encoding
one shared scorer
padding masks
compact candidate features
O(N) candidate-set pooling
```

Do not use a permanent global action vocabulary.

## P1 — Likely useful

- candidate-count bucketing;
- static JAX shapes per bucket;
- separate player action histories;
- recurrent support;
- actor/learner separation;
- vectorized CPU environments;
- compact integer transport formats.

## P2/P3 — Later

FiLM, candidate attention, deployment formats, specialized action encoders, full EnvPool-like vectorization, and large distributed training should remain evidence-driven later work.

---

# 23. What OCGForge should explicitly not copy

1. Do not call `resize(max_options)` on the authoritative candidate vector.
2. Do not let a global tensor width define gameplay semantics.
3. Do not store only `action_index` in a trajectory.
4. Do not remove cancel, finish, unselect, or tail candidates merely because they are inconvenient to encode.
5. Do not auto-resolve a required player choice to keep a duel running.
6. Do not enumerate a combinatorial response space flatly and then keep only the first N combinations.
7. Do not allow distinct semantic candidates to collapse into indistinguishable identities.
8. Do not rely on insertion order as durable replay identity.
9. Do not treat a supported-deck list as proof that overflow cannot occur.
10. Do not omit explicit overflow/failure evidence.

---

# 24. Extreme-domain computational cost

Recommended computation:

```text
h = ObservationEncoder(observation)          # once

e_i = CandidateEncoder(candidate_i)          # N times

logit_i = Scorer(h, e_i)                     # N times

policy = segmented_softmax(logits)           # exactly N legal candidates
```

The observation encoder must not be rerun 1,344 times.

For 1,344 candidates and width 128, candidate embeddings require roughly 336 KiB in FP16 or 672 KiB in FP32; 1,344 FP32 logits are only about 5.25 KiB. A modest candidate encoder is computationally practical. The extreme count is not a justification for semantic truncation.

The historical average was about 4.73 candidates/request, so padding every request globally to 1,344 would waste more than 99% of candidate slots for a typical request.

Recommended physical strategies:

```text
ragged flat arrays
request offsets
candidate-count buckets
batch-local padding
optional exact chunking for extremes
```

---

# 25. Candidate interactions

The correct first baseline is independent candidate scoring conditioned on the observation and decision context, O(N), optionally enhanced with an O(N) pooled candidate-set summary.

Full candidate-to-candidate attention is O(N²) and should be introduced only after a controlled experiment demonstrates strategic need.

---

# 26. Specific recommendation for OCGForge’s first action adapter

## Owning layer

```text
future model-facing tensor adapter
```

It sits above `PlayerObservation + DecisionRequest + ActionCandidate` and below the neural policy. It must not modify the protocol-layer candidate vector.

Recommended interface:

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

Required semantics:

- `N_b` is not capped by gameplay semantics.
- Every `ActionCandidate` receives exactly one tensor row.
- Padding exists only inside physical batch buckets.
- Padding rows never enter the candidate domain.
- The model returns a local index plus associated semantic key.
- The environment verifies that key against the unchanged request.
- Trajectories retain the complete candidate domain and chosen key.
- Tensorization failure rejects the sample/request; it never drops candidates.

For JAX/XLA, compile measured candidate-count buckets rather than one global shape. Exact bucket boundaries are operational choices, not contracts.

OCGForge should copy candidate row encoding, shared state encoding, shared scoring, masks, and static compiled buckets; it should replace `global max_options + authoritative resize` with `ragged complete domain + batch-local physical shape + fail-closed tensorization`.

---

# 27. OCGForge findings classification

## BLOCKER — Global model-width truncation

Any OCGForge tensor adapter that removes candidate 25+ to fit a neural shape would violate the complete-domain contract.

## BLOCKER — Index-only trajectory identity

A trajectory containing only `action=7` without the complete candidate domain and semantic key cannot satisfy OCGForge replay requirements.

## BLOCKER — Automatic resolution of required player decisions

Counter allocation, ordering, cancel, or unselection must not be automatically reduced unless removed responses are proven semantically equivalent.

## BLOCKER — Duplicate semantic candidates collapsing in the tensor adapter

Distinct semantic actions must remain separately identifiable and selectable.

## MAJOR — Global padding to largest observed domain

Padding all requests to 1,344 is inefficient and creates pressure to later reduce semantics.

## MAJOR — Treating historical 1,344 aggregate as a complete witness

The exact message family and action set are unavailable in the inspected snapshot. Architecture should not be optimized around an unexplained maximum without a reproducible request artifact.

## MAJOR — Failing to test continuation-local overflow

No-truncation gates must cover initial engine requests and every continuation-local request.

## MINOR — No per-family candidate-count distribution

A future model adapter should record histograms by decision family, continuation kind/step, deck, phase, and candidate feature shape.

## NOTE — Candidate scoring and fixed-shape acceleration are compatible

The choice is not complete semantics versus efficient batching. Ragged logical domains can be transformed into efficient bucketed physical batches.

---

# 28. Final verdict by implementation

## Overall

# **F — Mixed by decision family**

## `ygoenv/ygopro` / `YGOPro-v1`

- direct options truncate;
- several combinatorial choices are sequentialized;
- local continuation domains still truncate;
- some legal forms throw;
- some actions are filtered or auto-resolved.

## `ygoenv/ygopro0` / `YGOPro-v0`

- same fixed-width prefix truncation;
- older string-option encoding;
- internal multi-step handling for some families.

## `ygoenv/edopro` / `EDOPro-v0`

- same prefix truncation;
- flat combination enumeration for important multi-card decisions;
- separate `max_multi_select` bound;
- unsupported or automatic handling for other cases.

---

# 29. Direct answers

## 1. What exactly does `max_options=24` control?

The first dimension of the model-facing action tensor, scalar action-index range, maximum number of policy logits, padding width, random-baseline sampling range, and—through `resize(max_options)`—the number of options retained for model selection. It is both an ML batching parameter and an effective semantic cap.

## 2. Can ygo-agent represent more than 24 legal choices?

It cannot expose more than 24 simultaneous model-visible choices under the normal configuration, but can represent more than 24 original engine responses when a decision is losslessly sequentialized into local steps that each fit within 24.

## 3. If yes, how?

Through internal multi-select sequentialization that holds the engine message open, collects partial choices, and submits one final response.

## 4. If no, what happens to choice 25+?

For a direct or continuation-local domain above 24, `vector.resize(24)` removes choice 25 and every later choice. They are not encoded, masked, paginated, or recoverable by the model.

## 5. Is any legal candidate silently dropped?

Yes. The direct `MSG_SELECT_CHAIN` handler can build 24 activation choices followed by a legal cancel; at width 24 the cancel is silently removed.

## 6. Is large-action handling lossless?

Not generally. Some supported sequentialized families can be lossless if every local domain fits the cap, but direct overflow, EDOPro flat-combination overflow, ignored unselection, automatic counter allocation, and other reductions are not lossless.

## 7. Does behavior differ by decision family?

Yes: direct menus, sequential decompositions, flat combinations, unsupported exceptions, heuristic filtering, automatic fallbacks, and silent truncation all exist.

## 8. Does behavior differ between `ygopro` and `edopro`?

Yes. Both truncate to `max_options`; modern `ygopro` sequentializes several combinatorial choices, while `edopro` flatly enumerates full combinations and then truncates.

## 9. Largest candidate domain proven in ygo-agent?

- Model-visible under normal configuration: **24**.
- Source-proven pre-truncation domain: **>24**; a 25-option chain case is directly constructible.
- Largest domain observed in a committed supported-duel census: **unknown**.

## 10. Largest candidate domain proven in OCGForge?

- Explicit reconstructable unit-test domain: **129**.
- Historical aggregate: **1,344 candidates in one validated request**.
- Exact historical 1,344 request witness: not found in the inspected snapshot.

## 11. Are those counts semantically comparable?

No. `24` is a configured post-transformation model width; `1,344` is a protocol-level request-domain measurement before any ML tensor adapter.

## 12. Candidate scoring or fixed action vocabulary?

Candidate scoring. The model encodes each current row and emits one logit per row; it is not a fixed global Yu-Gi-Oh! action vocabulary.

## 13. Is option ordering semantically stable?

Callback-stable within a request, but not a versioned semantic identity.

## 14. Can a ygo-agent trajectory reconstruct the exact semantic action?

`action=7` alone cannot. Exact candidate tensor, environment version, state, continuation state, and callback semantics are required.

## 15. Does OCGForge continuation solve a problem ygo-agent handles differently?

Yes. OCGForge formalizes continuation identity, complete next-choice domains, stale-action rejection, no intermediate engine response, exact terminal response, and replay semantics; ygo-agent’s continuation remains internal and subject to `max_options` truncation.

## 16. What should OCGForge copy?

Candidate-relative encoding, shared observation encoding, one shared scorer, padding masks, compact candidate features, O(N) candidate-set pooling, and static candidate-count buckets.

## 17. What should OCGForge avoid copying?

Authoritative vector resize, global semantic action caps, index-only replay identity, ignored cancel/unselect actions, automatic player-choice resolution, flat combination truncation, silent overflow, and candidate feature collisions without sufficient discrimination.

## 18. Is OCGForge materially stronger or merely different?

Materially stronger **for supported decision families** in completeness, fail-closed behavior, semantic identity, continuation auditability, stale-action rejection, and replay integrity. ygo-agent remains more mature operationally as an ML/training stack.

## 19. Biggest unresolved evidence gap?

The exact OCGForge request that produced `candidate_max=1344` is not committed as a replayable witness in the inspected snapshot, and ygo-agent has no committed supported-corpus candidate census showing how often pre-truncation domains exceed 24.

## 20. What single experiment/test should OCGForge run next?

Run a **deterministic candidate-domain witness test** over the canonical fixed matchup. For every request record:

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

Later, when a tensor adapter exists, test boundary cases including `N=24`, `N=25`, `N=129`, and the reconstructed historical maximum, asserting:

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
