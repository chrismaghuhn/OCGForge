# OCGForge Future Meta-8 — Research Revision 01

Status: documentation-only research revision
Date: 2026-09-04
Tracking issue: #48

## Purpose

This revision does **not** rerun the Meta-8 research and does not introduce new external-source claims. It preserves the reviewed September 4 research conclusion and corrects only presentation, ranking methodology, confidence classification, and premature staging lock-in.

The durable decision remains:

```text
FINAL_DECISION=META8_BLOCKED_BY_FORMAT_INSTABILITY
DECKLIST_LOCKED=NO
OCGFORGE_IMPLEMENTATION_AUTHORIZED=NO
TRAJECTORY_GENERATION_AUTHORIZED=NO
CURRENT_FIXED_CURRICULUM_UNCHANGED=YES
```

The current certified Swordsoul Tenyi / Salamangreat curriculum remains separate and unchanged.

## Why the freeze remains blocked

Magnificent Monsters entered the legal TCG card pool on the research date, 4 September 2026. The reviewed tournament evidence ends on 3 September 2026, so there is no meaningful post-release tournament population yet.

The research also lacks a freeze-grade immutable capture of every dynamic-source row, event counter, player counter, event identifier, and decklist identifier. That is a source-provenance blocker independently of the format transition.

In addition, no future Meta-8 exact decklist is currently certified by OCGForge for complete passcode/script/database closure, mechanics closure, full-game replay/privacy evidence, and Teacher acceptance.

## Corrected candidate-board status

The available evidence supports a **provisional candidate board**, not an auditable strict 1-to-8 ranking.

```text
CANDIDATE_BOARD_STATUS=PROVISIONAL_UNORDERED
STRICT_1_TO_8_RANKING=NOT_SUPPORTED_BY_CURRENT_CAPTURE

HIGH_CONFIDENCE_PROVISIONAL_ROLES=
- Light-and-Darkness Ritual
- Sky Striker
- Branded
- Maliss
- Kewl Tune

BOUNDARY_REQUIRES_CHARACTERIZATION=
- DoomZ
- Elfnote
- Blitzclique
- Mitsurugi
- Invoked
- K9
```

The high-confidence group remains provisional. It is not a frozen five-deck subset and does not imply implementation authorization.

The boundary group is intentionally unordered. Elfnote and Blitzclique must not be presented as already owning final Meta-8 slots while their exact mechanics/package characterization remains incomplete. DoomZ is likewise retained as a boundary candidate until post-release evidence and closure analysis separate it from alternatives such as K9.

## Deterministic selection methodology

When the M8-0 freeze gate is eventually evaluated, candidate selection should use the following lexicographic rule instead of an arbitrary weighted score:

1. Require a minimum freeze-grade competitive-evidence threshold.
2. Exclude candidates with unresolved blocking mechanics or card-closure risk. A necessity exception is allowed only when a required curriculum coverage column would otherwise remain completely unfilled at confirmed `H` exposure and the candidate itself has confirmed `H` exposure in that column. `U` cells are unconfirmed coverage and cannot satisfy this necessity test. `M` or `V` exposure also cannot establish the exception.
3. Prefer additional strategic/mechanical coverage over lower OCGForge/Teacher risk only when the candidate closes at least one such confirmed `H`-coverage hole: a column for which no already selected or otherwise admissible lower-risk candidate supplies confirmed `H` exposure. `U`, `M`, and `V` cells do not count as closing that materiality threshold.
4. In every comparison where Step 3's materiality threshold is not met, prefer lower OCGForge closure and Teacher risk.
5. Use stronger competitive evidence to break remaining ties.
6. If the evidence still cannot separate candidates, retain an explicit `TIED / UNORDERED` status.

This rule makes the risk override deliberately narrow. Unknown mechanics cannot be treated as implied novelty, and a marginal increase from `M` to `H`, or from `U` to a hoped-for role, cannot justify selecting a materially riskier deck. Coverage outranks risk only to prevent a completely absent high-exposure curriculum role.

This intentionally avoids a synthetic formula such as `meta_share * weight + novelty * weight - risk * weight`, because unvalidated weights would only move the false precision into a score.

## Captured Light-and-Darkness evidence

