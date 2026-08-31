# Phase 4A Acceptance

| Field | Value |
|---|---|
| Status | `PASS` |
| Source HEAD | `66d30967018c6ce106d131c7e02147ffaf194a56` |
| H_exec | `66d30967018c6ce106d131c7e02147ffaf194a56` |
| Acceptance profile | `h-exec-fast-normal-plus-heavy` |
| Heavy evidence | `executed-at-h-exec` |
| Rules bundle | `3adfe6b4cfe2c2805e50b389fc0eb4e70a3b0b6107436614d328fddc865e585f` |

## Gate matrix

| Gate | Status | Exact evidence |
|---|---|---|
| P4A-G00 | `PASS` | public-fact-matrix |
| P4A-G01 | `PASS` | debug-configure, debug-build, debug-normal-ctest |
| P4A-G02 | `PASS` | repository-python, m3-python, m4-python |
| P4A-G03 | `PASS` | release-configure, release-build, release-normal-ctest, heavy-replay-release, heavy-lifecycle-stress, m4-canonical-full-game, m4-recommended-concurrency-soak |
| P4A-G04 | `PASS` | rules-deck-identity, rules-bundle-verification |
| P4A-G05 | `PASS` | focused-policy-ctest |
| P4A-G06 | `PASS` | focused-policy-ctest |
| P4A-G07 | `PASS` | focused-policy-ctest |
| P4A-G08 | `PASS` | focused-policy-ctest, policy-boundary |
| P4A-G09 | `PASS` | focused-policy-ctest |
| P4A-G10 | `PASS` | focused-policy-ctest |
| P4A-G11 | `PASS` | focused-policy-ctest |
| P4A-G12 | `PASS` | focused-policy-ctest |
| P4A-G13 | `PASS` | focused-policy-ctest |
| P4A-G14 | `PASS` | focused-policy-ctest |
| P4A-G15 | `PASS` | focused-policy-ctest |
| P4A-G16 | `PASS` | focused-policy-ctest |
| P4A-G17 | `PASS` | focused-policy-ctest |
| P4A-G18 | `PASS` | production-identity-source-scan |
| P4A-G19 | `PASS` | focused-policy-ctest |
| P4A-G20 | `PASS` | focused-policy-ctest |
| P4A-G21 | `PASS` | policy-determinism |
| P4A-G22 | `PASS` | focused-policy-ctest |
| P4A-G23 | `PASS` | policy-determinism |
| P4A-G24 | `PASS` | focused-policy-ctest |
| P4A-G25 | `PASS` | focused-policy-ctest |
| P4A-G26 | `PASS` | heavy-replay-release |
| P4A-G27 | `PASS` | focused-policy-ctest |
| P4A-G28 | `PASS` | public-fact-matrix |
| P4A-G29 | `PASS` | clean-checkout-reproduction |

## Command evidence

| Label | Result | Exit | Counts | Output SHA-256 |
|---|---|---:|---|---|
| debug-configure | `PASS` | 0 | - | `b72e50177ecc143e9252b6f00d724bd470e4176f1c6376f6ccd37fde181bd937` |
| debug-build | `PASS` | 0 | - | `50c2ccef9cc15f9339d9d77e014d38b2ef775903fb14c3144ac6c0d4da7749db` |
| focused-policy-ctest | `PASS` | 0 | 12/12 | `f4a7d9e744d0c2ffb6ad89b0b82ca9efb1db091d9d2cc6b8db7321e10fb2163f` |
| debug-normal-ctest | `PASS` | 0 | 127/127 | `c23acbae6353afe5c9f7eb25d8d71565c2ef357ac479cad75e4a6c7816134801` |
| release-configure | `PASS` | 0 | - | `5b348d390984e3b89df0cafdc2ac731c855ce99ebcbb2c09dd020a4d59b50a39` |
| release-build | `PASS` | 0 | - | `50c2ccef9cc15f9339d9d77e014d38b2ef775903fb14c3144ac6c0d4da7749db` |
| release-normal-ctest | `PASS` | 0 | 127/127 | `ee0670a3968f4f69ed951898ee7a1d3d38c23303e8a7c654191ac9b761bc13b2` |
| heavy-replay-release | `PASS` | 0 | 1/1 | `9357195c082390ed8731dd78a834623389048346f14f336aa9defaf98b2ab945` |
| repository-python | `PASS` | 0 | 15/15 | `c7b4b4e2b7acd49127f55696504340280f41b5023f4fc9d2f64683a6a8cb8d60` |
| m3-python | `PASS` | 0 | 17/17 | `b0c6d6a51f31c54a543326694dadc920cb73f96e4129a002501630033ef415b2` |
| m4-python | `PASS` | 0 | 127/127 | `852511a2fb08deb4993a2310ecdf3e3b742581bc61f751547ac74d46bd55ec18` |
| public-fact-matrix | `PASS` | 0 | - | `083ea6086bc6b0e689e848a2ea219490a210af991a24560bf07ae11e12bc601f` |
| rules-deck-identity | `PASS` | 0 | - | `7f116ce2afa6df65374ff59f7abd1b85ff0a24d1a2356b02bf7de5ef9237e5e0` |
| policy-determinism | `PASS` | 0 | - | `046199898490ae22f2d251ee72f9eb06824fb2721fb51d214ae6e2b9227acefc` |
| policy-boundary | `PASS` | 0 | - | `a63dc12bda566c2aaeb6231a6d6070deb104cefe5e391987e0d0742c4a9864eb` |
| rules-bundle-verification | `PASS` | 0 | - | `c64624f10ffd919dc3fbc329d6bf2a242451594af4d31e7a1450e3ca6bb37315` |
| production-identity-source-scan | `PASS` | 0 | - | `69d1e5bdd9943041f66b8b396eec09d8f325685697e43c0b34532de68a149b52` |
| m4-canonical-full-game | `PASS` | 0 | - | `d2017531bda0c6604b0c702120f3d2cf1fefe3d11a192a5f5da9b1bf15b31a1a` |
| heavy-lifecycle-stress | `PASS` | 0 | - | `371cb0edacf31e3aaf0c44d85b1a99c5abbd21bbf77db6fad367b56515974364` |
| m4-recommended-concurrency-soak | `PASS` | 0 | - | `2f0cf9381418024189d11a65ac208af6e64bc592a8c9948aa12800ab239e541b` |

