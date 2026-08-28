# Dataset manifest v1

Contract ID: `ocgforge.dataset_manifest.v1`

The manifest is immutable physical provenance for trusted admitted membership.
It is not a learner/model export.

## Canonical bytes

```text
domain:string = ocgforge.dataset_manifest.v1
schema:string = ocgforge.dataset_manifest.v1
dataset_identity_schema:string = ocgforge.dataset_identity.v1
trusted_trajectory_contract:string = ocgforge.trusted_trajectory.v1
dataset_semantic_id:string
member_count:u32be
members[member_count]:
    trajectory_record_id:string
    public_gameplay_trajectory_id:string
    admission_receipt_id:string
    candidate_shard_artifact_sha256:string
    episode_envelope_sha256:string
```

Members are strictly ordered by `trajectory_record_id` and must resolve to
verified receipt commitments. Duplicate, conflicting, unknown, malformed, or
trailing data fails closed. Mutable URIs and absolute paths do not enter the
manifest or logical dataset identity.
