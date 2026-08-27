# Candidate-domain evidence v1

## Contract ID

```text
ocgforge.candidate_domain_evidence.v1
```

## Independent candidate-domain digest

The candidate-domain digest schema remains independently testable as:

```text
ocgforge.candidate_domain.v1
```

Its canonical bytes contain only, in order:

```text
domain string
request kind string
candidate count: u32be
each semantic_key string in authoritative protocol order
```

Strings use `u32be byte_length || UTF-8 bytes`; the digest is lowercase
SHA-256 over the exact bytes. The codec does not read or depend on
`decision_contract_id`, `action_identity_schema_id`, `seed_derivation_id`,
or required-script closure identity. It does not sort, deduplicate, filter,
truncate, or fabricate candidate keys.

## Metric vocabulary

`candidate_domain_max` is:

```text
MAX(candidate_count)
```

over every individually published complete `DecisionRequest` or
adapter-local continuation domain in the measured corpus. It is a maximum
over legal domains, not a sum over jobs and not a candidate budget.

`candidate_max_total` is separate aggregate accounting over one value per
job:

```text
SUM(per-job candidate-domain maxima)
```

It may be useful for historical worker/report totals, but it is never a
claim about one legal request. The historical M4 field named `candidate_max`
was summed by the report aggregator across 64 result rows. Therefore:

```text
historical candidate_max = 1344
    = aggregate accounting value
    != one candidate-domain maximum
```

Historical M4 JSON, Markdown, and baseline artifacts remain unchanged. New
evidence must use `candidate_domain_max` for the per-domain maximum and
`candidate_max_total` for the aggregate sum.

## G28 witness selection

The future G28 corpus records one row for every complete individually
published domain, including continuation domains. Each row contains:

```text
candidate_count
request_kind
episode_semantic_id
environment_decision_index
engine_step_index
protocol_decision_id
candidate_domain_digest
ordered_semantic_keys
```

The selected witness is the lexicographically first row after sorting by:

```text
candidate_count             descending
episode_semantic_id         ascending
environment_decision_index ascending
engine_step_index           ascending
protocol_decision_id        ascending
candidate_domain_digest     ascending
```

The witness's count must equal `candidate_domain_max`, and its semantic
inputs must replay independently. No candidate cap or policy choice is
derived from this metric. The actual workload witness and final measured
value are deferred to the Phase-2 implementation/final-acceptance campaign;
this prerequisite ratifies only the vocabulary and deterministic selection
rule.

## Determinism, privacy, and replay

The evidence identity excludes paths, timing, PID, thread/worker identity,
compiler/build identity, raw engine state, response bytes, hidden card
identity, hidden deck order, and private observation data. Ordered semantic
keys are already owned by the Decision Protocol and Action Identity
contracts. Candidate-domain digests remain distinct from observation hashes,
semantic gameplay hashes, and EngineTrace audit hashes.
