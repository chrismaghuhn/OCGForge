# Player-view boundary v1

The environment API always projects state for an explicit player perspective.
The engine host may hold more information internally, but callers cannot ask
the public view for an omniscient projection.

## Visible card fields

The M0 projection retains controller, location, sequence, position, and an
original Yu-Gi-Oh! card passcode when the identity is visible. It also carries
the `identity_visible` bit so redaction is explicit rather than inferred from
a sentinel alone.

For a card retained in the projected list, an identity is visible when:

- the card is owned or controlled by the perspective player; or
- the engine has marked the card identity public.

All hidden deck entries are omitted from the card list, regardless of
perspective; opponent hidden hand entries are omitted as well. Their zone
counts remain available. An opponent's face-down field card is retained with
`identity_visible=false` and passcode `0`. The passcode `0` is a redaction
sentinel at this boundary; it is not a card-vocabulary entry.

## Counts

Own-deck, own-hand, opponent-deck, and opponent-hand counts remain available.
Counts do not reveal hidden card order or identities. Public field cards remain
visible according to their public-identity flag.

## Privacy invariants

The M0 tests assert that:

1. a player's own hand identities are visible;
2. hidden deck identities and ordering, plus opponent hidden hand identities,
   are not visible;
3. hidden opponent field cards with different passcodes produce the same
   redacted identity representation;
4. public opponent face-up cards remain visible;
5. zone counts remain present after redaction.

The projection is not a model tensor and does not map passcodes to a
contiguous vocabulary. That mapping is a separate future model contract.
