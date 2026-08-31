# OCGForge Hugging Face ML Tooling Evaluation

**Research date:** 2026-08-31  
**Scope:** research / future tooling direction only  
**Status:** non-normative until a later implementation task adopts a tool behind an OCGForge-owned contract

## Executive decision

Hugging Face tools may provide replaceable infrastructure around OCGForge, but they must never become the authority for gameplay semantics, dataset admission, semantic identity, replay, checkpoint identity, or acceptance evidence.

The durable boundary is:

```text
OCGForge-owned canonical contracts / admitted artifacts
        ↓
thin verified adapter
        ↓
optional Hugging Face tooling
```

Never:

```text
HF run name / branch / tag / cache path / artifact alias / internal fingerprint
        =
OCGForge semantic identity
```

## Recommended tools

| Tool | Decision | Intended OCGForge use | Earliest introduction |
| --- | --- | --- | --- |
| **Accelerate** | **USE LATER** | Learner execution and distributed-training abstraction over PyTorch | Phase 6 — first GPU Behavior Cloning learner |
| **Safetensors** | **USE LATER** | Physical container for exported neural weights | Phase 6 — first neural checkpoint contract |
| **Datasets** | **PILOT, THEN DECIDE** | Optional loading/batching/streaming layer over already-admitted OCGForge datasets | Phase 6, only after dataset/sampler/order contracts are frozen |
| **Trackio** | **KEEP OPTIONAL** | Read-only experiment/evaluation dashboard | Phase 4C evaluation pilot or Phase 6 learner |
| **huggingface_hub** | **KEEP OPTIONAL** | Artifact transport/publication after OCGForge manifests exist | Phase 6+ after canonical checkpoint/dataset verification exists |
| **PEFT** | **CONDITIONAL** | Later archetype/deck adapters around a stable multi-deck base model | Post-baseline multi-deck work only if measured useful |
| **Optimum** | **CONDITIONAL** | Export/quantization/inference optimization | Deployment phase only after profiling proves a bottleneck |
| **Kernels** | **CONDITIONAL** | Hardware-specific kernel optimization | Only after a measured kernel bottleneck and semantic-equivalence gates |

## Core recommendations

### Accelerate — USE LATER

Accelerate is the preferred future abstraction for learner execution. It allows the OCGForge Python learner to retain one PyTorch training loop while execution moves between local CPU/GPU, Kaggle/Colab, multi-GPU, mixed precision, FSDP, or DeepSpeed configurations.

Accelerate must not own:

- model semantics;
- admitted data eligibility;
- sample-order / sampler identity;
- training-algorithm semantics;
- actor/learner protocol;
- canonical checkpoint identity;
- evaluation/promotion gates.

Execution details such as Accelerate version, PyTorch/CUDA version, world size, backend, mixed precision, FSDP/DeepSpeed configuration, gradient accumulation, and device type are execution provenance, not gameplay or checkpoint semantic identity.

Reference: https://huggingface.co/docs/accelerate/index

### Safetensors — USE LATER

Safetensors is recommended as the physical weights container for neural policies. It is not the complete checkpoint contract.

Preferred separation:

```text
policy_weights.safetensors
+
ocgforge_checkpoint_manifest.json
+
optional optimizer / scheduler / RNG / sampler resume state
```

OCGForge must bind at least model architecture/schema, tensor names/shapes/dtypes, observation/action/vocabulary contracts, dataset identity, preprocessing contracts, parent checkpoint, and any resume state required by the training contract.

A Safetensors file digest identifies an exact weights artifact; it does not by itself identify the complete semantic policy/checkpoint.

Reference: https://huggingface.co/docs/safetensors/index

### Datasets — PILOT, THEN DECIDE

Hugging Face Datasets is a plausible loading/batching/streaming layer over already-admitted trajectory data. It should not be committed as a mandatory dependency before OCGForge freezes dataset transformation, row-order, split, sampler, and epoch semantics.

Before adoption, compare a thin PyArrow/PyTorch loader with HF Datasets on:

- throughput;
- memory usage;
- deterministic row/sample ordering;
- worker behavior;
- streaming behavior;
- provenance complexity;
- portability across local/Kaggle/Colab/cloud execution.

HF fingerprints, cache directories, generated Arrow files, IterableDataset state, or streaming shuffle state must never replace `dataset_semantic_id` or an OCGForge sampler/order contract.

Reference: https://huggingface.co/docs/datasets/index

## Optional surrounding tools

### Trackio — KEEP OPTIONAL

Trackio may consume canonical OCGForge result files and visualize Teacher quality, win rate, fallback rates, candidate-domain statistics, learner loss, throughput, or confidence intervals.

The integration must be one-way:

```text
canonical OCGForge evaluation/training result
        ↓
thin exporter
        ↓
Trackio dashboard
```

