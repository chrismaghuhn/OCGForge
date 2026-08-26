# Episodic Environment V1 — Phase 2 Public reset/step Facade

Status: implementation specification

Normative architecture: ADR-0002

Normative public contract: ocgforge.episodic_environment.v1

Base architecture: merged EpisodeDriver Phase 1

Phase: public reset/step facade

Production implementation: NOT INCLUDED

Trajectory/ML work: NOT INCLUDED

Base main SHA inspected: b4ca1fa01b2cafdce5ed486699490e693235d9c4

Phase-1 merge: PR #11, merge SHA b4ca1fa01b2cafdce5ed486699490e693235d9c4

Phase-1 implementation head: 0fea4670554fd4d950eef29ec4777535c2c279fd

Immutable pre-Phase-1 equivalence baseline:
72c29009f107a2ebb172d85de1c70b38d2f007d8

Proposed implementation branch:
chris/spec/episodic-phase2-public-facade

Proposed implementation PR title:
feat: add Episodic Environment V1 public facade

This document is documentation-only. It does not add the production
EpisodicEnvironment type, modify EpisodeDriver, change a public contract, or
produce Episodic V1 acceptance evidence.

## 1. Decision summary

The smallest safe Phase-2 architecture is a value-owning facade above the
existing EpisodeDriver:

    public ActionSelection
            |
            v
    EpisodicEnvironment
      lifecycle, IDs, tokens, rejection gate,
      owned public frames, closure values
            |
            v
      EpisodeDriver
        sole advancement owner
            |
            v
      CoreHost / Protocol / Observation / Trace

The facade must never:

- process the core;
- construct or submit a response;
- perform continuation transitions;
- select a hidden target or card identity;
- sort, filter, truncate, deduplicate, or fabricate candidates;
- expose the internal response bytes;
- retain a CoreHost after closure;
- reinterpret EngineTrace v2.

The recommended production representation is
ygo::environment::EpisodicEnvironment. It is compiled into the existing
ygo_m4 static library beside EpisodeDriver and canonical_simulation.cpp. No
new durable runtime target is justified by this milestone. A test-only
ygo_episodic_probe executable may link ygo_m4 later; it is not an actor
transport or a production service.

The design is implementation-blocked at this checkpoint for two reasons:

1. The accepted EnvironmentConfig names decision-contract,
   action-identity, and seed-derivation identifiers, but the live accepted
   sources do not assign concrete values to those identifiers. A Phase-2
   implementation must not invent values in a facade PR.
2. The live accepted M4 candidate_max value of 1344 is an aggregate sum of
   64 per-job maxima, not one legal candidate-domain size. G28 requires one
   replayable witness whose candidate count equals the accepted maximum.
   The current traceable witness is 21. The metric and G28 evidence contract
   must be reconciled before implementation acceptance.

These blockers do not require changing the accepted contracts in this
specification PR. They require an explicit prerequisite decision before the
implementation PR starts.

## 2. Live repository audit

### 2.1 Exact starting state

The requested fetch was performed before inspection:

    git fetch --prune origin main

origin/main resolved to:

    b4ca1fa01b2cafdce5ed486699490e693235d9c4

The local checkout was clean and was branched from that exact commit. PR #11
is merged at that SHA. No relevant runtime movement was found between the
handoff checkpoint and live main.

The current checkout before the branch operation was an older local main
commit, 48535b23e1ecfbb0aed402a435a90fee6217c595, but it had no uncommitted
changes. No reset, checkout-overwrite, or destructive operation was used.

### 2.2 Phase-1 ownership

Phase 1 moved authoritative advancement into EpisodeDriver. The current
driver owns:

- CoreHost and duel construction;
- seed-bundle construction;
- automatic process calls;
- message ingestion and protocol decode;
- candidate validation;
- perspective observation construction;
- continuation transitions;
- exact final response bytes and submission;
- terminal and process-budget closure;
- trace and gameplay-hash inputs.

The current DriverDecisionBoundary contains borrowed pointers to an internal
DecisionRequest and PlayerObservation. A mutating driver call invalidates
those pointers. This is an internal seam and must remain internal.

CanonicalSimulation now selects policy/replay keys and aggregates the result.
It must remain a client of the same advancement implementation after Phase 2.

### 2.3 Current seed path

The live Phase-1 and pre-extraction baseline both derive CoreHost's four-word
seed bundle from SimulationJob.seed using exactly:

    word[0] = root_seed
    word[1] = root_seed XOR 0x9e3779b97f4a7c15
    word[2] = root_seed + 0x6a09e667f3bcc909
    word[3] = (root_seed << 1) XOR 0xbb67ae8584caa73b

All arithmetic is unsigned 64-bit modulo 2^64. Phase 2 must extract this
function into one shared internal helper and use it from both
EpisodeDriver and CanonicalSimulation. It must not introduce a second RNG or
replace this mapping with M4's master-seed job derivation.

### 2.4 Protocol ordering and empty-domain audit

The current decoder constructs candidates in authoritative wire or explicit
protocol order:

- direct engine lists are traversed in wire order;
- bit masks are traversed in ascending bit order;
- fixed positions use the declared fixed order;
- continuation choices preserve the source order;
- ordered selections preserve meaningful permutation order;
- amount choices are ascending;
- finish/cancel candidates are appended by the continuation rule.

No unordered_map, unordered_set, pointer ordering, or filesystem iteration
was found in the current protocol candidate construction. The environment
must preserve this order and must not repair it by sorting.

Every currently supported interactive family calls candidate validation.
Zero-count option, card-selection, placement, counter, ordering, announcement,
or continuation domains are rejected as malformed/unsupported before a
request is published. Therefore no currently supported legal request is known
to legitimately publish an empty candidate vector. G09 can require empty and
duplicate domains to fail closed without fabricating a pass or cancel
candidate. If a future supported family violates this conclusion, Phase 2
must stop and classify the contract/implementation conflict.

### 2.5 Trace and audit-prefix state

EngineTrace v2 remains frozen. In particular:

- its decision index is not the public environment decision index;
- intermediate continuation records retain engine_advanced=false;
- only a terminal continuation carries the final response hash;
- response hashes and trace canonical bytes retain their existing meanings;
- semantic_gameplay_hash remains a separate semantic projection.

The accepted v2 canonical trace hash includes the trace manifest and therefore
may include build/provenance fields. It can safely serve as the
final_audit_prefix_hash only as an audit/provenance value, not as an
environment/episode/gameplay identity. The environment must never use it as a
cross-build semantic ID.

## 3. Findings and implementation gates

| ID | Classification | Finding | Required resolution |
| --- | --- | --- | --- |
| B1 | BLOCKER | EnvironmentConfig requires decision-contract, action-identity, and seed-derivation IDs, but no concrete accepted constants exist in the live contract/code. | Ratify exact constants in the owning normative contract or an explicit accepted identity ADR before production implementation. Do not invent them in Phase 2. |
| B2 | BLOCKER | G28 refers to an accepted aggregate maximum of 1344, but 1344 is the sum of 64 per-job candidate_max values. It cannot be a single request domain or witness. | Correct the metric/evidence vocabulary and generate a deterministic per-domain maximum witness, or obtain an explicit accepted G28 clarification. |
| M1 | MAJOR | Internal ActionCandidate locator fields and semantic keys are not automatically policy-safe. | Implement the fail-closed public projection audit and prove it over the certified corpus before publishing a public frame. |
| M2 | MAJOR | Current Driver does not return accepted-action response metadata as a value. | Add the minimal internal DriverApplyResult described in this document; do not derive metadata from response bytes or old trace fields. |
| M3 | MAJOR | Current Driver process budget is uint32 and has no public semantic-action budget or typed public interruption. | Add an internal run-control seam while preserving canonical-simulation mapping and trace semantics. |
| M4 | MAJOR | Current terminal observation materialization is not yet an owned two-perspective public closure value. | Materialize safe terminal observations before Driver teardown and cache only value data. |
| M5 | MAJOR | Required script closure is not a first-class semantic value in the current runtime config. | Derive and verify an exact code/script closure from the locked bundle before environment identity construction. |
| m1 | MINOR | Existing diagnostic error strings can contain paths or other restricted details. | Public failures expose only typed codes and an opaque restricted diagnostic reference. |
| m2 | MINOR | Existing M4 evidence calls an aggregate counter candidate_max. | Preserve historical evidence, but label any new per-domain fields unambiguously. |
| N1 | NOTE | The local facade is serial-call only and does not claim thread safety. | Document the requirement; no lock-order semantics are added in V1. |
| N2 | NOTE | M4 worker process isolation remains useful for acceptance probes, but Phase 2 does not add a worker protocol or RPC. | Reuse the existing process harness only as a test boundary. |

Because B1 and B2 remain open, the implementation status is:

    PHASE 2 IMPLEMENTATION SHOULD NOT BEGIN

## 4. Build ownership

The existing target graph is:

    ocgcore
      -> ygo_m0
        -> ygo_m4
             - episode_driver.cpp
             - canonical_simulation.cpp
          -> ygo_m4_worker
          -> Phase-1 episodic tests

Phase 2 should add episodic_environment.cpp to ygo_m4:

    ocgcore
      -> ygo_m0
        -> ygo_m4
             - episode_driver.cpp
             - episodic_environment.cpp
             - canonical_simulation.cpp
          -> ygo_m4_worker
          -> ygo_episodic_probe (test/acceptance only)

No new production static library is needed. A target split would duplicate
include/link definitions and create a new place for ownership drift without
changing the runtime semantics. If a probe is introduced, it must link the
same ygo_m4 target and use only the public facade. It must not move
EpisodeDriver into the probe or alter ygo_m4 worker behavior.

### 4.1 Accepted-concept ownership map

| Accepted concept | Existing Phase-1 support | Phase-2 owner | Production representation | New code required | Semantic risk | Privacy risk | Acceptance gates |
| --- | --- | --- | --- | --- | --- | --- | --- |
| EnvironmentConfig | canonical simulation identity and RulesBundlePaths | certified environment factory | immutable CertifiedEnvironmentConfig plus internal EnvironmentResources | factory/lock verification and B1 IDs | wrong resource changes gameplay | path or hidden resource leak | G01, G02, G30, G32 |
| environment_semantic_id | component identity fields exist, no aggregate codec | environment identity codec | lowercase SHA-256 string | canonical byte builder | omitted field creates alias | none if path-free | G01, G02, G30 |
| EpisodeSpec | SimulationJob seed/seat/start fields | facade reset validation | value DTO | validated public type and mapping | wrong seed/deck mapping | none | G01-G04, G19 |
| resolved seed bundle | Driver helper exists | shared internal seed helper | four u64 values | extract one helper and vector identity | second RNG or drift | none | G01, G19, G20 |
| episode_semantic_id | no public value | facade identity codec | lowercase SHA-256 string | episode codec | RunControl contamination | none | G01-G04, G20 |
| RunControl | Driver process budget only | Driver checks; facade validates/translates | two positive u64 budgets plus cancellation metadata | semantic budget and typed interruption | duplicate lifecycle authority | no policy data | G17, G18, G25 |
| lifecycle | Driver internal lifecycle and boundary | facade public lifecycle, Driver authoritative closure | EMPTY/AWAITING_ACTION/GAME_TERMINAL/INTERRUPTED/FAILED | persistent storage and typed results | ignored/implicit reset | closed diagnostics only | G03-G05, G12, G24-G27 |
| episode incarnation | none | facade token namespace | u64 control counter | monotonic counter and overflow handling | stale alias | none | G03-G05, G10-G13 |
| submission token | borrowed boundary only; no token | facade | two u64 control token | issue/compare/invalidate | stale action accepted | no semantic leak | G03, G10-G13, G25 |
| decision_index | EngineTrace index exists, different meaning | facade | u64 public index | separate counter and codec input | trace-index reinterpretation | none | G06, G10, G14, G19 |
| candidate_domain_digest | Phase-1 characterization digest is separate/historical | facade identity codec | lowercase SHA-256 string | accepted v1 digest codec | order/count loss | key may expose hidden identity if not audited | G07-G09, G20, G28 |
| semantic_decision_id | protocol decision_id exists | facade wrapper | lowercase SHA-256 string | decision codec | wrong observation/order binding | no new hidden data | G01, G06, G10, G20 |
| public DecisionFrame | borrowed DriverDecisionBoundary | facade | owned value DTO | safe projection and copy | borrowed lifetime leak | direct policy boundary | G06-G13, G20, G22-G23 |
| public candidate collection | internal ActionCandidate vector | facade projection | owned complete ordered vector | sanitized DTO and visibility checks | truncation/fabrication | response bytes/locators | G06-G09, G14-G16, G22 |
| ActionSelection | replay key exists; no public token gate | facade | five-field value DTO | validation precedence | candidate-index alias | raw bytes never accepted | G10-G13, G20 |
| StepAccepted | Driver boundary has no public accepted metadata | facade over DriverApplyResult | transition plus NextBoundary | result variants and metadata seam | accepted action mislabeled rejection | response hash only | G14-G20 |
| StepRejected | no public facade type | facade | typed value variant | rejection codes and snapshot tests | mutation after rejection | safe diagnostics only | G10-G13 |
| AcceptedActionTransition | trace has related but non-equivalent fields | Driver supplies facts; facade owns public IDs | selected key/index/submission fact/hash | DriverApplyResult | engine_advanced confusion | response bytes excluded | G14-G16 |
| EpisodeTerminal | Driver terminal boundary/winner/reason exists | facade closure | immutable terminal value | safe closure/evidence and views | budget mistaken for outcome | terminal reveal risk | G24, G29 |
| EpisodeInterrupted | process-budget Driver boundary exists | Driver reason, facade value | typed reason/prefix/run-control evidence | semantic budget/admin interrupt | winner/draw fabrication | control metadata only | G17, G18, G25 |
| EpisodeFailure | Driver failure code/counters exist | Driver classification, facade sanitization | typed code/stage/prefix/mutation flag | mapping and teardown | failure converted to sample | restricted diagnostics | G26-G27 |
| administrative interrupt | no public operation | Driver close, facade API | InterruptResult | typed control path | accidental gameplay mutation | no policy action | G12, G24-G25 |
| semantic-action budget | absent in Phase 1 | Driver | u64 count/limit | authoritative count/check | closure timing drift | none | G17-G18 |
| engine-process budget | Driver uint32 process limit | Driver | u64 internal/public limit | width/translation | canonical behavior drift | none | G18-G19 |
| terminal views | Driver currently materializes limited terminal observation | Driver materializes both; facade caches | two PlayerObservation values | eager safe cache | terminal state mismatch | reveal-all risk | G23-G24 |
| replay | canonical replay keys exist | test-only semantic replay harness | value script of semantic keys | cross-boundary comparator | hidden byte/index dependency | replay input leak | G20 |
| audit-prefix hash | EngineTrace v2 canonical hash exists | Driver/closure evidence | existing v2 prefix hash | exact prefix snapshot | new hash domain drift | provenance exposure | G15-G16, G20, G24 |
| cleanup/reset isolation | fresh Driver per SimulationJob | facade closed-state teardown | no mutable episode resource retained | destructor/reset ordering | state contamination | hidden knowledge reuse | G04-G05, G27 |

