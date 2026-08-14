# M3 Acceptance Matrix

Recommendation: **M3 FINAL PASS**

Pinned bundle: `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f`
Mechanics: 45 classified / 45 required; 0 pending.
Mechanics classifications: ENGINE_VERIFIED=38; PROTOCOL_VERIFIED=7; PUBLIC_API_LIMITATION=0; NOT_APPLICABLE_FIXED_MATCHUP=0; PENDING=0.
Baseline before M3.1: OBSERVATION_VERIFIED=21; PROTOCOL_VERIFIED=7; PUBLIC_API_LIMITATION=1; PENDING_ENGINE_FIXTURE=16; total=45.
Fixed games: 16 complete / 16 requested; start-player partitions: `[0, 1]`.

## Gate rows

| Criterion | Area | Status | Evidence | Details | Blocker |
| --- | --- | --- | --- | --- | --- |
| M3-DECK-FOUNDATION | Deck foundation | PASS | FIXED_MATCHUP.md; card_compatibility.json | Exact ordered 40 Main / 15 Extra decks with no side cards. |  |
| M3-MANIFEST-HASHES | Deterministic manifests/hashes | PASS | card_compatibility.json; FIXED_MATCHUP.md | A=8ee4b699de19ff256e388d46f35b8696a60ff6ec59f0324f060a2468876711b7; B=6041abe0a59463d0715ae1da9100090ad487de02a02794e8ec0686d4c0513188. |  |
| M3-COMPATIBILITY-110 | 110-slot compatibility audit | PASS | card_compatibility.json; CARD_COMPATIBILITY.md | 110 ordered slots and 50 unique passcodes. |  |
| M3-BABELCDB | BabelCDB resolution | PASS | CARD_COMPATIBILITY.md | Every locked slot resolves to the declared pinned CDB card. |  |
| M3-CARDSCRIPTS | CardScripts resolution | PASS | CARD_COMPATIBILITY.md | All 50 unique effect-card script resolutions are pinned and PASS. |  |
| M3-STATIC-METADATA | Static metadata validation | PASS | card_compatibility.json | CDB names, types, zones, printed metadata, and script evidence are valid for all unique cards. |  |
| M3-INSTANTIATION | Instantiate every unique card | PASS | card_instantiation_test | The pinned-core runtime loads every unique passcode from both exact decks. |  |
| M3-PRIVACY | Privacy regression | PASS | m3_real_deck_privacy_test | Hidden opponent identities remain observation-equivalent while visible identities diverge. |  |
| M3-FIXED-GAMES | 16 complete fixed-deck games | PASS | artifacts/m3/canonical_mr5/full_games/full_fixed_deck_results.json | complete_games=16; required=16. |  |
| M3-CONFORMANCE-REJECTIONS | No unsupported/retry/automatic/truncated decisions | PASS | full fixed-deck summaries | All completed games report zero unsupported, MSG_RETRY, automatic, and candidate-truncation counts. |  |
| M3-START-PARTITIONS | Available start-player partitions | PASS | full_fixed_deck_results.json; public_api_gaps.json | Observed start_player_partitions=[0, 1]; mirrored deck-seat runs are present but do not change the initial turn player. |  |
| M3-RULE-MODE | Locked TCG duel-mode configuration | PASS | RULE_MODE_AUDIT.md; rules_mode_audit.json; third_party/rules_bundle.lock.json | format=TCG_ADVANCED_2026_05_18; canonical_duel_mode=DUEL_MODE_MR5; canonical_duel_flags=0x2E800; bundle=3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f. |  |
| M3-DETERMINISM-INDEPENDENT | Independent-process gameplay hashes | PASS | artifacts/m3/canonical_mr5/determinism/m3_determinism_results.json | start-0 gameplay=e19349b22796b18eaf1fb35cf34b0b2c95cbb0ad36c161376c1d431dd9798320; trace=afb6ee362c5cabf850c5ec4c7098a12981ce0d550be7658a28309090343457a8; start-1 gameplay=39df99786adc36af6c40aa6fc88f78c47947ea3861e01688eb4d8586cd46d680; trace=7a252b56a5f4b22b9e9a58f17be1414137614fc5f7e8ff95e84ff4069f214a36. |  |
| M3-DETERMINISM-REPLAY | Semantic-action re-execution | PASS | artifacts/m3/canonical_mr5/determinism/m3_determinism_results.json | start-0 actions=612; start-1 actions=621; CRLF replay verified for both partitions. |  |
| M3-BATTLE-PHASE | Complete Battle Phase execution | PASS | m3_fixture_test btl01; artifacts/m3/canonical_mr5/full_games/full_fixed_deck_results.json | Dedicated Battle Phase fixture proves attack target, damage, destruction, lethal, and MSG_WIN as one pinned-core path. |  |
| M3-RULES-BUNDLE | Canonical rules environment | PASS | third_party/rules_bundle.lock.json; verify_rules_bundle.py | The canonical rules environment includes the deterministic M3.5 repository patchset; ocgcore base commit, OCG API, CardScripts and BabelCDB pins remain unchanged, and no upstream checkout was modified. |  |
| M3-REGRESSION | M0/M1/M2 regression suite | PASS | artifacts/m3/final_verification.json | Build=PASS; CTest=85/85 passed; failed=0. |  |
| M3-XYZ-LIMITATION | Known Xyz material identity limitation | RESOLVED_BY_REPOSITORY_PATCHSET | m35_xyz_material_query_test; m2_1_xyz_api_test; public_api_gaps.json | The existing overlay_seq public contract resolves visible individual material identity; hidden paired-world material identities remain redacted. |  |

