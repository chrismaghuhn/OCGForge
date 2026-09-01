# OCGForge Phase 5 acceptance plan

## Status and scope

**Status:** Acceptance plan frozen for Phase 5 Task 1. The gates below are
future executable gates for the model-facing implementation.

At this docs-only freeze:

~~~text
docs-only contract freeze: ACCEPTED
P5-G00..P5-G17: NOT_RUN
P5 implementation: NOT_STARTED
trusted_trajectory.v1 schema change: NO
~~~

No P5 gate is reported as PASS by this document. A gate may be reported as
PASS only after the complete required evidence is executed from the stated
checkout and the result is recorded with its exact source/head identity.

This plan covers the framework-neutral path:

~~~text
PublicEnvironmentObservation
+ complete ordered EnvironmentActionCandidate[]
    -> LogicalModelInputV1
    -> EncodedModelInputV1
    -> ModelBatchLayoutV1
~~~

It does not authorize a neural model, training code, a framework dependency, or
any gameplay/Teacher/trajectory change.

## 1. Normative sources

The gates must be evaluated against these sources in precedence order:

1. the pinned OCGForge public contracts and accepted ADRs;
2. this Phase 5 model-facing contract;
3. executable tests and generated evidence for the current implementation;
4. human-readable acceptance summaries.

The relevant existing authorities are:

- ADR-0004 and the public action-identity contract for public keys and order;
- the public environment and public safe-state contracts for observation privacy
  and decoding;
- the Decision Protocol for complete ordered candidate production;
- ADR-0005 and trusted_trajectory.v1 for trajectory boundaries;
- candidate-domain-evidence.v1 for G28 metrics and witness selection;
- ADR-0006 and P5_MODEL_CONTRACT.md for the Phase 5 representation boundary.

Historical Phase 3/4 evidence is regression input, not fresh P5 evidence.

## 2. Status vocabulary

Use the repository status vocabulary exactly:

| Status | Meaning |
| --- | --- |
| PASS | the complete gate ran successfully in the current environment |
| FAIL | the gate ran and its required property was disproved |
| NOT_RUN | no current execution evidence exists |
| BLOCKED | execution cannot proceed because a required external/implementation prerequisite is absent |
| SKIPPED | explicitly excluded by the gate definition; never equivalent to PASS |

A focused codec test, static scan, or prior artifact cannot be promoted to a
broader gate without the gate's complete evidence. A skipped backend or missing
G28 witness remains unresolved.

## 3. Required gate matrix

| Gate | Property | Required evidence | Task 1 status |
| --- | --- | --- | --- |
| P5-G00 | contract IDs, owner, and scope are frozen | exact ADR/contract/index review plus docs-only diff audit | NOT_RUN |
| P5-G01 | public-only input boundary | boundary test and forbidden-source scan | NOT_RUN |
| P5-G02 | existing public-safe decoder ownership | real decoder roundtrip and rejection tests | NOT_RUN |
| P5-G03 | exact N-to-N candidate preservation | count/order/key/property property tests | NOT_RUN |
| P5-G04 | no private identity or response leakage | negative privacy tests and field audit | NOT_RUN |
| P5-G05 | deterministic vocabulary | manifest/code known-answer tests and unknown/redacted tests | NOT_RUN |
| P5-G06 | deterministic integer/categorical encoding | independent-process byte/code vectors | NOT_RUN |
| P5-G07 | canonical model-input identity | digest KATs and semantic mutation tests | NOT_RUN |
| P5-G08 | physical layout excluded from identity | same-input/different-layout identity tests | NOT_RUN |
| P5-G09 | ragged/padded lossless roundtrip | offsets, masks, and unpad/pad property tests | NOT_RUN |
| P5-G10 | exact boundary domains | N=24, N=25, and N=129 tests | NOT_RUN |
| P5-G11 | real G28 maximum-domain witness | existing deterministic witness replay and full-domain encoding | NOT_RUN |
| P5-G12 | paired-hidden-world equality | two-world public/model byte-equality test | NOT_RUN |
| P5-G13 | trajectory sample label mapping | DecisionRecord selected-key/ordinal tests | NOT_RUN |
| P5-G14 | framework neutrality | dependency, type, and source-boundary audit | NOT_RUN |
| P5-G15 | Phase-3/4 regression | applicable existing regression suites | NOT_RUN |
| P5-G16 | failure-closed semantics | malformed/overflow/unknown/capacity negative tests | NOT_RUN |
| P5-G17 | clean-checkout acceptance | fresh checkout, exact head, clean status, and rerun evidence | NOT_RUN |

