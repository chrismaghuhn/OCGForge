# OCGForge Project Charter

## Mission

Build a trustworthy, deterministic Yu-Gi-Oh! simulation environment suitable for game-AI research.

OCGForge should make it possible to ask:

- What exact rules/data bundle produced this duel?
- What exact legal choices did the acting player have?
- What information was legitimately visible to that player?
- Can the same inputs reproduce the same semantic result?
- Can a failure be diagnosed without inventing engine behavior?
- What precise card/mechanic/deck slice has actually been certified?

The project should answer those questions before optimizing for training throughput.

## Priority order

When requirements conflict:

```text
correctness
→ determinism
→ information safety
→ decision completeness
→ replayability / auditability
→ maintainability
→ performance
→ ML scale
```

### Correctness

The pinned OCG rules stack is the game-semantics authority. OCGForge should adapt it, not become a second Yu-Gi-Oh! rules engine.

### Determinism

Equivalent canonical inputs should produce equivalent semantic outputs. Incidental implementation details must not become game state or action identity.

### Information safety

The omniscient engine boundary and the player-facing observation boundary are different trust domains.

Hidden information must not leak through identity fields, ordering, hashes, events, locators, diagnostics, or convenience APIs.

### Decision completeness

If OCGForge presents a candidate set as legal and complete, it must represent the full legal domain for the supported engine decision.

No silent truncation. No fabricated default action. No heuristic subset disguised as completeness.

### Replayability and auditability

Important semantic decisions and canonical identities should be inspectable after the fact.

### Maintainability

Dependencies, patches, contracts, evidence, and code ownership boundaries should stay explicit.

### Performance and ML scale

Throughput matters only after the semantic contract is trustworthy. Optimization must preserve the authoritative semantics.

## Architectural stance

OCGForge is an adapter-and-environment project, not a forked rules implementation.

The intended layering is:

```text
Pinned OCG rules/data
        ↓
CoreHost
        ↓
Decision protocol
        ↓
Player-facing environment boundary
        ↓
Observation / trace / replay evidence
        ↓
Future search, teacher, model, or training adapters
```

Python tooling may orchestrate tests, evidence, catalog analysis, or future training workflows. It must not become a second rules engine.

## Current certified scope

At the current M3/M3.5 checkpoint, the strongest certification is the locked fixed matchup:

- `ocgforge.swordsoul_tenyi.ml_v1`;
- `ocgforge.salamangreat.ml_v1`;
- exact 40-card Main Decks and 15-card Extra Decks;
- canonical `TCG_ADVANCED_2026_05_18` / `DUEL_MODE_MR5` configuration;
- repository-recorded 45/45 required mechanics classified;
- repository-recorded 16/16 complete deterministic games over the acceptance matrix.

This does not imply arbitrary-deck or global card support.

## Non-goals at the current checkpoint

The project does not currently promise:

- every OCG message family;
- every card, deck, archetype, or mechanic;
- compatibility with unpinned latest upstream data;
- a user-facing graphical duel client;
- inferred hidden-state beliefs in authoritative observations;
- a trained policy or competitive bot;
- fixed-size action or observation tensors in the authoritative C++ contracts;
- silent recovery from unsupported decisions;
- generalized fixture-only board mutation in production runtime;
- distributed/vectorized training scale before semantic stabilization.

## Success criteria for future expansion

A new scope should be considered trustworthy only when its claims are explicit and evidence-backed.

Typical evidence dimensions are:

- rules-bundle identity;
- card/script/database resolution;
- legal decision completeness;
- observation/privacy behavior;
- deterministic semantic identity;
- replay/re-execution behavior;
- focused mechanics fixtures;
- full-game execution where appropriate;
- failure-path diagnostics;
- regression against earlier certified scope.

## Maintenance principle

Prefer narrow, versioned, reviewable contracts over implicit behavior.

Prefer an honest `unsupported` result over a duel that continues using an invented action.

Prefer a smaller certified scope over a larger unmeasured one.
