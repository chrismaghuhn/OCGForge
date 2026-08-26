# ML Training Strategy — 2026-08-26 Interpretation

This note records how to read the preserved **25 August 2026** ML-training research after subsequent OCGForge work. It does not edit or supersede the original research artifact and is not itself a gameplay or training contract.

## Historical-status boundary

The archived report contains live-repository observations from its research date. Those observations are intentionally preserved as history. Since then, M4 was finalized and merged; the accepted M4 semantic source checkpoint remains `a49639bbb7ef8ce3406ac0d9aad295272872dda9`, and the M4 merge checkpoint was `9d5bb9fa4b8c700026dcf9665885c0dcfb1e8047`.

Current repository/PR/CI state must always be inspected live. The archived report must not override `docs/CURRENT_PROJECT_STATE.md`, accepted ADRs/contracts, or machine-readable acceptance evidence.

## Durable conclusions retained

The following research conclusions remain useful architectural direction:

- environment and data semantics must be trustworthy before ML algorithms;
- policy/model inputs remain perspective-safe `PlayerObservation + DecisionRequest + complete ActionCandidate[]`;
- candidate-relative scoring is preferred over a fixed global action vocabulary;
- no model/tensor width may truncate the authoritative legal domain;
- an OCGForge-native deterministic teacher is the preferred primary bootstrap;
- WindBot is a benchmark/heuristic/secondary-label source, not legality or privacy authority;
- feed-forward should be the first real model, with recurrence promoted only by a measured ablation;
- behavior cloning is a bootstrap/representation test, not the final objective;
- self-play begins with immutable snapshots before league complexity;
- search waits for deterministic checkpoint/restore/fork and information-safe search semantics;
- gameplay/environment reproducibility is distinct from bit-identical ML-training reproducibility.

## Sequencing refinement after episodic-contract research

The original report grouped the next environment and trajectory work closely. The later focused episodic-contract validation refined that sequencing:

```text
M4 FINAL
→ Episodic Environment V1
→ trajectory/data contract
→ deterministic teacher
→ model-facing adapter / first BC model
→ controlled RL
→ snapshot self-play
→ league/search only when justified
```

The first episodic milestone should therefore stabilize the authoritative reset/step lifecycle, shared `EpisodeDriver`, complete decision frames, deterministic semantic identities, non-semantic stale-submission freshness, typed terminal/interrupted/failed/rejected states, privacy-safe terminal views, and replay/equivalence gates **without implementing trajectory shards or ML code in the same milestone**.

This is a scope refinement, not a rejection of the research thesis that trustworthy trajectories are required before training.

## Reward interpretation

The research correctly prefers terminal duel outcome over dense heuristic shaping. The later episodic design makes the ownership boundary more explicit:

```text
authoritative game outcome
+
versioned RewardPolicy
→ numeric training reward
```

Changing reward scale or training shaping must not change episode semantic identity, legal candidates, observations, or gameplay hashes.

## Relationship to other preserved research

The detailed ygo-agent `max_options` / action-space audit is preserved separately under `docs/research/YGO_AGENT_ACTION_SPACE_RESEARCH_REPORT_2026-08-25.md`. Its key lesson is complementary: fixed-shape batching is acceptable, but allowing physical model width to remove legal candidates is not.

EDOPro/WindBot capability-mining research is likewise preserved separately. These reports are reference inputs for future adapters/teachers/evaluation, not competing environment authorities.

## Authority

For future implementation decisions, use this precedence:

1. accepted ADRs and versioned public contracts;
2. executable tests and acceptance evidence;
3. current project-state/roadmap summaries;
4. dated research reports such as this snapshot as architectural input.

The research remains valuable because it explains *why* the project should progress from trusted environment/data semantics toward teacher bootstrap, candidate-scoring models, controlled RL, snapshot self-play, and only later league/search/generalization. It does not by itself authorize any of those later stages.