All blocker properties in the matrix are required for a future P5 acceptance.
No gate may be closed by a smaller candidate domain, a fabricated witness, or a
framework-specific substitute.

## 4. Gate definitions

### P5-G00 — contract, ownership, and docs-only scope

Verify all of the following:

- the owning namespace is ygo::model;
- the only accepted semantic source is the public observation plus its complete
  ordered public candidate vector;
- the six Phase 5 contract IDs match P5_MODEL_CONTRACT.md;
- ADR-0006 is indexed exactly once and marked as the Task 1 freeze;
- no implementation or model/training dependency is present;
- the change set contains only the four files authorized by Task 1.

The gate must record the exact base and head. It must not interpret the presence
of the documents as a P5 implementation PASS.

### P5-G01 — public-only input boundary

Construct the model input through the public V2 boundary. The test must prove
that the representation can be built from:

~~~text
PublicEnvironmentObservation
public request metadata
complete ordered EnvironmentActionCandidate[]
immutable CardVocabularyV1
~~~

The test must fail or be impossible when a caller supplies PlayerObservation,
CoreHost, internal semantic keys, raw engine data, response bytes,
SubmissionToken, private locators, or hidden card properties.

No test helper may obtain the expected result by reading an internal object and
then comparing it to the model representation.

### P5-G02 — existing public-safe decoder

For a serialized public observation:

1. decode the outer value with the existing public-observation decoder;
2. pass the accepted canonical safe-state bytes to the existing
   decode_canonical_public_safe_state path;
3. project only the resulting public-safe view.

The test must prove canonical safe-state re-encoding is byte-stable and that
malformed, non-canonical, privacy-invalid, and unknown-enum input is rejected.
A duplicate parser in the model layer, a PlayerObservation shortcut, or a
caller-supplied state string fails this gate.

### P5-G03 — exact N-to-N candidate preservation

Use public candidate fixtures with distinct, valid public keys and distinct
descriptor fields. For each input vector, assert:

~~~text
source candidate count == logical candidate count
logical candidate count == encoded row count
encoded row count == routing-key count
routing-key[i] == source public_action_key[i]
descriptor[i] remains paired with routing-key[i]
~~~

The test must assert exact source order, not only set membership. A candidate
permutation must either remain a permutation in the same supplied order or
change the canonical model-input bytes; it must never be normalized back by a
sort. Duplicate or malformed keys reject the whole frame.

### P5-G04 — no private identity or response leakage

Run a negative field audit over logical values, encoded values, canonical bytes,
routing metadata, and failure diagnostics. The following must not occur:

~~~text
PlayerObservation
CoreHost
ActionCandidate.semantic_key
exact response bytes or response hashes
SubmissionToken
internal decision/continuation IDs
raw message hashes
private or persistent locators
hidden passcodes or hidden deck order
pointer, address, PID, path, timing, or worker identity
~~~

A public redacted slot may be represented only as the public unknown/redacted
category and its current public reference. A hash of a hidden value is also a
leak and fails the gate.

### P5-G05 — deterministic vocabulary

Use an explicit immutable vocabulary manifest and known-answer vectors. Prove:

- canonical vocabulary bytes use the fixed domain, schema, mapping rule, and
  strictly ascending public passcode list;
- the same passcode receives the same ID in independent processes;
- the reserved IDs are 0 for physical PAD and 1 for a real public
  unknown/redacted identity;
- a known public passcode absent from the manifest fails closed;
- an unknown/redacted entity never triggers a catalog lookup;
- a static unknown opponent deck remains unknown/empty;
- an unsorted, duplicate, or dynamically extended vocabulary is rejected, not
  silently sorted or extended.

The test may use catalog/database data only for already-public passcodes.

### P5-G06 — deterministic integer and categorical encoding

Use fixed vectors for every encoded public request/action category, public choice
kind, reference kind, continuation kind, continuation operation, optional
presence bit, signed amount, and public-safe enum value. Assert:

