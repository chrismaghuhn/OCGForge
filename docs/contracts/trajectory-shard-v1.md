# Candidate trajectory shard v1

Contract ID: `ocgforge.trajectory_shard.v1`

This is the Phase-3B immutable, uncompressed candidate container. It stores
canonical `EpisodeEnvelope` bytes for collection/audit; it is not a learner
projection.

## Canonical bytes

All strings and integer fields use the primitive codec in
`trusted-trajectory-v1.md`:

```text
domain:string = ocgforge.trajectory_shard.v1
schema:string = ocgforge.trajectory_shard.v1
entry_count:u32be
entries[entry_count]:
    episode_envelope_sha256:string
    envelope_length:u32be
    canonical_episode_envelope_bytes:bytes[envelope_length]
```

Entries are strictly ascending by the lowercase hexadecimal
`episode_envelope_sha256`. The digest must equal SHA-256 of the exact
envelope bytes. Duplicate, unsorted, malformed, truncated, mismatched, or
trailing data fails closed. The codec never sorts or deduplicates an input;
the writer rejects noncanonical order.
