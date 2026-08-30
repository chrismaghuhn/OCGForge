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
policy RNG state encoding. Phase 3B admission requires an explicitly injected,
typed descriptor for the exact policy RNG contract. That descriptor must
validate canonical initialization material and must additionally provide a
canonical `STATE` validator or prove unique `CURSOR` semantics for the
initialized stream before the corresponding per-decision provenance is
admitted. The default production authority registers only
`ocgforge.no_policy_rng.v1`; test or future policy RNG contracts are accepted
only through an explicit immutable registry. Unknown or unprovable non-`NONE`
material remains fail-closed rather than being defaulted or migrated.