## 5. Certified EnvironmentConfig and runtime resources

### 5.1 Public configuration strategy

V1 must not accept arbitrary rules, database, CardScripts, deck, format, or
patchset selectors. The public construction path should be:

    CertifiedEnvironmentConfig::canonical()
        -> validates the compiled canonical lock
        -> resolves internal runtime resources
        -> returns a value-only certified config or construction failure

The resulting EnvironmentConfig value contains semantic identities and
certified locked deck definitions. It does not expose filesystem paths as
semantic fields.

The conceptual separation is:

    Certified EnvironmentConfig
      semantic values only

    internal EnvironmentResources
      runtime paths and handles used only while constructing a fresh Driver

The resources are validated against the semantic config before any duel is
created. A mismatch is a reset-call rejection if detected before fresh
authoritative mutation. A failure after fresh duel construction begins is a
typed EpisodeFailure followed by immediate teardown.

### 5.2 Locked semantic values

The canonical factory must verify all of the following exact values from the
existing lock and canonical simulation config:

| Field | Required value |
| --- | --- |
| contract | ocgforge.episodic_environment.v1 |
| rules bundle | 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f |
| format | TCG_ADVANCED_2026_05_18 |
| duel mode | DUEL_MODE_MR5 |
| duel flags | 0x2E800, decimal 190464 |
| Core API | 11.0 |
| ocgcore commit | 9a0c558c2d686542f7914a6d529fd7aa57746aed |
| ocgcore resolved checkout hash | 161849049d34de7ea60b2f370cc35f903262c14769e399d0bf43a381d295d7f3 |
| patchset ID | ocgforge.ocgcore.api_hardening.v1 |
| patchset SHA-256 | 6b5421b3a852085f48fa161a5ba1540f902aa00784a337694b21c9efc34f69bd |
| CardScripts commit | f337c87018ca723c1aded5143e616bb649555273 |
| CardScripts resolved checkout hash | ce53e1033ea7a3057745bbe143c1803a4a0c7db7f6cded4c28b0170536320e3c |
| BabelCDB commit | 89ad6837b0766a52984d8c715a7d5d4f8447946b |
| BabelCDB resolved checkout hash | acef726f8fb74d7f2b3a43dc14088f117012dbf9c7eab35612716cb8e79df4d1 |
| cards.cdb SHA-256 | 7a6570fe313ae0affe4e1e2047c564b669397500777da6c3d76e79aac393726f |
| seat-0 deck ID | ocgforge.swordsoul_tenyi.ml_v1 |
| seat-0 deck SHA-256 | 8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7 |
| seat-1 deck ID | ocgforge.salamangreat.ml_v1 |
| seat-1 deck SHA-256 | 6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188 |

The ordered deck identity is part of the environment and episode inputs.
Seat assignment changes which certified deck identity occupies seat zero and
seat one; it does not select a different deck.

### 5.3 Runtime path checks

Runtime paths are used only to locate:

- the CardScripts root;
- the locked cards.cdb artifact;
- the two locked deck files;
- any test-only fixture setup script.

The factory must verify:

1. canonical path resolution remains inside the configured rules/deck roots;
2. the loaded deck bytes hash to the required ordered deck hashes;
3. cards.cdb hashes to the locked artifact hash;
4. the CardScripts checkout matches the locked checkout identity;
5. the ocgcore/patchset identity is the compiled canonical target identity;
6. required script resolution uses only the locked official/unofficial
   lookup rules;
7. the required script closure is exactly the one represented in the
   environment identity.

A path string, absolute/relative spelling, host drive, symlink, compiler,
build type, PID, worker slot, timestamp, or machine identity never enters
environment_semantic_id.

### 5.4 Required-script closure

The current M3/M4 setup derives required script codes by concatenating the
ordered main/extra passcodes of both locked decks, sorting numerically, and
removing duplicates. Phase 2 should retain that deterministic derivation and
make it an explicit certified value:

    required_script_closure_codes =
        sorted_unique(deck_0.main + deck_0.extra
                      + deck_1.main + deck_1.extra)

The closure field is encoded as a vector of u32 passcodes in the environment
identity. It is not a filesystem path and it does not depend on directory
iteration. The locked CardScripts checkout identity already binds the
contents; the closure vector binds the certified subset.

If the engine requests a script outside the certified resolution policy, or
if preflight cannot prove the requested code/path/content relation, the
environment fails closed. It does not silently broaden the closure.

## 6. Public schema IDs

The following values are already concrete in accepted sources:

| Semantic surface | Exact value | Authority |
| --- | --- | --- |
| episodic contract | ocgforge.episodic_environment.v1 | episodic-environment-v1 |
| environment identity | ocgforge.environment_identity.v1 | episodic-environment-v1 |
| episode identity | ocgforge.episode_identity.v1 | episodic-environment-v1 |
| semantic decision identity | ocgforge.semantic_decision_identity.v1 | episodic-environment-v1 |
| candidate digest | ocgforge.candidate_domain.v1 | episodic-environment-v1 |
| observation | ygo.player_observation.v1 | player-observation-v1 |
| trace | ygo.engine_trace.v2 | engine-trace-v2 |
| ocgcore patchset | ocgforge.ocgcore.api_hardening.v1 | rules-bundle lock |

The following fields are named by the accepted EnvironmentConfig but have no
concrete accepted constant in the live sources:

| Field | Current live state | Required action |
| --- | --- | --- |
| decision_contract_id | Decision protocol v1 is named, but no exact public ID constant is declared in decision-protocol-v1.md or code. | Ratify one exact value before implementation. |
| action_identity_schema_id | Semantic-key rules exist, but no separate accepted action identity ID is declared. | Ratify one exact value or explicitly bind it to a named existing contract. |
| seed_derivation_id | The four-word derivation is implemented, but no accepted schema/domain ID is declared. | Ratify one exact value and bind it to the shared helper. |

The implementation must reject an unknown or incompatible value for each
field once the constants are ratified. Until then, these are not permitted
to be filled with a guessed string. This is finding B1.

## 7. Canonical primitive encoding and identity byte layouts

All new identity payloads use the accepted primitive encoding:

- u8: one unsigned byte;
- u32: four-byte unsigned big-endian;
- u64: eight-byte unsigned big-endian;
- bool: 00 or 01;
- string: u32be byte length followed by UTF-8 bytes;
- byte string: u32be byte length followed by raw bytes;
- vector: u32be element count followed by encoded elements;
- optional: presence byte followed by the value when present;
- digest result: lowercase 64-character SHA-256 hex string in the public C++
  value representation.

The SHA-256 input begins with the length-prefixed domain string. No
implementation may serialize a C++ enum by object representation or rely on
native endianness. All enum-to-string mappings used in an identity are
explicit tables.

### 7.1 Environment semantic ID

environment_semantic_id is the lowercase hexadecimal SHA-256 of the
following exact sequence:

| Order | Field | Type | Source | Included | Reason |
| ---: | --- | --- | --- | --- | --- |
| 0 | hash domain | string | constant ocgforge.environment_identity.v1 | yes | Separates this digest domain |
| 1 | identity schema ID | string | same accepted constant | yes | Binds the field schema |
| 2 | episodic contract ID | string | ocgforge.episodic_environment.v1 | yes | Public semantic contract |
| 3 | decision contract ID | string | ratified constant, currently undefined | yes, blocked | Decision boundary version |
| 4 | observation contract ID | string | ygo.player_observation.v1 | yes | Safe observation schema |
| 5 | action identity schema ID | string | ratified constant, currently undefined | yes, blocked | Semantic-key identity rules |
| 6 | candidate digest schema ID | string | ocgforge.candidate_domain.v1 | yes | Domain digest codec |
| 7 | episode identity schema ID | string | ocgforge.episode_identity.v1 | yes | Episode identity codec |
| 8 | decision identity schema ID | string | ocgforge.semantic_decision_identity.v1 | yes | Decision identity codec |
| 9 | seed derivation ID | string | ratified constant, currently undefined | yes, blocked | Root-to-CoreHost seed mapping |
| 10 | rules bundle ID | string | locked bundle | yes | Rules identity |
| 11 | Core API version | string | locked bundle | yes | API capability |
| 12 | ocgcore commit | string | locked bundle | yes | Engine source |
| 13 | ocgcore resolved checkout hash | string | locked bundle | yes | Resolved source tree |
| 14 | patchset ID | string | locked bundle | yes | Repository patch identity |
| 15 | patchset SHA-256 | string | locked bundle | yes | Exact patch bytes |
| 16 | CardScripts commit | string | locked bundle | yes | Script source |
| 17 | CardScripts resolved checkout hash | string | locked bundle | yes | Resolved script tree |
| 18 | database commit | string | locked bundle | yes | Database source |
| 19 | database resolved checkout hash | string | locked bundle | yes | Resolved database tree |
| 20 | database artifact hash | string | locked bundle | yes | Runtime cards.cdb bytes |
| 21 | format ID | string | canonical simulation config | yes | Format semantics |
| 22 | duel mode | string | canonical simulation config | yes | Core mode semantics |
| 23 | duel flags | u64 | canonical simulation config | yes | Core option bits |
| 24 | locked decks | vector of {deck ID string, deck hash string} | ordered canonical deck definitions | yes | Deck identity and order |
| 25 | required script closure | vector of u32 | sorted unique locked-deck passcodes | yes | Certified script scope |

No path, compiler, build, worker, process, pointer, timestamp, counter, or
RunControl field occurs in this sequence.

### 7.2 Episode semantic ID

episode_semantic_id is the SHA-256 of:

