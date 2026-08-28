# OCGForge Architecture

## Overview

OCGForge is a layered adapter around a pinned OCG rules stack.

The key architectural separation is between:

- **omniscient engine authority**;
- **legal player decisions**;
- **perspective-safe player observations**;
- **deterministic audit artifacts**;
- **certified support scope**.

These layers must not collapse into one another.

## Component map

```text
third_party/rules_bundle.lock.json
          |
          v
+----------------------------+
| Rules Bundle Resolver      |
| fetch / verify / patchset  |
+----------------------------+
          |
          v
+----------------------------+
| ygo::core::CoreHost        |
| - duel lifecycle           |
| - callbacks                |
| - seeds                    |
| - process/messages         |
| - response submission      |
| - public queries           |
+----------------------------+
      |                |
      | messages       | public queries/events
      v                v
+------------------+  +--------------------------+
| ygo::protocol    |  | ygo::observation         |
| message decoder |  | query/event projection   |
| candidates      |  | PlayerObservation v1     |
| continuations   |  | canonical serialization  |
| response bytes  |  | observation hash         |
+------------------+  +--------------------------+
      |                |
      +-------+--------+
              |
              v
+----------------------------+
| ygo::trace / evidence      |
| engine trace v1/v2         |
| gameplay identities        |
| determinism/replay checks  |
+----------------------------+
              |
              v
+----------------------------+
| M3 conformance layer       |
| locked decks / mechanics   |
| fixtures / acceptance      |
+----------------------------+
              |
              v
 future environment / search /
 teacher / model / ML adapters
```

## 1. Rules bundle

The runtime rule inputs are reproducible repository inputs, not ambient machine state.

The bundle includes pinned revisions for:

- ocgcore;
- CardScripts;
- BabelCDB;
- expected public OCG API identity;
- deterministic checkout/file hashes;
- the repository-versioned API-hardening patchset when applicable.

The ignored `.cache/rules_bundle` directory is a materialized cache, not the source of truth.

### Invariant

A clean clone with the same canonical bundle inputs should resolve the same rules content.

## 2. `ygo::core`

`CoreHost` is the narrow C++ ownership boundary around the public OCG C API.

It owns:

- duel creation/destruction;
- card/script callbacks;
- seed input;
- deck/fixture loading;
- duel start;
- `process`;
- response submission;
- public query calls;
- API version observation.

`CoreHost` is allowed to interact with omniscient engine state because it is inside the trusted engine boundary.

It is **not** itself the agent observation API.

### Test-only setup

`load_fixture_card` and `load_fixture_script` exist for conformance infrastructure. They do not imply a general production board-construction API.

## 3. `ygo::protocol`

The protocol layer converts an interactive engine message into a typed `DecisionRequest`.

A request carries deterministic identity, engine-step identity, message metadata, complete ordered semantic candidates, and optional continuation state.

### Atomic decisions

For an atomic engine decision, a terminal candidate contains the exact response bytes that may be submitted to the engine.

### Continuations

Some legal choices are combinatorial and should not be flattened into an enormous fixed action list.

OCGForge therefore uses adapter-local continuation requests.

For one original engine message:

```text
engine message
  -> DecisionRequest
  -> zero or more local continuation transitions
  -> one terminal response
  -> exactly one engine response submission
  -> engine resumes
```

The engine does not advance during intermediate continuation transitions.

### Candidate identity

Candidate semantic keys must depend on semantic data, not runtime identity.

Physical card copies are distinguished using semantic card locators such as code/controller/location/sequence/position as applicable.

## 4. `ygo::observation`

The observation layer transforms public engine queries and perspective-filtered
events into the internal `PlayerObservation` record. The episodic public
boundary then emits the separate
`ocgforge.public_environment_observation.v1` projection; `PlayerObservation`
must not be serialized directly when its attached decision context contains
internal identity.

