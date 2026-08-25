# OCGForge Testing and Acceptance

OCGForge uses layered evidence because “the duel completed” is not sufficient proof for a deterministic game-AI environment.

## Evidence layers

```text
dependency identity
      ↓
core lifecycle
      ↓
protocol semantics
      ↓
observation/privacy
      ↓
determinism/trace
      ↓
focused mechanics
      ↓
full fixed games
      ↓
machine-readable acceptance
```

A higher layer does not replace the lower layers.

## 1. Rules-bundle verification

```text
python tools/fetch_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
```

Purpose:

- exact dependency revisions;
- expected hashes;
- reproducible local materialization;
- no floating/sibling checkout dependency.

## 2. Repository Python tests

```text
python -m unittest discover -s tests/python -v
```

These cover repository tooling such as fixture/deck/rules-bundle/patchset assumptions.

## 3. Configure/build/CTest

Native Windows:

```text
cmake --preset dev-windows
cmake --build --preset dev-windows --parallel
ctest --preset dev-windows --output-on-failure
```

Zig fallback:

```text
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig --parallel
ctest --preset dev-windows-zig --output-on-failure
```

CTest spans core, protocol, observation, privacy, trace, M3 fixtures, and M3.5 public-core behavior.

## 4. Decision coverage validator

```text
python tests/protocol/decision_coverage_test.py
```

This checks the decision-family inventory.

The classification matters:

- engine-verified;
- protocol-verified;
- unsupported fail-closed;
- out of scope.

Do not treat all of those as equivalent support.

## 5. Observation coverage validator

```text
python tests/observation/observation_coverage_test.py
```

This checks the documented observation/event inventory.

## 6. Controlled duel smoke

```text
build/dev-windows/ygo_core_probe.exe --max-steps 1000 --output artifacts/probe-trace.jsonl
```

The probe should exit nonzero if it encounters an unsupported required interactive message.

This is a feature: unsupported must not be silently auto-answered.

## 7. Independent-process determinism

The repository includes:

```text
python tests/determinism/determinism_test.py --probe build/dev-windows/ygo_core_probe.exe
```

Independent processes are important because pointer/layout/incidental process state must not become semantic identity.

## 8. Player-view and observation privacy

Privacy tests should establish that changes in hidden opponent identity do not alter the player-visible observation when the game-visible facts are equivalent.

Use paired-world tests for high-risk fields.

## 9. M3 Python validation

```text
python -m unittest discover -s tests/m3 -v
```

These validate M3 manifests/audits/coverage logic in addition to C++ engine fixtures.

## 10. M3 full fixed-game matrix

The accepted PR documented the canonical form:

```text
python tests/m3/full_game/full_fixed_deck_test.py \
  --probe build/windows-zig/ygo_core_probe.exe \
  --games 16 \
  --output artifacts/m3/canonical_mr5/full_games \
  --timeout 300
```

Use the equivalent native probe path when appropriate.

Acceptance should inspect more than terminal count:

- unsupported decisions;
- retries;
- automatic decisions;
- candidate truncation;
- core errors;
- starting-player partition;
- deck-seat partition;
- deterministic identities.

## 11. M3 determinism/replay matrix

The accepted PR documented:

```text
python tests/m3/determinism/m3_determinism_test.py \
  --probe build/windows-zig/ygo_core_probe.exe \
  --output artifacts/m3/canonical_mr5/determinism \
  --timeout 300
```

The acceptance evidence covers:

- independent-process semantic gameplay identity;
- semantic-action re-execution;
- CRLF replay handling;
- both starting-player partitions.

## 12. M3.5 public-core tests

M3.5 specifically covers:

- individual Xyz-material query ordering/bounds/empty behavior;
- starting-player default/explicit behavior;
- invalid starting-player rejection;
- post-start mutation rejection;
- opening ownership/state;
- integration back into M3 full games and determinism.

## 13. CI

The current GitHub Actions workflow is Windows-native and runs on pushes and pull requests.

It performs:

- exact rules-bundle fetch/verify;
- Python repository tests;
- decision coverage validation;
- observation coverage validation;
- CMake configure/build;
- CTest;
- controlled-duel smoke;
- independent-process determinism;
- hidden-information test;
- observation determinism;
- generated-lock diff check;
- failure diagnostic upload.

## 14. Reporting rules

### Fresh verification

Say:

```text
PASS — command executed successfully in this environment
```

only when the command was actually run.

### Historical evidence

Say:

```text
repository-recorded PASS at <commit/evidence>
```

when relying on committed acceptance artifacts without rerunning.

### Partial verification

If a required command cannot run, report the exact limitation.

Do not substitute “looks correct” for an acceptance gate.

## 15. Adding a new acceptance gate

A useful gate has:

- a precise scope;
- deterministic input identity;
- an observable pass/fail condition;
- failure diagnostics;
- machine-readable evidence when long-lived;
- no dependency on manually interpreting success from logs alone.

Prefer semantic assertions over snapshotting incidental build output.