| Order | Field | Type | Source | Included | Reason |
| ---: | --- | --- | --- | --- | --- |
| 0 | hash domain | string | ocgforge.episode_identity.v1 | yes | Separates episode domain |
| 1 | episode identity schema ID | string | ocgforge.episode_identity.v1 | yes | Binds schema |
| 2 | environment semantic ID | string | resolved environment value | yes | Parent environment |
| 3 | root seed | u64 | EpisodeSpec.root_seed | yes | Explicit semantic seed |
| 4 | resolved seed bundle | vector of four u64 | one shared seed helper | yes | Exact CoreHost seed |
| 5 | seat assignment | u8 | EpisodeSpec, normal=0/mirror=1 | yes | Seat-to-deck mapping |
| 6 | starting player | u8 | EpisodeSpec, validated 0 or 1 | yes | Initial turn semantics |
| 7 | resolved seat deck identities | vector of two {deck ID string, hash string} | seat mapping | yes | Exact per-seat decks |

RunControl, job ID, policy identity, token counters, process identity, and
execution provenance are excluded. Therefore repeated identical resets have
the same episode_semantic_id while receiving different live tokens.

### 7.3 Candidate-domain digest

candidate_domain_digest is the SHA-256 of:

| Order | Field | Type | Source | Included | Reason |
| ---: | --- | --- | --- | --- | --- |
| 0 | hash domain | string | ocgforge.candidate_domain.v1 | yes | Separates domain digest |
| 1 | request kind | string | explicit protocol kind-name table | yes | Binds family |
| 2 | candidate count | u32 | validated vector size | yes | Binds completeness |
| 3..n | semantic key | string, one per candidate | authoritative protocol vector order | yes | Binds exact membership/order |

The digest operation does not sort, normalize, deduplicate, filter, or cap.
The full candidate vector remains in the owned public frame.

### 7.4 Semantic decision ID

semantic_decision_id is the SHA-256 of:

| Order | Field | Type | Source | Included | Reason |
| ---: | --- | --- | --- | --- | --- |
| 0 | hash domain | string | ocgforge.semantic_decision_identity.v1 | yes | Separates decision domain |
| 1 | decision identity schema ID | string | same accepted constant | yes | Binds schema |
| 2 | episode semantic ID | string | current episode | yes | Parent episode |
| 3 | environment decision index | u64 | facade, first frame 0 | yes | Public decision position |
| 4 | protocol DecisionRequest.decision_id | string | Driver request | yes | Existing protocol identity |
| 5 | acting player | u8 | request player | yes | Perspective coupling |
| 6 | engine step index | u64 | request engine_step_index | yes | Core progress |
| 7 | observation hash | string | safe PlayerObservation | yes | Safe state identity |
| 8 | candidate-domain digest | string | exact current domain | yes | Complete action domain |

The facade wraps the protocol decision ID; it does not replace or
reinterpret it.

### 7.5 Golden-vector plan

Identity tests must contain independent reference vectors, not only
round-trips through the production codec:

| Vector | Fixed input | Independent check |
| --- | --- | --- |
| environment | canonical locked values, with every string and vector in the order above | Python/reference byte builder and C++ codec must produce identical payload bytes and digest |
| episode | root seed 1, normal seats, starting player 0, explicit four-word bundle | independently encode each u64 in big-endian and compare the digest |
| candidate domain | request kind idle_command and a two-key ordered vector | recompute from an independent implementation; swapping keys must change the digest |
| semantic decision | fixed episode ID, index 0, protocol decision ID, player 0, engine index 6, fixed observation hash and candidate digest | compare exact bytes and digest across two processes |
| negative order | same candidate keys with reversed order | digest must differ and the public domain must retain the reversed authoritative order if supplied by protocol |
| negative count | same keys with an altered count | digest must differ; malformed count cannot be published |

The environment, episode, and semantic-decision vectors cannot be finalized
until B1 constants are ratified. The candidate-domain vector is independently
specifiable now. No generated acceptance artifact may be hand-edited to make a
vector pass.

## 8. EpisodeSpec, seed mapping, and identity invariants

EpisodeSpec contains only:

- contract ID;
- root_seed: u64;
- seat_assignment: normal or mirror;
- starting_player: u8 in {0,1}.

The root seed maps exactly to SimulationJob.seed. M4's splitmix64 mapping
derives an external job seed from a master seed and is not part of the
environment's RNG implementation. If a test uses M4 job generation, the
derived job seed becomes root_seed and the four-word helper is then used.

The required invariant is:

    same certified environment
    + same root_seed
    + same resolved four-word seed bundle
    + same seat assignment
    + same starting player
    + same resolved seat deck identities
    = same episode_semantic_id

This equality must hold across reset calls, processes, workers, and accepted
build provenance. Changing either budget or cancellation metadata must not
change it.

## 9. Public candidate and request safety

The internal protocol types remain trusted internal types. The facade
publishes deterministic value DTOs named EnvironmentDecisionRequest and
EnvironmentActionCandidate (exact namespace spelling may follow repository
conventions, but they must not alias the internal structs).

### 9.1 DecisionRequest audit

| Field | Current meaning | Required by policy? | Privacy-safe? | Deterministic? | Public V1 representation |
| --- | --- | --- | --- | --- | --- |
| kind | semantic decision family | yes | yes | yes, with explicit enum/name table | EnvironmentDecisionRequest.kind |
| decision_id | existing protocol request identity | yes for replay and semantic ID | yes | yes | owned string |
| engine_step_index | core processing/request index | yes | yes | yes | u64 |
| player | acting player | yes | yes | yes | u8, coupled to frame/observation |
| engine_message_type | pinned wire message type | useful for policy/audit | safe as a typed enum in certified scope | yes | public numeric type only if the contract accepts it |
| engine_message_name | stable readable family name | useful but not authoritative | safe | yes | owned string or omitted in favor of kind |
| raw_message_hash | hash of complete engine frame | no for policy | restricted; may permit dictionary inference | yes but not policy-safe | private Driver/trace only |
| candidates | complete ordered legal domain | yes | only after per-field audit | yes if protocol order is preserved | owned vector of EnvironmentActionCandidate |
| continuation | continuation state and legal transition context | yes where present | only after sanitization | yes | EnvironmentContinuationView without raw hash/hidden locator |

The public request must preserve every policy-relevant field needed to
interpret the complete domain. It must not expose raw message bytes,
raw_message_hash, CoreHost queries, or private continuation buffers.

### 9.2 ActionCandidate audit

| Field | Current meaning | Required by policy? | Privacy-safe? | Deterministic? | Public V1 representation |
| --- | --- | --- | --- | --- | --- |
| action_kind | semantic candidate family | yes | yes | yes | typed public action kind |
| semantic_key | authoritative selection identity | yes | only after proving no hidden identity is encoded | yes by protocol contract | owned string, unchanged |
| source_card | source passcode | sometimes | only when visible to the acting player | yes | optional visible source reference; otherwise projection fails if required |
| source_controller | source owner/controller | sometimes | only with the corresponding visible reference | yes | optional u8 |
| source_location | source zone | sometimes | hidden zones are not automatically safe | yes | optional typed visible location |
| source_sequence | source slot identity | sometimes | current visible locator only; never persistent physical identity | yes | optional u32 |
| target_card | target passcode | sometimes | only when target is visible to acting player | yes | optional visible target reference |
| target_controller | target owner/controller | sometimes | only with visible target | yes | optional u8 |
| target_location | target zone | sometimes | hidden targets are not safe | yes | optional typed visible location |
| target_sequence | target slot identity | sometimes | current visible locator only | yes | optional u32 |
| phase | phase/action context | sometimes | yes | yes | typed phase value |
| position | selected card position | sometimes | safe when not coupled to hidden identity | yes | typed position |
| source_position | existing source position | sometimes | safe only with visible source | yes | optional typed position |
| source_index | continuation source index | yes for continuation semantics | safe as a public domain index | yes | u32 |
| amount | amount choice | yes when applicable | safe | yes | optional/sentinel-preserving integer |
| continuation_id | adapter-local continuation identity | yes for replay/audit | safe, if semantic and not a pointer | yes | owned string |
| submits_engine_response | intermediate versus terminal action | yes for transition metadata | safe | yes | bool |
| exact_response_bytes | trusted exact CoreHost response | no; explicitly forbidden | no | deterministic but privileged | private only; never copied |
| pointer/cache/internal identity | not present in the current public struct, but prohibited by contract | no | no | no | no public representation |

The projection may expose a visible card reference only after the
observation/visibility audit proves that the code, controller, current
location, and sequence are legal information for the acting perspective.
It may not replace an unsafe field with a guessed value. If a candidate's
semantic key or a policy-required locator depends on hidden identity, the
whole request fails closed as a privacy/invariant failure before frame
publication.

### 9.3 Sanitized continuation view

The public continuation view may contain:

- continuation_id;
- continuation_kind;
- continuation_step;
- original public message family when needed;
- selected/remaining public source indices;
- public count/amount constraints;
- assigned public amounts;
- can_finish and can_cancel;
- any other value needed to interpret the complete public candidate vector.

It must not contain:

- raw_message_hash;
- exact response bytes;
- CoreHost pointers;
- hidden CardLocator values;
- internal caches;
- an opponent-private card identity.

If a continuation field is policy-relevant but cannot be represented safely,
the request is unsupported rather than partially projected. The candidates
remain in exact protocol order and are copied exactly once.

### 9.4 Projection algorithm

For every boundary returned by EpisodeDriver:

1. validate the internal complete candidate set using the existing protocol
   validator;
2. reject empty, duplicate, malformed, unsupported, or incomplete domains;
3. build one public candidate per internal candidate in vector order;
4. copy the semantic key without normalization;
5. copy only fields proven safe by the current PlayerObservation and
   continuation contract;
6. copy the owned PlayerObservation value;
7. verify candidate count, key vector, request kind, request player, and
   observation perspective;
8. compute the candidate digest over the unmodified authoritative key order;
9. compute semantic_decision_id;
10. allocate a new submission token;
11. publish one fully owned DecisionFrame.

There is no sorting, filtering, truncation, deduplication, candidate
fabrication, response-byte copying, or hidden-field substitution.

## 10. Persistent lifecycle and storage

The public state machine is:

    EMPTY
      -> AWAITING_ACTION
      -> GAME_TERMINAL
      -> INTERRUPTED
      -> FAILED

Only a closed state may reset:

    GAME_TERMINAL / INTERRUPTED / FAILED
      -> AWAITING_ACTION | GAME_TERMINAL | INTERRUPTED | FAILED

The recommended internal ownership is:

    EpisodicEnvironment
      immutable CertifiedEnvironmentConfig
      internal EnvironmentResources
      token episode_incarnation_counter: u64
      token frame_generation_counter: u64
      lifecycle
      optional current owned DecisionFrame
      optional current semantic action domain metadata
      unique_ptr<EpisodeDriver> live_driver
      semantic action count
      optional closure value
      optional cached terminal PlayerObservation[2]
      last valid audit-prefix evidence

The live Driver exists only while an episode is open. Once terminal,
interrupted, or failed closure evidence and safe terminal views have been
materialized, the Driver, CoreHost, ObservationSessions, continuation state,
raw request, and mutable session state are destroyed immediately.

Across reset, the environment retains only:

- immutable certified configuration;
- the non-semantic token namespace counters;
- no current frame or live Driver;
- no mutable gameplay state.

The following must never survive into a new Driver:

- CoreHost or OCG_Duel;
- Lua/script state;
- ObservationSession values;
- continuation state;
- knowledge state;
- raw/current request;
- old public frame as current state;
- old token validity;
- semantic action count;
- trace prefix as mutable state.

An old public frame may remain in caller memory by value. It is not retained
as the environment's current frame and its ActionSelection must fail stale.

### 10.1 Reset lifecycle matrix

| Current state | reset(valid) | reset(invalid) | Authoritative mutation |
| --- | --- | --- | --- |
| EMPTY | Validate, increment episode incarnation, construct fresh Driver, and return first boundary or a typed closed result. | ResetRejected with validation/resource code; remain EMPTY. | No mutation for rejection. |
| AWAITING_ACTION | ResetRejected with RESET_WHILE_AWAITING_ACTION. Caller must step or explicitly interrupt first. | Same lifecycle rejection takes precedence; remain AWAITING_ACTION. | None. |
| GAME_TERMINAL | Validate before construction, tear down any residual closure state, construct a fresh Driver, and return its first boundary/closure. | ResetRejected; retain prior terminal evidence and remain GAME_TERMINAL. | None for rejection. |
| INTERRUPTED | Validate and construct a fresh Driver. | ResetRejected; retain interruption evidence and remain INTERRUPTED. | None for rejection. |
| FAILED | Validate and construct a wholly fresh Driver; prior failure cannot poison it. | ResetRejected; retain immutable failure evidence and remain FAILED. | None for rejection. |

