# Restricted collection evidence bundle v1

Contract ID: `ocgforge.restricted_collection_evidence_bundle.v1`

This artifact owns the restricted material required to admit a candidate
shard. It is never learner or policy input.

## Canonical bytes

```text
domain:string = ocgforge.restricted_collection_evidence_bundle.v1
schema:string = ocgforge.restricted_collection_evidence_bundle.v1
candidate_shard_artifact_sha256:string
interrupted_entry_count:u32be
interrupted_entries[interrupted_entry_count]:
    episode_envelope_sha256:string
    canonical RestrictedReplayEvidence bytes
rng_initialization_entry_count:u32be
rng_initialization_entries[rng_initialization_entry_count]:
    policy_rng_initialization_identity:string
    initialization_material:bytes
```

Interrupted entries are strictly ordered by envelope digest. RNG entries are
strictly ordered by initialization identity. Structural decoding rejects
duplicate, unsorted, malformed, or trailing entries. The cross-validator then
binds every interrupted entry to the exact candidate shard, rejects missing
or extra evidence, recomputes every referenced RNG initialization identity
from its raw material, and rejects conflicting or unreferenced material. The
bundle is bound to the exact candidate-shard artifact digest before
admission.

The generic `initialization_material` field is not by itself a proof of a
policy RNG state encoding. Phase 3B admission must have a registered,
contract-specific canonical RNG state codec before it can admit a non-`NONE`
initialization (and its per-decision `STATE`/`CURSOR` provenance). The current
V1 implementation has no such registered policy RNG codec, so non-`NONE`
material is deliberately rejected during cross-validation. The deterministic
`NONE` path remains the only positive admission path until a future,
explicitly versioned state codec is added; this is fail-closed behavior, not a
default or migration.
