# Contributing to OCGForge

OCGForge is a correctness-first deterministic simulation project. Small, reviewable changes with explicit evidence are preferred over broad feature patches.

## Before changing code

Read:

- `AGENTS.md`;
- `docs/PROJECT_CHARTER.md`;
- `docs/NORMATIVE_HIERARCHY.md`;
- `docs/CURRENT_PROJECT_STATE.md`;
- the relevant contract, ADR, coverage inventory, and tests.

## Development baseline

The canonical hosted integration path is Windows.

Native development:

```text
cmake --preset dev-windows
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows --output-on-failure
```

Repository Python tests:

```text
python -m unittest discover -s tests/python -v
```

Decision/observation inventory checks:

```text
python tests/protocol/decision_coverage_test.py
python tests/observation/observation_coverage_test.py
```

M3 Python tests:

```text
python -m unittest discover -s tests/m3 -v
```

See `docs/TESTING.md` for the larger verification matrix.

## Change design

Before implementation, identify which boundary owns the change:

- `ygo::core` — pinned core lifecycle and public query boundary;
- `ygo::protocol` — engine-message decoding and legal decision representation;
- `ygo::observation` — perspective-safe state/history projection;
- `ygo::trace` — deterministic trace semantics;
- `ygo::m3` — fixed-matchup conformance policy/evidence;
- `tools/` — reproducible dependency and evidence tooling;
- `third_party/` — canonical dependency identity and ordered repository patches.

Avoid introducing a second source of truth.

## Pull requests

A strong PR description states:

- problem and scope;
- contract/ADR affected;
- deterministic and privacy impact;
- rules-bundle impact;
- tests actually executed;
- evidence regenerated;
- known non-blockers;
- explicit non-goals.

Do not claim support beyond the tested slice.

## Rules-bundle changes

Changing a dependency pin, patch, rules mode, locked deck, or canonical bundle identity is a migration, not routine cleanup.

Such a change should include:

- rationale;
- old and new identity;
- affected compatibility scope;
- deterministic re-verification;
- privacy regression;
- regenerated acceptance evidence.

## Decision-protocol changes

For a new message family or continuation:

- prove the legal domain from the pinned engine protocol;
- preserve every legal candidate;
- use stable semantic keys;
- test malformed frames;
- test stale actions;
- test continuation immobility;
- test exact final response bytes;
- fail closed when completeness is not proven.

## Observation changes

For a new observation field or event:

- state why the field is visible to the selected perspective;
- test both players;
- use paired-world tests where hidden identity could leak;
- define canonical serialization order;
- update `player-observation-v1` only when the public contract truly changes;
- version the schema for incompatible semantic changes.

## Performance changes

Performance work must preserve semantic equivalence.

Benchmark first, then optimize. Do not introduce:

- candidate truncation;
- lossy observation compression at the authoritative layer;
- hidden-state shortcuts;
- nondeterministic parallelism in authoritative ordering;
- floating dependency state.

## Documentation

Keep current summaries short and point to normative sources.

Historical implementation plans belong in historical/provenance directories and should not override accepted contracts.
