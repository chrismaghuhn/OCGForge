# Phase 3A trusted trajectory contracts — acceptance matrix

## Status

This is the Phase-3A contract-review matrix for
`ocgforge.trusted_trajectory.v1` and `ocgforge.policy_provenance.v1`. It is
not runtime acceptance evidence and does **not** claim `TRUSTED TRAJECTORY V1
FINAL PASS`.

A later Phase-3B implementation must produce the executable evidence named in
each gate before persistence or admission can claim acceptance. A passing
documentation review never substitutes for a codec, recorder, replay, privacy,
or storage test.

## Scope

Phase 3A accepts only the proposed semantic contracts and ADR rationale for a
future `ygo::trajectory` owning layer. It does not implement a trajectory
recorder, reader/writer, shard, AdmissionReceipt, DatasetManifest, object
store, remote actor, Teacher, reward adapter, model adapter, tensorization,
training method, neural network, or gameplay change.

## Review rules

A gate is **PASS** only when its exact condition below is met by the proposed
normative documents and adversarial review. Any ambiguity that could weaken
correctness, determinism, privacy, complete candidate semantics, replay
identity, or producer attribution is a **BLOCKER**.

“Phase-3B executable evidence” names the minimum future evidence type; it is
not a claim that the test already exists.

| Gate | Purpose | Exact Phase-3A PASS condition | Severity | Owning layer | Required Phase-3B executable evidence |
| --- | --- | --- | --- | --- | --- |
| P3A-G01 Ownership boundary | Keep recording above the accepted public environment boundary. | ADR-0005 and both contracts state that `ygo::trajectory` consumes only immutable accepted V2 public frames/reset inputs/closures and must not query CoreHost, internal candidates, response bytes, raw state, or mutate/advance V2. | BLOCKER | architecture / `ygo::trajectory` | Boundary test with a recorder façade that accepts only public V2 values; static/source review rejects forbidden dependencies. |
| P3A-G02 Temporal convention complete | Make one record mean one accepted semantic action. | `F_t -- a_t -> F_(t+1)` or closure is exact; successor observations belong to the next frame; first frame, zero-decision terminal, interruption, failure, and administrative cancellation are all specified. | BLOCKER | trajectory core | Golden sequence tests for atomic, zero-decision terminal, interruption, and failure boundaries. |
| P3A-G03 Complete public candidate-domain persistence | Preserve the full legal public domain, not an action summary. | Every record binds the V2 request, complete ordered public candidate vector, observation/digest, domain digest, and public decision ID; the validation list prohibits filtering, sorting, deduplication, truncation, fabrication, and defaults. | BLOCKER | trajectory core | Codec golden vectors plus replay fixture proving byte-for-byte ordered-domain retention and divergence rejection. |
| P3A-G04 Public action selection identity | Bind selection to exactly one public semantic candidate. | `selected_public_action_key` is the only canonical action identity, is required to occur exactly once in the recorded ordered domain, and candidate vector position is expressly nonsemantic. | BLOCKER | trajectory core / V2 boundary | Positive/negative selection tests for unique membership, duplicate keys, unknown keys, and reordered candidates. |
| P3A-G05 Forbidden-field/privacy boundary | Stop hidden or control-plane values from becoming a learner oracle. | The privacy table classifies all required fields, including hashes; tokens, internal keys/IDs/digests, raw response data, raw messages, private observations, diagnostics, pointers, and caches are forbidden or restricted as specified. | BLOCKER | trajectory privacy boundary | Paired-world privacy tests and serialized-field deny-list tests, including secret-derived hash probes. |
| P3A-G06 Gameplay-versus-record identity separation | Distinguish public gameplay from collection provenance and storage. | The full five-level hierarchy is explicit; same reset/different actions and same public gameplay/different policy provenance consequences are stated; public gameplay and record codecs have exact different inputs. | BLOCKER | identity contracts | Golden identity fixtures for action divergence, provenance divergence, storage/build invariance, and failed-envelope rejection. |
| P3A-G07 Exact policy-producer attribution | Identify the concrete producer for every accepted decision. | Policy artifact, participant assignment, seat/deck/policy roles, assignment epochs, artifact adapters, sampling contract, and per-record assignment resolution are fully encoded and fail closed. | BLOCKER | policy provenance | Fixture corpus for RandomLegal, deterministic heuristic, checkpoint, search-assisted, and imported-demonstration provenance; malformed-resolution negatives. |
| P3A-G08 Policy RNG provenance separation | Attribute stochastic choice without importing engine RNG semantics. | Policy RNG contract, stream identity, and pre/post `NONE`/cursor/state modes are encoded; environment root seed and execution/hardware entropy are excluded from RNG identity and gameplay identity. | BLOCKER | policy provenance | Deterministic RNG-provenance vectors, distinct-seat stream tests, malformed state tests, and proof that replay does not consume policy RNG. |
| P3A-G09 Terminal/interrupted/failed closure semantics | Preserve outcome truth without inventing value labels. | Only true terminal carries winner/win reason/views; interrupted carries no game outcome or reward; failed has no fabricated outcome and is not learner eligible; zero-decision terminal is explicit. | BLOCKER | trajectory closure | Closure golden tests for terminal, zero-decision terminal, budget interruption, administrative cancellation, and failure quarantine. |
| P3A-G10 Rejected-call semantics | Keep rejected policy calls out of gameplay transitions. | A V2 rejection creates no record/index/successor/mutation; known policy rejections irreversibly move the collection disposition to quarantine without retaining token or rejected action identity. | BLOCKER | trajectory collection | Integration test for stale, malformed, unknown, and retry-after-rejection cases; asserts no clean admission. |
| P3A-G11 Continuation semantics | Preserve intermediate policy decisions and exact final submission boundary. | Intermediate continuation, final continuation, and atomic transitions have explicit codes/proofs; each accepted continuation action records a full frame; no internal continuation ID or response material is persisted. | BLOCKER | EpisodeDriver / V2 / trajectory | Continuation replay tests that prove paused intermediate behavior, record order, final submission count, and no private-ID persistence. |
| P3A-G12 Reward independence | Keep environment truth neutral to ML reward policy. | No numeric reward exists in canonical values; future RewardAdapter/View is derived and versioned; changing it cannot change semantic/public gameplay identities or outcomes; interrupted/failed have no implicit reward. | BLOCKER | trajectory derived boundary | Two reward-adapter fixtures over identical canonical episodes prove unchanged core identities and zero implicit interrupted/failed reward. |
| P3A-G13 Recurrent/POMDP perspective isolation | Prevent mixed-seat recurrent state or private observation joins. | `AgentDecisionStream(episode, participant_assignment)` contains only one assignment’s records in global order; same checkpoint on two seats remains separate; burn-in/TBPTT/state are derived. | BLOCKER | future model adapter | Derived-stream tests showing no cross-player concatenation and no extra canonical state fields. |
| P3A-G14 Canonical-versus-derived boundary | Keep the source data algorithm-neutral and regenerable. | The contract lists canonical values and the required derived values, including tensors, indexes, rewards, recurrence, returns, priorities, exports, splits, and normalization; derived data cannot change core identities. | BLOCKER | trajectory core | Projection reproducibility tests from canonical envelope plus explicit derivation configuration; identity invariance tests. |
| P3A-G15 Canonical codec/hash field order complete | Leave Phase 3B no semantic codec guesswork. | All new IDs have exact domain/schema strings, lexical forms, field order, primitive codecs, enum codes, exclusions, and mutation rule; frame/request/candidate/continuation/closure/manifest codecs are specified. | BLOCKER | identity and codec contracts | Cross-process golden codecs, SHA-256 vectors, parser rejection tests, and independent implementation comparison. |
| P3A-G16 Version-rejection/migration rules | Make incompatibility fail closed. | Each incompatible field/visibility/order/byte/identity change requires a new version; unknown version/enum/bytes reject; no aliases, defaults, inferred fields, or silent migrations exist. | BLOCKER | compatibility boundary | Negative tests for unknown versions, bad presence/enums, missing fields, altered order, and old-format alias attempts. |
| P3A-G17 Phase-3B persistence/admission handoff complete | Constrain persistence without prematurely designing it. | The contract defers physical artifacts but fixes the handoff from canonical episode through structural/privacy validation and semantic replay to future receipt/manifest; it names all deferred storage concerns. | MAJOR | Phase 3B admission | Candidate-shard structural validation, semantic replay, immutable receipt, and dataset-manifest integration tests. |
| P3A-G18 Scope/non-goal enforcement | Prevent Phase-3A from becoming an unreviewed runtime/ML change. | Diff contains only normative ADR/contracts/acceptance/discoverability documentation; no gameplay, V2, rules bundle, persistence, Teacher, ML, framework dependency, or implementation change is introduced. | BLOCKER | Phase-3A delivery | Phase-3B scope review and CI/dependency checks before any implementation PR. |

