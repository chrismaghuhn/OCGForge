# Public Card Selection Operation Stage A Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the approved public Select/Unselect operation metadata as a versioned Stage-A prerequisite while preserving the historical Episodic V2 and downstream V1 contracts.

**Architecture:** The protocol decoder adds typed `CardSelectionOperation` metadata without changing internal action keys, response bytes, candidate order, or continuation semantics. A V3-capable episodic facade projects that metadata into `EnvironmentActionCandidate`, hashes it through new public-action/domain/decision V2 identities, and binds those identities into `episodic_environment.v3` / `environment_identity.v3`. Historical V2 callers remain available through the existing V1 public identity path and `CertifiedEnvironmentConfig::canonical()`; `canonical_v3()` is the explicit successor entry point.

**Tech Stack:** C++17, CMake/Ninja, native Windows MSVC build, CTest, the pinned ocgcore adapter, existing public-safe observation projection, and standalone C++ test executables.

---

## Scope guard

This plan does not modify Teacher behavior, strategy state, continuation algorithms, Decision Protocol response semantics, trajectory/replay/model codecs, Task7 materialization, production budgets, RUN_A/B, performance branches, datasets, or training. The only semantic implementation files are the protocol action/decoder and environment public-projection/identity files listed below, plus their focused tests and the two new normative successor contracts.

The historical path remains exact:

```text
CertifiedEnvironmentConfig::canonical()
    -> episodic_environment.v2
    -> public_action/domain/decision identity v1
    -> existing trusted trajectory/admission/model callers
```

The new path is explicit:

```text
CertifiedEnvironmentConfig::canonical_v3()
    -> episodic_environment.v3
    -> public_action/domain/decision identity v2
    -> public EnvironmentActionCandidate.card_selection_operation
```

## Task 1: Add the failing protocol direction tests

**Files:**

- Modify: `tests/protocol/decision_family_test.cpp`

- [ ] Add assertions to the existing synthetic `MSG_SELECT_UNSELECT_CARD` fixture before implementation:

```cpp
if (unselect_request.candidates[0].card_selection_operation !=
        ygo::protocol::CardSelectionOperation::Unselect ||
    unselect_request.candidates[1].card_selection_operation !=
        ygo::protocol::CardSelectionOperation::Select ||
    unselect_request.candidates[2].card_selection_operation !=
        ygo::protocol::CardSelectionOperation::None) {
    std::cerr << "select-unselect-card operation metadata was not classified\n";
    return 1;
}
```

The fixture has one currently selected card, one currently unselected card,
and one finish candidate, so the assertions cover `Unselect`, `Select`, and
`None` in authoritative source order.

- [ ] Run the RED gate:

```text
cmake --build --preset dev-windows --target decision_family_test --parallel
```

Expected result: the test target does not compile because the new typed field
does not exist yet. This is the intended missing-API RED state, not a runtime
failure caused by a malformed fixture.

## Task 2: Implement internal decoder-owned operation metadata

**Files:**

- Modify: `include/ygo/protocol/action_candidate.hpp`
- Modify: `src/protocol/message_decoder.cpp`
- Modify: `tests/protocol/decision_family_test.cpp`

- [ ] Add the internal enum with fixed values and a default field:

```cpp
enum class CardSelectionOperation : std::uint8_t {
    None = 0,
    Select = 1,
    Unselect = 2,
};
```

Add `CardSelectionOperation card_selection_operation =
CardSelectionOperation::None;` to `ActionCandidate`. It is auxiliary metadata;
it is not appended to `semantic_key`, `exact_response_bytes`, or any internal
domain/trace identity.

- [ ] In `decode_unselect`, set `Unselect` for every candidate decoded from
the `selected` list and `Select` for every candidate decoded from the
`unselected` list. Leave finish/cancel at `None`.

- [ ] Extend `validate_candidate_set` with the fail-closed structural rule:

```text
request.kind == UnselectCard AND candidate.action_kind == CardSelection
    -> operation is Select or Unselect

otherwise
    -> operation is None
```

This rule rejects manually assembled inconsistent metadata and preserves the
existing response/candidate validation.

- [ ] Run the focused green gate:

```text
cmake --build --preset dev-windows --target decision_family_test --parallel
ctest --preset dev-windows -R "^decision_family_test$" --output-on-failure
```

