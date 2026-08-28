#!/usr/bin/env python3
"""Run the local Episodic V2 acceptance campaign and emit explicit evidence."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Iterable


ROOT = Path(__file__).resolve().parents[2]
CONTRACT_ID = "ocgforge.episodic_environment.v2"
ENVIRONMENT_ID_SCHEMA = "ocgforge.environment_identity.v2"
ANOMALY = "HISTORICAL_UNCLASSIFIED_ANOMALY"
OUTPUT_ROOT = "artifacts/episodic/v2/"
G32_RUN_PREFIXES = ("g32-run-a/", "g32-run-b/")

_CTEST_DURATION = re.compile(r"(?P<prefix>\b(?:Passed|Failed)\s+)\d+(?:\.\d+)?\s+sec\b")
_CTEST_TOTAL_DURATION = re.compile(r"(?P<prefix>\bTotal Test time \(real\) = )\d+(?:\.\d+)?\s+sec\b")
_CTEST_LABEL_DURATION = re.compile(r"(?P<prefix>\bM4_[A-Z0-9_]+\s*=\s*)\d+(?:\.\d+)?\s+sec\*proc\b")
_PYTHON_DURATION = re.compile(r"(?P<prefix>\bRan \d+ tests in )\d+(?:\.\d+)?s\b")
_CTEST_TEST_RESULT = re.compile(
    r"^\s*\d+/\d+\s+Test\s+#\d+:\s+(?P<name>\S+).*?(?P<status>Passed|Failed)\b"
)


def git_value(*args: str) -> str:
    completed = subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True)
    return completed.stdout.strip()


def relative_path(path: Path) -> str:
    return path.resolve().relative_to(ROOT.resolve()).as_posix()


def canonicalize_evidence_line(line: str) -> str:
    """Remove host paths and runtime-only timings from reproducible evidence."""
    canonical = line
    root = str(ROOT)
    for root_form in (root, root.replace("\\", "/"), root.replace("\\", "\\\\")):
        canonical = canonical.replace(root_form, "<REPO>")
    canonical = re.sub(
        r"(?P<path><REPO>[^\"\s]*)",
        lambda match: re.sub(r"\\+", "/", match.group("path")),
        canonical,
    )
    canonical = _CTEST_DURATION.sub(r"\g<prefix><elapsed> sec", canonical)
    canonical = _CTEST_TOTAL_DURATION.sub(r"\g<prefix><elapsed> sec", canonical)
    canonical = _CTEST_LABEL_DURATION.sub(r"\g<prefix><elapsed> sec*proc", canonical)
    canonical = _PYTHON_DURATION.sub(r"\g<prefix><elapsed>s", canonical)
    return canonical


def display_command(command: list[str]) -> list[str]:
    """Render repository-local absolute paths canonically in evidence."""
    rendered: list[str] = []
    for item in command:
        value = str(item)
        candidate = Path(value)
        if candidate.is_absolute():
            try:
                rendered.append(relative_path(candidate))
                continue
            except ValueError:
                pass
        normalized = value.replace("\\", "/")
        if normalized.startswith(OUTPUT_ROOT):
            suffix = normalized[len(OUTPUT_ROOT) :]
            for run_prefix in G32_RUN_PREFIXES:
                if suffix.startswith(run_prefix):
                    suffix = suffix[len(run_prefix) :]
                    break
            rendered.append("<OUTPUT>/" + suffix)
            continue
        rendered.append(canonicalize_evidence_line(normalized))
    return rendered


def parse_ctest_test_results(output: str) -> dict[str, str]:
    """Extract explicit per-test CTest results from the normal summary output."""
    results: dict[str, str] = {}
    for line in output.splitlines():
        match = _CTEST_TEST_RESULT.match(line)
        if match is None:
            continue
        results[match.group("name")] = "PASS" if match.group("status") == "Passed" else "FAIL"
    return results


def run_command(
    command: list[str],
    *,
    label: str,
    timeout: int = 3600,
    blocked_returncodes: set[int] | None = None,
) -> dict[str, Any]:
    completed = subprocess.run(command, cwd=ROOT, check=False, capture_output=True, text=True, timeout=timeout)
    stderr_lines = [canonicalize_evidence_line(line) for line in completed.stderr.splitlines() if line]
    stdout_lines = [canonicalize_evidence_line(line) for line in completed.stdout.splitlines() if line]
    result = "PASS" if completed.returncode == 0 else "FAIL"
    if blocked_returncodes is not None and completed.returncode in blocked_returncodes:
        result = "BLOCKED"
    return {
        "label": label,
        "command": display_command(command),
        "returncode": completed.returncode,
        "stdout_tail": stdout_lines[-8:],
        "stderr_tail": stderr_lines[-8:],
        "result": result,
        "ctest_tests": parse_ctest_test_results(completed.stdout),
    }


def gate(gate_id: str, result: str, command: str, reason: str, runs: Iterable[dict[str, Any]]) -> dict[str, Any]:
    return {
        "gate": gate_id,
        "result": result,
        "command": command,
        "reason": reason,
        "runs": list(runs),
    }


def gate_from_ctest_evidence(
    gate_id: str,
    run: dict[str, Any],
    expected_tests: set[str],
    reason: str,
) -> dict[str, Any]:
    """Bind a gate to the exact named tests observed in one CTest invocation."""
    observed = run.get("ctest_tests", {})
    missing = sorted(test for test in expected_tests if test not in observed)
    failed = sorted(test for test in expected_tests if observed.get(test) != "PASS" and test not in missing)
    result = "PASS" if run.get("returncode") == 0 and not missing and not failed else "FAIL"
    details = reason
    if missing:
        details += "; missing executed test(s): " + ", ".join(missing)
    if failed:
        details += "; failed test(s): " + ", ".join(failed)
    command = "ctest exact tests: " + ", ".join(sorted(expected_tests))
    return gate(gate_id, result, command, details, [run])


def run_ctest(build_dir: Path, regex: str, label: str) -> dict[str, Any]:
    return run_command(
        ["ctest", "--test-dir", str(build_dir), "-R", regex, "--output-on-failure"],
        label=label,
        timeout=7200,
    )


def artifact_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_json_artifact(path: Path) -> dict[str, Any] | None:
    if not path.is_file():
        return None
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return value if isinstance(value, dict) else None


def run_acceptance(probe: Path, build_dir: Path, output_dir: Path, run_all: bool) -> dict[str, Any]:
    probe = probe.resolve()
    build_dir = build_dir.resolve()
    output_dir = output_dir.resolve()
    runs: dict[str, dict[str, Any]] = {}

    focused = run_ctest(
        build_dir,
        "^(episodic_.*|episode_driver_.*|normative_prerequisites_test|public_action_identity_test|m4_simulation_contract_test|ygo_episodic_probe_smoke)$",
        "focused-episodic-ctest",
    )
    runs[focused["label"]] = focused

    worker = run_command(
        [sys.executable, "tests/episodic/episodic_worker_determinism.py", "--probe", relative_path(probe), "--max-actions", "4", "--workers", "16"],
        label="G21-worker-determinism",
        timeout=7200,
    )
    runs[worker["label"]] = worker

    internal_witness_path = output_dir / "g28_internal_candidate_witness.json"
    internal_witness = run_command(
        [relative_path(build_dir / "ygo_episodic_internal_witness.exe"), "--max-actions", "64", "--seeds", "4", "--output", relative_path(internal_witness_path)],
        label="G28-internal-witness",
        timeout=7200,
    )
    runs[internal_witness["label"]] = internal_witness

    witness_path = output_dir / "g28_max_candidate_witness.json"
    witness = run_command(
        [sys.executable, "tests/episodic/episodic_witness_discovery.py", "--probe", relative_path(probe), "--output", relative_path(witness_path), "--max-actions", "64", "--seeds", "4"],
        label="G28-witness-discovery",
        timeout=7200,
    )
    runs[witness["label"]] = witness

    reward_path = output_dir / "g29_reward_independence.json"
    reward = run_command(
        [sys.executable, "tests/episodic/reward_independence.py", "--probe", relative_path(probe), "--output", relative_path(reward_path)],
        label="G29-reward-independence",
        timeout=7200,
    )
    runs[reward["label"]] = reward

    interleaving_path = output_dir / "g04_reset_interleaving.json"
    interleaving = run_command(
        [relative_path(build_dir / "ygo_episodic_reset_probe.exe"), "--mode", "interleaving", "--output", relative_path(interleaving_path)],
        label="G04-reset-interleaving",
        timeout=7200,
    )
    runs[interleaving["label"]] = interleaving

    soak_path = output_dir / "g05_reset_soak.json"
    soak = run_command(
        [relative_path(build_dir / "ygo_episodic_reset_probe.exe"), "--mode", "soak", "--episodes", "500", "--output", relative_path(soak_path)],
        label="G05-reset-soak",
        timeout=14400,
        blocked_returncodes={2},
    )
    runs[soak["label"]] = soak

    rules = run_command(
        [sys.executable, "tools/verify_rules_bundle.py", "--lock", "third_party/rules_bundle.lock.json", "--cache", r"C:\yogiohML\.cache\rules_bundle"],
        label="rules-bundle-verification",
    )
    runs[rules["label"]] = rules

    full: dict[str, Any] | None = None
    python_suite: dict[str, Any] | None = None
    if run_all:
        full = run_ctest(build_dir, ".*", "full-ctest")
        runs[full["label"]] = full
        python_suite = run_command(
            [sys.executable, "-m", "unittest", "discover", "-s", "tests/python", "-v"],
            label="python-suite",
            timeout=7200,
        )
        runs[python_suite["label"]] = python_suite

    worker_ok = worker["returncode"] == 0
    witness_ok = witness["returncode"] == 0 and witness_path.is_file()
    internal_witness_ok = internal_witness["returncode"] == 0 and internal_witness_path.is_file()
    public_witness_value = read_json_artifact(witness_path)
    internal_witness_value = read_json_artifact(internal_witness_path)
    g28_ok = (
        witness_ok
        and internal_witness_ok
        and public_witness_value is not None
        and internal_witness_value is not None
        and public_witness_value.get("candidate_domain_max")
        == internal_witness_value.get("candidate_domain_max")
        and public_witness_value.get("candidate_max_total")
        == internal_witness_value.get("candidate_max_total")
    )
    reward_ok = reward["returncode"] == 0 and reward_path.is_file()
    interleaving_ok = interleaving["returncode"] == 0 and interleaving_path.is_file()
    soak_ok = soak["returncode"] == 0 and soak_path.is_file()
    soak_blocked = soak["result"] == "BLOCKED" and soak_path.is_file()
    rules_ok = rules["returncode"] == 0
    full_ok = full is not None and full["returncode"] == 0
    python_ok = python_suite is not None and python_suite["returncode"] == 0

    if not run_all:
        g31_result = "NOT_RUN"
        g31_reason = "full local acceptance was not requested"
    elif full_ok and python_ok and rules_ok:
        g31_result = "BLOCKED"
        g31_reason = "local gates passed, but native-MSVC/hosted equivalence is unavailable in this environment"
    else:
        g31_result = "FAIL"
        g31_reason = "one or more requested local prerequisite suites failed; native-MSVC/hosted equivalence is also unavailable"

    gates = [
        gate("G01", "PASS" if worker_ok else "FAIL", "G21-worker-determinism", "independent public reset/frame identities", [worker]),
        gate("G02", "PASS" if worker_ok else "FAIL", "G21-worker-determinism", "distinct seed/seat corpus identities", [worker]),
        gate_from_ctest_evidence("G03", focused, {"episodic_lifecycle_test"}, "fresh incarnation/token behavior covered by the lifecycle test"),
        gate("G04", "PASS" if interleaving_ok else "FAIL", "ygo_episodic_reset_probe --mode interleaving", "A-B-C-A-D-A persistent reset isolation with fresh references", [interleaving]),
        gate("G05", "PASS" if soak_ok else "BLOCKED" if soak_blocked else "FAIL", "ygo_episodic_reset_probe --mode soak --episodes 500", "500-episode persistent reset/resource isolation with mixed closures; public continuation coverage is required for PASS", [soak]),
        gate_from_ctest_evidence("G06", focused, {"episodic_environment_test", "episodic_environment_v2_public_projection_test", "episodic_paired_world_test"}, "public frame/player/observation coupling"),
        gate_from_ctest_evidence("G07", focused, {"episodic_environment_v2_public_projection_test", "episodic_paired_world_test"}, "complete public projection preserves the exercised authoritative domain"),
        gate("G08", "PASS" if worker_ok else "FAIL", "G21-worker-determinism", "ordered public domains reproduce across processes", [worker]),
        gate_from_ctest_evidence("G09", focused, {"episodic_environment_test", "public_action_identity_test"}, "independent public digest recomputation and mutation coverage"),
        gate_from_ctest_evidence("G10", focused, {"episodic_rejection_test"}, "stale token/decision validation precedes membership"),
        gate_from_ctest_evidence("G11", focused, {"episodic_rejection_test"}, "unknown public key rejection"),
        gate_from_ctest_evidence("G12", focused, {"episodic_lifecycle_test", "episodic_interrupt_test"}, "closed-state and reset lifecycle rejection"),
        gate_from_ctest_evidence("G13", focused, {"episodic_rejection_test"}, "caller-side rejection certifies zero mutation"),
        gate_from_ctest_evidence("G14", focused, {"episode_driver_seam_test", "episode_driver_ownership_guard", "episode_driver_stale_key_test", "episode_driver_tribute_integration_test", "episodic_replay_test"}, "continuation seam and public replay prefix"),
        gate_from_ctest_evidence("G15", focused, {"episode_driver_seam_test", "episode_driver_ownership_guard", "episode_driver_stale_key_test", "episode_driver_tribute_integration_test"}, "final continuation response path retained"),
        gate_from_ctest_evidence("G16", focused, {"episodic_environment_test"}, "atomic response submission classification"),
        gate_from_ctest_evidence("G17", focused, {"episodic_budget_test"}, "semantic budget interruption"),
        gate_from_ctest_evidence("G18", focused, {"episodic_budget_test"}, "engine process budget interruption"),
        gate_from_ctest_evidence("G19", focused, {"m4_simulation_contract_test", "episode_driver_seam_test", "episode_driver_ownership_guard", "episode_driver_stale_key_test", "episode_driver_tribute_integration_test"}, "canonical simulation regression suite"),
        gate_from_ctest_evidence("G20", focused, {"episodic_replay_test"}, "public-key replay reproduces public frames"),
        gate("G21", "PASS" if worker_ok else "FAIL", "episodic_worker_determinism.py", "independent-process public determinism", [worker]),
        gate_from_ctest_evidence("G22", focused, {"episodic_paired_world_test"}, "facade-level paired-world/public redaction contract"),
        gate_from_ctest_evidence("G23", focused, {"episodic_paired_world_test", "episodic_terminal_privacy_test"}, "paired terminal views obey perspective-safe projection"),
        gate_from_ctest_evidence("G24", focused, {"episodic_budget_test", "episodic_terminal_privacy_test"}, "interruptions are not outcomes"),
        gate_from_ctest_evidence("G25", focused, {"episodic_interrupt_test"}, "administrative cancellation"),
        gate_from_ctest_evidence("G26", focused, {"episodic_fault_injection_test"}, "driver fault closes fail-closed"),
        gate_from_ctest_evidence("G27", focused, {"episodic_reset_after_failure_test"}, "facade reset after a typed public failure restores fresh operation"),
        gate("G28", "PASS" if g28_ok else "FAIL", "internal witness + episodic_witness_discovery.py", "accepted internal v1 tie-break, complete public projection witness, and independent replay agree on the maximum", [internal_witness, witness]),
        gate("G29", "PASS" if reward_ok else "FAIL", "reward_independence.py", "external reward policies remain outside environment values", [reward]),
        gate_from_ctest_evidence("G30", focused, {"episodic_identity_test", "episodic_terminal_privacy_test"}, "versioned V2 identity rejection and golden IDs"),
        gate("G31", g31_result, "full-ctest + repository Python suites", g31_reason, [item for item in (full, python_suite, rules) if item is not None]),
        gate("G32", "BLOCKED", "not run", "clean exact-head checkout/render/hash gate is not yet executed", []),
    ]

    manifest = {
        "schema": "ocgforge.episodic_acceptance_manifest.v2",
        "source_head": git_value("rev-parse", "HEAD"),
        "source_base": git_value("rev-parse", "origin/main"),
        "contract_id": CONTRACT_ID,
        "environment_identity_schema_id": ENVIRONMENT_ID_SCHEMA,
        "historical_trace_anomaly": ANOMALY,
        "probe": relative_path(probe),
        "build_directory": relative_path(build_dir),
        "rules_bundle_verification": rules["result"],
        "gates": gates,
        "runs": runs,
        "artifacts": {},
    }
    if witness_path.is_file():
        manifest["artifacts"]["g28_max_candidate_witness.json"] = artifact_sha256(witness_path)
    if internal_witness_path.is_file():
        manifest["artifacts"]["g28_internal_candidate_witness.json"] = artifact_sha256(internal_witness_path)
    if reward_path.is_file():
        manifest["artifacts"]["g29_reward_independence.json"] = artifact_sha256(reward_path)
    if interleaving_path.is_file():
        manifest["artifacts"]["g04_reset_interleaving.json"] = artifact_sha256(interleaving_path)
    if soak_path.is_file():
        manifest["artifacts"]["g05_reset_soak.json"] = artifact_sha256(soak_path)
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build" / "dev-windows")
    parser.add_argument("--output", type=Path, default=ROOT / "artifacts" / "episodic" / "v2")
    parser.add_argument("--all", action="store_true")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = run_acceptance(args.probe, args.build_dir, args.output, args.all)
    rendered = json.dumps(manifest, sort_keys=True, separators=(",", ":")) + "\n"
    output_path = args.output / "episodic_acceptance_manifest.json"
    output_path.write_text(rendered, encoding="utf-8", newline="\n")
    sys.stdout.write(rendered)
    return 0 if all(item["result"] == "PASS" for item in manifest["gates"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