Validation includes contract ID, certified environment identity, root seed,
seat assignment, starting player, positive finite budgets, cancellation
metadata, token capacity, and resource identity. It occurs before creating a
new duel. A fresh Driver construction failure after authoritative
construction begins returns a typed EpisodeFailure closure, not a normal
StepRejected.

### 10.2 Reset result

The production result should be a value variant:

    ResetResult =
        ResetAccepted {
            NextBoundary next
        }
      | ResetRejected {
            ResetRejectionCode code
            Lifecycle observed_lifecycle
            safe diagnostic fields
        }

Required reset rejection codes:

- RESET_WHILE_AWAITING_ACTION;
- INVALID_CONTRACT;
- INVALID_ENVIRONMENT_ID;
- INVALID_EPISODE_SPEC;
- INVALID_STARTING_PLAYER;
- INVALID_RUN_CONTROL;
- RESOURCE_IDENTITY_MISMATCH;
- TOKEN_NAMESPACE_EXHAUSTED;
- UNSUPPORTED_RESET_CONFIGURATION.

Reset rejection is not StepRejected and is not an episode outcome. It never
creates a winner, draw, reward, or implicit interruption.

## 11. Submission tokens and freshness

The token is:

    SubmissionToken {
        uint64 episode_incarnation;
        uint64 frame_generation;
    }

Zero is reserved as invalid. Both counters are monotonic within one live
environment namespace. A successful reset increments the episode
incarnation. Every newly published actionable frame increments the global
frame generation, including a frame after a continuation and a frame after a
new reset. Counters are not reset to one on each episode.

A rejected step does not change the token. Closure invalidates the current
token by clearing the current frame and retaining no accepted token.

Before incrementing either counter, the implementation checks for
UINT64_MAX. Exhaustion fails closed:

- reset cannot start a new incarnation and returns
  TOKEN_NAMESPACE_EXHAUSTED before duel mutation;
- frame publication cannot publish a new frame; the active Driver is closed
  as TOKEN_NAMESPACE_EXHAUSTED with failure evidence and is torn down.

Tokens are non-semantic control-plane values. They do not enter environment
IDs, episode IDs, semantic decision IDs, gameplay hashes, trace canonical
bytes, replay equality, model inputs, or future semantic trajectories.

### 11.1 Process replacement

The local C++ facade guarantees freshness only inside one live
EpisodicEnvironment token namespace. It cannot guarantee global uniqueness
across destruction and recreation of independent processes: a new process may
start its local counters at the same values.

Process replacement invalidation in V1 is therefore a routing/session
responsibility, not a hidden property of the local counter. A future
transport must bind an external session/connection incarnation to the
correct environment instance and reject requests routed to an old session.
That transport identity is outside Phase 2. Phase 2 does not add networking,
RPC, or a random UUID workaround.

Repeated identical reset in one object must behave as:

    reset(A) -> semantic IDs X, token T1
    close
    reset(A) -> semantic IDs X, token T2

T1 and T2 differ because their episode/frame namespace values differ. The
semantic IDs do not change merely to provide freshness.

## 12. DecisionFrame and public decision index

The owned public frame contains:

    contract_id
    episode_semantic_id
    semantic_decision_id
    submission_token
    decision_index: u64
    engine_step_index: u64
    acting_player: u8
    PlayerObservation value
    EnvironmentDecisionRequest value
    complete EnvironmentActionCandidate vector
    candidate_domain_digest

The public hard invariant is checked before publication:

    frame.acting_player == frame.request.player
    frame.acting_player == frame.observation.perspective_player

Failure of this invariant is an EpisodeFailure, never a partially published
frame.

The environment decision index is separate from EngineTrace v2:

- first published frame after reset is 0;
- StepRejected does not increment it;
- an accepted action that yields another actionable frame increments it by
  exactly one;
- every intermediate continuation action increments it;
- a terminal/interrupted/failure closure does not receive a fabricated policy
  index;
- closure stores the last index as an optional value.

The Driver's existing trace decision index and terminal-record behavior are
unchanged.

## 13. ActionSelection and rejection ordering

ActionSelection is value-owned and contains exactly:

    contract_id
    episode_semantic_id
    semantic_decision_id
    submission_token
    semantic_key

Candidate index is never accepted as authority. Exact response bytes and raw
protocol response objects are never accepted from the caller.

Every step uses this fixed rejection precedence:

1. contract/version compatibility;
2. lifecycle must be AWAITING_ACTION;
3. submitted episode_semantic_id must equal the current episode;
4. submitted SubmissionToken must equal the current token;
5. submitted semantic_decision_id must equal the current frame;
6. semantic_key must belong to the complete current domain.

Required rejection codes:

- INCOMPATIBLE_CONTRACT;
- INVALID_LIFECYCLE;
- WRONG_EPISODE;
- STALE_SUBMISSION_TOKEN;
- WRONG_SEMANTIC_DECISION;
- UNKNOWN_SEMANTIC_KEY.

The precedence is evaluated from the input fields and current value frame,
not by candidate-vector iteration order. A previous-frame selection is stale
even when its semantic_key is legal in the new frame: the old token rejects
first; a token-forged old selection is rejected by semantic_decision_id.

### 13.1 Zero-mutation rejection proof

Before every caller-validation rejection, the test harness captures:

- canonical bytes of the current owned public frame;
- current episode semantic ID;
- current semantic decision ID;
- current submission token;
- current candidate-domain digest;
- Driver continuation-state hash;
- Driver response-submission count;
- Driver process count;
- Driver semantic-action count;
- EngineTrace length and prefix hash;
- observation-session semantic state hash;
- environment decision index;
- lifecycle.

After the rejection, every authoritative value above must compare byte-for-
byte equal. Only non-semantic rejection diagnostics/counters may change.
The current frame remains usable with the same valid selection.

The environment performs current-domain membership validation before calling
EpisodeDriver. If a key absent from the public/internal domain is submitted,
it returns UNKNOWN_SEMANTIC_KEY and performs no Driver call.

If the facade proves a key is present but EpisodeDriver reports
InvalidSemanticKey, this is an internal domain-integrity divergence, not a
normal caller rejection. The environment closes as
EpisodeFailure(INTERNAL_DOMAIN_DIVERGENCE), records whether mutation may have
occurred, invalidates the token, and tears down the Driver.

## 14. Accepted actions and Driver metadata

An input becomes an accepted semantic action only after all six rejection
checks succeed and the current semantic key is proven to be a member of the
complete domain. From that point onward, any continuation, response, process,
observation, or integrity error is execution failure after an accepted action,
not StepRejected.

The facade must not reconstruct AcceptedActionTransition from private
response bytes, candidate kind, or old EngineTrace.engine_advanced.

### 14.1 Minimal internal Driver API delta

The current public-in-repository internal API is conceptually:

    DriverBoundary apply_semantic_key(string key)

The smallest safe extension is:

    DriverAcceptedAction {
        string selected_semantic_key
        bool core_response_submitted
        optional<string> final_response_sha256
    }

    DriverApplyResult {
        optional<DriverAcceptedAction> accepted
        DriverBoundary next
    }

    DriverApplyResult apply_semantic_key(string key)

The accepted value is populated only after the Driver has validated the
trusted internal candidate. For an intermediate continuation it has:

    core_response_submitted = false
    final_response_sha256 = absent

For an atomic or terminal continuation response it has exactly one successful
response-submission result and the hash of the exact response bytes as an
audit-only value. The bytes themselves never cross the Driver API.

If response construction fails after selection, the result carries accepted
metadata plus DriverFailure. If submission may have happened but the call
cannot prove its result, the failure is RESPONSE_INCONSISTENCY with
mutation_may_have_occurred=true and an optional response hash only when
independently known.

The Driver also needs:

- a typed internal DriverInterrupted result with
  ADMINISTRATIVE_CANCEL, ENGINE_PROCESS_BUDGET, or
  SEMANTIC_ACTION_BUDGET;
- a minimal internal run-control input for the two budgets;
- an administrative interrupt operation that closes at an actionable
  boundary without submitting or processing;
- owned safe terminal observations for both perspectives before teardown;
- widened internal process/action counters where required to avoid uint32
  wraparound.

No policy callback, public DTO, response-byte accessor, candidate-index
API, or second advancement loop belongs in EpisodeDriver.

### 14.1.1 Driver delta audit

| Current API/state | Required new capability | Why the facade cannot safely derive it | Semantic impact | Canonical-simulator impact | Phase-1/evidence gate |
| --- | --- | --- | --- | --- | --- |
| DriverBoundary apply_semantic_key(string) | DriverApplyResult with accepted key, submission bool, optional final response hash, and next boundary | response bytes are private; the facade cannot infer submission from candidate kind or trace engine_advanced | adds value metadata only; exact response/continuation path unchanged | canonical client ignores metadata and preserves result mapping | G14-G16, G19 |
| DriverFailure with free-form diagnostic text | typed internal failure stage/mutation metadata and restricted public mapping | public facade must not expose path-bearing or raw engine diagnostics | failure classification becomes explicit; gameplay unchanged | canonical result may retain its existing private diagnostic mapping | G26-G27, G32 |
| DriverProcessBudgetExceeded | typed Driver interruption reason shared with semantic budget | facade must not duplicate process-loop checks | public maps to interruption; Driver check order is authoritative | canonical still maps process exhaustion to nonterminal | G18-G19 |
| EpisodeDriverConfig.engine_process_budget: u32 | validated u64 run-control representation or checked u32 bridge | public V1 requires finite u64; silent narrowing could alter closure | wider limit, same behavior for existing u32 values | existing max_steps values convert exactly; overflow rejects | G18-G19 |
| no semantic-action limit | Driver-owned semantic action count/limit and closure boundary | facade counting alone can race with continuation/process ownership and duplicate lifecycle | accepted count occurs at Driver boundary | canonical mode uses the same old behavior with semantic limit disabled/unbounded by its config | G17-G19 |
| no admin close operation | Driver administrative close at current boundary | facade cannot destroy Driver and independently prove trace/prefix semantics | no response/process and typed interruption | canonical never calls it | G12, G25 |
| append_terminal currently builds limited observation evidence | Driver-owned two-perspective terminal observation materialization | facade cannot query CoreHost after teardown or safely reconstruct a view | safe values only; EngineTrace terminal semantics unchanged | canonical may ignore cached public views | G23-G24, G19 |
| borrowed DriverDecisionBoundary | retain as internal seam; add safe boundary inspection/projection inputs | exposing pointers would violate owned-frame requirement | no semantic change | Phase-1 seam tests remain valid | G06-G13, G19 |
| current Driver internal counters are mixed widths | checked u64 semantic/process counters where public values require them | facade cannot safely widen after overflow or infer exact accepted count | representation hardening only | preserve legacy SimulationResult width with checked conversion | G17-G19 |

No row adds a policy callback or makes Driver a public policy facade. Every
row must be covered by the Phase-1 characterization/equivalence gate before
the implementation PR can claim semantic compatibility.

### 14.2 CanonicalSimulation compatibility

CanonicalSimulation adapts to DriverApplyResult but ignores the additional
accepted-action metadata. It retains its historical mapping:

    Driver process-budget exhaustion
        -> SimulationResult.failure_code = "nonterminal"
        -> pass=false

It does not expose public EpisodeInterrupted and does not change its
SimulationResult semantics. Canonical replay/policy selection still passes
the exact semantic key. Phase-1 characterization remains the regression gate;
the immutable baseline is not regenerated or reblessed.

This change must preserve:

- action/key sequence;
- acting players and indices;
- complete domains/order;
- observations and hashes;
- continuation transitions;
- response hashes;
- terminal state;
- semantic gameplay hash;
- canonical trace bytes.

## 15. StepResult and accepted-action path

The public result is:

    StepResult =
        StepAccepted {
            AcceptedActionTransition transition
            NextBoundary next
        }
      | StepRejected

NextBoundary is one of:

    AwaitingAction(owned DecisionFrame)
    EpisodeTerminal
    EpisodeInterrupted
    EpisodeFailure

StepRejected is only for caller-side validation failure before authoritative
mutation. It is not appended to the semantic trace and it does not alter the
current frame.

AcceptedActionTransition is:

    episode_semantic_id
    semantic_decision_id
    decision_index
    selected_semantic_key
    core_response_submitted
    optional final_response_sha256

The transition's semantic_decision_id and decision_index refer to the frame
that accepted the selection. The next frame, if any, carries a new
environment decision index and new semantic decision ID.

If a valid selected key is accepted and subsequent execution fails, the
StepResult is StepAccepted with next=EpisodeFailure. The original call is not
reclassified as StepRejected. The failure retains the accepted semantic
prefix and explicitly records mutation_may_have_occurred.