Run names, dashboard grouping, artifact aliases, or hosted state are presentation metadata only. Trackio does not own acceptance status, dataset identity, checkpoint promotion, or trajectory lineage.

Earliest useful pilot: Phase 4C. No Phase-4B runtime dependency is justified.

Reference: https://huggingface.co/docs/trackio/index

### huggingface_hub — KEEP OPTIONAL

The Hub client may later transport/publish frozen datasets, checkpoints, model cards, dataset cards, and evaluation bundles. OCGForge must bind its own content hashes and use immutable revisions when relying on Hub-hosted artifacts.

Do not use mutable references such as `main`, `latest`, or `best` as semantic identity. A Hub locator is transport metadata; every artifact must remain usable and verifiable without Hugging Face.

Reference: https://huggingface.co/docs/huggingface_hub/guides/download

## Conditional later tools

### PEFT

PEFT becomes relevant only after a stable multi-deck base model exists and measurements show that full fine-tuning is unnecessarily expensive. Any adapter policy identity must bind the exact base checkpoint, adapter architecture/configuration, adapter tensors, application/merge semantics, and evaluation evidence.

Do not use PEFT for the first candidate-scoring model.

Reference: https://huggingface.co/docs/peft/index

### Optimum

Optimum may be useful for later export, quantization, or hardware-specific inference optimization. Any optimized artifact is a distinct policy artifact until score/action equivalence is proven under explicit tolerances and deterministic tie-breaking rules.

Do not introduce Optimum before profiling shows an inference/deployment bottleneck.

Reference: https://huggingface.co/docs/optimum/index

### Kernels

Remote or hardware-specific kernels add supply-chain, hardware, and reproducibility complexity. Version labels/aliases must not be treated as immutable OCGForge identity. If Kernels is ever used, pin an immutable revision and bind independently verified artifact hashes/provenance.

Do not introduce Kernels before a measured bottleneck and semantic-equivalence gate exist.

Reference: https://huggingface.co/docs/kernels/index

## Tools not recommended for the core stack

### Transformers — not for the initial model

The first OCGForge neural policy should remain a custom PyTorch structured candidate-scoring model. Using transformer-style layers internally does not require the Hugging Face Transformers library.

Reconsider only for a concrete future need such as a supported pretrained card-text/image encoder.

### Tokenizers — not needed

OCGForge state, entities, card identities, relationships, events, and semantic actions are structured contract fields rather than natural-language token streams. OCGForge should own its feature vocabulary and card-ID mapping.

### TRL — not needed

TRL is oriented toward post-training transformer language models. It does not replace OCGForge actors, admitted trajectories, variable candidate domains, replay semantics, partial-observability handling, policy distribution, self-play, league management, or controlled game-RL benchmarking.

### Evaluate — not an evaluator authority

OCGForge evaluation needs exact matchup/rules identities, frozen seed/scenario sets, public-decision failures, privacy/replay gates, deterministic evaluation-bundle identity, and promotion semantics. Scalar metrics may be implemented directly without delegating benchmark authority to another framework.

### OpenEnv — not the authoritative environment

A future remote adapter could wrap OCGForge, but OpenEnv must not redefine `reset`, `step`, public state, decision completeness, seed identity, or replay semantics.

### Storage Buckets — scratch/transport only

Mutable/non-versioned storage may be useful for disposable logs or intermediate transfer, but never as the only trusted copy of admitted datasets, canonical checkpoints, replay evidence, acceptance bundles, or provenance manifests.

## Planned integration sequence

```text
Phase 4B
  TeacherCore + StrategyProfiles
  → no Hugging Face runtime dependency

Phase 4C
  frozen Teacher evaluation
  → optional Trackio read-only exporter pilot

Phase 5
  model-facing candidate-scoring adapter
  → remain framework-neutral; custom PyTorch model contract design

Phase 6
  Behavior Cloning baseline
  → Accelerate learner backend
  → Safetensors weight container + OCGForge checkpoint manifest
  → pilot HF Datasets vs thin PyArrow/PyTorch loader
  → optional huggingface_hub transport after content verification exists

Phase 7
  controlled RL comparison
  → retain Accelerate only as learner-execution infrastructure
  → tracking remains optional/presentation-only

Post-baseline / deployment
  → PEFT only after stable multi-deck base model + measured value
  → Optimum/Kernels only after measured bottlenecks + semantic-equivalence gates
```

## Final architectural rule

```text
OCGForge owns:
meaning, legality, privacy, complete candidate domains,
trajectory admission, semantic identity, checkpoint manifests,
replay, evaluation authority, and acceptance evidence.

Hugging Face may provide:
loading, learner execution, physical tensor storage,
optional transport/publication, and optional visualization.
```

No tool adoption in this document authorizes ML implementation before the prerequisite OCGForge phases and contracts are accepted.
