# Phase 3B — Immutable trusted-trajectory persistence and replay admission

Status: implementation design approved by the task request

Base SHA: `689710a90e751b046c062a8c0b3f56ec2cef5500`

Scope: one Phase-3B implementation PR, internally delivered as seven vertical
slices. Phase 3A remains the authority for every logical trajectory value,
identity, privacy rule, and codec field order.

## 1. Design decision

Phase 3B adds a C++-owned `ygo::trajectory` layer above
`ygo::environment::EpisodicEnvironment` V2:

```text
EpisodicEnvironment V2
        |
        v
TrajectoryRecorder -> canonical EpisodeEnvelope
        |
        +--> CandidateTrajectoryShard
        +--> RestrictedCollectionEvidenceBundle
                         |
                         v
                 semantic V2 replay verifier
                         |
                         v
                  immutable AdmissionReceipt
                         |
                         v
                    DatasetManifest
```

The new layer consumes only value-owned V2 public frames, ordered public
candidate values, V2 result/closure values, and explicit policy provenance.
It does not query `CoreHost`, `EngineTrace`, internal semantic keys,
internal candidate-domain digests, protocol decision IDs, continuation IDs,
response bytes, submission tokens, or raw observations.

The implementation uses one static library (`ygo_trajectory`) linked above
the existing environment library. The V2 implementation is not reinterpreted
or modified to make persistence easier. Any missing V2 value needed for
replay is resolved through a narrow adapter that preserves the existing byte
meaning and rejects unknown identities.

## 2. Vertical slices and closure conditions

The implementation proceeds in this order. Each slice has a focused failing
test first, a green implementation, and an adversarial invariant check before
the next slice starts.

1. **Canonical values and strict codecs** — own the accepted Phase-3A value
   types and exact codecs, including strict decode/re-encode, identity
   derivation, V2 identity-input resolution, and restricted replay evidence.
2. **Recorder** — record reset, public frames, policy attribution, accepted
   actions, rejections, interruption, terminal closure, and failure without
   fabricating actions or restoring clean status after rejection.
3. **Candidate shard and restricted evidence** — provide deterministic,
   uncompressed, hash-addressed containers and atomic local publication.
4. **Replay verifier and admission** — validate every entry against V2 and
   admit a shard atomically only when all structural, provenance, privacy,
   RNG, evidence, replay, and closure conditions hold.
5. **AdmissionReceipt** — bind the exact candidate/evidence artifact bytes to
   ordered admitted record commitments without environmental metadata.
6. **DatasetManifest and dataset identity** — build trusted membership from
   receipts and derive logical identity solely from sorted record IDs.
7. **Acceptance, clean checkout, and determinism evidence** — run fresh
   targeted and regression gates, independent-process comparisons, corruption
   checks, re-sharding checks, privacy checks, and exact-head evidence.

 A slice is closed only when its local tests pass, its negative/corruption
 cases fail closed, its outputs are byte-stable, and its cross-slice contract
 assumptions are recorded. A failed invariant is a blocker; no fallback or
 silent migration is allowed.

## 3. Phase-3A value ownership

The following values are represented in C++ as owned types and are encoded
using the exact field order and primitive rules in the accepted contracts:

- `PolicyArtifact`, `ParticipantPolicyAssignment`,
  `PolicyRngInitializationIdentity`, `PolicyRngStreamIdentity`, and
  `PolicyRngDecisionProvenance`;
- `PublicFrameSnapshot`, `DecisionRecord`, and the three closure variants;
- `PolicyProvenanceEnvelope`, `EpisodeManifest`, and `EpisodeEnvelope`;
- `RestrictedReplayEvidence`;
- public gameplay, trajectory-record, and dataset identity input values.

The codec is a bounded byte reader/writer. Integers are big-endian; strings
and byte vectors have a u32 big-endian length; vectors have a u32 count;
optional values use only 0/1 presence; booleans use only 0/1; digests are
lower-case 64-hex strings. Decoders reject unknown schemas/enums, malformed
UTF-8, overflow, truncation, trailing bytes, noncanonical ordering,
duplicate IDs, invalid lexical IDs, digest mismatches, and cross-field
inconsistency. Every accepted value satisfies:

```text
decode(encode(value)) == value
encode(decode(canonical_bytes)) == canonical_bytes
```

The second equality is byte equality, not merely semantic equality.