## 16. Continuation and atomic execution

### 16.1 Intermediate continuation

One selected candidate is one environment step even when the engine remains
paused:

    validate and accept current key
      -> Driver continuation transition
      -> append one existing EngineTrace v2 intermediate record
      -> no CoreHost submit_response
      -> no CoreHost process
      -> semantic action count +1
      -> environment decision index +1
      -> engine_step_index unchanged
      -> build the next complete public domain
      -> new semantic_decision_id
      -> new submission token
      -> AwaitingAction(next frame)

The Driver remains the only owner of continuation state and candidate
generation. The facade only projects the resulting boundary.

If the semantic-action budget is exhausted on this accepted intermediate
action, the environment closes as SEMANTIC_ACTION_BUDGET before publishing
the next frame. The core remains paused and no response is submitted.

### 16.2 Terminal continuation

For a terminal continuation candidate:

    accept one key
      -> Driver applies exact existing final transition
      -> exactly one response submission
      -> automatic core advancement
      -> next actionable request, true terminal, interruption, or failure

The facade does not construct the response. The Driver reports only the
boolean submission fact and optional response hash.

### 16.3 Atomic candidate

For an atomic candidate:

    accept one key
      -> Driver selects the exact trusted response
      -> exactly one response submission
      -> automatic core advancement
      -> next actionable request, true terminal, interruption, or failure

There is no macro-action and no candidate-index translation.

## 17. RunControl and budget ownership

RunControl is:

    RunControl {
        uint64 engine_process_budget;
        uint64 semantic_action_budget;
        CancellationMetadata cancellation;
    }

V1 accepts positive finite u64 values only. Zero is invalid. The
implementation must check each counter before incrementing and must fail
closed rather than wrap. No wall-clock deadline is authoritative.

CancellationMetadata is control-plane value data. V1 supports only the
administrative-cancel reason and a bounded, non-semantic source label if the
caller supplies one. It does not carry a thread, PID, pointer, timestamp, or
network connection identity.

### 17.1 Ownership decision

EpisodeDriver owns the authoritative run-control checks because it already
owns process advancement, semantic action count, continuation state, and
closure. EpisodicEnvironment owns only:

- validating the public RunControl before reset;
- translating typed Driver interruption values into public
  EpisodeInterrupted;
- retaining immutable closure evidence;
- exposing no Driver lifecycle directly.

This avoids a second advancement/lifecycle implementation in the facade.

### 17.2 Process budget

The Driver checks cancellation first, then process budget, before every
CoreHost process call. The existing process-budget behavior remains the
canonical-simulation behavior. The public mapping is:

    DriverProcessBudgetExceeded
        -> EpisodeInterrupted(ENGINE_PROCESS_BUDGET)

No response is fabricated. No winner, win reason, draw, or reward is
assigned. The last valid semantic prefix is retained.

If a response has been submitted and the next automatic process would exceed
the budget, the accepted transition remains accepted, and the next boundary
is EpisodeInterrupted(ENGINE_PROCESS_BUDGET).

### 17.3 Semantic-action budget

Exactly N semantic actions may be accepted for a budget of N.

The check occurs at the accepted semantic-action boundary, after the selected
key is valid and before any next actionable frame is published:

- if the accepted action reaches true engine terminal, true terminal wins;
- if the accepted action must continue and count == N, close as
  SEMANTIC_ACTION_BUDGET before publishing another frame;
- if an automatic process is required, process-budget exhaustion is checked
  before semantic-budget closure;
- for an intermediate continuation, close immediately at count N with the
  core paused and without a response;
- no fabricated terminal, winner, draw, or pass/cancel action is produced.

The public interruption precedence at a control point is:

    ADMINISTRATIVE_CANCEL
      > ENGINE_PROCESS_BUDGET
      > SEMANTIC_ACTION_BUDGET

True engine terminal reached while completing an accepted action wins over
future external interruption. In the specific automatic-progress boundary,
the sequence is:

    cancellation check
      -> true terminal result check
      -> process-budget check before process()
      -> semantic-budget check before publishing next frame

This preserves the accepted rule that a real terminal wins over a future
budget closure, while a process-budget exhaustion during required progress
wins over a semantic-action budget.

Acceptance fixtures must include:

1. an intermediate continuation whose Nth action would otherwise publish a
   frame;
2. an atomic Nth action that reaches true terminal;
3. an atomic Nth action that requires a process but exhausts process budget;
4. an administrative interrupt at an actionable continuation boundary.

## 18. Administrative interrupt

The public operation is:

    interrupt(InterruptRequest)

V1 accepts only InterruptRequest.reason=ADMINISTRATIVE_CANCEL. Calls are
serialized with reset and step.

When lifecycle is AWAITING_ACTION, interrupt:

- accepts no gameplay action;
- calls no candidate selector;
- submits no response;
- calls no CoreHost process;
- preserves the last valid semantic prefix;
- asks Driver to close administratively;
- invalidates the current token;
- destroys the Driver/CoreHost/session state;
- returns EpisodeInterrupted(ADMINISTRATIVE_CANCEL).

The result is:

    InterruptResult =
        InterruptAccepted { EpisodeInterrupted closure }
      | InterruptRejected {
            InterruptRejectionCode code
            safe lifecycle evidence
        }

Required rejection codes:

- INTERRUPT_INVALID_LIFECYCLE;
- INTERRUPT_UNSUPPORTED_REASON;
- INTERRUPT_TOKEN_NAMESPACE_EXHAUSTED (only if closure evidence cannot be
  allocated safely).

Interrupt in EMPTY, GAME_TERMINAL, INTERRUPTED, or FAILED rejects without
mutation. It does not auto-reset or turn a closed episode into a draw.

## 19. Failure model and teardown

Caller mistake is a StepRejected. Engine, protocol, observation, privacy, or
integrity failure is EpisodeFailure. Neither is a pass, loss, draw, cancel
candidate, or implicit terminal result.

### 19.1 Failure mapping

| Failure | Owning layer | Public failure code | Failure stage | Mutation may have occurred | Driver teardown |
| --- | --- | --- | --- | --- | --- |
| retry message or retry exhaustion | Driver/protocol | RETRY_FAILURE | decode/advance | no or unknown; record exact stage | immediate |
| CoreHost construction/setup error | CoreHost/Driver | CORE_ERROR | reset construction | partial setup possible | immediate |
| CoreHost process error | CoreHost/Driver | CORE_ERROR | automatic process | yes/unknown | immediate |
| unsupported protocol message | protocol/Driver | UNSUPPORTED_PROTOCOL | decode | no if before frame; otherwise known prefix | immediate |
| malformed protocol message | protocol/Driver | MALFORMED_PROTOCOL | decode/validation | no if before frame; otherwise known prefix | immediate |
| empty candidate domain | protocol/Driver | INCOMPLETE_CANDIDATES | publication | no frame mutation | immediate |
| duplicate semantic keys | protocol/Driver | DUPLICATE_CANDIDATES | publication | no frame mutation | immediate |
| missing response classification | protocol/Driver | RESPONSE_INCONSISTENCY | publication/selection | no or unknown | immediate |
| candidate/observation mismatch | observation/Driver | CANDIDATE_OBSERVATION_INCONSISTENCY | projection | no public frame | immediate |
| unsafe candidate or hidden locator | environment/observation | PRIVACY_INVARIANT | projection | no public frame | immediate |
| frame/request/perspective mismatch | environment | PUBLIC_FRAME_INVARIANT | projection | no public frame | immediate |
| invalid authoritative Driver state | Driver | INVALID_AUTHORITATIVE_STATE | any Driver boundary | unknown | immediate |
| response construction failure | protocol/Driver | RESPONSE_INCONSISTENCY | accepted action | no or unknown | immediate |
| response submission failure | CoreHost/Driver | RESPONSE_SUBMISSION_FAILURE | accepted action | yes/unknown | immediate |
| observation failure after progress | observation/Driver | OBSERVATION_FAILURE | post-process projection | yes | immediate |
| facade/internal domain divergence | environment/Driver | INTERNAL_DOMAIN_DIVERGENCE | accepted call boundary | no or unknown | immediate |
| token counter exhaustion | environment | TOKEN_NAMESPACE_EXHAUSTED | reset/publication | no for reset; possible Driver progress for publication | immediate |
| resource identity mismatch | factory/environment | RESOURCE_IDENTITY_MISMATCH | pre-reset validation | no new duel | no Driver exists |

Public failure values contain:

- contract ID;
- optional episode semantic ID when known;
- typed failure code;
- failure stage;
- semantic action count;
- optional last semantic decision ID;
- optional last decision index;
- final engine-step index when known;
- optional last valid audit-prefix hash;
- mutation_may_have_occurred;
- a restricted diagnostic reference.

They do not contain raw CoreHost state, raw message bytes, response bytes,
private query buffers, hidden observations, paths, or unbounded exception
strings.

### 19.2 Failure teardown

Once a failure is classified:

1. stop using the Driver;
2. materialize only safe immutable evidence needed for the failure value;
3. destroy continuation state, observations sessions, CoreHost, duel, and
   mutable Driver state immediately;
4. clear the current public frame and invalidate its token;
5. retain only the immutable EpisodeFailure value and restricted diagnostic
   reference;
6. require reset before any further step.

Reset after failure validates its new inputs and creates a wholly new Driver.
The old failed CoreHost is never retained for public debugging.

## 20. Terminal results and terminal views

EpisodeTerminal is emitted only after the Driver has observed a true engine
terminal. It contains:

- contract ID;
- episode semantic ID;
- winner;
- win reason;
- semantic action count;
- optional last decision index;
- final engine-step index;
- semantic gameplay hash;
- final audit-prefix hash.

Budget exhaustion, administrative cancellation, and failure never populate
winner or win_reason.

### 20.1 PerspectiveTerminalView

The method is:

    optional<PlayerObservation>
    perspective_terminal_view(uint8 player) const

It returns a copy/value view from the immutable terminal cache for player 0 or
1. Invalid player values return no value. It returns no value for interrupted
or failed episodes.

At true terminal the Driver eagerly materializes both perspective-safe
PlayerObservation values while CoreHost and both ObservationSessions are
still available. The materialization must:

- apply the existing PlayerObservation visibility rules independently for
  each player;
- preserve current-slot locator semantics;
- avoid persistent physical IDs;
- avoid revealing hidden opponent hand/deck/extra identities;
- avoid a global reveal-all terminal object;
- verify each observation hash and perspective field.

After both values and terminal evidence are copied, mutable Driver/CoreHost
state is destroyed. The public method never retains or queries CoreHost.

Paired-world terminal tests must show that changing one world's hidden
opponent state changes neither the other world's terminal view nor any public
terminal metadata unless the accepted visibility contract permits it.

## 21. Audit-prefix hash

final_audit_prefix_hash is the existing
canonical_trace_hash_v2 over the exact trace prefix that was valid at
closure. It is not a new trace format and it does not alter EngineTrace v2.

The meanings are:

- true terminal: the prefix includes the existing terminal trace record;
- administrative interruption: the prefix includes all accepted semantic
  records up to the interrupt and no synthetic terminal record;
- process/semantic-budget interruption: the same, with no synthetic outcome;
- failure: the last valid trace prefix before the failing operation, with no
  fabricated terminal or StepRejected record.

Because canonical_trace_hash_v2 includes its existing manifest/provenance
surface, final_audit_prefix_hash is audit evidence, not a semantic equality
key. Replay compares semantic trace projections, response hashes, observation
hashes, and semantic gameplay hash separately. No trace-v2 field is
reinterpreted, and environment core_response_submitted is not mapped onto
trace engine_advanced.

## 22. Public C++ sketch

The following is documentation-only and becomes normative only after the
missing identity constants are ratified:

~~~cpp
namespace ygo::environment {

class EpisodicEnvironment final {
public:
    static EnvironmentFactoryResult create(
        CertifiedEnvironmentConfig certified_config);

    ResetResult reset(const EpisodeSpec&, const RunControl&);
    StepResult step(const ActionSelection&);
    InterruptResult interrupt(const InterruptRequest&);

    std::optional<PlayerObservation>
    perspective_terminal_view(std::uint8_t player) const;

private:
    explicit EpisodicEnvironment(CertifiedEnvironmentConfig);
    // Immutable config, token namespace, lifecycle, owned frame,
    // live EpisodeDriver, closure values, and terminal observations.
};

}  // namespace ygo::environment
~~~

Method contracts:

