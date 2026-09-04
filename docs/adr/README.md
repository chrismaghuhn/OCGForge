# Architecture Decision Records

Architecture Decision Records (ADRs) document decisions that should remain understandable after the implementation details change.

## Current ADRs

- [ADR-0001 — Build a modern adapter over the pinned OCG C API](ADR-0001-modern-ocg-adapter.md) — **Accepted**
- [ADR-0002 — Use a shared semantic EpisodeDriver for the episodic environment](ADR-0002-episodic-environment.md) — **Accepted**
- [ADR-0003 — Ratify Episodic V1 prerequisite identities](ADR-0003-episodic-v1-normative-prerequisites.md) — **Accepted**
- [ADR-0004 — Define a perspective-safe public episodic action identity](ADR-0004-perspective-safe-episodic-action-identity.md) — **Accepted**
- [ADR-0005 — Define a trusted trajectory core above EpisodicEnvironment V2](ADR-0005-trusted-trajectory-core.md) — **Accepted Phase 3A architectural authority**
- [ADR-0006 — Freeze the framework-neutral model-facing candidate-scoring boundary](ADR-0006-model-facing-candidate-scoring-adapter.md) — **Accepted Phase 5 contract freeze; implementation FINAL PASS**
- [ADR-0007 — Freeze the Phase-6 Behavior Cloning boundary](ADR-0007-phase6-behavior-cloning-boundary.md) — **Accepted Phase 6 Task 1 contract freeze**
- [ADR-0008 — Select PyTorch as the Phase-6 Task7 execution backend](ADR-0008-phase6-pytorch-primary-backend.md) — **Proposed / pending independent Task6 review; Task7 readiness blocked**

## When to write an ADR

Create an ADR before or with a change that establishes a long-lived architectural commitment, such as:

- checkpoint/persistence format;
- RNG ownership or stream derivation;
- environment reset/step contract;
- vectorization/process architecture;
- model-facing vocabulary ownership;
- stable replay/checkpoint compatibility;
- dependency/upstream strategy;
- public API patch/upstreaming policy;
- authoritative state ownership.

Do not use an ADR for routine refactors.

## ADR format

Recommended structure:

```text
# ADR-NNNN: Title

## Status
Proposed | Accepted | Superseded | Rejected

## Context

## Decision

## Alternatives considered

## Consequences

## Compatibility / migration

## Verification
```

## Status rules

### Proposed

Open for review. Not yet architectural authority.

### Accepted

Normative for the architectural question it resolves.

### Superseded

Historical record retained. The replacement ADR must be named.

### Rejected

Historical record of a considered choice.

## Relationship to contracts

An ADR explains **why an architecture is chosen**.

A versioned contract defines **what public semantics mean**.

Do not stuff a full wire/schema contract into an ADR when the semantics need independent versioning.

## Relationship to plans

Implementation plans may describe how to execute an accepted ADR.

Plans do not override an accepted ADR or a later accepted contract.