## Mechanics rows

| Criterion | Group | Status | Evidence | Card/path | Blocker | Observation hash |
| --- | --- | --- | --- | --- | --- | --- |
| SS-01 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss01 | Mo Ye normal summon, reveal, Token |  | 608e2bd1a7af99c4f08c3c902a9658abca6efe1eac3961927306542d95a55f56 |
| SS-02 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss01 | Swordsoul Token Synchro path into Chixiao |  | 608e2bd1a7af99c4f08c3c902a9658abca6efe1eac3961927306542d95a55f56 |
| SS-03 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss01 | Mo Ye and Chixiao simultaneous trigger ordering |  | 608e2bd1a7af99c4f08c3c902a9658abca6efe1eac3961927306542d95a55f56 |
| SS-04 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss01 | Chixiao search |  | 608e2bd1a7af99c4f08c3c902a9658abca6efe1eac3961927306542d95a55f56 |
| SS-05 | swordsoul | PROTOCOL_VERIFIED | m3_fixture_test ss05 | Chixiao negate | Pinned public observation has no separate negated-status field; target selection and chain resolution are proven. | c2eafc98223301fdf3eecb8f5250dc54b02aa0a2579e47a0876f212f6d57dc43 |
| SS-06 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss06 | Longyuan discard, Token, level-10 Synchro, burn |  | 2a89009a075580042af1ab3903af92458467702e096f7310f88d6afdfd819c61 |
| SS-07 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss07 | Swordsoul Token Extra Deck restriction and expiry |  | b595ee656db4da48f2a3ef51a3334c6893ce2efd891ec0ad23c627b3826400dc |
| SS-08 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss08 | Baxia multi-target shuffle/return |  | 68e5145ca56a44bce3d67a67ab368cd52b1aa7a6242e3cedd2fd27782f2bfee8 |
| SS-09 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss08 | Baxia destruction and revival |  | 68e5145ca56a44bce3d67a67ab368cd52b1aa7a6242e3cedd2fd27782f2bfee8 |
| SS-10 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss10_banish | Blackout constrained selection and banished-trigger path |  | ac883ab313622c62076d555ac40dd7f922197b8c0155d1f8b0a26c074e9a8093 |
| SS-11 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss12 | Heavenly Dragon Circle cost and search |  | 452b97207975988d46c763a524eca09110d1d81d44ba4c97f67a4fddd8a68ff7 |
| SS-12 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss12_condition | Tenyi no-effect-monster condition true and false |  | 6cc4a1d775978400181f4918ac5ef977ad22ccd8aefc36fea907c94429898010 |
| SS-13 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss14 | Ashuna restriction and expiry |  | 9aab457770f1f208bbc15d2fccbb233f407cf64b08ee862a4ebe5e173f45ebf5 |
| SS-14 | swordsoul | PROTOCOL_VERIFIED | m3_fixture_test ss15 | Vishuda activation and return | The opposing hand result is not identity-exposed by the pinned public projection. | 76d1594ccc8dea4f4540b7810e98018db4869daf3027965e266b5f2507e0ad5a |
| SS-15 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss16 | Adhara banished-Wyrm recovery |  | 8b1cbe87f3b6220b3bfd1ac50d1ae1133a66c3a454989be7f98b5a2dc013d248 |
| SS-16 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss16_chengying | Chengying dynamic state and banish trigger |  | ac883ab313622c62076d555ac40dd7f922197b8c0155d1f8b0a26c074e9a8093 |
| SS-17 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss17 | Qixing Longyuan interaction/chain path |  | 821e3956f4952c0cfebc7a7daa20e00cd2bee0c58f37cf55df4f4a00ff76894f |
| SS-18 | swordsoul | ENGINE_VERIFIED | m3_fixture_test ss18 | Monk/Shaman Link procedures and Tenyi Links |  | 44da5a3cadd2188999902c2ccf4c3f82900af9e591473735c11e971eb221c583 |
| SG-01 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg01 | Salamangreat of Fire to Balelynx |  | 77020cb8c29bc03fa655851511a0c7b5042f4e4c8b71de5e96b5040e4e4fc525 |
| SG-02 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg02 | Balelynx Sanctuary search |  | c1b874f23a96a4a5b079cb9ebe1c826bcb32161850f712e7ecd82c6d5454077b |
| SG-03 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg03 | Salamangreat Sanctuary Field Zone state |  | 15c7156bca72f4fee5928c80ab7467ad22a812cc344de9835feccca2e2a53583 |
| SG-04 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg04 | Sanctuary-enabled same-name reincarnation Link Summon |  | c1b874f23a96a4a5b079cb9ebe1c826bcb32161850f712e7ecd82c6d5454077b |
| SG-05 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg05 | Gazelle optional hand summon |  | e7907ba0f2ad7aae9f396da79c3ca345f5a5bb1badb63014fefb7bd5c7e42646 |
| SG-06 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg06 | Gazelle, Spinny, and Foxy GY engine |  | 8f8423312c0c517cde4fee170d4f9db3118548af14433f6494ff5f22ecfccd91 |
| SG-07 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg07_jack; m3_fixture_test sg07_weasel; m3_fixture_test sg07_falco | Jack Jaguar, Weasel, and Falco GY paths |  | b2efa022ee7d9e92b68fa75efdc5905d90a5e8e881afeaaa8e7ff429588fa3e7 |
| SG-08 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg08_real | Miragestallio Xyz legality |  | 99727821397ccea1325162f308031835c39d0a77bfc4fb9e19caa6d88ac53cb0 |
| SG-09 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg09_direct | Miragestallio detach, Deck summon, and FIRE restriction |  | e5756cd8c76208ebee996413ca105f29d0d0bf05e6f4f23a6abe7dd84ff8f4ab |
| SG-10 | salamangreat | ENGINE_VERIFIED | m35_xyz_material_query_test; m2_1_xyz_api_test; mechanics_projection_test | Miragestallio material identity |  | 84e264b9c655509ade9d905c43a6e609171acba178cdd118e3955aef4d0062a4 |
| SG-11 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg11 | Sunlight Wolf linked-zone recovery |  | 5395086f6234cb46ec9237c0392803f9754aeb7310b02c480bfd2410a5633ff6 |
| SG-12 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg12 | Sunlight Wolf Spell/Trap recovery |  | 010eac5086e23b3bd056cd146454eacb0dd405ba1eabaceeca0f5ad4d353d02b |
| SG-13 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg13 | Raging Phoenix normal/reincarnation/search |  | f49b3a6f12e316631db0359cdd0dcec0ae4676b4e54f7e966c7f7251679f6e8e |
| SG-14 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg14 | Pyro Phoenix reincarnation payoff (fixed Deck A branch) | Proven under MR5 0x2E800. Opponent-Link revival is a separate NOT_APPLICABLE_FIXED_MATCHUP subpath because the locked Deck A Extra Deck has no Link Monster. | 1888ddf7cae911a67dd7562723733cdd17257667813cc824b79c0d04079105e1 |
| SG-15 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg15 | Heatleo target and reincarnation |  | dcc2b241dae68390641ad51e4a1eaa6e8967e672599ec035e9694aa659beb5da |
| SG-16 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg16_negate; m3_fixture_test sg16_recovery | Salamangreat Roar negate/recovery |  | 747ffea3f27230cbe6f0257922e42c8a4bfecc00287c6ff56475cc18f85b7ab9 |
| SG-17 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg17 | Salamangreat Rage target-count/removal |  | c85da64c34f73767d2c5c1033d3c83917bed4f7b38a17540d5027148dfe1ea4e |
| SG-18 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg18 | Promethean Princess Link and FIRE revival (fixed matchup) | Proven under MR5 0x2E800. The non-FIRE restriction negative branch is a separate NOT_APPLICABLE_FIXED_MATCHUP subpath because the locked Salamangreat monster pool is FIRE-only and has no special-summon path for the non-FIRE hand traps. | c3090cfdc60874caed75882ab0be336f2a50e0b90e409bb91c4b16dcccdac2b9 |
| SG-19 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg19 | Hiita opponent-owned FIRE revival and no-target domain |  | e5803ff523812967c7fd2888d3cb227ae985a98a4a7e740edaf1c20178a6350b |
| SG-20 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg20 | Multi-material Link continuation PICK/PICK/FINISH |  | 39a65959f757c5fc60d0e1440db192321a7172e4c564613550dd6b3688d503db |
| SG-21 | salamangreat | ENGINE_VERIFIED | m3_fixture_test sg20 | Link material and zone continuations |  | 39a65959f757c5fc60d0e1440db192321a7172e4c564613550dd6b3688d503db |
| INT-01 | shared | PROTOCOL_VERIFIED | m3_fixture_test int01 | Ash Blossom | The public projection does not expose a separate negated-effect flag. | c03c7eadda4a1c1b57611377a7532c55bbf2743130399ffd7b76b1634aff2281 |
| INT-02 | shared | PROTOCOL_VERIFIED | m3_fixture_test int02 | Effect Veiler | The public projection does not expose a separate negated-effect flag. | eae27320c682cdb3a6eab55b2afd0048cb169ca53fb9438519edb58f8ccafac8 |
| INT-03 | shared | PROTOCOL_VERIFIED | m3_fixture_test int03 | Infinite Impermanence | The public projection does not expose a separate negated-effect flag. | 995c409046514aa8b64324c04cef4c24eb7ac6289876b5e1d0afe854dee88836 |
| INT-04 | shared | PROTOCOL_VERIFIED | m3_fixture_test int04 | Ghost Belle & Haunted Mansion | The public projection does not expose a separate negated-effect flag. | 232ebc5c4c04765fe7b4dfd92466fe936f12a99ad8cea9ed57339aeeadf16012 |
| INT-05 | shared | PROTOCOL_VERIFIED | m3_fixture_test int05 | Called by the Grave | Target and chain resolution are proven; the public projection does not expose a separate negated-effect flag. | fe2da1a7159f87d2b9ccd777eac017118eedbde10193bcb11ea15939ddf8fac2 |
| BTL-01 | shared | ENGINE_VERIFIED | m3_fixture_test btl01 | Complete Battle Phase execution |  | 6559c3dd371db01f393cb1de612a696d5f8eb57167e79ee11a584c72bd8a80ab |

The individual Xyz-material identity gap is closed by the narrow existing overlay_seq repository patch; hidden identities remain redacted.
The immutable base core and upstream CardScripts/BabelCDB sources remain unchanged; canonical execution uses the ordered derived patchset.
