# ML Training Strategy Research Snapshot

This directory preserves the completed 25 August 2026 research report **Best ML Training Strategy for a Deterministic Yu-Gi-Oh! Environment** as historical research input.

It is **not** a live repository-status document, an accepted gameplay contract, an episodic contract, a trajectory contract, or authorization to start ML work. Repository/PR status statements inside the report describe the inspection date and must not override current live GitHub state or accepted OCGForge contracts.

## Original artifact identity

The supplied original report is exactly:

- source filename: `OCGForge_ML_Training_Strategy_2026-08-25.md`
- bytes: `104550`
- SHA-256: `7c998810bfc36d3ae0cd63aeddc999cd5177eaa5c476301a7445513d91908b07`

To preserve that byte identity without silently reformatting the research artifact, the repository stores it as eight consecutive raw Markdown parts under `archive/`. Concatenating the files in lexical order reproduces the exact original byte stream and SHA-256.

```bash
cat archive/OCGForge_ML_Training_Strategy_2026-08-25.part-*.md > OCGForge_ML_Training_Strategy_2026-08-25.md
sha256sum OCGForge_ML_Training_Strategy_2026-08-25.md
```

See `OCGForge_ML_Training_Strategy_2026-08-25.manifest.json` for per-part byte counts and hashes.

## Durable research direction

The report recommends the staged direction:

```text
Environment
→ Data
→ deterministic teacher / bootstrap
→ first candidate-scoring neural policy
→ controlled RL
→ snapshot self-play
→ league/search/generalization only when justified
```

Its durable architectural conclusions include:

- keep `PlayerObservation + DecisionRequest + complete ActionCandidate[]` as the policy information boundary;
- preserve complete variable legal candidate domains and use candidate-relative scoring rather than a fixed global action vocabulary;
- treat recurrence as a measured feed-forward-vs-GRU ablation rather than a mandatory first architecture;
- use an OCGForge-native deterministic teacher as the primary trusted bootstrap; WindBot is a benchmark/heuristic/secondary-label source only after exact safe mapping;
- keep terminal duel outcome as the game objective and keep shaped rewards/auxiliary targets separate from authoritative game semantics;
- begin self-play with frozen snapshots before adding league complexity;
- defer search until deterministic checkpoint/restore/fork and hidden-information-safe search semantics exist.

## Later interpretation

`OCGForge_ML_Training_Strategy_Interpretation_2026-08-26.md` records how the research should be read after M4 finalization and the later episodic-contract research. It does not modify the historical report.

The ygo-agent `max_options` investigation is preserved separately in the already completed action-space research under `docs/research/YGO_AGENT_ACTION_SPACE_RESEARCH_REPORT_2026-08-25.md`; it is intentionally not duplicated here.