V2 environment and episode identity bytes are decoded by a strict adapter,
re-encoded, and compared byte-for-byte before they are used. The resolver
accepts only the current immutable certified environment configuration and
the exact fields represented by the accepted `EpisodeSpec`; unknown or
unresolvable identities fail closed without network access.

## 4. Recorder state machine

`TrajectoryRecorder` is a single-session state machine. It stores a copied
current frame and public history; it never stores an internal candidate
binding as canonical data. The caller supplies the policy attribution for a
selected public action and passes the V2 result back to the recorder.

The recorder enforces:

```text
reset accepted                    -> open at first DecisionFrame
accepted public action             -> exactly one DecisionRecord
StepRejected                      -> zero records; disposition quarantined
accepted continuation intermediate -> one record; no engine-response claim
accepted continuation final       -> one record; V2 owns final submission
terminal boundary                 -> closure only; no fake action
admin interrupt                   -> pending copied frame; no fake action
budget interruption               -> restricted evidence required later
failure                           -> failed closure; never learner eligible
```

The successor of a record is either the next copied public frame or the
typed terminal/interrupted/failed boundary returned by V2. The recorder does
not create a successor observation from a record; the next frame owns it.
An administrative interruption receives the pending frame observed by the
recorder before the public V2 interrupt call and must match the interruption's
episode ID, decision index, and public decision ID.

A policy-origin rejection irreversibly changes collection disposition to
`QUARANTINED_AFTER_POLICY_REJECTION`; a later retry cannot restore `CLEAN`.
Only the typed public rejection classification is retained. Tokens, internal
submitted identities, raw submitted data, and restricted diagnostics are not
canonical recorder payload.

## 5. Physical artifact contracts

Phase 3B introduces only these physical/admission domains:

```text
ocgforge.trajectory_shard.v1
ocgforge.restricted_collection_evidence_bundle.v1
ocgforge.admission_receipt.v1
ocgforge.dataset_manifest.v1
ocgforge.dataset_identity.v1
```

The exact binary formats are documented in the owning contract files and
golden-tested. All are uncompressed and use explicit length/count fields.

### CandidateTrajectoryShard

The shard consists of its domain/schema, a u32 entry count, and entries sorted
lexically by the 64-lowercase-hex SHA-256 of the exact canonical
`EpisodeEnvelope` bytes. Each entry contains the envelope digest, a u32 byte
length, and the bytes. Reader validation recomputes the digest, rejects
duplicates, requires strict envelope decoding, requires canonical sort, and
rejects trailing bytes.

### RestrictedCollectionEvidenceBundle

The bundle binds one exact candidate-shard artifact digest. It contains
sorted interrupted-episode evidence keyed by envelope digest and sorted
non-`NONE` RNG initialization material keyed by initialization identity. Each
entry is recomputed and cross-checked. Missing required evidence and
unreferenced extra evidence both fail admission. Restricted raw material is
never copied into a public frame, identity, receipt member, or dataset
semantic ID.

### Atomic publication

The storage helper writes a temporary sibling, flushes and closes it,
rereads exact bytes, verifies the expected digest, and atomically creates the
content-addressed final directory entry without replacement. An existing
final file is idempotent only when its bytes are identical; a mismatch or
symbolic-link target fails. Temporary names and paths never enter canonical
bytes. A partial temporary file is never treated as published.

## 6. Provenance and RNG validation

Admission validates the exact Phase-3A policy provenance envelope. Artifact
and assignment collections use their normative identity ordering and lexical
IDs. A local explicit resolver/registry may prove the immutable fixture
content used by acceptance tests; unknown required content fails closed.
Contract identities are validated as exact versioned contracts and are not
pretended to be mutable URLs or downloadable artifacts.

For every referenced non-`NONE` RNG initialization, the restricted raw
material must recompute the declared initialization identity and pass a
registered contract-specific state codec. `CURSOR` is accepted only when its
declared contract and initialization identity prove a unique stream state;
otherwise the producer must use `STATE`. This Phase-3B V1 implementation has
no production policy-owned RNG contract by default. Non-`NONE` policy RNG
provenance is admission-eligible only through an explicit immutable typed
registry entry whose descriptor proves canonical initialization material and
the supported `STATE`/`CURSOR` semantics. Acceptance tests use test-only
injected descriptors. `NONE` uses the accepted no-RNG contract and exact IDs.
RNG raw material remains restricted evidence and is excluded from gameplay and
record identities.

## 7. Replay and whole-shard admission

