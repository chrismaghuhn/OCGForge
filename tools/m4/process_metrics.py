"""Small stdlib-only process metrics helpers for M4 coordinator runs."""

from __future__ import annotations

from dataclasses import dataclass
import ctypes
import os
from pathlib import Path
import threading
import time
from typing import Callable, Iterable


NOT_MEASURED = "NOT_MEASURED"


def read_working_set_bytes(pid: int) -> int | None:
    """Return one process working-set sample, or ``None`` if unavailable."""

    if os.name != "nt" or pid <= 0:
        return None

    try:
        from ctypes import wintypes

        PROCESS_QUERY_LIMITED_INFORMATION = 0x1000
        PROCESS_VM_READ = 0x0010
        handle = ctypes.windll.kernel32.OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ,
            False,
            pid,
        )
        if not handle:
            return None

        class ProcessMemoryCountersEx(ctypes.Structure):
            _fields_ = [
                ("cb", wintypes.DWORD),
                ("PageFaultCount", wintypes.DWORD),
                ("PeakWorkingSetSize", ctypes.c_size_t),
                ("WorkingSetSize", ctypes.c_size_t),
                ("QuotaPeakPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPagedPoolUsage", ctypes.c_size_t),
                ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t),
                ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                ("PagefileUsage", ctypes.c_size_t),
                ("PeakPagefileUsage", ctypes.c_size_t),
                ("PrivateUsage", ctypes.c_size_t),
            ]

        counters = ProcessMemoryCountersEx()
        counters.cb = ctypes.sizeof(counters)
        get_process_memory_info = ctypes.windll.psapi.GetProcessMemoryInfo
        get_process_memory_info.argtypes = [
            wintypes.HANDLE,
            ctypes.POINTER(ProcessMemoryCountersEx),
            wintypes.DWORD,
        ]
        get_process_memory_info.restype = wintypes.BOOL
        if not get_process_memory_info(handle, ctypes.byref(counters), counters.cb):
            return None
        return int(counters.WorkingSetSize)
    except (AttributeError, OSError, TypeError, ValueError):
        return None
    finally:
        try:
            if os.name == "nt" and "handle" in locals() and handle:
                ctypes.windll.kernel32.CloseHandle(handle)
        except (AttributeError, OSError):
            pass


@dataclass(frozen=True)
class ProcessMetricsSnapshot:
    """Observed coordinator/worker process metrics."""

    process_count: int | str
    peak_total_working_set_bytes: int | str
    peak_worker_working_set_bytes: int | str
    memory_per_active_environment_bytes: int | str


class ProcessMetricsSampler:
    """Periodically samples worker working sets without a dependency stack."""

    def __init__(
        self,
        pid_supplier: Callable[[], Iterable[int]],
        *,
        interval_seconds: float = 0.05,
    ) -> None:
        self._pid_supplier = pid_supplier
        self._interval_seconds = interval_seconds
        self._stop = threading.Event()
        self._thread: threading.Thread | None = None
        self._lock = threading.Lock()
        self._peak_by_pid: dict[int, int] = {}
        self._peak_total = 0
        self._last_process_count: int | None = None
        self._measured = False

    def start(self) -> None:
        if self._thread is not None:
            return
        self._thread = threading.Thread(
            target=self._sample_loop,
            name="ocgforge-m4-process-metrics",
            daemon=True,
        )
        self._thread.start()

    def stop(self) -> ProcessMetricsSnapshot:
        self._stop.set()
        if self._thread is not None:
            self._thread.join(timeout=max(1.0, self._interval_seconds * 10))
        self._sample_once()
        with self._lock:
            process_count = self._last_process_count
            peak_total = self._peak_total
            peak_worker = max(self._peak_by_pid.values(), default=0)
            measured = self._measured
        if not measured:
            return ProcessMetricsSnapshot(
                process_count=NOT_MEASURED,
                peak_total_working_set_bytes=NOT_MEASURED,
                peak_worker_working_set_bytes=NOT_MEASURED,
                memory_per_active_environment_bytes=NOT_MEASURED,
            )
        worker_count = max(1, (process_count or 1) - 1)
        return ProcessMetricsSnapshot(
            process_count=process_count if process_count is not None else NOT_MEASURED,
            peak_total_working_set_bytes=peak_total,
            peak_worker_working_set_bytes=peak_worker,
            memory_per_active_environment_bytes=peak_total // worker_count,
        )

    def _sample_loop(self) -> None:
        while not self._stop.is_set():
            self._sample_once()
            self._stop.wait(self._interval_seconds)

    def _sample_once(self) -> None:
        try:
            pids = [int(pid) for pid in self._pid_supplier() if int(pid) > 0]
        except (TypeError, ValueError):
            return
        values: list[int] = []
        for pid in pids:
            value = read_working_set_bytes(pid)
            if value is None:
                continue
            values.append(value)
            with self._lock:
                self._peak_by_pid[pid] = max(value, self._peak_by_pid.get(pid, 0))
        with self._lock:
            self._last_process_count = len(pids) + 1
            if values:
                self._measured = True
                self._peak_total = max(self._peak_total, sum(values))


def stderr_size(path: str | os.PathLike[str]) -> int | str:
    """Return a diagnostic file size without turning a missing file into zero."""

    try:
        return Path(path).stat().st_size
    except (FileNotFoundError, OSError):
        return NOT_MEASURED


__all__ = [
    "NOT_MEASURED",
    "ProcessMetricsSampler",
    "ProcessMetricsSnapshot",
    "read_working_set_bytes",
    "stderr_size",
]
