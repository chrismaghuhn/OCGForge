# M2 observation field coverage

The machine-readable inventory is
`docs/observation/observation_field_coverage.json`. Classifications mean:

- `EXPOSED_PUBLIC`: exposed after the pinned query proves public visibility;
- `EXPOSED_PRIVATE_TO_OWNER`: exposed only to the explicit perspective when
  the owner/player is entitled to see it;
- `REDACTED_WHEN_HIDDEN`: decoded for projection but removed or null when the
  privacy boundary cannot prove visibility;
- `STATIC_CONTEXT`: configured match/deck knowledge, never current ordered
  engine state;
- `DERIVED`: deterministic semantic projection from authoritative fields;
- `INTENTIONALLY_NOT_EXPOSED`: deliberately omitted because it is raw engine
  causality, an identity leak, or not needed by M2;
- `OUT_OF_SCOPE_M2`: requires a later authority/API decision and is not
  claimed as implemented.

The query decoder validates exact record lengths, duplicate flags, known flag
payloads, and query terminators before any field reaches the projection.
Unknown or unproven visibility fails closed. In particular, the pinned
`QUERY_OVERLAY_CARD` parent record contains an aggregate count and ordered raw
material passcodes, but M2.1 confirmed that the pinned per-material
`LOCATION_OVERLAY` query returns no record and `OCG_DuelQueryLocation` returns
only a null marker. M2 retains material count and redacted attachment nodes
rather than joining unproven raw codes to material entities. The exact API
evidence is recorded in `M2_1_XYZ_API_INVESTIGATION.md`.

Turn/phase/player-to-act values are populated when the observation is built
with the perspective-filtered `ObservationSession` and/or a
`DecisionRequest`; they remain explicit `null` for a query-only caller that
has not supplied those authoritative sources. `PlayerObservation` is a
variable-length semantic structure. No inventory entry introduces a fixed ML
tensor shape.
