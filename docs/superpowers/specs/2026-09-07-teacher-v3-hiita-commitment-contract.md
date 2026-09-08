# Teacher V3 Hiita Continuation-Commitment Contract

Status: Stage B1 contract freeze and V2 successor implementation.

This document defines the Teacher successor that consumes the V3 public
card-selection operation metadata. It does not change the accepted Stage-A
environment or public-action contracts.

## Ownership and authority

The owning layer is the Teacher public policy boundary and its retained
goal/line scoring. The Environment remains authoritative for candidate
membership, candidate order, candidate cardinality, public action keys, and
card-selection operations. Teacher code copies and consumes those public
values; it never reconstructs them.

This contract does not own Environment legality, Decision Protocol response
bytes, trajectory migration, replay migration, dataset or model migration, or
Task7 materialization.

Teacher inputs are limited to:

- PublicEnvironmentObservation;
- the complete ordered EnvironmentActionCandidate domain;
- the applicable versioned local strategy state;
- the validated strategy profile.

CoreHost, private PlayerObservation internals, hidden passcodes, raw engine
response bytes, internal semantic keys, pointers, process identifiers,
wall-clock values, and unordered iteration are outside the Teacher boundary.

## Public-action version boundary

The historical Teacher path is bound to a V2 environment frame that publishes
public_action.v1.* keys. Every candidate must pass the authoritative
is_public_action_key() validator and its card_selection_operation must be
None. This historical path and its behavior remain unchanged.

The V3 Teacher successor is bound to a V3 environment frame that publishes
public_action.v2.* keys. Every candidate must pass the authoritative
is_public_action_key_v2() validator. The domain is homogeneous:

- all candidates V1: historical Teacher boundary;
- all candidates V2: V3 Teacher successor boundary;
- a V1/V2 mixture: invalid and fail closed;
- a malformed key: invalid and fail closed.

The implementation must use the versioned validators. It must not classify
versions by manually parsing prefixes, normalize both namespaces into one
namespace, or silently reinterpret a V1 contract as V2.

## Public operation structure

For a public observation whose decision-context kind is unselect_card, a
CardSelection candidate must carry Select or Unselect. A candidate from the
currently unselected list is Select; a candidate from the currently selected
list is Unselect.

Cancel and Finish carry None. Every non-CardSelection candidate carries None.
Any contradictory combination is invalid and fails closed.

The historical V1 path requires None for every candidate. The Teacher must
consume the explicit EnvironmentActionCandidate.card_selection_operation
value. It must not infer a direction from candidate position, ordering,
source_index, locator shape, response index, continuation-selected indices,
raw engine bytes, or an internal semantic key.

## Candidate features

The future V3-aware candidate feature representation contains the public
operation explicitly as None, Select, or Unselect. Its value is derived only
from EnvironmentActionCandidate.card_selection_operation. No new legality
source or hidden-information source is introduced.

## State and provenance successors

EpisodeLocalStrategyStateV1 and TeacherStateDeltaV1 remain frozen historical
contracts. Their public-action-key fields remain V1-bound:

- last_accepted_public_action_key;
- base_last_accepted_public_action_key;
- proposed_for_public_action_key.

Inserting a V2 public action key into either V1 state or delta is rejected.
The V1 state and delta are never broadened to accept V2 keys.

The future V3 Teacher implementation uses these successor semantics:

- EpisodeLocalStrategyStateV2;
- TeacherStateDeltaV2.

The corresponding three public-action-key fields in the successor state and
delta are V2 public-action keys. These successor types are implemented by the
B1 GREEN slice; Runner, Trajectory, and downstream migration remain deferred.

Changing Teacher semantics changes policy provenance. The historical Teacher
producer remains `ocgforge.policy.teacher_core.v1`; the V3-aware successor
producer is `ocgforge.policy.teacher_core.v2`. The historical action adapter is
`ocgforge.policy.public_action_key.v1`; the V3 action adapter is
`ocgforge.policy.public_action_key.v2`. A changed Teacher core must therefore
produce a changed Teacher policy artifact and binding identity. The old
TeacherPolicyBinding content identity and old Teacher policy artifact identity
are not reused for the new semantics.

The `TeacherPolicyBindingV1` schema, `PolicyArtifact` schema,
`StrategyProfileV1`, score vector, fallback, tie-break, diagnostic, deterministic
sampling, and no-policy-RNG contracts remain unchanged. The V2 identities are
new content identities of those existing schemas; this slice does not create a
V2 binding, artifact, or profile schema.

## Retained-line commitment scoring

The generic commitment rule applies only when normal public reconciliation
leaves a valid active goal and a valid active line in the reconciled state. At
an intermediate native `unselect_card` boundary, those retained identifiers
constitute a **reconciled continuation commitment** even when the current
public decision facts make the goal or line ineligible for a fresh strategic
selection. In particular, `select_goal_and_line()` need not return the active
goal/line at that boundary. This rule consumes the existing reconciled state;
it does not add a second commitment state, synthesize missing identifiers, or
change reconciliation semantics.

