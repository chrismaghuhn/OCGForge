# Admission receipt v1

Contract ID: `ocgforge.admission_receipt.v1`

An admission receipt is immutable derived evidence for a whole-shard
admission. It is produced only after every candidate entry and every required
restricted companion has passed strict validation and semantic V2 replay.

## Canonical bytes

```text
domain:string = ocgforge.admission_receipt.v1
schema:string = ocgforge.admission_receipt.v1
admission_contract_id:string
candidate_shard_artifact_sha256:string
restricted_evidence_artifact_sha256:string
entry_count:u32be
entries[entry_count]:
    trajectory_record_id:string
    public_gameplay_trajectory_id:string
    environment_semantic_id:string
    episode_semantic_id:string
    episode_envelope_sha256:string
    closure_kind:u8
```

Entries are strictly ordered by `trajectory_record_id`. The receipt ID is
`admission_receipt.v1.<sha256(canonical bytes)>`; it is not encoded into its
own bytes. Paths, filenames, hosts, PIDs, clocks, scheduling, and cloud/build
metadata are not fields.

