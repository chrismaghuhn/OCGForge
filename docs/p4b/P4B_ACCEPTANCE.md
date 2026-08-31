# OCGForge Phase 4B Acceptance

- schema_version: ocgforge.phase4b_acceptance.v1
- status: PASS
- source_head: cd00c3d34cc41c50ac1e7730a26a0e532cd21902
- source_base: 8f0e3465a09de69707dcabfeec30c4681aa1fa2e

## Environment

- matchup_id: ocgforge.matchup.swordsoul_salamangreat.v1
- rules_bundle_id: 3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f
- format_id: TCG_ADVANCED_2026_05_18
- duel_mode: DUEL_MODE_MR5
- duel_flags: 190464

## Teacher identities

- producer: ocgforge.policy.teacher_core.v1
- deterministic_sampling: ocgforge.policy.deterministic_lexicographic_argmax.v1

- profiles:
  - swordsoul: ocgforge.strategy_profile.v1.7a96ab091b52b8988a6873beb3b7d58575d5ea6f0e0aa7bf5059a1c87a748f74
  - salamangreat: ocgforge.strategy_profile.v1.3499e34962230eda64e9ef52af53433272cda5ca45ffae61258e0809dbfefa55

- bindings:
  - swordsoul: ocgforge.teacher_policy_binding.v1.4f78a100a75f98b8c5a7845198984a8ea34db8b6a75b6fde396c19d2b3ca6d0c
  - salamangreat: ocgforge.teacher_policy_binding.v1.ecbf2ae56dab29e93f319399a08930a3700466cd3d9ab553ef964fc109846c56

- policy_artifacts:
  - swordsoul: policy_artifact.v1.52f56b550a2a674430439d3db104a0b2281b69df79891573e4d71967e3d4310d
  - salamangreat: policy_artifact.v1.a68642ee28f0dd53ebe4908994664f178b3d5cea6fb7c06421990729cd9c4527

## Gates

| Gate | Status |
| --- | --- |
| P4B-G00 | PASS |
| P4B-G01 | PASS |
| P4B-G02 | PASS |
| P4B-G03 | PASS |
| P4B-G04 | PASS |
| P4B-G05 | PASS |
| P4B-G06 | PASS |
| P4B-G07 | PASS |
| P4B-G08 | PASS |
| P4B-G09 | PASS |
| P4B-G10 | PASS |
| P4B-G11 | PASS |
| P4B-G12 | PASS |
| P4B-G13 | PASS |
| P4B-G14 | PASS |
| P4B-G15 | PASS |
| P4B-G16 | PASS |
| P4B-G17 | PASS |
| P4B-G18 | PASS |

## Fixed matchup matrix

| Seat assignment | Starting player | Status |
| --- | ---: | --- |
| normal | 0 | PASS |
| normal | 1 | PASS |
| mirror | 0 | PASS |
| mirror | 1 | PASS |

## Command evidence