## Required Phase-3A review record

Before the documentation PR is marked ready, reviewers must perform a
separate standards and specification pass over the proposed diff. The review
must explicitly inspect for:

- a hidden-information oracle through direct values or hashes;
- accidental internal-ID, token, response, or audit persistence;
- candidate-index authority or domain reshaping;
- ambiguous reward timing or duplicated successor observation;
- a fake terminal action or ambiguous zero-decision closure;
- episode-global behavior-policy attribution;
- mixed-player recurrent streams;
- algorithm-, framework-, storage-, provider-, build-, or hardware-specific
  canonical fields;
- unversioned semantics, silent aliases, and missing codec field order; and
- a Phase-3B artifact being implemented under a Phase-3A claim.

Every **BLOCKER** must be resolved before the PR can be called ready. A
focused document check or an unavailable build cannot be promoted into Phase
3B acceptance.

## Phase-3B handoff

Phase 3B begins only after this contract PR is reviewed and merged. Its
separate scope is:

```text
canonical episode
  -> candidate shard
  -> structural / schema / privacy validation
  -> semantic replay verification
  -> immutable AdmissionReceipt
  -> DatasetManifest
```

That work may choose physical encodings, compression, storage objects,
publication, and transport only if it preserves the v1 semantic bytes,
privacy boundary, and identity hierarchy. It does not authorize Teacher,
model, training, tensorization, or framework work.

## Non-acceptance statement

This document establishes a contract-review milestone only. It does not claim
a trusted trajectory runtime, a persisted trusted dataset, a learner-eligible
corpus, or any final Phase-3 acceptance status.
