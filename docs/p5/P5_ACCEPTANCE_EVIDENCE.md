# Phase 5 Acceptance Evidence

- Status: **FINAL_PASS**
- Original implementation base: `790606c5ff253c21847ae8339e2d517f185a993d`
- Test/evidence execution head (`H_exec`): `3c99e86c487361fc4e0f5f12678b4867e59232b7`
- Previous evidence head: `f388451144a13594491ad7b27254b5d41744b178`
- Branch: `chris/phase5-final-acceptance`

## G00-G17

| Gate | Status |
| --- | --- |
| P5-G00 | PASS |
| P5-G01 | PASS |
| P5-G02 | PASS |
| P5-G03 | PASS |
| P5-G04 | PASS |
| P5-G05 | PASS |
| P5-G06 | PASS |
| P5-G07 | PASS |
| P5-G08 | PASS |
| P5-G09 | PASS |
| P5-G10 | PASS |
| P5-G11 | PASS |
| P5-G12 | PASS |
| P5-G13 | PASS |
| P5-G14 | PASS |
| P5-G15 | PASS |
| P5-G16 | PASS |
| P5-G17 | PASS |

## Durable G08/G10 evidence

- G08 layout variants: `ragged, padded_W_max, padded_W_greater, composed, reordered`.
- G08 sample A identity: `model_input.v1.a57241cec0771ceb73fa15e00f02572e276a4977e914e88e8062178aa86e21da`.
- G08 sample B identity: `model_input.v1.71c940b998fd74a2657b3f16fd1b778d1de9278ac9dc5e7d461aababeacdd8a0`.
- G08 identities remain unchanged after composition/reordering and padding changes.
- N24: digest `8f588290fc553fd091d29e53de182ac952252bc7a74b4cab93a83e09fa4a92c0`, W=N `24`, W>N `25`, W<N `23` → `CapacityTooSmall/no mutation`.
- N25: digest `7fa2332004e555a8695be51697e282f1b95ec0221911f7d381c6c570650e5d51`, W=N `25`, W>N `26`, W<N `24` → `CapacityTooSmall/no mutation`.
- N129: digest `b218ebdb3a7143090f7421a43fad4401fbeb8d506522fa2760d9f4fa4c59df76`, W=N `129`, W>N `130`, W<N `128` → `CapacityTooSmall/no mutation`.

## Exact command records

| ID | Gate | Exit | Result | Observed | Command |
| --- | --- | ---: | --- | --- | --- |
| G00-scope | P5-G00 | 0 | PASS | exactly four Task-1 documents | `git diff --name-status c2fe44a1eb84d88a9b10c6c906eec46e216e4335 9b277393d4f50c0efc1b3b378a04bcb8a7b72cef` |
| configure | P5-G14/G17 | 0 | PASS | configure/generate completed | `cmake -S . -B build/p5-acceptance -G Ninja -DCMAKE_BUILD_TYPE=Release` |
| build | P5-G14/G17 | 0 | PASS | Release build completed | `cmake --build build/p5-acceptance --parallel 1` |
| focused-p5 | P5-G01..G05/G08..G10/G12/G13/G16 | 0 | PASS | 11/11 tests passed | `ctest --test-dir build/p5-acceptance --output-on-failure -R "^(episodic_paired_world_test|public_action_identity_test|public_safe_state_test|logical_model_input_test|card_vocabulary_test|encoded_model_input_test|model_batch_layout_test|model_supervision_sample_test|logical_model_public_boundary_test|observation_contract_test|privacy_projection_test)$"` |
| durable-model-output | P5-G08/G10 | 0 | PASS | G08 variants and G10 N=24/25/129 records emitted | `build/p5-acceptance/encoded_model_input_test.exe` |
| full-ctest | P5-G15 | 0 | PASS | 163/163 tests passed | `ctest --test-dir build/p5-acceptance --output-on-failure -j 1` |
| python-suite | P5-G15 | 0 | PASS | 15/15 tests passed | `python -m unittest discover -s tests/python -v` |
| rules-bundle | P5-G15 | 0 | PASS | bundle verification ok | `python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache C:/yogiohML/.cache/rules_bundle` |
| independent-model-a | P5-G06/G07 | 0 | PASS | full KAT emitted | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/full_model_probe.exe > C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/final-h-exec-full-a.txt` |
| independent-model-b | P5-G06/G07 | 0 | PASS | full KAT emitted; output byte-equal to process A | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/full_model_probe.exe > C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/final-h-exec-full-b.txt` |
| batch-identity | P5-G08/G09/G10 | 0 | PASS | layout identity and all boundaries emitted | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/batch_identity_probe.exe > C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/final-h-exec-batch.txt` |
| paired-world | P5-G12 | 0 | PASS | hidden worlds differ internally; public/logical/encoded/identity equal | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/paired_model_probe.exe > C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/final-h-exec-paired.txt` |
| adversarial | P5-G16 | 0 | PASS | logical/encoded/batch negative cases rejected | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/adversarial_model_probe.exe > C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/final-h-exec-adversarial.txt` |
| g28-internal | P5-G11 | 0 | PASS | internal corpus: 1040 complete domains; maximum 9 | `build/p5-acceptance/ygo_episodic_internal_witness.exe --max-actions 64 --seeds 4 --output C:/Users/chris/AppData/Local/Temp/ocgforge-p5-g28-final/internal.json` |
| g28-public | P5-G11 | 0 | PASS | public witness replay selected count 9 | `python tests/episodic/episodic_witness_discovery.py --probe build/p5-acceptance/ygo_episodic_probe.exe --output C:/Users/chris/AppData/Local/Temp/ocgforge-p5-g28-final/public.json --max-actions 64 --seeds 4` |
| g28-model-a | P5-G11 | 0 | PASS | full public witness passed P5 model layers | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/g28_model_probe.exe C:/Users/chris/AppData/Local/Temp/ocgforge-p5-g28-final/public.json` |
| g28-model-b | P5-G11 | 0 | PASS | second process byte-equal to G28 model A | `C:/Users/chris/AppData/Local/Temp/ocgforge-p5-independent-probe/g28_model_probe.exe C:/Users/chris/AppData/Local/Temp/ocgforge-p5-g28-final/public.json` |
| clean-configure-build | P5-G17 | 0 | PASS | fresh checkout build 346/346 | `cmake -S . -B build/p5-g17-final -G Ninja -DCMAKE_BUILD_TYPE=Release && cmake --build build/p5-g17-final --parallel 1` |
| clean-ctest | P5-G17 | 0 | PASS | fresh checkout 163/163 tests passed | `ctest --test-dir build/p5-g17-final --output-on-failure -j 1` |
| clean-python | P5-G17 | 0 | PASS | fresh checkout 15/15 tests passed | `python -m unittest discover -s tests/python -v` |
| clean-rules | P5-G17 | 0 | PASS | fresh checkout bundle verification ok | `python tools/verify_rules_bundle.py --lock third_party/rules_bundle.lock.json --cache C:/yogiohML/.cache/rules_bundle` |