Expected result: the selected/unselected/finish assertions pass and existing
protocol behavior remains unchanged.

## Task 3: Add failing public V2 identity tests

**Files:**

- Modify: `tests/episodic/public_action_identity_test.cpp`

- [ ] Add compile-time and runtime tests for the successor values and API:

```cpp
static_assert(ygo::environment::kPublicActionIdentityV2SchemaId ==
              "ocgforge.public_action_identity.v2");
static_assert(ygo::environment::kPublicCandidateDomainV2SchemaId ==
              "ocgforge.public_candidate_domain.v2");
static_assert(ygo::environment::kPublicSemanticDecisionIdentityV2SchemaId ==
              "ocgforge.public_semantic_decision_identity.v2");

auto select = hidden_card_action();
select.card_selection_operation =
    ygo::environment::PublicCardSelectionOperation::Select;
auto unselect = select;
unselect.card_selection_operation =
    ygo::environment::PublicCardSelectionOperation::Unselect;
require(ygo::environment::public_action_key_v2(select) !=
            ygo::environment::public_action_key_v2(unselect),
        "Select and Unselect shared a V2 public action identity");
require(ygo::environment::is_public_action_key_v2(
            ygo::environment::public_action_key_v2(select)),
        "V2 public action key failed canonical validation");
require(!ygo::environment::is_public_action_key_v2(
            ygo::environment::public_action_key(select)),
        "V1 public action key was accepted by the V2 validator");
```

Also assert that a non-`card_selection` action with `Select` or `Unselect`
fails closed in the V2 codec, and that the existing V1 golden vectors still
pass with the default `None` operation.

- [ ] Run the RED gate:

```text
cmake --build --preset dev-windows --target public_action_identity_test --parallel
```

Expected result: compilation fails for the missing V2 enum/constants/functions.

## Task 4: Implement public action/domain/decision identity V2

**Files:**

- Modify: `include/ygo/environment/public_action_identity.hpp`
- Modify: `src/environment/public_action_identity.cpp`
- Modify: `src/environment/public_action_identity_internal.hpp`
- Modify: `tests/episodic/public_action_identity_test.cpp`

- [ ] Define `PublicCardSelectionOperation` with exact codes `None=0`,
`Select=1`, and `Unselect=2`, add it to `PublicActionKeyInput` with default
`None`, and expose V2 constants/functions alongside the unchanged V1 API:

```text
canonical_public_action_key_bytes_v2
public_action_key_v2
is_public_action_key_v2
canonical_public_candidate_domain_bytes_v2
public_candidate_domain_digest_v2
canonical_public_semantic_decision_identity_bytes_v2
public_semantic_decision_id_v2
resolve_public_action_key_v2
```

- [ ] Keep the V1 codec byte-for-byte unchanged. It rejects a non-`None`
operation rather than silently dropping newly meaningful metadata.

- [ ] Encode V2 action bytes in this exact order:

```text
V2 schema string
V2 schema string
action_kind string
card_selection_operation:u8
choice
source reference
target reference
phase
position
source index
amount
continuation operation
```

The V2 validator permits `None | Select | Unselect` structurally only for
`action_kind == "card_selection"`; every other action kind requires `None`.
The request-family rule remains owned by Environment projection.

- [ ] Encode V2 candidate-domain bytes with the V2 domain schema and only
V2 public keys, preserving supplied candidate order and duplicate/empty-domain
rejection. Encode V2 public semantic decision identity with the V2 schema in
both domain/schema positions and the existing public fields/order.

- [ ] Add a V2 frame-local resolver that validates only V2 keys and preserves
the existing collision/unknown-key fail-closed behavior. Do not expose
internal semantic keys or change the existing V1 resolver.

- [ ] Run the green gates:

```text
cmake --build --preset dev-windows --target public_action_identity_test --parallel
ctest --preset dev-windows -R "^public_action_identity_test$" --output-on-failure
```

## Task 5: Add failing V3 public-projection and identity tests

**Files:**

- Create: `tests/episodic/episodic_environment_v3_public_operation_test.cpp`
- Modify: `tests/episodic/episodic_identity_test.cpp`
- Modify: `CMakeLists.txt`