## Generated artifact hashes

| Path | Bytes | SHA-256 |
|---|---:|---|
| `artifacts/p4a/h-exec/m4/full_game/full_fixed_deck_results.json` | 28374 | `81437213a66dc548dd85cf6f2145a3728c4b3b4b811eb41bf69e5cdcd86045b6` |
| `artifacts/p4a/h-exec/m4/full_game/seed-1-mirror-start-0.jsonl` | 697913 | `efaeb06f27d12a3b08558c455e348b3549f017bc688f036a9050620bd32bbb56` |
| `artifacts/p4a/h-exec/m4/full_game/seed-1-mirror-start-1.jsonl` | 687868 | `dce4d872e41dde0a1136ba4ca56839ce9e72b8e11fbfb5a1c7298cb244860ed0` |
| `artifacts/p4a/h-exec/m4/full_game/seed-1-normal-start-0.jsonl` | 687868 | `84d6e8860fd642d713ba138c68c8199ed159ddc5069d972c0009c7d6d10a11cb` |
| `artifacts/p4a/h-exec/m4/full_game/seed-1-normal-start-1.jsonl` | 697913 | `937f8e0d40d330727d34f9394991c314ebc09aeb9e524bb1035ef5bae9e77a83` |
| `artifacts/p4a/h-exec/m4/full_game/seed-2-mirror-start-0.jsonl` | 697706 | `a9c98e8724ffaa8238ec025d265913ab331c9e906cb2bea65b796e02a32e071a` |
| `artifacts/p4a/h-exec/m4/full_game/seed-2-mirror-start-1.jsonl` | 687661 | `bc33ac87e3baf90bfae07068cbf7faa32c43b959087808f94aa0bb04c7d73afc` |
| `artifacts/p4a/h-exec/m4/full_game/seed-2-normal-start-0.jsonl` | 687661 | `4a0961ae76f9bb4310b11b4aaa4f434cfe28a4bcbec549489776c7926271cabc` |
| `artifacts/p4a/h-exec/m4/full_game/seed-2-normal-start-1.jsonl` | 697706 | `3c2391c53b9c261ec5f9e193ba8f9c522fff53ef57c51d97e148c5ffc5c268d5` |
| `artifacts/p4a/h-exec/m4/full_game/seed-3-mirror-start-0.jsonl` | 697557 | `4d797e3427330d9b1428ec7199174ba2ef1e28d76e7366136f10c1bf7189cb05` |
| `artifacts/p4a/h-exec/m4/full_game/seed-3-mirror-start-1.jsonl` | 687512 | `669e15c67ec58c0c2a2b90f62b6de1ccefb4ac8ac30c2a36ceba7b7fead3484b` |
| `artifacts/p4a/h-exec/m4/full_game/seed-3-normal-start-0.jsonl` | 687512 | `c529ab248ea6262a946ad0b11b7be0a5d0260159812015001b5716230285cb60` |
| `artifacts/p4a/h-exec/m4/full_game/seed-3-normal-start-1.jsonl` | 697557 | `9ea4d6c5a478b0bd56b63824345b36b4678f61aecd92f5f80ce2205bfce6d465` |
| `artifacts/p4a/h-exec/m4/full_game/seed-4-mirror-start-0.jsonl` | 697504 | `72b9bf1aa286ff88b3e1ed37017d1bd39bf407b0a780f7a56ea9d700cccbada8` |
| `artifacts/p4a/h-exec/m4/full_game/seed-4-mirror-start-1.jsonl` | 687460 | `f180c03799d38c720dd5bbcd60a59b67282aedbf5f8e8b2b0535c2aa65efbe68` |
| `artifacts/p4a/h-exec/m4/full_game/seed-4-normal-start-0.jsonl` | 687460 | `9ddb17b6efe7d6b592857f7a0138305d474beb29da07dd7359b7f6c64691fd40` |
| `artifacts/p4a/h-exec/m4/full_game/seed-4-normal-start-1.jsonl` | 697504 | `e5ec42984b4dcf07766a24418d8a9b916eb21f71691ff1954be5b736b25b79e9` |
| `artifacts/p4a/h-exec/m4/lifecycle_stress.json` | 1227 | `f3723d80034df9054b37912c91651f99238120deb2f2b02ef638e627e93a3c77` |
| `artifacts/p4a/h-exec/m4/soak.json` | 284263 | `2bb15461aa65b780dbe4eac5a13a87fd30040351ac54cc51da07793bbbb3306f` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-000.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-001.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-002.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-003.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-004.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-005.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-006.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-007.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-008.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-009.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-010.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-011.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-012.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-013.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-014.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |
| `artifacts/p4a/h-exec/m4/soak.workers/worker-015.stderr.log` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` |

## Runtime budget

| Normal seconds | Heavy seconds | Measured seconds | Combined acceptance seconds | Heavy inherited |
|---:|---:|---:|---:|---|
| 2549.755 | 441.864 | 2991.619 | 2991.619 | False |

## Scope and limitations

- This report records executable evidence only; it does not implement TeacherCore or either StrategyProfile.
- `BLOCKED` facts in the public-fact matrix remain blocked and are not replaced with private-state access.