| Label | Status | Exit | Expected | Observed | stdout SHA-256 | stderr SHA-256 |
| --- | --- | ---: | ---: | ---: | --- | --- |
| source-head | PASS | 0 | None | None | 792aa8220a3bc122afb58deaf1e878db646296c70698c669cfec7b00af1df663 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| source-base | PASS | 0 | None | None | 8d62d58d84b88464911b5032f08ea47455503cd6f15688bccd41b2f79ccc9516 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| dev-configure | PASS | 0 | None | None | 0ef6342c464b50e6f31c1ec0727f1bcf4b4067fa5648a1258a981573c12bb16f | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| dev-build | PASS | 0 | None | None | 8ec5e685df645501bdcc0382587512dabcdee9d84445cbd65bc2ebecd9029aa5 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| profile-registry-probe | PASS | 0 | None | None | 3437a2b45731f86563129b11632d33d560a0b1de8527f004b3e1d11f2fb0b573 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-ctest-cardinality | PASS | 0 | 1 | 1 | 9e9fda06d13b12a94e9df0378b85e2f464792ab2e473ec7330a152c46048da80 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-ctest | PASS | 0 | 1 | 1 | 5c07387aa6fc6ba1eb616f83b50fc3e7ddfe7645d07fe4b13c6961aa7a60174e | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g01-ctest-cardinality | PASS | 0 | 1 | 1 | 278768c2daf9f4bf37c4309cafda35d203eee410edc66c1a70c5eda2c7ba7d3f | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g01-ctest | PASS | 0 | 1 | 1 | e998b14f8bb9de60ac508d7533e7af5859a85cc0c1526dcbd936257250617791 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g02-ctest-cardinality | PASS | 0 | 1 | 1 | 063f47da4e4cd6d7c3c77a73ba56668fb131d12007a467f5e14fb841a37bf1ad | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g02-ctest | PASS | 0 | 1 | 1 | 2f7a4d88a9c0a26f17381aabe053fd6e8d61763faf0040cf8f8f9980a822f533 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g04-ctest-cardinality | PASS | 0 | 1 | 1 | 51f97abb3945d646b99d642a1f6f595a114147b534ca4c31af98398f053c013d | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g04-ctest | PASS | 0 | 1 | 1 | 62932d3ff2bd96a4c1d1b1f04ca5350f848224ebcc695fef4be7fb6d107f3405 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g05-ctest-cardinality | PASS | 0 | 1 | 1 | e22d6f3e8803df5704631db9ab61814216cc4baab4ae714670b20c3c4712eb1c | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g05-ctest | PASS | 0 | 1 | 1 | 487d2db5384ffe5b66fa1a42ce5c561db4f8d9929f1a0cd1c4a6cbb9756194aa | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g07-ctest-cardinality | PASS | 0 | 1 | 1 | c342be68870ec1b868f46884c929fba9d1146d3c861acbce917630bc807f68e4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g07-ctest | PASS | 0 | 1 | 1 | 8622cbde029065b3856e9b81945753bca9b466a614c74437ca25ec62d9506a96 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g08-ctest-cardinality | PASS | 0 | 1 | 1 | c1328ed48003ae1cb09f551399e13b0e1030c7b5c913b8c5132535d24b6fefca | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g08-ctest | PASS | 0 | 1 | 1 | 754c9ba7a5bbe74ace1aadbf438ebceae04a6a8ce61dcb467eebc7cf0e77b30e | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g09-ctest-cardinality | PASS | 0 | 1 | 1 | 43b93687d6c12793493b4fc408d0ffabdc79d85345befb70e61aec1af8c02226 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g09-ctest | PASS | 0 | 1 | 1 | a7f4a3b655b17c577e074c48965c1e393925047124d240fdadffd15b85b261fa | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g11-ctest-cardinality | PASS | 0 | 1 | 1 | cb9d222754c39ac73fa771a211a3d764b8d9311d0f5d9c19b9ee54fa57158203 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g11-ctest | PASS | 0 | 1 | 1 | e9bbf3e3d5623202014c253867d6502544f84c820db824c3a432be54e697c0da | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g12-ctest-cardinality | PASS | 0 | 1 | 1 | a90869875a7f52a5b9f0f46c2c728529e86a4d874941997cecfbe87b36ce9c61 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g12-ctest | PASS | 0 | 1 | 1 | b73037c0a2bd9b1d07d182addcfafedce647cbd268961d183bac7a1edc29194c | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g13-ctest-cardinality | PASS | 0 | 1 | 1 | 5b20815c43d6a4c8cd8090c4922add0986f88734c3401db38e220f4f30ce4072 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g13-ctest | PASS | 0 | 1 | 1 | a4efe4fa89fc10ad06c0659cb6dcd2ef82622443559e5931974ab2936e82c55d | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g15-ctest-cardinality | PASS | 0 | 1 | 1 | 3ff67b15722a02fb6c4ed1f881344fae157271ae25136ddfe576fdeab52f26b4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g15-ctest | PASS | 0 | 1 | 1 | 1e7df9dec141f065494f0e326e0fb968e1a40e3855a65581c57e05d24b5178c6 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g16-ctest-cardinality | PASS | 0 | 1 | 1 | 7eb7c6386eaef425bc10d44e4b910ef1f925694687b32fe077030d020882fd08 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g16-ctest | PASS | 0 | 1 | 1 | 83eb57fa36f928c9762b7dba4f825c724329302dbba0e7cfa0884aee70f76e79 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g17-ctest-cardinality | PASS | 0 | 1 | 1 | 70aeda0a77edf4026bca46a89871e4433ef2c1b8dd8c0a2e785dd67b03bb7782 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g17-ctest | PASS | 0 | 1 | 1 | 90b29ba80732d4f3f07ad8c9c0fb56b98f81566e4427acc2399fe0e5a9484c69 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g14-ctest-cardinality | PASS | 0 | 6 | 6 | 9007acefc9707c12a6ced0e2ea40550fd239a023e1f98e549d061671f0bd9366 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g14-ctest | PASS | 0 | 6 | 6 | 644fbebc8eb07a3c844f1e4db894def2ba89c7db86727721ea8d0dbd5496aed6 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g00-python | PASS | 0 | None | None | 48a060cc5fa80f2ec5713e0a03aaf70ea29c6dc606b0ef6b09e541b4c85b6e9d | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g03-python | PASS | 0 | None | None | 76168866eb102b16558b08446f89c99386734c0c6a0beef6edf14c9470c81013 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g06-python | PASS | 0 | None | None | e3db72b46d5c44a86e7d965ee137707d879032732c93ad4a82038380b37d19f8 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g10-python | PASS | 0 | None | None | 9891d6cb3c3ac710c7e7fd3a968916ead8f2402b9e1d5d255e1608752ab8e382 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g14-policy-boundary-python | PASS | 0 | None | None | c505edd816513c8ee27f20d5721095e1d4e829dd300aee3f5da7a32157bc6e20 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g14-public-fact-matrix-python | PASS | 0 | None | None | 99ca49bcc9012c623592ea88daa4b7f7b1fcd256db462317466bafac330cf8d0 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| g18-python | PASS | 0 | None | None | 174212f2888a45ea46d79bfd78189f4691878e1f0a891c1269b57cf7b6557678 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| profile-scenarios-ctest-cardinality | PASS | 0 | 4 | 4 | fb102cbbd26ff0db37a5f37d7cf81196f549886d46164eaaca97af53f15f12ed | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| profile-scenarios-ctest | PASS | 0 | 4 | 4 | 4b89ec63ff91365c8e337887a7244e1f3699b5808d12ab34c7354c96efda1bc9 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| trajectory-short-ctest-cardinality | PASS | 0 | 2 | 2 | e9855ae0ca38968d7f0e43ac8ba020e646529caadea33f7d238fb064099c64ad | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| trajectory-short-ctest | PASS | 0 | 2 | 2 | 4b29da1f51aeaa254a3453adac0cb27158f8b68215ac79785b62e92f93f9c3d4 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| rules-deck-python | PASS | 0 | None | None | 795fa054bf05c7af9b5a2542ece0680009724f68c12950fafed1dac48d336a41 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| repository-python | PASS | 0 | 15 | 15 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 | d234d10601990be5519f25c9a30ce6cb4404f54321d720716f5b5d6fa61007f0 |
| fixed-matrix-probe | PASS | 0 | None | None | 228676e8eb557da6e7884e03189273026f5dc1845f5f1f3b2f7a92b5a55d95ca | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| h-exec-diff-check | PASS | 0 | None | None | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |
| acceptance-validator-self-test | PASS | 0 | None | None | 71517f8ea470ecc50391b2f379cff3ec95f98fdc24381099868219ac1cb213a8 | e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855 |

## Heavy evidence

- current_head_heavy_replay: NOT_RUN
- phase4a_h_exec: 66d30967018c6ce106d131c7e02147ffaf194a56
- phase4a_h_evidence: 6ab937a61ac39eecfb7bc91174ca0ba92b3edd09
- mode: frozen_baseline_reference

## Scope limitations

- fixed matchup only
- no arbitrary-deck claim
- F2 profile utility remains unsupported/fail-closed
- F3 generic tactical utility remains unsupported/fail-closed
- copy-budget-dependent strategy remains fail-closed where public proof is absent
- safe-stop/lethal remain omitted where unprovable
- no Phase 4C claim
- no ML claim