For a native V3 unselect_card decision:

| Candidate | Generic retained-commitment progress |
| --- | ---: |
| CardSelection + Select | +1 |
| CardSelection + Unselect | 0 |
| Cancel | 0 |
| Finish | 0 |

There is no generic negative penalty for Unselect, Cancel, or Finish. The
operation contribution is combined with existing progress using MAX, never
addition:

- explicit active-line intent match: +3;
- validated recovery progress: +2;
- generic retained Select progress: +1.

Thus explicit active match plus Select remains +3, not +4; recovery plus
Select remains governed by existing recovery semantics where applicable; a
retained-line Select with no other progress contributes +1; Unselect, Cancel,
and no progress contribute 0.

The +1 is not applicable when there is no retained active line, the domain is
historical V1, the decision kind is not unselect_card, the candidate is not
CardSelection, the operation is None or Unselect, or the candidate is Cancel
or Finish.

Recovery/F1 behavior is unchanged. This rule does not add generic progress
for Pick, AssignAmount, Place, Bypass, ordering, sum, tribute, or any other
continuation family. Those families require separate evidence and
authorization.

## Hiita semantic sequence

For the confirmed Hiita case, the initial native material boundary contains:

- material A: CardSelection + Select, generic progress +1;
- material B: CardSelection + Select, generic progress +1;
- Cancel: operation None, progress 0.

The existing complete-domain deterministic resolver remains authoritative
between A and B. No candidate is selected by hard-coded index or card name.

After A is selected, the next native boundary contains:

- material A: CardSelection + Unselect, progress 0;
- material B: CardSelection + Select, progress +1;
- Cancel: operation None, progress 0.

The Teacher therefore selects B. The same rule applies symmetrically when B
is selected first. The intended public gameplay frontier is:

IDLE -> Hiita -> UNSELECT Select first material -> UNSELECT Select second
material -> PLACE -> public Hiita summon completion -> next legal public
boundary

The B1 test slice does not run a real Task7 collection episode.

The RED fixture uses the canonical `make_salamangreat_profile()` without
adding a CardSelection intent or changing any Salamangreat profile data. It
sets the existing active identifiers to `goal.main1.salamangreat` and
`line.main1.salamangreat` before the `unselect_card` boundary. The fixture
characterizes that normal reconciliation retains both identifiers while
`select_goal_and_line()` reports no currently eligible Main1 line because the
profile's `idle_context` applicability is not true for `unselect_card`.
The future generic Select rule therefore relies on the reconciled continuation
commitment, not on current strategic eligibility.

## Invalidations and non-applicability

Existing authoritative invalidations remain in force:

- a public-state contradiction clears active goal, active line, and completed
  line nodes;
- goal completion clears the active goal and line;
- invalid state, profile, reference, or arithmetic fails closed;
- no active goal/line causes no synthetic commitment;
- a rejected step does not mutate committed state.

Normal reconciliation is unchanged. A decision-kind transition by itself does
not clear the active goal or line; only the existing authoritative public-state
contradiction, completion, invalid-state, or other fail-closed paths do so. If
reconciliation clears either identifier, the reconciled continuation
commitment no longer exists and the generic progress rule is not applicable.
No additional persistence rule is introduced for later decision families.

## Privacy, determinism, and replay

The future implementation consumes only public observation, the complete
ordered public candidate domain, version-compatible local state, and the
validated strategy profile. Paired public-equivalent worlds produce the same
Teacher result. Candidate order and cardinality remain Environment-owned.

No hidden identity, CoreHost state, internal semantic key, response bytes,
pointer, PID, wall-clock value, or unordered-container order may influence the
result. Public action version and state version are checked explicitly and
mixed versions fail closed. Existing replay/action-identity contracts remain
version-bound.

Runner/Trajectory migration is deferred. Replay migration is deferred. Model
input and dataset migration are deferred. Task7 materialization and any
Task7 RUN are deferred. RUN_A, RUN_B, dataset generation, and training are
outside this slice.

## B1 RED boundary and GREEN implementation

The accepted B1 RED at the preceding base commit recorded that the production
Teacher was V1-bound and rejected a homogeneous V2 public-action domain. The
GREEN implementation retains that historical V1 path and adds an explicit V2
path with `TeacherCoreV2`, V2 state/delta, V2 candidate extraction, V2 domain
evaluation, V2 fallback resolution, and V2 provenance factories.

The GREEN path consumes the complete ordered V2 domain and explicit
Select/Unselect metadata. It retains a reconciled active goal/line commitment
at the Hiita `unselect_card` boundary, gives only Select the generic +1, uses
MAX for overlapping progress sources, and keeps V1 state/result contracts
isolated. The V2 state, result, and provenance implementations are not wired
into TeacherRunner or any Trajectory, Model, Task7, dataset, or training path.
