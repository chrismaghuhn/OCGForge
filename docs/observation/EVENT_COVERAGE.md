# M2 visible event coverage

The machine-readable inventory is `docs/observation/event_coverage.json`.
`event_projection` accepts the pinned length-framed engine message stream and
creates typed events after perspective filtering. It never forwards raw
packets, raw hidden passcodes, or persistent engine identifiers.

Supported knowledge-destroying families emit both the visible action and an
explicit `RandomizationBoundary` where appropriate. A move into the Main Deck
has no safe entity locator; a later slot locator is not a physical-card ID.
`MSG_CONFIRM_*` reveals are emitted only to the packet recipient. Unknown or
deferred families are omitted and documented rather than decoded by guessing.

The event stream is cumulative in `ObservationSession`; callers may pass the
session's perspective-filtered vector into the next observation. Event indices
are session-local and engine-step indices are retained for deterministic
alignment. Event targets are sorted only during canonical serialization.
