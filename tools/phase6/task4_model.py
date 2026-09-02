"""Small provisional PyTorch BC scorer for the Phase-6 Task-4A seam.

This module is deliberately limited to the numeric surfaces produced by the
Task-4A projection.  Candidate routing identities and public action keys stay
outside the learned network and are handled by the inference boundary.
"""

from __future__ import annotations

from typing import Optional

import torch
from torch import Tensor, nn
from torch.nn import functional

from . import task4_codec as codec


class Task4ModelError(ValueError):
    """Raised when a model input or loss would violate the BC contract."""


def _require_float32_matrix(value: Tensor, width: int, name: str) -> None:
    if not isinstance(value, Tensor):
        raise Task4ModelError(f"{name} is not a tensor")
    if value.ndim != 2 or value.shape[1] != width:
        raise Task4ModelError(f"{name} has the wrong rank or width")
    if value.dtype != torch.float32:
        raise Task4ModelError(f"{name} is not float32")
    if value.shape[0] == 0:
        raise Task4ModelError(f"{name} is empty")
    if value.device.type == "meta":
        raise Task4ModelError(f"{name} uses an unsupported device")
    if not torch.isfinite(value).all().item():
        raise Task4ModelError(f"{name} contains a non-finite value")


def validate_logits(logits: Tensor, expected_count: Optional[int] = None) -> Tensor:
    """Validate the exact finite f32 score surface returned by the model."""

    if not isinstance(logits, Tensor) or logits.dtype != torch.float32:
        raise Task4ModelError("logits are not a float32 tensor")
    if logits.ndim != 1:
        raise Task4ModelError("logits are not a one-dimensional candidate vector")
    if expected_count is not None and logits.shape[0] != expected_count:
        raise Task4ModelError("logit count does not equal the candidate count")
    if logits.shape[0] == 0 or not torch.isfinite(logits).all().item():
        raise Task4ModelError("logits are empty or non-finite")
    return logits


class _StateEncoder(nn.Module):
    def __init__(self, config: codec.ArchitectureConfigV1) -> None:
        super().__init__()
        self.input = nn.Linear(config.state_row_width, config.state_hidden_width)

    def forward(self, state_rows: Tensor) -> Tensor:
        hidden = torch.relu(self.input(state_rows))
        return torch.cat((hidden.mean(dim=0), hidden.amax(dim=0)), dim=0)


class _CandidateEncoder(nn.Module):
    def __init__(self, config: codec.ArchitectureConfigV1) -> None:
        super().__init__()
        self.input = nn.Linear(
            config.state_hidden_width * 2 + config.candidate_row_width,
            config.candidate_hidden_width,
        )

    def forward(self, state_representation: Tensor, candidate_rows: Tensor) -> Tensor:
        count = candidate_rows.shape[0]
        shared_state = state_representation.unsqueeze(0).expand(count, -1)
        return torch.relu(self.input(torch.cat((shared_state, candidate_rows), dim=1)))


class Phase6TorchCandidateScorer(nn.Module):
    """Provisional exact-domain candidate scorer.

    The module accepts only projected numeric rows.  It has no parameter or
    input for routing keys, public action keys, candidate ordinals, or locator
    strings.  The caller owns the source-order candidate-to-key sidecar.
    """

    def __init__(self, config: Optional[codec.ArchitectureConfigV1] = None) -> None:
        super().__init__()
        self.config = codec.default_architecture_config() if config is None else config
        if codec.architecture_config_identity(self.config) != codec.architecture_config_identity():
            raise Task4ModelError("only the accepted provisional architecture is supported")
        self.state_encoder = _StateEncoder(self.config)
        self.candidate_encoder = _CandidateEncoder(self.config)
        self.score_head = nn.Linear(
            self.config.state_hidden_width * 2 + self.config.candidate_hidden_width,
            1,
        )

    def forward(
        self,
        state_rows: Tensor,
        candidate_rows: Tensor,
        physical_candidate_capacity: Optional[int] = None,
    ) -> Tensor:
        _require_float32_matrix(state_rows, codec.STATE_ROW_WIDTH, "state rows")
        _require_float32_matrix(candidate_rows, codec.CANDIDATE_ROW_WIDTH, "candidate rows")
        if state_rows.device != candidate_rows.device:
            raise Task4ModelError("state and candidate rows use different devices")
        count = int(candidate_rows.shape[0])
        if physical_candidate_capacity is not None:
            if not isinstance(physical_candidate_capacity, int) or physical_candidate_capacity < count:
                raise Task4ModelError("physical candidate capacity is smaller than N")
        state_representation = self.state_encoder(state_rows)
        candidate_representation = self.candidate_encoder(
            state_representation, candidate_rows
        )
        shared_state = state_representation.unsqueeze(0).expand(count, -1)
        logits = self.score_head(
            torch.cat((shared_state, candidate_representation), dim=1)
        ).reshape(count)
        return validate_logits(logits, count)


def exact_domain_cross_entropy_from_padded(
    logits: Tensor, labels: Tensor, real_candidate_mask: Tensor
) -> Tensor:
    """Compute BC loss only over exact real candidate domains.

    ``logits`` and ``real_candidate_mask`` have shape ``[batch, width]``.
    Padding is physical storage only: every row is compacted by its mask
    before cross entropy, and no padded position can become a class.
    """

    if not isinstance(logits, Tensor) or logits.ndim != 2 or logits.dtype != torch.float32:
        raise Task4ModelError("padded logits are not a float32 matrix")
    if not isinstance(real_candidate_mask, Tensor) or real_candidate_mask.shape != logits.shape:
        raise Task4ModelError("candidate mask does not match logits")
    if real_candidate_mask.dtype != torch.bool:
        raise Task4ModelError("candidate mask is not boolean")
    if not isinstance(labels, Tensor) or labels.ndim != 1 or labels.dtype != torch.long:
        raise Task4ModelError("labels are not a long vector")
    if labels.shape[0] != logits.shape[0]:
        raise Task4ModelError("label count does not match batch size")
    if logits.device != real_candidate_mask.device or logits.device != labels.device:
        raise Task4ModelError("loss inputs use different devices")
    if not torch.isfinite(logits).all().item():
        raise Task4ModelError("padded logits contain a non-finite value")

    compact_logits = []
    for row, mask, label in zip(logits, real_candidate_mask, labels):
        real = row[mask]
        if real.shape[0] == 0 or label.item() < 0 or label.item() >= real.shape[0]:
            raise Task4ModelError("label is outside the exact candidate domain")
        compact_logits.append(real)
    loss = torch.stack(
        [functional.cross_entropy(row.unsqueeze(0), label.reshape(1))
         for row, label in zip(compact_logits, labels)]
    ).mean()
    if not torch.isfinite(loss).item():
        raise Task4ModelError("BC loss is non-finite")
    return loss