- codes match the frozen tables;
- integer values are exact;
- signed values preserve their two's-complement bits;
- optional absence is distinct from a present zero;
- no floating-point field or normalization is needed;
- unknown codes fail closed.

Run the vectors in at least two independent processes and compare exact encoded
bytes, not only decoded values.

### P5-G07 — canonical model-input identity

For one valid public frame, compute canonical logical bytes, canonical encoded
bytes, and model-input identity. Assert:

- repeated encoding produces byte-identical output;
- independent processes produce identical bytes and identity;
- changing a visible safe-state value changes identity;
- changing a public candidate descriptor, key, membership, or order changes
  identity;
- changing the vocabulary identity or encoded code table changes identity;
- the identity binds the complete ordered candidate domain.

The proof must compare semantic values, not filesystem or build hashes.

### P5-G08 — physical layout is not semantic identity

Build the same logical/encoded model input into at least these distinct physical
views:

~~~text
one ragged batch
one padded batch with W = max(N_i)
one padded batch with W > max(N_i)
a differently composed/reordered batch of the same samples
~~~

Assert that every sample's model-input identity is unchanged. Also assert that
the layout representation itself preserves its declared layout metadata for
diagnostics without feeding it into model-input identity.

Mutating padding width, bucket capacity, batch order, offsets, row masks, storage
dtype, device, or framework must never alter the model-input identity. Changing
a semantic encoded row or routing key must alter it.

### P5-G09 — ragged/padded lossless roundtrip

Use a batch with different candidate and state sequence lengths. Assert:

- every offset vector has B+1 entries, starts at zero, is monotonic, and ends
  at its flat-buffer length;
- candidate_offsets[i+1] - candidate_offsets[i] equals N_i;
- flat rows retain each sample's exact public order;
- every real candidate row has mask 1;
- every padded row has mask 0 and no routing key;
- optional-field presence masks are distinct from row masks;
- unpad(pad(ragged(input), W)) equals ragged(input) exactly;
- a bucket capacity below any N_i rejects without mutation or truncation;
- a real unknown card uses ID 1, never the PAD ID 0.

The roundtrip must cover variable entities, relationships, chain links, events,
context references, and public deck vectors where present, not only candidates.

### P5-G10 — N=24, N=25, and N=129

Create valid complete public candidate domains with exactly:

~~~text
N = 24
N = 25
N = 129
~~~

For each domain:

- preserve all N keys and descriptors in exact order;
- produce exactly N logical rows, encoded rows, and routing slots;
- compute a complete candidate-domain digest over all N keys;
- materialize a ragged view with no cap;
- materialize a padded view with W=N and with W>N;
- verify masks mark exactly N real rows;
- verify W<N fails closed rather than truncating;
- verify the N=129 case does not use a hidden width-128 assumption.

The test must exercise both non-continuation and continuation-shaped public
requests where the implementation supports those public forms.

### P5-G11 — real G28 maximum-domain witness

Use the existing candidate-domain evidence corpus and deterministic selector.
The proof must:

1. enumerate every complete individually published request or continuation
   domain in the selected corpus;
2. compute candidate_domain_max as the maximum of individual domain counts;
3. select the witness with the accepted deterministic tie-break;
4. assert witness count equals candidate_domain_max;
5. independently replay the witness inputs;
6. pass the witness's full ordered public candidate vector through all Phase 5
   layers; and
7. prove no candidate was dropped, sorted, deduplicated, fabricated, or
   truncated.

candidate_max_total is aggregate accounting only and MUST NOT be used as a
tensor width, bucket capacity, or witness count. In particular, the historical
aggregate value 1344 is not a single legal candidate-domain maximum.

If the real measured witness is not available, this gate is BLOCKED or NOT_RUN;
it is never passed with a synthetic replacement.

### P5-G12 — paired-hidden-world equality

Use the established paired-world construction in which hidden opponent-card
identity differs while the acting player's public state and public candidate
domain are equal. Route both worlds through the real public projection and the
existing public-safe decoder.

Assert byte equality for:

~~~text
PublicEnvironmentObservation
canonical logical model bytes
canonical encoded model bytes
public candidate-domain digest
model-input identity
~~~

Also assert that the internal hidden identities and internal semantic keys may
differ, but none are present in the model values or diagnostics. A visible
public-state mutation must change the corresponding model bytes and identity.

