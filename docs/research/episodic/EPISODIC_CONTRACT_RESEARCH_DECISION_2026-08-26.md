# Episodic Contract Research Decision Record — 2026-08-26

**Scope:** research input for ADR-0002 and `ocgforge.episodic_environment.v1`; not production acceptance evidence.  
**OCGForge checkpoint reviewed by the research:** `9d5bb9fa4b8c700026dcf9665885c0dcfb1e8047`.  
**M4 semantic source checkpoint:** `a49639bbb7ef8ce3406ac0d9aad295272872dda9`.

## Source report

The design review used the externally supplied report:

`OCGForge_Episodic_Environment_Contract_Validation_2026-08-26.md`

Source artifact properties at review time:

- byte length: `98770`;
- SHA-256: `23ceac7408dd820bfff8d2e9801ccf2520b2403c422f464996e8ab639fca7073`.

The full source report is intentionally not declared as repository acceptance evidence by this decision record. This file preserves the architectural conclusions that were adopted into the proposed ADR/contract.

## Primary systems compared by the research

The report compared current/pinned primary sources for:

- OCGForge live checkpoint;
- Gymnasium;
- PettingZoo;
- OpenSpiel;
- RLlib;
- EnvPool;
- `sbl1996/ygo-agent`;
- ProjectIgnis WindBot;
- EDOPro / ygopro-core as adjacent protocol/rules references.

The report explicitly kept current upstream EDOPro/ygopro-core as research references only; they do not replace OCGForge's certified pinned rules bundle.

## Research determination

Final determination:

> **B — Accept V0.1 with targeted architectural corrections.**

The central hypothesis survived falsification:

> One accepted semantic `ActionCandidate` selection should equal one authoritative environment step.

This includes intermediate continuation actions. A continuation action can change authoritative adapter state, decision context, and the complete legal domain even when ocgcore remains paused.

## Findings adopted into ADR-0002

### BLOCKER adopted — semantic identity is not live freshness

The tuple:

```text
episode_semantic_id
+ semantic decision identity
+ semantic_key
```

cannot safely reject a delayed response across two identical deterministic resets, because those resets should reproduce the same semantic identities.

Adopted correction:

```text
semantic episode identity
semantic decision identity
submission freshness token
semantic_key
```

The freshness token is non-semantic and excluded from gameplay hashes, replay identity, model input, and persisted semantic trajectory meaning.

### MAJOR adopted — one shared EpisodeDriver

Canonical evaluation and the episodic API must not maintain separate advancement loops. The current canonical path already owns the high-risk semantics: fresh duel construction, automatic processing, decode, candidate validation, observation construction, continuation transitions, exact response submission, trace, and terminal handling.

Target architecture:

```text
                    shared EpisodeDriver
                     /              \
                    /                \
canonical evaluator                  episodic facade
policy/replay loop                    reset/step
```

### MAJOR adopted — explicit closure taxonomy

Adopted internal meanings:

- `GameTerminal`: actual engine-defined duel outcome only;
- `Interrupted`: valid semantic prefix deliberately stopped by run control;
- `Failed`: invalid semantic/integrity execution;
- `StepRejected`: caller error caught before mutation; not an episode state.

Framework `terminated/truncated` vocabulary is adapter-only.

### MAJOR adopted — terminal privacy remains active

Episode end does not authorize reveal-all state. Terminal outcome is independent of perspective; any terminal observation remains an explicit perspective-safe `PlayerObservation` view.

### MAJOR adopted — complete ragged candidate domains remain authoritative

The research rejected ygo-agent-style fixed `max_options` truncation and fixed/global action-mask assumptions as authoritative semantics. Padding, masks, candidate scoring, segmented softmax, ragged arrays, and bucketing remain later lossless model-adapter choices.

### MAJOR adopted — policy decision index is not terminal trace indexing

The episodic `decision_index` counts policy decisions only. Existing trace schemas are not silently reinterpreted. New episodic transition metadata uses the clearer `core_response_submitted` concept rather than redefining historical trace `engine_advanced` semantics.

### MAJOR adopted — reward is outside game truth

Authoritative environment semantics expose outcome/status. Numeric reward belongs to a separate versioned reward policy/adapter and cannot change semantic episode/frame/action/gameplay identity.

## Deliberate V1 strengthening beyond the report's minimum

The report treated an evaluation decision budget as deferrable. ADR-0002 makes a count-based `semantic_action_budget` **REQUIRED NOW** in `RunControl`.

Reason: legal reversible continuation behavior can consume repeated semantic actions without increasing `engine_step_index`. An engine-process budget alone therefore cannot bound one live environment slot. The semantic-action budget is explicitly non-semantic and produces `Interrupted(SEMANTIC_ACTION_BUDGET)`, not failure or game outcome.

This strengthening does not cap legal candidates or modify legality. It only limits how long a particular execution is allowed to continue.

## External conventions explicitly not adopted

The proposed V1 does not copy:

- automatic reset;
- entropy/default random reset seeds;
- global/all-player policy state;
- simultaneous action dictionaries;
- fixed global neural action IDs or masks as authoritative legality;
- legal-domain caps/truncation;
- embedded reward/shaping in game semantics;
- silent singleton/default/fallback actions;
- failed episodes relabeled as normal truncations;
- raw CoreHost/client-card state as policy input;
- reveal-all terminal observations;
- PID/time/worker/machine identity in gameplay hashes.

## Deferred after episodic V1

The research and ADR intentionally defer:

- trajectory/shard format;
- behavior/opponent policy provenance;
- tensorization/embeddings/padding/masks;
- actor/learner transport;
- BC/RL/search algorithm selection;
- self-play league/matchmaking;
- checkpoint/fork/restore;
- arbitrary deck/rules expansion;
- reward shaping;
- EDOPro deployment and WindBot Arena integration;
- unrelated performance optimization.

## Acceptance implication

Research does not produce a milestone PASS. Implementation must satisfy `docs/episodic/EPISODIC_V1_ACCEPTANCE.md` and preserve all prior M0–M4 evidence/contracts before any episodic FINAL/PASS claim.