## G15 regression suites

| Suite | Status | Command record | Observed |
| --- | --- | --- | --- |
| native CTest complete | PASS | `full-ctest` | 163/163 |
| trusted trajectory/admission/replay | PASS | `full-ctest` | trajectory_codec_test, trajectory_recorder_test, trajectory_shard_test, trajectory_restricted_evidence_test, trajectory_replay_admission_test, trajectory_receipt_test, trajectory_dataset_manifest_test, trajectory_artifact_determinism_test, trajectory_privacy_test |
| public environment/action/privacy | PASS | `full-ctest` | episodic_environment_test, episodic_environment_privacy_fail_closed_test, episodic_environment_v2_public_projection_test, episodic_paired_world_test, public_action_identity_test, public_safe_state_test, privacy_projection_test |
| Teacher and battle-sidecar | PASS | `full-ctest` | teacher_policy_boundary_compile_test, teacher_domain_preservation_test, teacher_runner_trajectory_test, phase4b_teacher_identity_regression_test, phase4c_teacher_trajectory_test, public_battle_snapshot_test, provable_lethal_test |
| M3/M3.5 fixed-deck/conformance | PASS | `full-ctest` | m35_probe_trace_starting_player_test, m3_rules_mode_test, m3_real_deck_privacy_test, m1_engine_fixture_test, m1_engine_semantic_determinism_test, determinism_test, fixture_deck_test, decision_coverage_test |
| M4 worker/lifecycle | PASS | `full-ctest` | m4_worker_protocol_test, m4_failure_isolation_test, m4_failure_isolation_fast_test, m4_worker_integration_test, m4_worker_integration_fast_test |
| Python regression | PASS | `python-suite` | 15/15 |
| rules bundle | PASS | `rules-bundle` | ok=true |

## G28

- Real witness: `candidate_domain_max=9`, witness count `9`, complete domains `1040`.
- Witness episode: `04e5b50566391f7e46b27e43fc63d3e28883fc2696d373ae4016bcdd7b009ddf`; decision `24`; engine step `52`.
- Public observation digest: `a4294295cf3c651914a09950a86ced275adb906e89a2fcaeb2864c6ca449cfa4`.
- Candidate-domain digest: `945a4d0118d2719a7138bbc649e4ae1d2432e108d7ac0d2007e61cdcd125e997`.

## Execution provenance

- Local executable evidence: PASS.
- Hosted/PR evidence: NOT_RUN (no PR requested).
- Wrapper-blocked evidence: NOT_RUN.
- Native CTest: 163/163 PASS; Python: 15/15 PASS; rules bundle: PASS.
- Fresh clean checkout at H_exec: Release build 346/346 and CTest 163/163 PASS; worktree clean before/after.
- Production files changed by the closure: 0.
- Trusted trajectory schema unchanged; ML/Phase 6 not started.

The JSON command records contain the exact invocations, exit codes, observed counts, and log hashes used to derive this summary.
