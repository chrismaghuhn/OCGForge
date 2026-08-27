# Versioning and Compatibility

OCGForge already has versioned traces and observation/decision contracts. Future persistence, model adapters, and replay tooling will make compatibility more important.

This document defines the project-level policy.

## 1. Version identities are semantic

A version identifier means a specific semantic contract.

Do not keep the same version while changing:

- field meaning;
- visibility semantics;
- canonical serialization;
- hash input domain;
- response encoding;
- decision identity;
- persisted replay interpretation.

If old data would be interpreted differently, create a new version.

## 2. Existing contract families

Current repository contract families include:

- decision protocol v1;
- player view v1;
- player observation v1;
- engine trace v1;
- engine trace v2;
- episodic environment v1;
- perspective-safe public action identity v1 and episodic environment v2.

Trace v2 exists specifically because continuation-aware semantics differ from the older atomic trace assumptions.

That is the model to follow: preserve old meaning and introduce a new explicit contract when needed.

## 3. Rules-bundle identity

A rules-bundle change can alter game semantics even when OCGForge source code is unchanged.

Therefore the canonical bundle ID belongs in long-lived run/evidence provenance.

Changes to any of these may require a new bundle identity:

- ocgcore revision;
- CardScripts revision;
- BabelCDB revision/content;
- expected OCG API identity;
- ordered repository patchset;
- canonical rules configuration when bound into the bundle design.

Do not compare results from different bundles as if they were the same environment without an explicit compatibility statement.

## 4. Repository patchset identity

The M3.5 ocgcore hardening is an ordered repository input.

Patch ordering and content are semantic/provenance inputs.

If an upstream release later contains equivalent functionality:

1. pin the new upstream revision;
2. remove/adjust local patches deliberately;
3. produce a new bundle identity;
4. rerun affected protocol/observation/M3 evidence;
5. document whether behavior is semantically equivalent.

Do not silently swap patched-local behavior for “latest upstream”.

## 5. Canonical hashes

Every persisted digest should have a documented domain.

A robust persisted identity conceptually binds:

```text
algorithm
+ digest domain/version
+ canonical codec/version
+ semantic bytes
```

Do not rely on a field name like `hash` to carry all of that meaning forever.

## 6. Observation compatibility

`ygo.player_observation.v1` has canonical serialization semantics.

An additive C++ field is **not automatically backward-compatible** if it changes canonical serialized bytes or the observation hash.

When adding a field, decide explicitly:

- Is it outside canonical v1 serialization?
- Is it optional but still changes v1 meaning?
- Does it require `player_observation.v2`?

Compatibility is about consumers and persisted bytes, not whether old source code still compiles.

## 7. Decision compatibility

Candidate semantic keys are part of policy/replay identity.

Changing key construction or ordering can invalidate:

- recorded semantic actions;
- deterministic traces;
- trained model assumptions;
- replay fixtures.

Treat such a change as a contract migration unless proven representation-only.

The internal `ocgforge.action_identity.v1` and the public
`ocgforge.public_action_identity.v1` are separate identity domains. An
episodic facade must not expose the internal semantic key merely because the
Decision Protocol owns it. Changing a public selection field from the
internal key to a perspective-safe key also changes candidate-domain hash
input, public decision identity, and public replay interpretation; that
migration is therefore `ocgforge.episodic_environment.v2`, not a reinterpretation
of episodic environment v1.

## 8. Trace compatibility

A trace reader should never infer the meaning of an unrecognized trace version.

Fail closed or use an explicit migration reader.

Historical trace versions should remain interpretable according to their original semantics.

## 9. Acceptance evidence compatibility

Machine-readable acceptance artifacts should record enough identity to explain what they certify.

At minimum, long-lived evidence should be attributable to:

- contract/schema version;
- bundle;
- deck/matchup identity where applicable;
- relevant deterministic configuration;
- verification scope.

A future tool should not merge acceptance rows from incompatible environments merely because criterion names match.

## 10. Model-facing adapters

Future vocabulary/tensor schemas need their own versions.

Do not use authoritative card passcodes, observation schema version, and model vocabulary version as if they were the same thing.

A model adapter may define:

- vocabulary version;
- feature schema;
- action encoding;
- padding/bucketing rules;
- normalization;
- reward schema.

Those are downstream contracts.

## 11. Persistence/checkpoints

Checkpoint/fork support is not yet a certified current capability.

Before implementing persisted checkpoints, create an ADR that defines:

- authoritative state boundary;
- codec;
- codec version;
- digest domain;
- RNG state;
- rules-bundle identity;
- restore compatibility;
- migration policy;
- failure behavior for unsupported historical versions.

Once persisted checkpoints exist, their old meaning must not be silently reinterpreted by new code.

## 12. Deprecation

Deprecation should be explicit.

For a public semantic contract:

1. document the replacement;
2. keep the old reader/meaning when long-lived artifacts require it, or explicitly retire support;
3. do not alias an old version name to new semantics;
4. add migration tests when migration exists.

## 13. Compatibility rule of thumb

If two artifacts with the same advertised version can contain the same bytes but mean different things, versioning has failed.

If two semantically identical runs can produce different advertised canonical identities because of incidental runtime details, canonicalization has failed.