- [ ] Add a focused V3 test target using the existing test-only
`EpisodicEnvironmentTestAccess::project_frame_for_test` seam. Construct a
public-safe observation and a `DecisionRequestKind::UnselectCard` with one
selected-list candidate, one unselected-list candidate, and cancel. Assert:

```text
canonical_v3 config is accepted by EpisodicEnvironment::create
frame.contract_id == ocgforge.episodic_environment.v3
frame.request candidates retain source order and cardinality
candidate 0 operation == PublicCardSelectionOperation::Unselect
candidate 1 operation == PublicCardSelectionOperation::Select
candidate 2 operation == PublicCardSelectionOperation::None
candidate keys use public_action.v2.
domain digest uses public_candidate_domain.v2
decision ID uses public_semantic_decision_identity.v2
```

- [ ] Add negative assertions that inconsistent internal operation metadata,
an operation on a non-UNSELECT card-selection request, and a non-card action
with a non-`None` operation fail closed before a public frame is emitted.

- [ ] Add V3 constants and `canonical_v3` identity assertions to the existing
identity test. Preserve the historical V1/V2 golden identity assertions for
`CertifiedEnvironmentConfig::canonical()`.

- [ ] Add the new executable and CTest registration in `CMakeLists.txt`
without altering production libraries or Task7 budgets.

- [ ] Run the RED gate:

```text
cmake --build --preset dev-windows --target episodic_environment_v3_public_operation_test --parallel
```

Expected result: compilation fails for the missing V3 configuration and
V2-public projection APIs.

## Task 6: Implement V3 configuration, projection, and environment identity

**Files:**

- Modify: `include/ygo/environment/public_action_identity.hpp`
- Modify: `include/ygo/environment/public_decision.hpp`
- Modify: `include/ygo/environment/episodic_environment.hpp`
- Modify: `src/environment/episodic_environment.cpp`
- Modify: `src/environment/public_action_identity.cpp`
- Modify: `src/environment/public_action_identity_internal.hpp`
- Modify: `tests/episodic/episodic_environment_v3_public_operation_test.cpp`
- Modify: `tests/episodic/episodic_identity_test.cpp`

- [ ] Add explicit successor constants:

```text
kEpisodicEnvironmentV3ContractId = ocgforge.episodic_environment.v3
kEnvironmentIdentityV3SchemaId = ocgforge.environment_identity.v3
```

Keep `kEpisodicEnvironmentV2ContractId` and the existing V1 public identity
constants for historical callers. Add `CertifiedEnvironmentConfig::canonical_v3()`
which derives the certified resource fields from `canonical()`, then sets:

```text
contract_id = episodic_environment.v3
public_action_identity_schema_id = public_action_identity.v2
public_candidate_digest_schema_id = public_candidate_domain.v2
public_decision_identity_schema_id = public_semantic_decision_identity.v2
environment_semantic_id = SHA256(canonical environment identity V3 bytes)
```

The episode identity codec and all retained internal/observation schemas stay
unchanged.

- [ ] Make environment identity byte generation choose V2 or V3 only from
the validated config contract version. V3 retains the exact current field
order and changes only the identity schema/contract/public successor IDs.
No raw action key, continuation ID, pointer, or hidden state enters the
identity.

- [ ] Make `EpisodicEnvironment::create` accept exactly the historical
canonical V2 config or the explicit canonical V3 config. Do not accept mixed
versions. Keep the existing resource/deck validation.

- [ ] Make all current facade output/rejection/terminal/failure contract IDs
derive from the live config, so V2 and V3 sessions cannot cross-contaminate.
Reset/step/interrupt must compare submitted contract IDs to the current
config contract ID.

- [ ] In V3 projection, enforce the public request rule:

```text
request.kind == UnselectCard AND candidate.action_kind == CardSelection
    -> internal operation must be Select or Unselect
    -> copy the matching PublicCardSelectionOperation into the DTO/key input

request.kind != UnselectCard OR candidate.action_kind != CardSelection
    -> internal operation must be None
    -> public operation is None
```

Use `public_action_key_v2`, `public_candidate_domain_digest_v2`,
`public_semantic_decision_id_v2`, and the V2 frame-local resolver only for V3.
V2 projection continues using the unchanged V1 functions and emits the
default `None` DTO metadata.

- [ ] Add the public field to `EnvironmentActionCandidate` without changing
existing V1 serialization code. The operation is a public auxiliary field and
is included in V2 public action identity through `PublicActionKeyInput`.