The preserved capture supports:

```text
CAPTURED_NORMALIZED_TOPS=56/276
APPROX_CAPTURE_SHARE=~20.3%
FREEZE_GRADE_SOURCE_CAPTURE=NO
```

The earlier `20.29%` value is mathematically derivable from 56/276, but two-decimal presentation overstates the authority of a dynamic capture whose complete event/player/row ledger was not durably preserved. The freeze-grade artifact should therefore retain the integer count and use only an approximate share until a static source ledger exists.

## Conditional staging only

Pre-stage M8-0 remains the mandatory gate before any Meta-8 code, Teacher, trajectory, or dataset work.

Therefore all subsequent staging is planning-only:

```text
STAGE_A_TO_D_STATUS=CONDITIONAL_TEMPLATES
STAGE_MEMBERSHIP_ACCEPTED=NO
```

Illustrative sequencing may still use the lower-risk/high-value provisional roles to estimate implementation order, but it must not be mistaken for slot acceptance.

A permissible planning shape is:

```text
Illustrative Stage A:
  Branded + Sky Striker
  only if both survive M8-0

Illustrative Stage B:
  Maliss + one accepted Xyz/resource role
  DoomZ is only a candidate for that role

Illustrative Stage C:
  Kewl Tune + Light-and-Darkness Ritual
  only if both survive M8-0

Illustrative Stage D:
  TWO UNASSIGNED BOUNDARY SLOTS
  candidates include Elfnote, Blitzclique, Mitsurugi, Invoked, K9,
  plus any post-release family that satisfies the freeze gate
```

Stage D must not be committed in advance to Elfnote + Blitzclique.

## Teacher information-safety gate

The strongest accepted architectural conclusion from the research is retained unchanged: the primary shared-model corpus must not use privileged opponent-role information as Teacher input.

The primary Teacher boundary should be own-deck/public-evidence based. It must not condition authoritative labels on:

```text
opponent normalized family
opponent deck hash
opponent full list
hidden opponent cards
matchup coordinate ID
Teacher-only opponent metadata
```

Opponent adaptation may use only facts legitimately available through the public observation/history boundary.

Required future paired-world acceptance property:

```text
same public observation
+ same complete public candidate domain
+ same own-deck context
+ different hidden opponent role
-> same Teacher evaluations
-> same Teacher selection
```

A privileged-oracle experiment may be separately labeled later, but it must never be silently mixed into the primary public-policy corpus.

## M8-0 freeze gate

Meta-8 remains blocked until all of the following are satisfied:

1. At least two full post-release tournament weekends.
2. At least three substantial independent TCG events.
3. Preferably an aggregate of at least 1,000 participating players.
4. A static YuGiOhMeta capture/export with counters and rows needed for reproduction.
5. Multiple verified legal lists for every selected family.
6. One unchanged representative list selected by the declared medoid rule.
7. Official banlist rechecked on freeze day.
8. All candidate families renormalized after Magnificent Monsters.
9. Boundary candidates directly compared rather than treated as predetermined swaps.
10. Exact card-text and decision-family audit for unresolved boundary candidates, especially Elfnote and Blitzclique.
11. No hidden-opponent-role dependency in the planned primary Teacher corpus.
12. A new versioned rules-bundle/card-pool proposal without changing the existing Swordsoul/Salamangreat environment.

Until that gate closes:

```text
META8_BLOCKED_BY_FORMAT_INSTABILITY
EXACT_DECKLISTS_RECOMMENDED=NO
DECKLIST_LOCKED=NO
OCGFORGE_IMPLEMENTATION_AUTHORIZED=NO
TRAJECTORY_GENERATION_AUTHORIZED=NO
TRAINING_AUTHORIZED=NO
```

## Scope boundary

This revision changes documentation/research framing only.

It does not authorize or modify:

- production code;
- rules bundles or pinned card data;
- decks or fixtures;
- Teachers;
- trajectory generation;
- datasets;
- Behavior Cloning;
- RL or self-play;
- OCGForge-Ignis;
- the current Swordsoul Tenyi / Salamangreat curriculum.

The next Meta-8 action is a future M8-0 evidence refresh after the post-release window, not implementation.
