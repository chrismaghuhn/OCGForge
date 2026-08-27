# Seed derivation v1

## Contract ID

```text
ocgforge.seed_derivation.v1
```

## Purpose

This contract owns the deterministic mapping from an episodic root seed to
the four-word `ygo::core::SeedBundle` passed to `CoreHost`. It is the seed
derivation named by `EnvironmentConfig.seed_derivation_id`.

## Mapping

Input:

```text
root_seed: u64
```

Output is an ordered array of four unsigned 64-bit words:

```text
word[0] = root_seed
word[1] = root_seed XOR 0x9e3779b97f4a7c15
word[2] = root_seed + 0x6a09e667f3bcc909
word[3] = (root_seed << 1) XOR 0xbb67ae8584caa73b
```

All operations are unsigned `u64` arithmetic modulo `2^64`. The word order
is fixed. The mathematical mapping has no endianness; any later canonical
serialization of the words must declare its byte order separately.

The mapping does not use wall time, PID, thread ID, worker count, process
identity, compiler identity, host path, or random UUIDs. M4's external
`(master_seed, job_index) -> job seed` allocation is a separate coordinator
contract and is not this mapping.

## Compatibility and replay

The four words are semantic episode input and are recorded in the existing
trace/episode identity surfaces according to their owning contracts. An
incompatible arithmetic or ordering change requires a new seed-derivation ID.
This contract does not add a second RNG and does not change CoreHost's seed
consumption.

## Verification vectors

The following vectors are unsigned hexadecimal `u64` values in word order:

| `root_seed` | `word[0]` | `word[1]` | `word[2]` | `word[3]` |
| --- | --- | --- | --- | --- |
| `0x0000000000000000` | `0x0000000000000000` | `0x9e3779b97f4a7c15` | `0x6a09e667f3bcc909` | `0xbb67ae8584caa73b` |
| `0x0000000000000001` | `0x0000000000000001` | `0x9e3779b97f4a7c14` | `0x6a09e667f3bcc90a` | `0xbb67ae8584caa739` |
| `0xffffffffffffffff` | `0xffffffffffffffff` | `0x61c8864680b583ea` | `0x6a09e667f3bcc908` | `0x4498517a7b3558c5` |
| `0x8000000000000000` | `0x8000000000000000` | `0x1e3779b97f4a7c15` | `0xea09e667f3bcc909` | `0xbb67ae8584caa73b` |
| `0x0123456789abcdef` | `0x0123456789abcdef` | `0x9f143cdef6e1b1fa` | `0x6b2d2bcf7d6896f8` | `0xb921244a979d3ce5` |