| Method | Accepted lifecycle | Mutation | Returned ownership | Identity effect | Token effect |
| --- | --- | --- | --- | --- | --- |
| create | no episode | validates config/resources only | value environment or typed construction failure | computes certified environment ID only | initializes unused namespace |
| reset | EMPTY or closed | fresh Driver/duel construction only after validation | owned boundary/closure or ResetRejected | same semantic inputs reproduce IDs; new episode identity when inputs differ | new episode incarnation and later frame generation |
| step | AWAITING_ACTION | only after all rejection checks; then Driver may advance | owned StepAccepted or value StepRejected | accepted action/next frame updates semantic prefix and index | new token for next frame; closure invalidates |
| interrupt | AWAITING_ACTION | closes without gameplay mutation | owned EpisodeInterrupted or rejection | preserves semantic prefix; no outcome | invalidates current token |
| perspective_terminal_view | GAME_TERMINAL | none | copied safe PlayerObservation | none | none |

The constructor/factory must not expose a path-bearing runtime config or
arbitrary deck selector. An invalid certified config is a construction or
reset validation failure, not a gameplay action.

## 23. Required lifecycle pseudocode

### 23.1 Construction

~~~text
create certified environment:
    validate contract and all ratified schema IDs
    load canonical semantic lock values
    resolve runtime resource locations
    verify resource bytes/commits/patches/decks/database/scripts
    derive required_script_closure_codes deterministically
    compute environment_semantic_id
    initialize counters to zero and lifecycle EMPTY
    retain no CoreHost and no current frame
~~~

### 23.2 reset()

~~~text
reset(spec, run_control):
    if lifecycle == AWAITING_ACTION:
        return ResetRejected(RESET_WHILE_AWAITING_ACTION)

    validate contract/version IDs
    validate certified environment identity
    validate root_seed, seat assignment, starting player
    validate both positive finite budgets
    validate cancellation metadata
    validate token capacity
    validate runtime resource identity
    if any validation fails:
        return ResetRejected
        # no counter, Driver, CoreHost, or lifecycle mutation

    increment episode_incarnation; fail closed on overflow
    clear old closure values, old frame, and old semantic counters
    map seat assignment to the ordered certified deck values
    derive the existing four-word seed bundle from root_seed
    compute episode_semantic_id
    construct a fresh EpisodeDriver with exact resources/spec/run control
    if construction fails before authoritative start:
        return ResetRejected(resource/setup validation)
    if construction fails after authoritative mutation begins:
        close EpisodeFailure and return ResetAccepted(Failure)

    ask Driver to advance until its first boundary
    translate process/semantic budget or driver failure to a closed value
    if actionable:
        project borrowed boundary into a new owned DecisionFrame
        verify acting player coupling and complete domain
        increment frame_generation; fail closed on overflow
        publish AWAITING_ACTION
        return ResetAccepted(AwaitingAction(frame))
    if true terminal:
        materialize both safe terminal views
        destroy Driver
        return ResetAccepted(GameTerminal)
    if interrupted:
        destroy Driver
        return ResetAccepted(Interrupted)
    if failure:
        destroy Driver
        return ResetAccepted(Failure)
~~~

### 23.3 step()

~~~text
step(selection):
    validate contract ID
    if lifecycle != AWAITING_ACTION:
        return StepRejected(INVALID_LIFECYCLE)
    if selection.episode_semantic_id != current:
        return StepRejected(WRONG_EPISODE)
    if selection.token != current token:
        return StepRejected(STALE_SUBMISSION_TOKEN)
    if selection.semantic_decision_id != current:
        return StepRejected(WRONG_SEMANTIC_DECISION)
    if selection.semantic_key is absent from the complete current domain:
        return StepRejected(UNKNOWN_SEMANTIC_KEY)

    capture accepted-frame identity and zero-mutation baseline
    mark this semantic key accepted at the Driver boundary
    call Driver.apply_semantic_key(selection.semantic_key)
    # no caller validation is performed after this point

    if Driver reports internal invalid-key divergence:
        close EpisodeFailure(INTERNAL_DOMAIN_DIVERGENCE)
        return StepAccepted(transition if known, Failure)

    build AcceptedActionTransition from DriverApplyResult metadata
    increment/retain semantic action count according to Driver evidence

    if Driver reports true terminal:
        materialize both terminal views
        destroy Driver
        return StepAccepted(transition, EpisodeTerminal)
    if Driver reports interruption:
        destroy Driver
        return StepAccepted(transition, EpisodeInterrupted)
    if Driver reports failure:
        destroy Driver
        return StepAccepted(transition, EpisodeFailure)
    if Driver reports actionable boundary:
        apply process/semantic budget boundary before publication
        if budget closes:
            destroy Driver
            return StepAccepted(transition, EpisodeInterrupted)
        increment environment decision_index exactly once
        project complete safe owned frame
        verify frame coupling and semantic IDs
        increment frame_generation; fail closed on overflow
        replace current frame and return StepAccepted(transition, frame)
~~~

The baseline is used only for diagnostics and acceptance tests; it is not
needed to reject a caller after the six prevalidation checks. Any unexpected
Driver mutation after a caller rejection is a Driver/facade integrity bug.

### 23.4 Intermediate continuation

~~~text
Driver sees accepted intermediate candidate
    apply continuation transition only
    append existing v2 intermediate record
    do not submit response
    do not call process
    retain engine_step_index
    return next complete request
Facade:
    action count +1
    if semantic budget now exhausted:
        close Interrupted(SEMANTIC_ACTION_BUDGET)
    else:
        public decision_index +1
        new semantic decision ID
        new token
        publish owned frame
~~~

### 23.5 Terminal continuation or atomic response

~~~text
Driver sees accepted terminal/atomic candidate
    use exact trusted response bytes internally
    submit exactly once
    report core_response_submitted=true and optional hash
    process automatically
    if true terminal:
        terminal wins over future budget closure
    else if process budget prevents required process:
        Interrupted(ENGINE_PROCESS_BUDGET)
    else if semantic budget is exhausted before next frame:
        Interrupted(SEMANTIC_ACTION_BUDGET)
    else:
        publish the next owned frame with decision_index +1
~~~

### 23.6 interrupt()

~~~text
interrupt(request):
    if lifecycle != AWAITING_ACTION:
        return InterruptRejected(INTERRUPT_INVALID_LIFECYCLE)
    if request.reason is not ADMINISTRATIVE_CANCEL:
        return InterruptRejected(INTERRUPT_UNSUPPORTED_REASON)
    call Driver administrative close
    do not select a candidate
    do not submit a response
    do not process the core
    preserve the last valid audit prefix
    invalidate token and clear current frame
    destroy Driver/CoreHost/observation/continuation state
    lifecycle = INTERRUPTED
    return InterruptAccepted(EpisodeInterrupted(ADMINISTRATIVE_CANCEL))
~~~

### 23.7 true terminal closure

~~~text
true terminal:
    accept Driver terminal evidence
    materialize PlayerObservation for perspective 0
    materialize PlayerObservation for perspective 1
    verify both hashes and visibility rules
    compute final semantic gameplay hash through existing trace semantics
    compute v2 audit hash over trace including terminal record
    copy winner/reason and safe counters
    clear current frame and invalidate token
    destroy Driver/CoreHost/ObservationSessions
    lifecycle = GAME_TERMINAL
~~~

### 23.8 failure closure

~~~text
failure:
    classify typed owning-layer code and stage
    retain only safe immutable prefix evidence
    set mutation_may_have_occurred explicitly
    do not assign winner, loss, draw, cancel, or reward
    clear current frame and invalidate token
    destroy Driver/CoreHost/ObservationSessions/continuation immediately
    lifecycle = FAILED
~~~

### 23.9 reset after closure

~~~text
reset after GAME_TERMINAL, INTERRUPTED, or FAILED:
    validate all new inputs before construction
    discard all old mutable episode state
    retain immutable certified environment config only
    allocate a new episode incarnation
    construct a new EpisodeDriver and duel
    derive the same semantic IDs for the same semantic inputs
    issue a different live token namespace
~~~

## 24. Mutation table

| Operation | Candidate state | Semantic mutation? | Driver mutation? | Token change? | Decision index change? | Trace change? |
| --- | --- | --- | --- | --- | --- | --- |
| valid reset | no current episode/closed | new episode identity/prefix initialized | fresh construction and initial advance | new incarnation; frame token if frame published | first frame is 0 | new empty trace/initial records |
| rejected reset | unchanged | no | no | no | no | no |
| valid atomic step | actionable | yes, one accepted key | exact response, one submit, automatic progress | new frame token or invalidated on closure | +1 only if next frame | existing trace action/terminal semantics |
| valid intermediate continuation | actionable continuation | yes, one accepted key | continuation only; no submit/process | new token if next frame, otherwise invalidated | +1 if next frame | existing intermediate trace record |
| valid final continuation | actionable continuation | yes, one accepted key | exact final response, one submit, progress | new token or invalidated | +1 only if next frame | intermediate/final trace semantics unchanged |
| wrong episode | actionable | no | no | no | no | no |
| stale token | actionable | no | no | no | no | no |
| wrong semantic decision | actionable | no | no | no | no | no |
| unknown key | actionable | no | no | no | no | no |
| invalid lifecycle step | closed/empty | no | no | no | no | no |
| administrative interrupt | actionable | closes existing prefix, no gameplay action | close only; no submit/process | invalidated | no fabricated index | no synthetic terminal |
| process budget | after accepted action/progress | accepted prefix retained; closure control only | no process beyond budget | invalidated | no fabricated index | prefix only |
| semantic budget | after accepted action | accepted prefix retained; closure control only | continuation may remain paused | invalidated | no fabricated index | accepted prefix only |
| true terminal | after accepted action | outcome committed | exact existing terminal progression | invalidated | optional last index only | terminal trace record |
| Driver failure | after accepted action or boundary | failure prefix retained | teardown immediately | invalidated | no fabricated index | prefix only, no failure record added as gameplay action |

## 25. Semantic versus control-plane values

| Value | Semantic identity? | Gameplay hash? | Replay equality? | Persisted semantic trajectory later? | Reason |
| --- | ---: | ---: | ---: | ---: | --- |
| environment_semantic_id | yes | indirectly through existing semantic environment inputs | yes | eligible | certified environment |
| root seed | yes | through episode/gameplay derivation | yes | eligible | episode source |
| resolved seed bundle | yes | through exact CoreHost initialization | yes | eligible | exact accepted seed mapping |
| episode_semantic_id | yes | parent semantic identity, not a replacement trace field | yes | eligible | episode identity |
| decision_index | yes | semantic action position | yes | eligible | public policy index |
| semantic_decision_id | yes | semantic decision metadata as already defined | yes | eligible | complete decision identity |
| candidate-domain digest | yes | semantic domain identity | yes | eligible | complete ordered domain |
| semantic_key | yes | accepted action projection | yes | eligible | replay input |
| submission token | no | no | no | no | live freshness only |
| episode incarnation | no | no | no | no | token namespace only |
| frame generation | no | no | no | no | token namespace only |
| engine process budget | no | no | no | no | run control |
| semantic action budget | no | no | no | no | run control |
| cancellation metadata | no | no | no | no | control plane |
| PID | no | no | no | no | execution provenance |
| worker slot/restart index | no | no | no | no | execution provenance |
| compiler/build/platform | no | no | no | no | execution provenance |
| timing/wall clock | no | no | no | no | measurement only |
| final_audit_prefix_hash | no semantic ID; audit value | no semantic gameplay hash | compare as audit evidence when provenance matches | optional diagnostic artifact only | existing trace-v2 provenance hash |
| winner/win reason | terminal semantic outcome | yes through existing terminal projection | yes | eligible | true engine terminal only |
| reward | no | no | no | no in Phase 2 | external policy boundary |

## 26. Determinism audit

New state is classified as follows:

| State | Classification | Deterministic requirement |
| --- | --- | --- |
| environment ID payload | semantic | fixed field order, fixed widths, exact lock values |
| episode ID payload | semantic | root/seed/deck/seat mapping only |
| candidate digest | semantic | protocol order, no environment sorting |
| semantic decision ID | semantic | exact current frame values |
| episode incarnation | control plane | monotonic local counter, excluded from all semantic values |
| frame generation | control plane | monotonic local counter, excluded from all semantic values |
| public decision_index | semantic | starts at zero and changes only on accepted action yielding a new frame |
| lifecycle | control/closure | fixed transition table, no ignored calls |
| budget counters | control plane | finite validated counters, excluded from IDs |
| response hashes | semantic/audit according to existing trace contract | exact Driver-produced bytes, no facade reconstruction |
| paths/compiler/PID/timing | provenance | never in semantic IDs or gameplay hash |

