# Phase 4A Public-Fact Sufficiency Matrix

Status: Phase 4A substrate evidence only. This document does not implement
`TeacherCore`, either deck `StrategyProfile`, or a model-facing adapter.

The research documents under `docs/research/teacher_strategy/` are non-normative
inputs. The accepted public contracts remain authoritative. A future Teacher
may consume the acting perspective's `PublicEnvironmentObservation`, a typed
view decoded by `ygo::environment` from its already-accepted
`ocgforge.public_safe_state.v1` bytes, and the complete ordered
`EnvironmentActionCandidate` vector supplied by V2.

`DIRECT` means the fact is explicitly present in an accepted public-safe
field. `SAFE_DERIVATION` means it can be derived only from those fields and
perspective-authorized accepted public history. `BLOCKED` is intentional: the
fact is required by the researched capability but is not proven available at
the current boundary. A blocked row must not be bypassed with `CoreHost`, raw
engine state, private observation data, hidden identity, or a secret-derived
hash.

The typed view deliberately does not expose `engine_step_index`; that internal
progress value remains outside the public-safe semantic state.

| ID | Teacher requirement | Exact public source | Availability | Evidence | Blocked reason |
|---|---|---|---|---|---|
| P4A-G00-01 | Acting perspective and player-to-act context | `include/ygo/environment/public_environment_observation.hpp:PublicEnvironmentObservationInput.perspective_player` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp::paired_public_observation` | - |
| P4A-G00-02 | Turn player | `include/ygo/observation/observed_player_globals.hpp:ObservedPlayerGlobals.turn_player` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-03 | Current phase | `include/ygo/observation/observed_player_globals.hpp:ObservedPlayerGlobals.phase` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-04 | Life points for both players | `include/ygo/observation/observed_player_globals.hpp:ObservedPlayerGlobals.life_points` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-05 | Visible field cards and their safe locators | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView.entities().zone` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp::paired_world_projection` | - |
| P4A-G00-06 | Visible graveyard and banished cards | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView.entities().zone` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp::paired_world_projection` | - |
| P4A-G00-07 | Known hand cards, limited to identities authorized by perspective | `include/ygo/observation/observed_card.hpp:ObservedCard.identity_known` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/observation/privacy_projection_test.cpp` | - |
| P4A-G00-08 | Zone counts and public/hidden counts | `include/ygo/observation/observed_zone.hpp:ObservedZone.total_count` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-09 | Public card properties, printed/current values where exposed | `include/ygo/observation/observed_card.hpp:ObservedCard.printed` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/observation/privacy_projection_test.cpp` | - |
| P4A-G00-10 | Face-up, face-down, position, and visibility state | `include/ygo/observation/observed_card.hpp:ObservedCard.face_up` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/observation/privacy_projection_test.cpp` | - |
| P4A-G00-11 | Public card relationships such as targets, equips, and materials | `include/ygo/observation/relationship.hpp:Relationship` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-12 | Current chain length and links | `include/ygo/observation/chain_state.hpp:ChainState.links` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-13 | Chain source for each public link | `include/ygo/observation/chain_state.hpp:ChainLink.source` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-14 | Chain targets for each public link | `include/ygo/observation/chain_state.hpp:ChainLink.targets` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-15 | Public effect description when the contract exposes it | `include/ygo/observation/chain_state.hpp:ChainLink.effect_description` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-16 | Public event history exposed by the safe projection | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView.visible_events()` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp::paired_world_projection` | - |
| P4A-G00-17 | Summon and card-movement history | `include/ygo/environment/public_safe_state.hpp:PublicSafeVisibleEvent.kind` | SAFE_DERIVATION | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp::paired_world_projection` | - |
| P4A-G00-18 | Shuffle and randomization boundaries | `include/ygo/observation/visible_event.hpp:VisibleEventKind.RandomizationBoundary` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/environment/public_safe_state_test.cpp` | - |
| P4A-G00-19 | Exact public once-per-turn and effect-use history | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView.visible_events()` | BLOCKED | `tests/policy/public_fact_matrix_test.py::check_blocked_reason`; `tests/trajectory/privacy_test.cpp`; `tests/policy/policy_boundary_test.py` | The accepted visible-event vocabulary does not encode every effect-use, restriction, or reset fact needed to prove exact once-per-turn availability. A private engine or observation side channel is forbidden. |
| P4A-G00-20 | Candidate source reference | `include/ygo/environment/public_decision.hpp:EnvironmentActionCandidate.source_reference` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp` | - |
| P4A-G00-21 | Candidate target reference | `include/ygo/environment/public_decision.hpp:EnvironmentActionCandidate.target_reference` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp` | - |
| P4A-G00-22 | Candidate action kind and request kind | `include/ygo/environment/public_decision.hpp:EnvironmentActionCandidate.action_kind` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp` | - |
| P4A-G00-23 | Candidate amount | `include/ygo/environment/public_decision.hpp:EnvironmentActionCandidate.amount` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp` | - |
| P4A-G00-24 | Candidate position and phase | `include/ygo/environment/public_decision.hpp:EnvironmentActionCandidate.position` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp` | - |
| P4A-G00-25 | Candidate choice, source index, and continuation metadata | `include/ygo/environment/public_decision.hpp:EnvironmentActionCandidate.choice` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp` | - |
| P4A-G00-26 | Complete candidate count, order, membership, and existing public key | `include/ygo/environment/public_decision.hpp:EnvironmentDecisionRequest.candidates` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/policy/random_legal_test.cpp`; `tests/policy/policy_runner_integration_test.cpp` | - |
| P4A-G00-27 | Decklist context only when the contract marks it known | `include/ygo/observation/match_context.hpp:MatchContext.knowledge` | DIRECT | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp::paired_world_projection` | - |
| P4A-G00-28 | Opponent hidden hand identities and hidden deck order | `include/ygo/observation/match_context.hpp:MatchContext.opponent_deck` | BLOCKED | `tests/policy/public_fact_matrix_test.py::check_blocked_reason`; `tests/observation/privacy_projection_test.cpp`; `tests/trajectory/privacy_test.cpp` | The canonical matchup marks opponent deck knowledge false and the public contract omits hidden entries/order. Research requirements depending on those facts cannot be implemented at this boundary. |
| P4A-G00-29 | Hidden physical identity after a shuffle/randomization boundary, including a hidden Foxy top-deck identity | `include/ygo/environment/public_safe_state.hpp:PublicSafeVisibleEvent.entity` | BLOCKED | `tests/policy/public_fact_matrix_test.py::check_blocked_reason`; `tests/trajectory/privacy_test.cpp`; `tests/episodic/episodic_paired_world_test.cpp` | Public locators and redacted slots do not prove physical identity across knowledge-destroying transitions. Retaining engine identity, pointer identity, or a secret-derived hash is forbidden. |
| P4A-G00-30 | Exact temporary summon/effect restrictions such as an Ashuna Wyrm-only lock | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView.visible_events()` | BLOCKED | `tests/policy/public_fact_matrix_test.py::check_blocked_reason`; `tests/policy/policy_boundary_test.py` | The current public-safe state has no complete typed restriction ledger. Candidate legality remains engine-owned; the Teacher may not reconstruct the missing restriction from private state. |
| P4A-G00-31 | Omitted engine event families, raw protocol details, and private diagnostics | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView.visible_events()` | BLOCKED | `tests/policy/public_fact_matrix_test.py::check_blocked_reason`; `tests/policy/policy_boundary_test.py`; `tests/trajectory/privacy_test.cpp` | The projection intentionally exposes only its accepted public event vocabulary and excludes raw messages, response bytes, internal decision IDs, and engine-step metadata. A Teacher rule requiring omitted facts is blocked. |
| P4A-G00-32 | Public resource/threat/line facts derived from current state and accepted public history | `include/ygo/environment/public_safe_state.hpp:PublicSafeStateView` | SAFE_DERIVATION | `tests/policy/public_fact_matrix_test.py::check_source_reference`; `tests/trajectory/privacy_test.cpp`; `tests/episodic/episodic_paired_world_test.cpp` | - |

## Consequence for the future Teacher

Rows marked `DIRECT` or `SAFE_DERIVATION` are substrate candidates, not a
legality oracle. The environment remains the sole authority for legal
candidate generation. Rows marked `BLOCKED` stop the corresponding researched
capability until a separately reviewed public-contract extension provides the
missing fact. No Swordsoul Tenyi or Salamangreat Teacher rule is implemented
in Phase 4A on the basis of a blocked fact.
