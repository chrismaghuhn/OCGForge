# OCGForge Normative Hierarchy

OCGForge contains rules-engine inputs, public environment contracts, implementation code, tests, generated evidence, milestone summaries, and historical plans.

They do not all have the same authority.

## 1. Separate the kinds of authority

There is no single document that is authoritative for every question.

### Game legality and current rules state

Authority:

1. the exact pinned rules bundle;
2. the ordered repository-versioned ocgcore patchset when the canonical bundle includes it.

OCGForge must not override a legal result merely because a higher-level wrapper would prefer another outcome.

### OCGForge public environment semantics

Authority:

1. accepted ADRs for architectural decisions;
2. versioned public contracts under `docs/contracts/`;
3. the public C++ API and implementation that realizes those contracts.

If implementation and an accepted contract disagree, that is a defect or a contract migration — not permission to silently reinterpret the contract.

### Milestone/certification status

Authority:

1. machine-readable coverage and acceptance artifacts;
2. executable tests and fixtures that generate or validate them;
3. human-readable acceptance summaries derived from the evidence.

A README statement cannot promote an unverified capability to `PASS`.

### Dependency identity

Authority:

1. `third_party/rules_bundle.lock.json`;
2. repository-versioned patch files and their declared ordered patchset identity;
3. verification tooling.

Floating upstream branches are not canonical inputs.

## 2. Practical precedence table

| Question | Highest relevant authority |
| --- | --- |
| Is this move/card effect legal? | Pinned rules bundle behavior |
| Which ocgcore revision is canonical? | `third_party/rules_bundle.lock.json` |
| Which local core patches are canonical? | Lock/patchset identity + versioned patch files |
| What does a `DecisionRequest` mean? | `docs/contracts/decision-protocol-v1.md` |
| What may an agent observe? | `docs/contracts/player-observation-v1.md` |
| What does trace v2 mean? | `docs/contracts/engine-trace-v2.md` |
| Is a decision family globally engine-verified? | Decision coverage inventory + evidence |
| Is the fixed M3 matchup closed? | M3 machine-readable acceptance evidence + validating tests |
| What is currently complete? | `docs/CURRENT_PROJECT_STATE.md`, derived from the above |
| What should be built next? | `docs/ROADMAP.md` |
| Why was a major architecture choice made? | Accepted ADR |
| How was an old milestone implemented? | Historical plans/specs, non-normative once superseded |

## 3. Status vocabulary

Use precise status language.

### `PASS`

Use only for a defined acceptance gate with evidence.

A contributor may say a gate *currently passes* only after running the relevant verification in the current environment, or may say the repository *records* a prior PASS.

### `ENGINE_VERIFIED`

The required behavior was exercised through the pinned engine path and accepted by the relevant verification.

### `PROTOCOL_VERIFIED`

The adapter/parser/oracle semantics are verified, but an appropriate real engine fixture has not established the stronger engine path.

Do not rewrite `PROTOCOL_VERIFIED` as `ENGINE_VERIFIED`.

### `UNSUPPORTED_FAIL_CLOSED`

The environment intentionally refuses to fabricate a complete legal domain.

This is a valid safety state, not permission to auto-answer.

### `PENDING`

Evidence is incomplete for the required gate.

### `NOT_APPLICABLE`

The capability is outside the defined milestone/scope. It does not mean globally unsupported or globally certified.

## 4. Historical plans are not current truth

Files under `docs/superpowers/plans/` and `docs/superpowers/specs/` preserve design and implementation history.

After a feature is implemented:

- accepted contracts own public semantics;
- current code owns implementation;
- tests/evidence own certification claims;
- current-state documentation owns the project summary.

Do not resurrect an outdated plan requirement when accepted implementation evidence deliberately changed it.

## 5. Summary documents must derive

`README.md`, `docs/CURRENT_PROJECT_STATE.md`, and `docs/ROADMAP.md` are navigation and status documents.

They should point to the source of truth rather than duplicate large normative tables.

When generated evidence changes, update summaries if the project-level conclusion changes.

## 6. Resolving contradictions

When two sources conflict:

1. identify what type of claim is in conflict;
2. select the authority for that claim using this document;
3. inspect the exact pinned/contract/evidence version;
4. treat implementation-vs-contract disagreement as a defect or explicit migration;
5. do not silently edit only the summary layer;
6. add or update an ADR when the resolution changes architecture.

## 7. New contracts and migrations

Introduce a new contract version when semantics change incompatibly.

Do not change the meaning of a persisted or externally consumed version while keeping its old version identifier.

See `VERSIONING_AND_COMPATIBILITY.md`.
