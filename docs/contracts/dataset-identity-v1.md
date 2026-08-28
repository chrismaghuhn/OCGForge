# Dataset identity v1

Contract ID: `ocgforge.dataset_identity.v1`

Logical dataset identity is independent of physical packing and publication.

## Canonical identity input

```text
dataset identity domain:string = ocgforge.dataset_identity.v1
dataset_identity_schema:string = ocgforge.dataset_identity.v1
trusted_trajectory_contract:string = ocgforge.trusted_trajectory.v1
member_count:u32be
sorted_unique_trajectory_record_id:string[member_count]
```

`dataset_semantic_id` is the lowercase SHA-256 digest of those exact bytes.
It excludes shard names/paths, artifact hashes, compression, receipt packing,
host, provider, and time. Re-sharding an identical admitted record set MUST
produce the same dataset semantic ID.