- [ ] Run the green gates:

```text
cmake --build --preset dev-windows --target episodic_environment_v3_public_operation_test episodic_identity_test --parallel
ctest --preset dev-windows -R "^(episodic_environment_v3_public_operation_test|episodic_identity_test)$" --output-on-failure
```

## Task 7: Add normative V2/V3 contract documents

**Files:**

- Create: `docs/contracts/public-action-identity-v2.md`
- Create: `docs/contracts/episodic-environment-v3.md`
- Modify: `docs/contracts/README.md` if its contract index needs the two successor links

- [ ] Publish the accepted design as executable normative successors, not as
aliases. `public-action-identity-v2.md` must state the V2 operation enum,
exact action/domain/decision byte order, codec-level versus projection-level
validation, V1 rejection, complete-domain/order rules, frame-local mapping,
and paired-world privacy requirements.

- [ ] Publish `episodic-environment-v3.md` with the V3 DTO field,
`canonical_v3()` entry point, V3/environment identity field order, historical
V2 coexistence/rejection matrix, V1 observation/episode/internal identity
retention, and fail-closed mixed-version behavior.

- [ ] Keep trajectory, replay, model, Task7 materialization, Teacher, and
training migrations explicitly deferred to their authorized later slices.

- [ ] Run documentation checks:

```text
git diff --check
rg -n "TBD|TODO|INCOMPLETE|UNSPECIFIED" docs/contracts/public-action-identity-v2.md docs/contracts/episodic-environment-v3.md
```

The completeness scan must return no matches in the two new normative files.

## Task 8: Full Stage-A verification and deterministic review

**Files:**

- No additional source files are authorized beyond Tasks 1–7.

- [ ] Configure and build the complete native Windows target from this
worktree:

```text
cmake --preset dev-windows
cmake --build --preset dev-windows --parallel
```

- [ ] Run focused protocol, public-identity, episodic projection, privacy,
identity, replay-boundary, and determinism tests. Report exact CTest counts;
do not relabel historical/absent tests as current PASS.

- [ ] Run the existing Teacher and trajectory/model tests as regression only.
They must continue to pass on the historical V2 path; no V3 trajectory/model
consumer is introduced in Stage A.

- [ ] Run the new V3 public-operation test in two separate fresh processes and
compare stdout, stderr, and exit code. The public V2 key/domain/decision values
must be deterministic and the paired hidden-world assertions must not expose
internal semantic keys or hidden passcodes.

- [ ] Perform a cumulative scope check against diagnostic base
`827f73db843636e289e5687698bb77996b4692ef`. Allowed implementation additions
are protocol headers/decoder, environment headers/identity/projection,
focused tests, CMake test registration, and the two V2/V3 contract docs. No
Teacher, trajectory, model, continuation, Decision Protocol response, rules,
deck, Task7 budget, RUN, performance, dataset, or training file may change.

- [ ] Commit implementation in reviewable slices with messages that identify
the protocol, public identity, and V3 projection portions. Push only after all
local verification gates pass; do not create a PR, merge, start RUN_A/B, or
train.

## Completion gates

The Stage-A implementation may be reported ready for independent review only
when all of these are true from fresh commands:

```text
INTERNAL_SELECTED_LIST_OPERATION=UNSELECT
INTERNAL_UNSELECTED_LIST_OPERATION=SELECT
FINISH_CANCEL_OPERATION=NONE

V1_PUBLIC_IDENTITY_BYTES_UNCHANGED=YES
V2_PUBLIC_ACTION_IDENTITY_INCLUDES_OPERATION=YES
V2_PUBLIC_DOMAIN_AND_DECISION_IDENTITIES=YES
V3_ENVIRONMENT_AND_IDENTITY=YES
MIXED_V2_V3_REJECTED=YES
PUBLIC_PRIVACY_GATE=PASS
FRESH_PROCESS_DETERMINISM=PASS
HISTORICAL_V2_REGRESSION=PASS
PRODUCTION_TEACHER_CONTINUATION_TRAJECTORY_MODEL_CHANGES=NO
```

Stage A does not authorize Teacher continuation retention, downstream V2
trajectory/replay/model migration, Task7 materialization, RUN_A, RUN_B,
performance merge, dataset generation, or training.