The verifier reconstructs the accepted V2 environment/reset inputs from the
strictly decoded manifest, resets V2, and compares the first public boundary.
For every record it compares the complete ordered request, public observation
and digest, public candidate-domain digest, public semantic decision ID,
selected-key membership, transition class, and successor semantics. Stored
observations are evidence only; regenerated V2 values are authoritative.

Terminal closure compares winner, win reason, semantic action count, last
decision index, the canonical terminal closure/public gameplay identity, and
both perspective-safe terminal views. Terminal closure does not persist or
compare `final_engine_step_index`; that field belongs only to restricted
interruption evidence. Zero-decision terminals are valid. Interrupted closure requires the exact
restricted evidence companion. Budget interruptions replay with the exact
recorded run-control values; administrative cancellation replays to the exact
pending frame and then calls the public V2 interrupt boundary. The accepted
Phase-3A terminal identity does not persist run-control values, so terminal
replay requires an explicit caller-supplied valid `ReplayOptions::terminal_run_control`.
This is verifier input, not a canonical default or migration; a terminal
envelope is rejected when that input is absent or invalid. Interrupted replay
requires an explicit caller-supplied cancellation source in addition to its
restricted evidence, because the source is likewise not a persisted public
identity field. Failed episodes remain structurally persistable but cannot be
admitted.

Admission is whole-shard atomic: every entry must independently pass schema,
canonicality, identities, complete ordered domain, privacy, provenance, RNG,
closure, restricted-evidence, and semantic-replay validation. One failure
produces no receipt and no partial admission.

## 8. AdmissionReceipt

The receipt binds:

- the admission contract ID;
- the exact candidate-shard artifact digest;
- the exact restricted-bundle artifact digest; and
- entries sorted by `trajectory_record_id`, each containing the trusted
  record ID, public gameplay ID, environment ID, episode ID, envelope digest,
  and closure kind.

Its ID is the lower-case digest of the canonical receipt bytes under
`ocgforge.admission_receipt.v1`; the ID is not self-referential. No path,
filename, host, PID, wall clock, worker order, or cloud/build metadata is
present. A receipt is produced only after the entire shard has passed.

## 9. DatasetManifest and logical identity

The physical manifest contains the dataset contract, dataset semantic ID,
member count, and members sorted by record ID. Each member retains enough
artifact provenance to locate and verify its accepted record: record ID,
public gameplay ID, receipt ID, candidate-shard digest, and envelope digest.
It contains no mutable URI or absolute path.

`dataset_semantic_id` is independently derived from the dataset identity
domain/schema, the trusted trajectory contract, and the sorted unique set of
`trajectory_record_id` values. It does not include shard partitioning,
artifact hashes, packing, compression, machine, provider, or time.

Therefore two valid physical packings may have different shard/receipt and
manifest bytes while their equal admitted record set has the same logical
dataset identity. Duplicate membership, conflicting bytes, unknown receipts,
and wrong artifact bindings fail closed.

## 10. Privacy and determinism review boundary

There is no Phase-3B learner projection API. Raw envelopes, global IDs,
restricted evidence, policy RNG material, and physical artifacts are
collection/replay values only. Structural tests inspect all public/learner
surfaces for internal keys, protocol IDs, raw hashes, response material,
tokens, private observations, root seeds, passcodes, restricted diagnostics,
and global identity fields. Paired-world fixtures prove that changing hidden
state does not change the acting player's permitted public values.

Canonical order is explicit at every boundary: V2 candidate order is copied,
shard entries sort by envelope digest, restricted evidence sorts by its
normative keys, receipts sort by record ID, and manifests sort by record ID.
No filesystem, thread, PID, wall-clock, random UUID, or unordered-container
iteration can influence canonical bytes or semantic IDs.

## 11. Acceptance and stop conditions

The implementation stops with `BLOCKER` if an accepted Phase-3A field order,
identity meaning, V2 reset reconstruction, public candidate completeness,
privacy proof, RNG proof, or atomic publication guarantee is ambiguous or
contradictory. It does not add a compatibility decoder, candidate cap,
default, silent migration, alternate identity, or lower-layer semantic fix.

Final acceptance must include the requested P3B-G01..G29 evidence, fresh
clean-checkout results at the exact head, full available CTest and Python
regressions, existing M4 isolation/integrity tests, independent-process byte
comparisons, corruption/truncation rejection, and re-sharding invariance.
Hosted CI status remains distinct from local status and is reported only for
the exact final head.
