# OCGForge

OCGForge is a correctness-focused M0 spike for a deterministic Yu-Gi-Oh!
environment. It builds a thin C++ adapter over the public C API of one pinned
OCG core snapshot. It is not a general Yu-Gi-Oh! environment and it does not
contain machine-learning code.

## M0 result

The verified fixture slice uses 40 normal-monster entries per player and
reaches a terminal duel result with the pinned core. The adapter exposes typed
candidate vectors for the controlled protocol slice:

- idle commands
- battle commands
- chain pass/effect choices
- single-card selection
- zone placement
- position selection
- yes/no selection

The terminal trace exercised idle commands, battle commands, chain choices,
single-card selection, and zone placement. Position and yes/no decoding are
covered by protocol tests but were not emitted by this simple fixture.

Other interactive messages fail closed with a structured diagnostic. Candidate
sets are not resized to a fixed maximum. Original card passcodes remain the
environment representation; model vocabulary mapping is deliberately outside
the core and protocol layers.

The probe policy is `m0.deterministic_priority.seeded_tie.v1`. It uses the
complete semantic-key ordering for normal-summon ties and applies the first
seed word only within that tie, so seed sensitivity is explicit without relying
on pointer order, hash-map iteration, wall-clock time, or thread scheduling.

## Reproducible rules bundle

The exact runtime inputs and the deterministic `bundle_id` are recorded in
[third_party/rules_bundle.lock.json](third_party/rules_bundle.lock.json).
The repository-local dependency cache is ignored by Git and is populated by
the exact-commit fetcher:

```text
python tools/fetch_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache .cache/rules_bundle
```

See [THIRD_PARTY.md](THIRD_PARTY.md) for the license records. The complete
project is not represented as MIT-only.

## Windows build

The documented native path uses MSVC and Ninja from a Visual Studio developer
environment:

```text
cmake --preset dev-windows
cmake --build --preset dev-windows
ctest --preset dev-windows
```

The `dev-windows` configure preset verifies or fetches the pinned rules bundle
into `.cache/rules_bundle`. A clean build starts with an empty
`build/dev-windows` and an empty dependency cache; no sibling checkout is
required.

When native MSVC is unavailable, the repository also contains a local fallback
using pinned repository-local Zig 0.14.1 and Ninja binaries:

```text
cmake --preset dev-windows-zig
cmake --build --preset dev-windows-zig
ctest --preset dev-windows-zig
```

Those fallback binaries are ignored cache inputs and are not part of the
rules bundle.

## Useful probes

Run the controlled duel and write a canonical JSONL trace:

```text
build/dev-windows/ygo_core_probe.exe --max-steps 1000 --output artifacts/probe-trace.jsonl
```

The probe exits nonzero on an unsupported interactive message. The deliberate
diagnostic path is:

```text
build/dev-windows/ygo_core_probe.exe --force-unsupported
```

The output contains the message type, raw-message hash, step, player, bundle,
deck hashes, complete seed bundle, and recent trace context. It never submits
a fabricated response.

## Contracts and audit

- [engine trace v1](docs/contracts/engine-trace-v1.md)
- [player view v1](docs/contracts/player-view-v1.md)
- [adapter ADR](docs/adr/ADR-0001-modern-ocg-adapter.md)
- [ygo-agent reference audit](docs/audits/ygo-agent-ygoenv-reference-audit.md)

The pinned `sbl1996/ygo-agent` snapshot is read-only audit material. The
project does not fork its ygoenv implementation.

## Scope boundary

M0 excludes PPO, R2D2, IMPALA, behavior cloning, neural networks, JAX,
PyTorch, MCTS, self-play, web or GUI integration, online multiplayer, full
card support, full engine-message support, EnvPool, and performance
optimization. Later work must extend the typed protocol and privacy boundary
with new tests rather than silently broadening this slice.
