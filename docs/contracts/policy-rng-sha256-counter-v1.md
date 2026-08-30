# `ocgforge.policy_rng.sha256_counter.v1`

Status: normative Phase 4A policy-RNG contract.

This contract is the production counter-addressed RNG for stochastic policy
execution. It is separate from engine seed derivation and public gameplay
identity. It has no implicit seed and does not read environment, episode,
process, host, thread, wall-clock, pointer, PID, provider, or scheduling
state.

## Contract identities

The fixed contract identity is:

```text
ocgforge.policy_rng.sha256_counter.v1
```

The fixed domain-separated initialization and block domains are:

```text
ocgforge.policy_rng.sha256_counter.init.v1
ocgforge.policy_rng.sha256_counter.block.v1
```

The policy-owned stream selector is a canonical lower-case token containing
only lower-case ASCII letters, digits, `_`, and `-`; its first and last
characters must be alphanumeric. The participant policy assignment is the
canonical content identity with prefix:

```text
participant_policy_assignment.v1.<64 lowercase hexadecimal digits>
```

## Initialization material

The caller must provide an explicit `policy_rng_root_seed` as a `u64`. A
missing root is rejected, while zero remains a valid explicit value. The
canonical initialization material is the following concatenation, using the
repository's length-prefixed UTF-8 string encoding and big-endian integers:

| Field | Encoding |
| --- | --- |
| initialization domain | string `ocgforge.policy_rng.sha256_counter.init.v1` |
| RNG contract identity | string `ocgforge.policy_rng.sha256_counter.v1` |
| policy-owned root | `u64be` |
| participant policy assignment ID | canonical string |
| policy RNG stream ID | canonical string |

The resulting material is used as-is. It is never copied from
`EpisodeSpec::root_seed`, `episode_semantic_id`, V2 seed material, or any
other environment value. Changing only `episode_semantic_id` or
`EpisodeSpec::root_seed` therefore cannot change this material or any RNG
output when the policy-owned inputs remain equal.

The material is bound to the existing Phase-3 trajectory identity wrapper:

```text
policy_rng_initialization.v1.<lowercase SHA-256 digest>
```

The digest input is the canonical concatenation of:

```text
string ocgforge.policy_rng_initialization_identity.v1
string ocgforge.policy_rng_initialization_identity.v1
string ocgforge.policy_rng.sha256_counter.v1
string policy_rng_stream_id
bytes initialization_material
```

The implementation uses `compute_policy_rng_initialization_id` so this
identity remains the same typed Phase-3 identity authority used by trajectory
codecs.

## Counter-addressed raw words

The cursor is a consumed-raw-`u64` word count and starts at zero. For cursor
`c`:

```text
block_index = c / 4
lane        = c % 4
```

The canonical block input is:

| Field | Encoding |
| --- | --- |
| block domain | string `ocgforge.policy_rng.sha256_counter.block.v1` |
| initialization identity | string `policy_rng_initialization.v1.<digest>` |
| block index | `u64be` |

The SHA-256 digest of those bytes is split into four consecutive 8-byte
groups. Each group is interpreted as one big-endian `u64`; `lane` selects the
consumed word. No block cache, pointer, process state, or provider state is
semantically observable.

One successful `next_raw_u64` call consumes exactly one cursor position. A
cursor at `UINT64_MAX` is exhausted and fails without hashing or changing the
cursor. The cursor never wraps.

## Uniform bounded sampling

For `uniform_below_u64(n)`:

```text
n == 0 -> PolicyErrorCode::EmptyCandidateDomain; cursor unchanged
n == 1 -> value 0; cursor unchanged
n > 1  -> threshold = (-n) % n
          consume raw words until raw >= threshold
          return raw % n
```

The negation is unsigned `u64` arithmetic. Every rejected raw word consumes
exactly one cursor position. The result reports the exact pre- and post-
cursor values. Exhaustion is a structured `RngExhausted` failure and never a
fallback result.

## Reproduction and isolation

Equal explicit policy-owned initialization inputs produce equal initialization
material, initialization identity, block bytes, raw words, and cursor traces
in every process and on every supported platform. Different policy-owned
roots, participant assignments, or streams must produce different
initialization identities and first blocks. Episode semantic identity and
environment root are deliberately absent from this contract.