The public projection owns the exact nested
`ocgforge.public_safe_state.v1` serializer. It encodes only the explicitly
listed safe-state fields (`globals`, `zones`, `entities`, `relationships`,
`chain`, `visible_events`, and `match_context`) with fixed primitive/optional/
enum encodings and deterministic container ordering. The projection generates
those bytes from `PlayerObservation`; a call site cannot substitute the v1
observation bytes or arbitrary text. `PlayerObservation` v1 field and byte
semantics remain unchanged, but its direct policy-facing use is superseded by
this public projection.

`PublicEnvironmentObservation` is the intended state boundary for:

- agents;
- teachers;
- search adapters;
- model adapters.

It contains:

- player globals;
- typed zone counts;
- visible/redacted entities;
- relationships;
- chain state;
- visible event history;
- sanitized public decision context;
- static match context;
- canonical public observation digest.

The schema intentionally does not impose fixed tensor dimensions or a model vocabulary.

Those belong in downstream ML adapters.

## 5. Visibility and privacy projection

The engine can know more than the player.

The observation layer must enforce the perspective boundary.

Examples:

- opponent Main Deck entries/order are not emitted;
- opponent hidden Hand identities are not emitted;
- opponent face-down Extra Deck identities are not emitted;
- hidden field cards may be represented as redacted slots without identity;
- legitimate own private cards remain visible to their owner;
- public cards are projected when the pinned public query proves visibility.

A locator is an observation reference, not a persistent hidden physical-card identity.

## 6. Visible history

`ObservationSession` maintains cumulative perspective-filtered visible events for the supported event subset.

Knowledge-destroying transitions produce explicit randomization boundaries where appropriate.

Unknown/deferred engine event families are omitted/documented rather than guessed from raw packets.

## 7. `ygo::trace`

Trace contracts capture deterministic engine/protocol interaction.

Trace v2 exists for continuation-aware semantics:

- intermediate adapter-local actions record that the engine did not advance;
- only terminal continuation records carry the final engine response identity.

Trace/provenance hashes and semantic gameplay hashes are not interchangeable.

## 8. M3 conformance layer

M3 is a certification layer over a deliberately locked matchup.

It answers a narrower question than “does OCGForge support Yu-Gi-Oh!?”:

> Does this exact repository/rules/deck/mechanics slice execute with the required decision, privacy, determinism, and fixed-game evidence?

M3 tooling covers:

- deck manifests;
- card/database/script resolution;
- mechanics inventory;
- focused fixtures;
- rules mode;
- full games;
- deterministic action re-execution;
- acceptance reports.

## 9. M3.5 ocgcore API hardening

M3.5 adds two narrow capabilities through an ordered repository patchset over an immutable pinned base core:

- correction of the existing `overlay_seq` path for individual Xyz-material query;
- pre-duel starting-player selection with explicit validation.

The patchset is part of canonical repository identity until/unless an equivalent upstream capability is adopted through a deliberate bundle migration.

## 10. Python boundary

Python currently supports orchestration, evidence generation/validation, determinism tests, deck/catalog tooling, and full-game verification.

Python must not independently decide Yu-Gi-Oh! legality.

A future Python ML interface should wrap the authoritative C++/core environment boundary rather than reimplement it.

## 11. Future architecture boundaries

### Proposed Phase-3A trusted trajectory core

The proposed `ygo::trajectory` layer sits above `EpisodicEnvironment V2` and
below future persistence, evaluation/Teacher, model-adapter, and ML layers.
It owns immutable public frames, complete ordered public candidate domains,
selected public action keys, closure semantics, and collection policy
provenance. It must not query the core or trace, reconstruct legal domains,
advance the environment, or consume private observations. The proposal is
documented in ADR-0005 and the versioned trusted-trajectory contracts; it is
not a production recorder, storage layer, Teacher, or ML implementation.

Future work may add:

- performance/throughput measurement;
- an episodic environment API;
- checkpoint/fork support;
- vectorized execution;
- model/action vocabulary adapters;
- trajectory export;
- search/teacher/training systems.

These should be layered above the existing authoritative semantics rather than changing them for convenience.
