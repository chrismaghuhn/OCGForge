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
strictly ordered by initialization identity. The reader recomputes all
digests/identities and rejects duplicate, unsorted, missing, extra, corrupt,
or trailing entries. The bundle is bound to the exact candidate-shard
artifact digest before admission.

