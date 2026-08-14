# M3.1 Fixed-Deck Mechanics Closure

This file is maintained from `m3_1_closure.json`. Every row in the authoritative baseline inventory has been explicitly reconsidered. The old status is retained for auditability; `new_status` is the final M3.1 classification.

Baseline: `artifacts/m3/m3_1_baseline.json`
Pending after closure: **0**
Historical rule-mode note: M3.1 Link fixtures used the pinned core's explicit MR5 test mode `0x2E800`; the then-locked `duel_flags=0` mismatch was retained as a separate configuration blocker. M3.2 resolves that blocker and re-runs the fixtures under the canonical lock-backed MR5 mode.

| ID | Deck | Passcode(s) | Old status | New status | Closure result | Evidence |
| --- | --- | --- | --- | --- | --- | --- |
| SS-10 | Swordsoul | 14821890 / 20001444 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test ss10_banish`, `m3_ss10_blackout_banish` |
| SS-13 | Swordsoul | 87052196 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test ss14`, `m3_ss14_ashuna_restriction_expiry` |
| SS-16 | Swordsoul | 96633955 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test ss16_chengying`, `m3_ss16_chengying` |
| SS-17 | Swordsoul | 47710198 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test ss17`, `m3_ss17_qixing_interaction` |
| SG-07 | Salamangreat | 56003780 / 57357130 / 20618081 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | Jack, Weasel, and Falco fixtures; `m3_sg07_gy_paths` |
| SG-08 | Salamangreat | 87327776 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg08_real`, `m3_sg08_miragestallio_xyz` |
| SG-09 | Salamangreat | 87327776 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg09_direct`, `m3_sg09_miragestallio_effect` |
| SG-11 | Salamangreat | 87871125 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg11`, `m3_sg11_sunlight_wolf` |
| SG-12 | Salamangreat | 87871125 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg12`, `m3_sg12_sunlight_wolf_st` |
| SG-13 | Salamangreat | 57134592 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg13`, `m3_sg13_raging_phoenix` |
| SG-14 | Salamangreat | 31313405 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg14`, `m3_sg14_pyro_phoenix` |
| SG-15 | Salamangreat | 41463181 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg15`, `m3_sg15_heatleo` |
| SG-16 | Salamangreat | 51339637 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | Negate + recovery fixtures; `m3_sg16_roar` |
| SG-17 | Salamangreat | 14934922 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg17`, `m3_sg17_rage` |
| SG-18 | Salamangreat | 2772337 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | `m3_fixture_test sg18`, `m3_sg18_promethean` |
| SG-19 | Salamangreat | 48815792 | PENDING_ENGINE_FIXTURE | ENGINE_VERIFIED | ENGINE_VERIFIED | Positive + no-target fixtures; `m3_sg19_hiita` |

Explicit subpath classifications:

- `SG-14` opponent-Link revival: `NOT_APPLICABLE_FIXED_MATCHUP`, because the exact locked Swordsoul Extra Deck has no Link Monster. Evidence: `fixtures/decks/swordsoul_tenyi_ml_v1.ydk`, `official/c31313405.lua`.
- `SG-18` non-FIRE special-summon restriction negative branch: `NOT_APPLICABLE_FIXED_MATCHUP`, because the locked Salamangreat monster pool is FIRE-only and its non-FIRE hand traps do not provide a special-summon candidate. Evidence: `.cache/derived/fixture_card_data.tsv`, `official/c2772337.lua`, `fixtures/decks/salamangreat_ml_v1.ydk`.

Neither subpath is used to mark a difficult reachable fixture green, and Jack Jaguar/other Link-zone mechanics were not classified N/A because of the pre-M3.2 `duel_flags=0` configuration.