No unordered container, pointer address, wall clock, random UUID, thread
schedule, or host path may affect a semantic value.

## 27. Privacy audit

### Public frame

The frame contains one perspective-safe PlayerObservation value, a
sanitized request, complete sanitized candidates, semantic IDs, digest, and
token. It contains no raw CoreHost query, pointer, cache, response byte,
opponent-private observation, hidden locator, or recurrent/model state.

### Candidate projection

All candidate keys and fields are audited against the acting player's
observation. Current slot locators are permitted only where the observation
contract permits them. A semantic key that encodes a hidden card identity is
not made safe by hiding the corresponding field; the entire request fails
closed.

### Rejection payload

Rejections may echo safe submitted IDs, the current semantic IDs, the current
candidate digest, and a typed code. They do not echo private request bytes,
raw hashes, paths, or hidden card details.

### Terminal result/view

Terminal outcome values contain only accepted public outcome and audit
evidence. Terminal views are independently materialized for each perspective
and cached as safe PlayerObservation values. There is no reveal-all object.

### Failure diagnostics

Public failures use typed codes, stages, counts, hashes already allowed by
the contracts, and a restricted diagnostic reference. Raw exception text
and path-bearing diagnostics stay in a separate test/operator artifact.

### Replay

Replay input is EnvironmentConfig semantic identity, EpisodeSpec, and the
ordered accepted semantic-key vector. It contains no token, candidate index,
raw response bytes, hidden state, PID, or timing.

## 28. Replay design

The test-only semantic replay harness consumes:

    EnvironmentConfig semantic identity
    EpisodeSpec
    ordered accepted semantic_keys

For every reset, frame, action, and closure it compares:

- environment and episode IDs;
- semantic decision IDs;
- environment decision indices;
- engine step indices;
- protocol decision IDs;
- request kinds;
- complete candidate count, membership, and order;
- candidate-domain digest;
- perspective-safe observation hashes;
- continuation IDs and state hashes;
- accepted response-submission booleans;
- final response hashes;
- closure kind/reason;
- terminal outcome where present;
- semantic gameplay hash.

The replay harness regenerates tokens locally and explicitly excludes token
values from equality. It revalidates each current complete domain before
submitting the recorded key, so replay cannot succeed by selecting a
candidate index or by bypassing a missing key. It never replays response
bytes; the Driver reconstructs and submits them from the exact authoritative
candidate.

An interruption is represented by its semantic prefix and closure reason:

- administrative cancel: explicit control closure at an actionable boundary;
- process budget: accepted prefix stopped before an over-budget process;
- semantic budget: accepted prefix stopped before publishing another frame;
- failure: accepted prefix plus typed failure stage and mutation evidence.

Replay must reproduce the same closure class and semantic evidence. It must
not turn interruption or failure into a terminal outcome.

## 29. Test-only fault injection boundaries

Fault injection is test-only and cannot enter public EnvironmentConfig or
the normal certified factory:

| Injection | Boundary | Allowed use |
| --- | --- | --- |
| forced unsupported | existing Driver test flag or probe-only setup | G26 mapping; never a production caller option |
| malformed request | protocol fixture/decoder test | G09/G26 before frame publication |
| duplicate candidate | protocol validator fixture | G09/G26 before frame publication |
| privacy mismatch | paired observation/projection fixture | G22/G23 and public projection failure |
| CoreHost process error | Driver/CoreHost test seam | G26 failure mapping and teardown |
| response inconsistency | Driver/protocol seam around trusted response path | G26 accepted-action-after-failure semantics |
| token exhaustion | deterministic test counter seam | reset/frame fail-closed behavior |

Fixture setup scripts remain test infrastructure under the existing
CoreHost::load_fixture_script boundary. They must not be exposed as runtime
board-construction or hidden-information APIs.

## 30. Worker/process determinism harness

Phase 2 does not change the M4 worker protocol. The test harness may launch
multiple independent ygo_episodic_probe processes or reuse the existing M4
coordinator's process-management pattern:

- each probe constructs a canonical certified environment;
- each probe runs value-only reset/step/interrupt scripts;
- stdout emits safe canonical acceptance JSON only;
- stderr remains diagnostic-only and path/provenance bound;
- worker count and scheduling are not included in any semantic ID;
- a worker process owns one environment object and is discarded after its
  script unless the persistent-reset test explicitly exercises one process.

G21 compares the ordered semantic outputs of one-process and 16-process
execution over the same deterministic job list. It does not claim
thread-safety or add distributed actor semantics.

The existing M4 job-generation function may supply deterministic root seeds,
seat assignments, and starting players. The M4 worker's JSON protocol remains
unchanged.

## 31. Persistent reset-isolation harness

The acceptance harness must run the required interleaving in one persistent
process:

    A -> B -> C -> A -> D -> A

The corpus mixes:

- distinct root seeds;
- normal and mirror seat assignment;
- both starting players;
- short budget closures;
- terminal episodes;
- continuation-heavy paths;
- atomic/non-continuation-heavy paths;
- an injected failure before a later successful reset.

For each A occurrence, a fresh-process A run is the reference. Compare
episode IDs, first frames, domains, observations, replay outputs, closure
values, and gameplay hashes. The persistent process must not reuse any
CoreHost, Lua state, ObservationSession, continuation, knowledge state, frame,
or token.

The soak contains at least 500 closed/completed episodes and reports:

- episode count and closure count;
- semantic ID equality against fresh references;
- zero stale-token acceptance;
- zero candidate loss/duplication/truncation;
- zero privacy mismatches;
- zero resource/handle growth attributable to old episodes;
- no drift in trace/gameplay hashes.

## 32. G28 maximum-domain conclusion and closure procedure

The current accepted M4 evidence has two different meanings:

| Evidence | Recorded value | Meaning |
| --- | ---: | --- |
| M4 baseline operation_counters.candidate_max | 1344 | aggregate sum across 64 result rows |
| M4 throughput-w1 per-job counters.candidate_max | 21 in the inspected rows | per-game maximum candidate count |
| Phase-1 characterization witness | 21 | first deterministic legal request with the maximum in that corpus |

The M4 report implementation sums candidate_max with the other operation
counters. Therefore 1344 is not a legal domain maximum and cannot be a G28
witness. Treating it as one would require a fabricated 1344-candidate
request, which is forbidden.

G28 remains BLOCKED. The implementation prerequisite is:

1. define separate evidence fields:
   candidate_domain_max = maximum candidate count over individual published
   requests;
   candidate_max_total = aggregate sum retained for M4 throughput accounting;
2. run a deterministic witness-discovery corpus with trace persistence;
3. inspect every complete request, including continuation requests;
4. record candidate count, request kind, episode identity, decision index,
   engine index, protocol decision ID, candidate digest, and ordered keys;
5. select the maximum with deterministic tie-break:

       candidate count descending
       episode semantic ID ascending
       environment decision index ascending
       engine step index ascending
       protocol decision ID ascending
       candidate digest ascending

6. replay the selected semantic-key script in an independent process;
7. prove the witness count equals candidate_domain_max;
8. bind the witness and aggregate metric to the exact rules/deck/tool/head
   identity;
9. only then close G28.

If the deterministic corpus establishes 21 as the true per-domain maximum,
the evidence must say 21 and preserve 1344 as an aggregate counter. If it
finds a larger per-domain maximum, that value and its replayable witness
become authoritative. No convenience cap or hand-selected easier request is
allowed.

## 33. Reward independence

Reward is absent from EpisodicEnvironment V1. G29 is a test-only harness:

1. run one deterministic terminal environment replay;
2. capture the terminal semantic result, frame/action sequence, hashes, and
   closure;
3. apply external RewardPolicy A with reward_policy_id A;
4. apply external RewardPolicy B with reward_policy_id B;
5. compare all environment values and require equality;
6. allow only the external numeric reward and reward_policy_id to differ.

Interrupted and Failed closures receive no implicit reward. Reward does not
enter EnvironmentConfig, any identity, any frame, any candidate, any trace,
or semantic gameplay hash.

## 34. Acceptance matrix G01-G32

The following plan maps every accepted gate to an owning layer, executable
shape, setup, exact condition, evidence, and production requirement. Names
beginning with ygo_episodic_probe or episodic_acceptance are proposed
test-only additions; they are not present in this docs-only change.

| Gate | Owning layer | Test executable/script | Corpus/setup | Exact PASS condition | Evidence artifact | Requires production code? |
| --- | --- | --- | --- | --- | --- | --- |
| G01 | environment/factory/identity | episodic_acceptance.py --gate G01 plus ygo_episodic_probe | same canonical EpisodeSpec in three independent processes | environment ID, episode ID, first frame IDs, domain, observation hash, and safe frame bytes are identical | G01 independent-process JSON with binary/head/rules binding | yes |
| G02 | identity/seed | episodic_acceptance.py --gate G02 | paired runs differing only in root_seed | episode and downstream semantic IDs differ; all other certified identity values remain fixed | G02 seed-separation record | yes |
| G03 | lifecycle/token | episodic_acceptance.py --gate G03 | reset A, close, reset A in one process; retain T1 selection | semantic IDs repeat, T1 differs from T2, T1 rejects before mutation | G03 reset-freshness and zero-mutation snapshots | yes |
| G04 | lifecycle/reset isolation | episodic_acceptance.py --gate G04 | persistent A-B-C-A-D-A and fresh references | every A matches fresh A semantically and no prior state/token is accepted | G04 interleaving comparison | yes |
| G05 | lifecycle/resource ownership | episodic_acceptance.py --gate G05 | at least 500 mixed closed episodes in one process | no semantic drift, stale acceptance, privacy leak, state reuse, or resource growth | G05 soak summary plus bounded diagnostics | yes |
| G06 | frame projection | episodic_environment_frame_test | canonical full fixed-deck corpus including continuations | acting_player=request.player=observation.perspective_player for every frame | G06 frame invariant artifact | yes |
| G07 | protocol/projection | episodic_domain_integrity_test | all current certified request families and Phase-1 corpus | public count/membership/order/kinds and continuation classifications exactly match Driver; no loss/truncation/fabrication | G07 per-frame domain comparison | yes |
| G08 | protocol ordering | protocol order regression plus episodic probe | repeated independent runs and fixed decoder fixtures | authoritative protocol order is byte-identical; facade performs no sorting | G08 order digest/evidence | no new protocol behavior; facade yes |
| G09 | digest/validation | candidate_domain_codec_test | golden vectors, key swap/count mutation, empty/duplicate fixtures | independent digest recomputation matches; every mutation changes digest or fails closed; empty/duplicate never publish | G09 codec and rejection evidence | yes |
| G10 | freshness/rejection | episodic_rejection_test | retain frame N selection; accept action to frame N+1 where same key remains legal | old complete selection rejects with stale token/decision precedence and zero mutation | G10 stale-action snapshot | yes |
| G11 | rejection | episodic_rejection_test | wrong episode, token, decision, absent key permutations | exact deterministic rejection code and no Driver/core/trace mutation | G11 rejection matrix | yes |
| G12 | lifecycle/control | episodic_lifecycle_test | step in EMPTY/closed, reset while awaiting, valid interrupt | no silent ignore or auto-reset; exact typed rejection/interrupt result | G12 lifecycle matrix | yes |
| G13 | zero mutation | episodic_zero_mutation_test | each G10-G12 invalid call | all required before/after snapshot fields remain equal; current valid frame remains usable | G13 snapshots with prefix hashes | yes |
| G14 | continuation/Driver | episode_driver_continuation_test plus facade probe | tribute/selection/ordering continuation fixtures | one candidate equals one environment step; no response/process; engine index unchanged; next index/token/domain new | G14 continuation transitions | Driver metadata/control seam yes |
| G15 | continuation/response | episodic_response_equivalence_test | terminal continuation fixtures | exactly one final response and hash equal to trusted Phase-1 path; no intermediate response | G15 response count/hash artifact | Driver metadata seam yes |
| G16 | atomic/response | episodic_response_equivalence_test | atomic fixed-deck conformance corpus | response count/bytes-by-hash, next boundary, trace and gameplay hash equal to canonical client | G16 atomic equivalence artifact | Driver metadata seam yes |
| G17 | semantic budget | episodic_budget_test | N=1 and continuation-heavy Nth action cases | exactly N actions accepted; if Nth is nonterminal no next frame; true terminal wins; no outcome fabricated | G17 interruption records | yes |
| G18 | process budget | episodic_budget_test | budgets at before-first-process, after-submit, and next-frame boundaries | public interruption is ENGINE_PROCESS_BUDGET; canonical simulator still reports historical nonterminal | G18 public/canonical paired evidence | Driver run-control seam yes |
| G19 | canonical equivalence | existing Phase-1 acceptance plus shared simulation compatibility | immutable baseline 72c29009f107a2ebb172d85de1c70b38d2f007d8 and Phase-1 corpus | semantic action/request/domain/observation/continuation/response/terminal/gameplay results unchanged | existing Phase-1 raw/provenance artifacts and new comparison | Driver API adaptation yes |
| G20 | replay | episodic_replay_test | accepted semantic-key scripts from terminal/interrupted/failure runs | replay matches all listed semantic frame/domain/observation/response/closure/gameplay values; tokens excluded | G20 replay report | yes |
| G21 | process determinism | episodic_worker_determinism.py | same jobs through one and sixteen independent probe processes | semantic output equality; worker count/slot/PID absent from semantic values | G21 process comparison | test harness plus public facade |
| G22 | privacy | episodic_paired_world_test | two worlds differing only in hidden opponent state, every live frame | acting perspective cannot distinguish disallowed hidden identity/state | G22 paired-world projection artifact | projection behavior yes |
| G23 | terminal privacy | episodic_terminal_privacy_test | paired worlds closed at true terminal | cached terminal views obey PlayerObservation visibility for both players | G23 terminal view artifact | terminal cache yes |
| G24 | terminal ownership | episodic_closure_test | true terminal, process budget, semantic budget, admin cancel, failure | only true engine terminal has winner/win_reason; other closures are typed non-outcome | G24 closure classification | yes |
| G25 | admin interrupt | episodic_interrupt_test | actionable atomic and continuation boundaries | no action/response/process; token invalid; prefix retained; reset required | G25 interrupt evidence | Driver control seam yes |
| G26 | failure mapping | episodic_fault_injection_test | unsupported, malformed, duplicate, privacy, core, response fixtures | exact typed EpisodeFailure, explicit mutation stage, no fallback outcome/candidate, immediate teardown | G26 failure matrix/diagnostics refs | test hooks plus production mapping |
| G27 | reset after failure | episodic_reset_after_failure_test | inject failure, then run valid A/B/A resets | new Driver/resources work and match fresh references; failed state cannot poison reset | G27 resource/semantic comparison | yes |
| G28 | domain maximum | episodic_witness_discovery.py | deterministic trace-persisted workload after B2 metric reconciliation | witness candidate count equals true candidate_domain_max with deterministic tie-break and independent replay | G28 signed/bound witness plus corrected metric | acceptance/evidence prerequisite; no cap |
| G29 | external reward harness | reward_independence_test.py | same terminal run with policies A and B | all environment values/hashes/actions/outcome equal; only reward and policy ID differ | G29 reward-independence artifact | no reward production code |
| G30 | version rejection | episodic_version_test and identity golden-vector test | mutate every public contract/schema ID and each golden payload | unknown/incompatible values reject before mutation; known vectors exact | G30 version/rejection artifact | yes; B1 constants prerequisite |
| G31 | regression | existing ctest, repository Python, M3, M4 suites | current clean Release checkout plus Phase-1 equivalence | all M0-M4 gates pass; no reblessing or skipped coverage | fresh verification manifest | no new behavior beyond regression guard |
| G32 | evidence hygiene | clean-checkout acceptance script | fresh clone at exact implementation commit, pinned bundle, no ignored mutable baseline | all required gates run and artifacts bind to exact head/contract/rules/decks; no hand edits | final machine-readable acceptance manifest | acceptance harness |

