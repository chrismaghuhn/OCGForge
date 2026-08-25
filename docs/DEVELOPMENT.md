# OCGForge Development Guide

## Supported development shape

The repository's canonical integration path is Windows.

Primary local preset:

```text
dev-windows
```

Fallback preset:

```text
dev-windows-zig
```

The fallback is still Windows-oriented and uses repository-local ignored toolchain binaries.

## Prerequisites

For native `dev-windows`:

- Windows;
- Visual Studio/MSVC developer environment;
- CMake;
- Ninja;
- Python.

Hosted CI currently sets up Python 3.11.

## Clean native build

```text
cmake --preset dev-windows
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows --output-on-failure
```

The configure preset enables automatic rules-bundle fetching.

Build directory:

```text
build/dev-windows
```

## Zig fallback

```text
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
```

Build directory:

```text
build/windows-zig
```

The Zig/Ninja cache is not part of the canonical rules bundle.

## Rules bundle

Fetch and verify explicitly:

```text
python tools/fetch_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
```

Important:

- `.cache/rules_bundle` is disposable materialization;
- `third_party/rules_bundle.lock.json` is canonical identity;
- the pinned base core should remain immutable;
- repository patches are applied through the patchset workflow, not by editing cached upstream source.

## Repository modules

### `include/ygo/core` and `src/core`

Trusted engine boundary:

- `CoreHost`;
- rules-bundle paths;
- seed bundle;
- card-data access;
- script/database callbacks.

### `include/ygo/protocol` and `src/protocol`

Player decision boundary:

- message decoding;
- semantic candidates;
- continuations;
- response construction;
- protocol errors.

### `include/ygo/observation` and `src/observation`

Perspective-safe state/history boundary:

- query decoding;
- card/zone projection;
- visible event projection;
- observation builder/session;
- canonical serialization.

### `include/ygo/trace` and `src/trace`

Trace and hashing support.

### `include/ygo/m3` and `src/m3`

Locked fixed-matchup conformance policy.

### `tools/`

Python orchestration and reproducibility tooling.

Notable areas:

- rules-bundle fetch/verify;
- ocgcore patchset preparation/verification;
- M3 deck/catalog/coverage/audit tooling;
- core and observation probes.

### `fixtures/`

Versioned focused engine fixtures and locked deck inputs.

Fixtures are evidence tools, not arbitrary production state mutation.

### `tests/`

Tests are grouped by semantic concern:

- core lifecycle/conformance;
- protocol;
- observation;
- privacy;
- determinism;
- trace;
- M3;
- M3.5;
- repository Python tooling.

## Working on protocol code

When adding or changing an interactive engine message:

1. identify the exact pinned message wire format;
2. define the semantic request kind;
3. prove complete legal candidate enumeration;
4. define deterministic ordering and semantic keys;
5. use continuation if a combinatorial family should remain primitive;
6. define exact terminal response bytes;
7. reject malformed input;
8. reject stale actions;
9. add protocol/oracle tests;
10. add a real engine fixture when feasible;
11. update decision coverage classification.

Do not mark parser coverage as engine verification.

## Working on observations

For each field/event:

1. identify the perspective visibility rule;
2. identify the public query/event evidence;
3. define null/redacted/omitted behavior;
4. define deterministic serialization order;
5. add paired-world privacy tests when hidden identity could affect output;
6. add canonical hash regression;
7. update coverage inventory;
8. migrate the schema version if semantics become incompatible.

## Working on M3 support

M3-style support is evidence closure, not only code.

A new fixed matchup should consider:

- exact decks;
- card database resolution;
- script resolution;
- rules mode;
- decision-family requirements;
- observation requirements;
- mechanics matrix;
- focused fixtures;
- full games;
- determinism;
- privacy;
- failure counters;
- machine-readable acceptance output.

## Diagnostics

The core probe deliberately supports a fail-closed diagnostic path:

```text
build/dev-windows/ygo_core_probe.exe --force-unsupported
```

A useful diagnostic should preserve:

- message type/name;
- raw-message hash;
- engine step;
- acting player;
- bundle identity;
- deck identity;
- seed input;
- recent trace context.

Diagnostics must not silently submit a fabricated engine response.

## Generated artifacts

Treat generated evidence as derived data.

When a validator fails:

- find the source mismatch;
- fix source/generator/behavior;
- regenerate;
- do not simply patch the expected JSON until green.

## Branch/PR hygiene

Prefer one semantic change per branch.

A PR should make it easy to answer:

- what contract changed?
- what evidence changed?
- did the rules bundle change?
- did privacy change?
- did deterministic identity change?
- what exact commands were run?
