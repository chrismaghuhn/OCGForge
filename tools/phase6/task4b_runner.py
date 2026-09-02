"""Task-4B runner primitives.

The complete smoke runner is added in later authorized plan tasks.  This
module currently contains only the Task-1 ownership and TRAIN-order helpers.
"""

from __future__ import annotations

import dataclasses
from typing import Sequence

import torch

from . import task4_codec as codec


class Task4BSmokeError(RuntimeError):
    """Raised when a Task-4B runner invariant cannot be established."""

    def __init__(
        self,
        code: str,
        message: str,
        *,
        report: "Task4BExecutionReportV1 | None" = None,
    ) -> None:
        super().__init__(message)
        self.code = code
        self.report = report

    @property
    def report_json(self) -> str:
        return self.report.to_json() if self.report is not None else str(self)


@dataclasses.dataclass
class _SuccessfulStepCounter:
    """Internal count of optimizer steps that returned successfully."""

    value: int = 0

    def mark_success(self) -> None:
        self.value += 1


def _apply_optimizer_update(
    optimizer: torch.optim.Optimizer,
    loss: torch.Tensor,
    counter: _SuccessfulStepCounter,
) -> None:
    """Backpropagate and count only an optimizer step that succeeds."""

    loss.backward()
    optimizer.step()
    counter.mark_success()


def _ordered_train_samples(
    samples: Sequence[codec.CorpusSampleV1],
) -> tuple[codec.CorpusSampleV1, ...]:
    """Return only TRAIN samples in unsigned UTF-8 identity order."""

    train_samples = [sample for sample in samples if sample.partition == "train"]
    ordered = sorted(
        train_samples,
        key=lambda sample: sample.bc_sample_identity.encode("utf-8"),
    )
    if not ordered:
        raise Task4BSmokeError(
            "EMPTY_TRAIN_PARTITION",
            "admitted corpus has no train samples",
        )
    return tuple(ordered)