Dependencies:

    B1 identity constants
      -> G01/G02/G03/G09/G19/G20/G30

    B2 corrected candidate maximum
      -> G28

    public projection + Driver metadata
      -> G06-G18, G20, G22-G27

    lifecycle + reset isolation
      -> G01-G05, G10-G13, G20-G27

    G19 and G31
      -> G32

G31 cannot be reported as fresh from historical M4 documents; the commands
must run successfully in the implementation environment. G32 must be the
last clean-checkout gate.

## 35. Staged implementation and verification sequence

The implementation PR should use one branch and one main PR because lifecycle,
identity, token freshness, zero-mutation rejection, and Driver accepted-action
metadata are tightly coupled. The commits may remain reviewable:

1. test: lock identity codec/golden/public-projection fixtures;
2. prerequisite: ratify missing public identity constants and correct G28
   metric vocabulary in their owning review, if not already complete;
3. feat: add canonical identity codecs and certified resource validation;
4. feat: add value-owned EnvironmentDecisionRequest and
   EnvironmentActionCandidate projections;
5. feat: add persistent EpisodicEnvironment construction/reset lifecycle;
6. feat: add token namespace and semantic decision identities;
7. feat: add fixed rejection precedence and zero-mutation assertions;
8. refactor: add DriverApplyResult accepted-action metadata;
9. feat: add Driver run-control and administrative interruption seam;
10. feat: add accepted step, semantic/process budgets, and typed failures;
11. feat: materialize safe two-perspective terminal views;
12. test: close replay, reset isolation, paired-world privacy, and worker
    determinism;
13. test: close G01-G30 including the reconciled G28 witness;
14. test/evidence: fresh G31-G32 clean-checkout acceptance.

If B1 or B2 is unresolved, commits after step 1 must not begin. Do not split
competing lifecycle authorities across parallel implementation branches.

## 36. Performance policy

Phase 2 is a semantic/API milestone. It does not include:

- serialization optimization;
- candidate compression;
- candidate caps;
- caching redesign;
- batching;
- parallelism;
- allocator changes;
- tensorization;
- M4 unrelated performance work.

Reset-soak duration, process counts, and memory samples may be recorded as
diagnostics, but correctness, determinism, privacy, complete domains, and
failure behavior remain the acceptance priority.

## 37. Self-review checklist

Before the implementation PR is opened, reviewers must answer yes to all:

### Correctness

- Does every accepted semantic key reach EpisodeDriver unchanged?
- Does the facade avoid every core/process/response/continuation decision?
- Is a valid action followed by failure represented as accepted-plus-failure?
- Are terminal, interruption, and failure mutually distinct?

### Determinism

- Are every new canonical field and byte width explicit?
- Is every candidate vector in protocol order without environment sorting?
- Are decision indices distinct from trace indices?
- Are IDs independent of process/build/worker/timing?

### Information safety

- Does the public DTO contain no exact response bytes?
- Does every card locator have a visibility proof?
- Can no semantic key encode hidden identity?
- Are terminal views independently perspective-safe?

### Candidate completeness

- Is count identical?
- Is every semantic key preserved exactly once?
- Is meaningful order preserved?
- Are empty, duplicate, malformed, and unsupported domains rejected?

### Replay and freshness

- Can semantic keys alone reproduce every published frame?
- Are tokens regenerated and excluded from replay?
- Can a stale old frame ever alias a live frame?
- Does unknown-key rejection happen before Driver mutation?

### Lifecycle and failure

- Can reset be silently ignored or auto-triggered?
- Does reset while awaiting reject?
- Is failed mutable state destroyed immediately?
- Can an execution failure ever become a normal sample/outcome?

### Trace and scope

- Is EngineTrace v2 untouched?
- Is final_audit_prefix_hash only the existing v2 prefix hash?
- Is reward outside the environment?
- Are trajectory, ML, framework, RPC, arbitrary-deck, and M4-performance
  changes absent?

## 38. Direct answers to the required design questions

1. The public type is ygo::environment::EpisodicEnvironment with value-owned
   ResetResult, StepResult, InterruptResult, and terminal views.
2. Compile it into the existing ygo_m4 static library.
3. Construct it only from a canonical CertifiedEnvironmentConfig factory that
   verifies the locked bundle; do not expose arbitrary selectors.
4. Keep runtime paths in internal EnvironmentResources and exclude them from
   semantic identity.
5. Public candidates contain safe action kind, unchanged semantic key,
   policy-safe visible references, phase/position/index/amount/continuation
   values, and submits_engine_response; all values are owned.
6. Keep exact_response_bytes, raw message hashes, hidden locators, pointers,
   caches, and private query data internal.
7. Project one candidate per authoritative internal candidate in order after
   validation; compare count/key vector; never sort/filter/truncate/fabricate.
8. Environment ID uses the 26-field sequence in section 7.1 with fixed
   big-endian primitives and SHA-256.
9. Episode ID uses the eight-field sequence in section 7.2.
10. Candidate digest uses domain, request kind, count, and ordered keys in
    section 7.3.
11. Semantic decision ID uses the nine-field sequence in section 7.4.
12. EpisodeSpec.root_seed is SimulationJob.seed and uses the existing
    four-word helper, shared by Driver and canonical simulation.
13. Only certified immutable config and non-semantic counters survive reset.
14. No CoreHost, duel, Lua/script, observations, continuation, knowledge,
    frame, or mutable trace survives.
15. SubmissionToken is two monotonic u64 counters: episode incarnation and
    global frame generation.
16. Counter exhaustion fails closed before reset mutation or during frame
    publication as a typed failure.
17. Local freshness is only in-process; future transport must bind session
    routing for process replacement. Phase 2 adds no transport.
18. Reset rejection is ResetRejected with typed ResetRejectionCode, not an
    accidental exception or StepRejected.
19. Step precedence is contract, lifecycle, episode, token, decision ID,
    semantic-key membership.
20. Capture the complete snapshot in section 13.1 and require exact equality
    after every rejection.
21. The old token rejects first; a forged/reused token then fails old
    semantic_decision_id even if the key remains legal.
22. DriverApplyResult returns a boolean and optional response hash; no bytes
    cross the facade.
23. Yes. Add DriverAcceptedAction/DriverApplyResult and typed interruption,
    run-control, and terminal-view value seams.
24. EpisodeDriver owns semantic-action-budget counting and closure checks;
    the facade translates results.
25. EpisodeDriver owns the administrative close; the facade exposes the
    serialized public operation and result.
26. CanonicalSimulation continues mapping process budget to historical
    nonterminal; only public facade maps it to EpisodeInterrupted.
27. Administrative cancel, then engine process budget, then semantic action
    budget; true terminal wins over future external interruption.
28. An action is accepted after all caller validation and current-domain
    membership succeeds, before Driver progression.
29. Return StepAccepted with the accepted transition and next=EpisodeFailure;
    never StepRejected.
30. Driver/CoreHost is destroyed on terminal, interruption, failure, and
    before the next reset.
31. Materialize both perspective-safe PlayerObservation values at true
    terminal, cache values, then destroy mutable state.
32. It is canonical_trace_hash_v2 over the exact valid trace prefix, with
    existing provenance semantics and no synthetic closure record.
33. Replay environment identity, EpisodeSpec, and ordered semantic keys;
    regenerate tokens and response bytes through Driver; compare semantic
    frames, hashes, responses, closure, and gameplay.
34. Use the A-B-C-A-D-A persistent corpus and a 500-episode mixed soak,
    comparing every repeated A against fresh references.
35. Run the same value-only probe scripts in one and sixteen independent
    processes; compare semantic output, never worker provenance.
36. G28 is currently blocked because 1344 is aggregate. Correct the metric,
    discover the true per-domain maximum, and bind a deterministic replayable
    witness with the specified tie-break.
37. Apply two external reward policies to the same terminal environment
    result and require all environment values to match; reward stays outside.
38. New production behavior is required for public DTO projection, lifecycle,
    identities, tokens, step/rejection, Driver metadata, budgets,
    interruption, failures, terminal views, and replay support.
39. Harness-only work covers independent probes, worker-count comparison,
    reward independence, G28 discovery, and clean evidence; existing M0-M4
    tests remain regression gates.
40. Use one implementation PR with the staged commits in section 35, after B1
    and B2 are resolved; do not merge automatically.

## 39. Scope and contract non-modification statement

This specification does not modify:

- ADR-0002;
- ocgforge.episodic_environment.v1;
- Decision Protocol v1;
- PlayerObservation v1;
- EngineTrace v2;
- locked rules/decks;
- M4 worker protocol;
- canonical simulation result semantics.

The missing identity constants and G28 vocabulary conflict are reported,
not silently repaired. Any resolution belongs in an explicitly reviewed
owning contract/evidence change before production implementation.

No trajectory schema, writer, shard, actor/learner transport, teacher,
WindBot, model, tensor, framework adapter, reward implementation, arbitrary
deck/format/rules support, networking, checkpoint/fork, or unrelated M4
performance work is included.

## 40. Implementation readiness

The facade design is complete enough to review, but production implementation
is not authorized by this specification while B1 and B2 remain unresolved.

    PHASE 2 IMPLEMENTATION SHOULD NOT BEGIN
