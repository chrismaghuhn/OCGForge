# ADR-0003: Ratify Episodic V1 prerequisite identities

## Status

Accepted

## Context

The merged Phase-2 public-facade specification identifies three normative
prerequisites before a production `EpisodicEnvironment` may be implemented:
the public Decision Protocol, semantic action identity, and seed derivation
need concrete version identifiers; `required_script_closure_identity` needs a
precise meaning; and G28 must distinguish one legal candidate domain from the
historical M4 aggregate counter.

The existing code already provides the relevant semantics. `CoreHost` loads
`constant.lua`, `utility.lua`, and `proc_normal.lua` through `ScriptStore`.
`ScriptStore` resolves the pinned tree with its existing root,
`official/`, and `unofficial/` lookup behavior. The existing required-card
vector is a deterministic sorted set used for fail-closed missing-script
diagnostics. The protocol decoder and continuation code already construct
stable semantic keys and preserve their authoritative order.

These definitions must not change gameplay, privacy, trace, observation,
ScriptStore, or M4 behavior.

## Decision

The following exact identifiers are accepted:

| Semantic surface | Exact identifier | Normative owner |
| --- | --- | --- |
| DecisionRequest/ActionCandidate public semantic contract | `ocgforge.decision_protocol.v1` | `docs/contracts/decision-protocol-v1.md` |
| `ActionCandidate.semantic_key` identity rules | `ocgforge.action_identity.v1` | `docs/contracts/action-identity-v1.md` |
| CoreHost four-word seed mapping | `ocgforge.seed_derivation.v1` | `docs/contracts/seed-derivation-v1.md` |
| ScriptStore resolution semantics | `ocgforge.script_resolution.v1` | `docs/contracts/script-resolution-v1.md` |
| Required-script closure serialization | `ocgforge.required_script_closure.v1` | `docs/contracts/script-resolution-v1.md` |
| Required-script closure hash domain | `ocgforge.required_script_closure_identity.v1` | `docs/contracts/script-resolution-v1.md` |
| Candidate-domain digest serialization | `ocgforge.candidate_domain.v1` | `docs/contracts/episodic-environment-v1.md` |
| Candidate-domain evidence and G28 metrics | `ocgforge.candidate_domain_evidence.v1` | `docs/contracts/candidate-domain-evidence-v1.md` |

The C++ constants mirror these accepted values. They are not a second
normative registry and must not be inferred from filenames, build metadata, or
runtime state.

`required_script_closure_identity` is a resolution-environment identity. It
binds the pinned CardScripts source commit, the resolved CardScripts tree
content hash, the versioned ScriptStore resolution contract, the ordered
CoreHost bootstrap-script names, and the sorted unique required-card code
seed set. It does not enumerate runtime requests and does not authorize or
enforce a script allowlist.

G28 uses `candidate_domain_max` for the maximum of individual complete
published domains. `candidate_max_total` is the separate sum of per-job
maxima used by historical aggregate accounting. The deterministic witness
tie-break is part of the evidence contract, but the actual witness remains a
Phase-2/final-acceptance deliverable.

## Alternatives considered

### One ID for the whole decision protocol and action keys

Rejected. The accepted protocol describes request shape, candidate
completeness, and engine-response ownership, while semantic-key encoding has
its own compatibility lifecycle. A key-identity migration must be visible
without implying that every request-level rule changed.

### Treat `required_script_codes` as the runtime closure

Rejected. The live ScriptStore accepts global and otherwise requested scripts
under its existing resolution rules. Turning the expected-card diagnostic set
into an allowlist would change runtime semantics and could silently reject
legal gameplay.

### Enumerate the transitive runtime script graph

Rejected for V1. Dynamic script requests and Lua dependencies are not a stable
public enumeration boundary. Binding the certified resolution environment is
auditable and preserves the current loader.

### Rename or rewrite historical M4 `candidate_max`

Rejected. Historical evidence is immutable. New evidence uses explicit
`candidate_domain_max` and `candidate_max_total` vocabulary.

## Consequences

- Phase-2 can construct environment identity inputs without inventing IDs.
- The closure identity is reproducible across hosts because it excludes paths,
  traversal order, timestamps, process/thread/worker identity, compiler
  identity, and load timing.
- These prerequisite identities contain no opponent hidden-card identity,
  hidden deck order, private observation, response bytes, or raw engine state.
- The identities do not replace semantic gameplay hashes, observation hashes,
  candidate-domain digests, or EngineTrace audit hashes.
- The actual G28 corpus, witness, and replay evidence remain pending.

## Compatibility / migration

This ADR ratifies identities over the already accepted, unchanged rules
bundle, locked decks, protocol behavior, observation behavior, trace behavior,
and ScriptStore behavior. It is not a rules migration and does not rebless
historical acceptance artifacts. Any incompatible change to one of the
defined meanings requires a new versioned identifier and an explicit contract
migration.

## Verification

`tests/episodic/normative_prerequisites_test.cpp` provides exact constant
checks, seed known-answer tests, closure and candidate-domain golden vectors,
metric reduction tests, and the deterministic G28 tie-break oracle. Existing
M0–M4 and Python evidence suites remain regression gates; no historical M4
artifact is regenerated by this decision.