### P5-G13 — trajectory sample label mapping

From an accepted trusted trajectory DecisionRecord:

- rebuild the model input from its PublicFrameSnapshot public observation and
  complete ordered public candidate vector;
- locate selected_public_action_key by exact string equality;
- require exactly one match;
- emit its zero-based current candidate ordinal as the derived label;
- retain the exact selected public key as routing/audit metadata.

Test missing, duplicate, malformed, and mismatching selected keys. Assert that
the ordinal is not used as replay identity and that trusted_trajectory.v1
canonical bytes and identities are unchanged. Replayed selection must resolve
the public key against the regenerated current domain, never submit an ordinal.

### P5-G14 — framework neutrality

Perform a source, build, and contract audit proving that the Phase 5 semantic
layer has:

- no PyTorch, JAX, NumPy, Trackio, Accelerate, RL, or ML dependency;
- no framework-owned tensor or checkpoint type in the contract;
- no float normalization, loss, optimizer, or network decision;
- only framework-neutral integer/categorical/presence-mask values;
- no ownership transfer to Teacher, trajectory, gameplay, or rules layers.

A later physical adapter may use a framework, but that adapter cannot change the
canonical bytes, candidate domain, routing key, or model-input identity.

### P5-G15 — Phase-3/4 regression

From the exact accepted implementation head, run the applicable existing
Phase-3 and Phase-4 regression suites, including:

- trusted trajectory and admission/replay compatibility;
- public environment and public action identity/privacy;
- deterministic candidate-domain evidence;
- accepted Teacher and battle-sidecar regressions;
- fixed-deck/conformance and clean semantic identity checks required by the
  current repository acceptance manifests.

Record each suite separately. A docs-only focused check is not a substitute for
the applicable regression suite, and historical PASS text is not fresh evidence.
Any regression, identity drift, or trajectory-byte change fails this gate.

### P5-G16 — failure-closed behavior

Exercise malformed and adversarial values:

~~~text
unknown contract/schema IDs
non-canonical safe-state bytes
inconsistent public observation digest
empty, duplicate, malformed, or reordered candidate domain
unsafe/unresolved public reference
unknown category or continuation token
known passcode outside immutable vocabulary
hidden identity presented as known
integer/count/offset overflow
insufficient padding capacity
row-mask/offset/routing-key mismatch
trajectory selected-key ambiguity
~~~

Each case must reject the complete operation before any model-side mutation.
No failure message may disclose the forbidden value that caused the failure.
There must be no first-match, default, truncation, sort, or auto-resolution
fallback.

### P5-G17 — clean-checkout acceptance

Create a fresh clean checkout from the exact branch head and verify:

- the checked-out head equals the reported head;
- the base identity is recorded;
- the worktree is clean before and after acceptance;
- only the four Task 1 files differ from the exact base;
- the four files contain no unresolved conflict markers or placeholders;
- all required P5 evidence is generated by commands, not hand-edited;
- applicable Phase-3/4 regression results are independently reproducible.

A clean docs checkout is necessary but does not turn unrun semantic gates into
PASS.

## 5. Required evidence record

A future P5 acceptance record MUST include:

~~~text
BASE
HEAD
branch
working-tree status
rules-bundle identity used by regression gates
vocabulary identity
model-input identity known-answer vectors
candidate counts and ordered-key digests
N=24 result
N=25 result
N=129 result
G28 candidate_domain_max
G28 selected witness identity and full count
paired-hidden-world result
ragged/padded roundtrip result
trajectory label result
framework/dependency audit result
Phase-3/4 regression results
clean-checkout result
per-gate PASS/FAIL/NOT_RUN/SKIPPED/BLOCKED status
~~~

The record must distinguish local execution, hosted execution, wrapper-blocked
execution, and semantic evidence. No gate is promoted because a wrapper,
focused test, or historical artifact happened to succeed.

## 6. Task 1 closure boundary

This task closes only the documentation freeze represented by:

~~~text
ADR-0006 = architecture decision
P5_MODEL_CONTRACT.md = normative versioned semantics
P5_ACCEPTANCE_PLAN.md = future executable gates
docs/adr/README.md = ADR index
~~~

It does not close P5 implementation, model training, backend selection, G28
witness discovery, trajectory materialization, or any ML milestone.
