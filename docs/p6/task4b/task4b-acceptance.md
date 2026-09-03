# Task4B Acceptance

H_exec: 8f682d4c9eb53a32be7cd8f6125048583943f19e
corpus_probe_sha256: 074a796dab428af07ca8a81489f03a1f1aa52a1e581979726faee4fe2a0190c2
corpus_probe_source_commit: 8f682d4c9eb53a32be7cd8f6125048583943f19e
checkpoint_identity: phase6_checkpoint.v1.62f4532a5e551886affbd65bc47f7645017dedf6c5ca3a0b7b87b4a978943327
smoke_evidence_identity: phase6_task4b_smoke_evidence.v1.f540220507ae36f8704608b9dd3364ef03ed6e6d8aa7952e7221ed1231e301fe
SMOKE_PASS: true
TASK4B_PASS: false

| gate | status |
| --- | --- |
| task4-focused-python | PASS |
| admitted-forward | PASS |
| full-non-long-ctest | FAIL |
| project-python | PASS |
| rules-bundle | PASS |
| rules-deck | PASS |
| teacher-binding | PASS |
| public-boundary | PASS |
| source-boundary | PASS |
| base-to-h-exec-diff-check | PASS |

| command_id | argv | exit_code | stdout_sha256 | stderr_sha256 | status |
| --- | --- | ---: | --- | --- | --- |
| task4-focused-python | `["python","-m","unittest","-v","tests.phase6.phase6_task4a_codec_test","tests.phase6.phase6_task4a_model_test","tests.phase6.phase6_task4a_inference_test","tests.phase6.phase6_task4a_cuda_preflight_test","tests.phase6.phase6_task4b_runner_test"]` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | `c99b3a60acdddcfdab03ae86b19fb2b2dedc33fe4b4d7f00bdc77d5d3bcc4f73` | PASS |
| admitted-forward | `["python","-m","tests.phase6.phase6_task4a_corpus_test","C:\\Users\\chris\\.config\\superpowers\\worktrees\\yogiohML\\chris\\phase6-task4b-cuda-smoke\\build\\task4b-cuda-smoke\\phase6_task4_corpus_probe.exe"]` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | `e16260751046c5acc2c9db3c37fa08cbc6379e85de89727c46068184ea60e8cd` | PASS |
| admitted-forward | `["python","-m","tests.phase6.phase6_task4a_admitted_model_test","C:\\Users\\chris\\.config\\superpowers\\worktrees\\yogiohML\\chris\\phase6-task4b-cuda-smoke\\build\\task4b-cuda-smoke\\phase6_task4_corpus_probe.exe"]` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | `2ce899decfb80220a816e0bad8e1e1839b212b9250a5af8d20c8a7836216e452` | PASS |
| full-non-long-ctest | `["ctest","--test-dir","C:\\Users\\chris\\.config\\superpowers\\worktrees\\yogiohML\\chris\\phase6-task4b-cuda-smoke\\build\\task4b-cuda-smoke","--output-on-failure","-j","1","-LE","P4A_HEAVY_REPLAY|M4_HEAVY_LIFECYCLE|M4_ACCEPTANCE_SCALE"]` | 8 | `2472cc059396c80c6d87419eae9b05e05e4e17fed8abfc6ea22f474d8aa05530` | `18e00877dc1071c85cd4f479d451bebeef8309022e46cf8c63b0bed5151984a4` | FAIL |
| project-python | `["python","-m","unittest","discover","-s","tests/python","-v"]` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | `40beb653f198339fc6454212f5f34a363d912065a1f703bddda07afca25ec43d` | PASS |
| rules-bundle | `["python","tools/verify_rules_bundle.py","--lock","third_party/rules_bundle.lock.json","--cache",".cache/rules_bundle"]` | 0 | `8f55be55e5abdf53cd6515af7e28f1eddb96cc1970f667634e82926e7cfcfaa5` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| rules-deck | `["ctest","--test-dir","C:\\Users\\chris\\.config\\superpowers\\worktrees\\yogiohML\\chris\\phase6-task4b-cuda-smoke\\build\\task4b-cuda-smoke","--output-on-failure","-j","1","-R","^(fixture_deck_test|deck_loader_test|m3_rules_mode_test|m3_real_deck_privacy_test)$"]` | 0 | `b7aa04246240e2cc4b77f06ece50935b096f3130f446e63276723e877012bb6c` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| teacher-binding | `["ctest","--test-dir","C:\\Users\\chris\\.config\\superpowers\\worktrees\\yogiohML\\chris\\phase6-task4b-cuda-smoke\\build\\task4b-cuda-smoke","--output-on-failure","-j","1","-R","^(teacher_policy_boundary_compile_test|teacher_domain_preservation_test|teacher_provenance_test|phase4b_teacher_identity_regression_test)$"]` | 0 | `f5f581f60cccead1c70a0bc34ea8f68ba7a5971e195da135ce215d318ef7f206` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| public-boundary | `["ctest","--test-dir","C:\\Users\\chris\\.config\\superpowers\\worktrees\\yogiohML\\chris\\phase6-task4b-cuda-smoke\\build\\task4b-cuda-smoke","--output-on-failure","-j","1","-R","^(episodic_environment_v2_public_projection_test|public_action_identity_test|public_safe_state_test|privacy_projection_test|logical_model_public_boundary_test)$"]` | 0 | `48617331b452d551165573efb5eac1fe339cd2f9b8e4c0af2db9cc5eb53a032a` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| source-boundary | `["python","tests/policy/policy_boundary_test.py"]` | 0 | `c505edd816513c8ee27f20d5721095e1d4e829dd300aee3f5da7a32157bc6e20` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| source-boundary | `["python","tests/teacher/teacher_public_boundary_test.py"]` | 0 | `48a060cc5fa80f2ec5713e0a03aaf70ea29c6dc606b0ef6b09e541b4c85b6e9d` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| source-boundary | `["python","tests/episodic/episode_driver_ownership_guard.py"]` | 0 | `209bb191450bfcd3b3fdab0c247d297211014762e2fb282039569f83a2233e0b` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| source-boundary | `["python","tests/model/logical_model_public_boundary_test.py","include/ygo/model","src/model"]` | 0 | `098f1615688ffb81cf201214d45e858d07995195d332ddec478ee91f716b182d` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
| base-to-h-exec-diff-check | `["git","diff","--check","1727f09eb0fdc4e4e25e3f9ced9748feb4058234","8f682d4c9eb53a32be7cd8f6125048583943f19e"]` | 0 | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | `e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855` | PASS |
