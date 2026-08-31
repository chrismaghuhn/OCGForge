# OCGForge Phase 4C Acceptance

- schema_version: ocgforge.phase4c_acceptance.v1
- status: PASS
- source_base: cf5786c8c0b08140b997e6df2fa397cc41538020
- source_head: 9fe935531b63aaaf9535201dd4daf3f25e0f1a93

## Environment

- matchup_id: ocgforge.matchup.swordsoul_salamangreat.v1
- rules_bundle_id: 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
- format_id: TCG_ADVANCED_2026_05_18
- duel_mode: DUEL_MODE_MR5
- duel_flags: 190464

## Teacher identities

- producer: ocgforge.policy.teacher_core.v1
- sampling: ocgforge.policy.deterministic_lexicographic_argmax.v1

- profiles:
  - swordsoul: ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74
  - salamangreat: ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55

- bindings:
  - swordsoul: ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c
  - salamangreat: ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56

- policy_artifacts:
  - swordsoul: policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d
  - salamangreat: policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527

## Battle contracts

- snapshot_schema: ocgforge.public_battle_snapshot.v1
- lethal_schema: ocgforge.provable_lethal.v1
- integration_decision: TEACHER_V1_PLUS_EVALUATION_SIDECAR
- positive_lethal_capability: BLOCKED_BY_ACCEPTED_CURRENT_ACTION_CONTRACT

## Gates

| Gate | Status |
| --- | --- |
| P4C-G00 | PASS |
| P4C-G01 | PASS |
| P4C-G02 | PASS |
| P4C-G03 | PASS |
| P4C-G04 | PASS |
| P4C-G05 | PASS |
| P4C-G06 | PASS |
| P4C-G07 | PASS |
| P4C-G08 | PASS |
| P4C-G09 | PASS |
| P4C-G10 | PASS |
| P4C-G11 | PASS |
| P4C-G12 | PASS |
| P4C-G13 | PASS |
| P4C-G14 | PASS |

## Fixed matchup matrix

| Seat assignment | Starting player | Status | Records | Battle records | Battle candidates |
| --- | ---: | --- | ---: | ---: | ---: |
| normal | 0 | PASS | 32 | 4 | 8 |
| normal | 1 | PASS | 32 | 4 | 8 |
| mirror | 0 | PASS | 32 | 4 | 8 |
| mirror | 1 | PASS | 32 | 4 | 8 |

## Task-5 metrics

- record_count: 128
- battle_decision_record_count: 16
- battle_command_candidate_count: 32
- battle_coverage: PASS
- sidecar_invalid_count: 0
- proven_lethal_count: 0
- lower_bound_present_count: 0
- sidecar_influences_gameplay: NO

## Command evidence

