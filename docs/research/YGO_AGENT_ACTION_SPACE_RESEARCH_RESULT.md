# ygo-agent Action-Space Research Result

Status: **COMPLETED RESEARCH SNAPSHOT — 25 August 2026**.

This file records the interpretation and project consequences of the completed research task in `YGO_AGENT_ACTION_SPACE_RESEARCH_TASK.md`. The full completed report is preserved in:

- `YGO_AGENT_ACTION_SPACE_RESEARCH_REPORT_2026-08-25.md`

The report is research evidence and architectural input. It is not a versioned gameplay contract and does not authorize ML implementation by itself.

## Primary verdict

**F — Mixed by decision family.**

The inspected `sbl1996/ygo-agent` / `ygoenv` stack uses a strong candidate-relative policy design, but `max_options=24` is not only padding. In the inspected `ygopro`, `ygopro0`, and `edopro` implementations, model-facing option vectors are prefix-resized to `max_options` when larger. That means physical tensor width can become an effective gameplay-semantic cap when a direct or continuation-local legal domain exceeds the configured width.

This does not mean every ygo-agent decision is truncated. Several combinatorial families are sequentialized and can losslessly represent more than 24 original engine responses when every local branching domain fits the bound. Other families are restricted, filtered, auto-resolved, or unsupported. Hence the mixed verdict.

## OCGForge consequences

### BLOCKER — never introduce semantic candidate truncation

A future model adapter must never remove candidate 25+ merely to fit a fixed tensor shape. `DecisionRequest.candidates` remains authoritative and complete for supported families.

### BLOCKER — no index-only trajectory identity

A trajectory must retain the complete candidate domain and chosen semantic action identity. A bare `action_index` is insufficient for replay/auditability.

### BLOCKER — do not auto-resolve required player decisions

Cancel, finish, unselect, ordering, allocation, or other required player choices must remain explicit unless the removed responses are proven semantically equivalent.

### MAJOR — obtain a reproducible large-domain witness

Historical M4 evidence recorded `candidate_max = 1344`, but the research snapshot could not reconstruct the exact request that produced it. M4 finalization should produce a deterministic per-request maximum-domain witness with semantic identity and provenance rather than relying only on an aggregate maximum.

### MAJOR — continuation-local domains need the same no-truncation guarantee

Lossless decomposition is valid, but every continuation-local `DecisionRequest` must remain complete. The model adapter must not cap continuation-local branching.

## What OCGForge should copy conceptually

P0 ideas worth carrying into the first model adapter:

- candidate-relative action encoding;
- encode `PlayerObservation` once per request;
- one shared candidate scorer;
- compact typed candidate features;
- padding masks for absent batch slots only;
- O(N) pooled candidate-set summary;
- candidate-count bucketing / static compiled shapes as physical optimization.

## What OCGForge should not copy

Do not copy:

- authoritative `resize(max_options)` behavior;
- a global gameplay-semantic action cap;
- index-only replay identity;
- silent overflow;
- ignored cancel/unselect/tail choices;
- automatic required-player decisions;
- flat combinatorial enumeration followed by truncation.

## First model-adapter direction

The recommended logical representation is:

```text
PlayerObservation batch
        ↓
ObservationEncoder once per request
        ↓
shared request embedding

flat complete ActionCandidate array
        ↓
CandidateEncoder once per candidate
        ↓
shared scorer
        ↓
exactly one logit per legal candidate
        ↓
segmented softmax by request
```

Physical batching may use ragged arrays, offsets, candidate-count buckets, batch-local padding, and exact chunking for extreme domains. Physical representation must never alter the logical candidate set.

## Scope boundary

The research recommendation for tensor boundary tests (`N=24`, `N=25`, `N=129`, reconstructed large-domain witness) belongs to the future model-facing adapter / `TRAINING_READY` work.

The only immediate M4-relevant follow-up is the deterministic large-domain witness/evidence improvement. Do not expand M4 into tensorization or ML implementation.

## Historical-status note

The completed report contains a live-repository snapshot from its inspection date. That section is historical. In particular, PR #3 was open during the research and was merged later on 25 August 2026. Current repository state must always be read live rather than inferred from the report.