| Label | Status | Exit | Expected | Observed | stdout SHA-256 | stderr SHA-256 |
| --- | --- | ---: | ---: | ---: | --- | --- |
| source-head | PASS | 0 | None | None | 1fdd0a6067e1e627f90753816cabc4ffc0a8a3cd29042b4c64c05277607aec0b | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| source-base | PASS | 0 | None | None | 450d94684c52a1a4e974b3126b5ad936b86a9c9bb76e5f6c39e67a6ecd04535d | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| dev-configure | PASS | 0 | None | None | dc40bafa5b764ba388d8db7ea07a85d9c023b92e54654a3380ac2f267d7440e2 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| dev-build | PASS | 0 | None | None | 71aa391ec7d7e291b419a7f40fba91919e14ac2394ab4e8b66ed126e267d70f6 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-public-boundary-ctest-cardinality | PASS | 0 | 1 | 1 | b9b817967c2454306e414651e604f34832ed91f2ae153941085e8d3da9f50f8f | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-public-boundary-ctest | PASS | 0 | 1 | 1 | fd1738bd9056cca46f4596089df5a8e321e4b70bce5575711ae30145b2a1439e | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-public-boundary-python | PASS | 0 | None | None | 18154bc9b7ec2bbc2017020a5812ff893b3c801dfa560d4d70111359e576eed4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-lethal-boundary-ctest-cardinality | PASS | 0 | 1 | 1 | 2bcb4ad619f93d4383adb9fbe93832bc0866d9211abc8af54126db894a73fbb0 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-lethal-boundary-ctest | PASS | 0 | 1 | 1 | ec64e05445e85b37fd61307838a14fb33585309720adad5a35b0f3698af0758f | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g01-03-snapshot-ctest-cardinality | PASS | 0 | 1 | 1 | 53b744a1c8c27cbe11d13665f78c104fd631ecf2ee26f7519a398fc0b63a37b6 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g01-03-snapshot-ctest | PASS | 0 | 1 | 1 | 2f7a60a98bbf879e8592c4320cf973baac8f124b111f346ef1ddb1b80b5a45f3 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g04-battle-shape-ctest-cardinality | PASS | 0 | 1 | 1 | f253ce3aea1a8c32e15e968d6a24d995666ec1567a69139f9ea7a79b3a2b493e | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g04-battle-shape-ctest | PASS | 0 | 1 | 1 | 7be31ee464b0e40d357c572d57a1e2c7ddc404799e3f3683c5155786c7d9188d | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g05-battle-determinism-python | PASS | 0 | None | None | 32489721b2dd838927cab44b057852a0ab17f17f669b4751202fe7f39bbc2fa5 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g06-battle-paired-world-python | PASS | 0 | None | None | b76983696f87939462da7f7013677a1f9ed8d59914613d8780536bfa2dca5aa2 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g07-10-lethal-ctest-cardinality | PASS | 0 | 1 | 1 | 7ca39ce01e6579df9ca12a69eaf10fa165c7c0109941753930e9010c3dfa4e7b | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g07-10-lethal-ctest | PASS | 0 | 1 | 1 | 11bd279ad026c90db14fa25a19c5c243b0bd37b2483631f24a86fe9b6dabb515 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g11-identity-ctest-cardinality | PASS | 0 | 1 | 1 | 463868869dadbfa076a7ce43e652c36524915c44d8942d2c3d39b65249e79700 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g11-identity-ctest | PASS | 0 | 1 | 1 | e95eea55be10b35652e9af852002c83e409287d746e4d3c6352c03d3c565ebbf | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g12-trajectory-ctest-cardinality | PASS | 0 | 2 | 2 | 249599c80f23008f1fd6a8bcf7ea610590468f5b4c8236f844dc719006fd2f44 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g12-trajectory-ctest | PASS | 0 | 2 | 2 | c6f49b28fd83c2c2d5d244ee707e0eb8638bbc665c00ff28635e60ff839c16e3 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g13-rules-python | PASS | 0 | None | None | 795fa054bf05c7af9b5a2542ece0680009724f68c12950fafed1dac48d336a41 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g14-teacher-regression-ctest-cardinality | PASS | 0 | 4 | 4 | 01a509a28d2376d6971216737f47a3006195d81d560381cb8275730718d34f45 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g14-teacher-regression-ctest | PASS | 0 | 4 | 4 | 86707ab9ed8de4381dffe22574b7a692363bf1f7943f029fd51f540d88468c9c | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| task5-trajectory-ctest-cardinality | PASS | 0 | 1 | 1 | b95f9ef37b6a9a9d11e316df797afafd908e7fb5bfb81cf0c1098123773994b4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| task5-trajectory-ctest | PASS | 0 | 1 | 1 | e367dd39cf2918ec15b3013870c2d5da9e71645157969b249584bdd239759bdc | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| task5-evaluation-selftest | PASS | 0 | None | None | 23821a3c49a9f0c238aee92014aa9c3f0483d3f415014a94d2e16e100064a578 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| task5-evaluation-orchestrator | PASS | 0 | None | None | 9fe16335f8919479a8a049ef3f5c21a051b22bfcb2721171c6c53576f28b9843 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| fixed-row-normal-0 | PASS | 0 | None | None | a32aca508539417ce214f5ff3598a20eb05ded8c56483e1b637e4cbb85248c23 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| fixed-row-normal-1 | PASS | 0 | None | None | 1179c74c29f756f467e80d4a99dccce62044f548ce29b6b9e8a3f75f88de31b4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| fixed-row-mirror-0 | PASS | 0 | None | None | 7a4377af70e2234400cc77a8d142a14ecae8ce99bbd4350f3e047cafcc2e820b | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| fixed-row-mirror-1 | PASS | 0 | None | None | f17c65e04e03caa6e64b0c093d5da9321aeccf04f1f98336ffe7ca4cf706049a | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| phase4b-full-teacher-ctest-cardinality | PASS | 0 | 10 | 10 | 60605fc272856745ec812ff11672cd225c9266733ec8b887b1dd34a97be55ac7 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| phase4b-full-teacher-ctest | PASS | 0 | 10 | 10 | 160d6cb5eaae489509cbd4ec32da97fb4d7f607cc82714fc088c37e879dcdf4e | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| phase4b-public-boundary-python | PASS | 0 | None | None | 48a060cc5fa80f2ec5713e0a03aaf70ea29c6dc606b0ef6b09e541b4c85b6e9d | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| phase4b-paired-world-python | PASS | 0 | None | None | 76168866eb102b16558b08446f89c99386734c0c6a0beef6edf14c9470c81013 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| phase4b-profile-binding-python | PASS | 0 | None | None | e3db72b46d5c44a86e7d965ee137707d879032732c93ad4a82038380b37d19f8 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| phase4b-determinism-python | PASS | 0 | None | None | 9891d6cb3c3ac710c7e7fd3a968916ead8f2402b9e1d5d255e1608752ab8e382 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| policy-boundary-python | PASS | 0 | None | None | c505edd816513c8ee27f20d5721095e1d4e829dd300aee3f5da7a32157bc6e20 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| public-fact-matrix-python | PASS | 0 | None | None | 99ca49bcc9012c623592ea88daa4b7f7b1fcd256db462317466bafac330cf8d0 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| trajectory-short-ctest-cardinality | PASS | 0 | 5 | 5 | 073000e74c8faf31dfe2dafe1f66220c82fac6a279b6c5d382828889aa881644 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| trajectory-short-ctest | PASS | 0 | 5 | 5 | f17b87ae08f00fd4c2a789d06107d225701b96b9da4baf5b8a62b7c176e19bc1 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| repository-python | PASS | 0 | 15 | 15 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 | 40beb653f198339fc6454212f5f34a363d912065a1f703bddda07afca25ec43d |
| h-exec-diff-check | PASS | 0 | None | None | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| acceptance-validator-self-test | PASS | 0 | None | None | ba70c71d3f325bf9422a82f74eb9f96cb9dfed8e9a275ace0bd56cf9b0c18ba4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |

## Heavy evidence

- current_head_heavy_replay: NOT_RUN
- phase4a_h_exec: 66d30967018c6ce106d131c7e02147ffaf194a56
- phase4a_h_evidence: 6ab937a61ac39eecfb7bc91174ca0ba92b3edd09
- mode: frozen_baseline_reference

## Scope limitations

- fixed certified matchup only
- Teacher v1 remains gameplay policy
- Battle/Lethal are evaluation-only sidecar evidence
- positive lethal is blocked under the accepted current-action contract
- no general provable-lethal claim
- no complete battle-resolution claim
- no arbitrary-deck battle-intelligence claim
- no Teacher-v2 claim
- no ML claim
- no Phase 5 claim
