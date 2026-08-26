from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "episodic" / "fixtures" / "phase1-pre-extraction-characterization.json"
BASELINE_COMMIT = "72c29009f107a2ebb172d85de1c70b38d2f007d8"

if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _git_head(source_root: Path) -> str:
    try:
        result = subprocess.run(
            ["git", "-C", str(source_root), "rev-parse", "HEAD"],
            check=True,
            capture_output=True,
            text=True,
        )
    except (OSError, subprocess.SubprocessError) as error:
        raise AssertionError(f"unable to resolve git HEAD for {source_root}: {error}") from error
    return result.stdout.strip()


def _source_root_for_raw_root(raw_root: Path) -> Path:
    resolved = raw_root.resolve()
    if len(resolved.parents) < 4:
        raise AssertionError(f"raw-artifact root is not under an OCGForge worktree: {raw_root}")
    source_root = resolved.parents[3]
    if not (source_root / "artifacts" / "episodic" / "phase1").is_dir():
        raise AssertionError(f"raw-artifact root is not under an OCGForge worktree: {raw_root}")
    return source_root


def _resolve_source_relative(source_root: Path, value: str, field: str) -> Path:
    relative = Path(value.replace("\\", "/"))
    if relative.is_absolute():
        raise AssertionError(f"manifest {field} must be source-root-relative")
    resolved_root = source_root.resolve()
    resolved = (resolved_root / relative).resolve()
    try:
        resolved.relative_to(resolved_root)
    except ValueError as error:
        raise AssertionError(f"manifest {field} escapes the source root") from error
    return resolved


def _assert_build_binding(
    source_root: Path,
    manifest: dict,
    *,
    label: str,
    expected_probe_sha256: str | None = None,
) -> None:
    build = manifest.get("build")
    if not isinstance(build, dict):
        raise AssertionError(f"{label} raw manifest has no build identity")
    probe_path = _resolve_source_relative(source_root, str(build.get("probe_path", "")), "build.probe_path")
    if not probe_path.is_file():
        raise AssertionError(f"{label} probe binary is missing: {probe_path}")
    actual_probe_sha256 = _sha256_file(probe_path)
    if actual_probe_sha256 != build.get("probe_sha256"):
        raise AssertionError(f"{label} probe binary hash does not match its raw manifest")
    if expected_probe_sha256 is not None and actual_probe_sha256 != expected_probe_sha256:
        raise AssertionError(f"{label} probe binary hash does not match the requested probe binding")

    build_directory = _resolve_source_relative(source_root, str(build.get("build_directory", "")), "build.build_directory")
    cache_path = build_directory / "CMakeCache.txt"
    if not cache_path.is_file():
        raise AssertionError(f"{label} CMake cache is missing: {cache_path}")
    if _sha256_file(cache_path) != build.get("cmake_cache_sha256"):
        raise AssertionError(f"{label} CMake cache hash does not match its raw manifest")
    build_identity = str(build.get("build_identity", ""))
    if build_identity and f"CMAKE_BUILD_TYPE:STRING={build_identity}" not in cache_path.read_text(encoding="utf-8"):
        raise AssertionError(f"{label} build identity is not proven by its CMake cache")


def _normalized_raw_manifest(manifest: dict) -> dict:
    normalized = json.loads(json.dumps(manifest))
    normalized["capture_worktree"] = "<immutable-worktree-a>"
    for artifact in normalized.get("artifacts", []):
        artifact["path"] = str(artifact["path"]).replace("\\", "/")
    for probe in normalized.get("negative_probes", []):
        for name in ("output_path", "stderr_path"):
            if name in probe:
                probe[name] = str(probe[name]).replace("\\", "/")
    return normalized


def _semantic_projection(value: dict) -> dict:
    projected = json.loads(json.dumps(value))
    projected.pop("build", None)
    projected.pop("source_base_commit", None)
    projected["collector"].pop("source_sha256", None)
    for job in projected["jobs"]:
        for record in job["records"]:
            record.pop("source_base_commit", None)
    return projected


def _artifact_suffix(path: str) -> str:
    parts = path.replace("\\", "/").split("/")
    if len(parts) < 5 or parts[:3] != ["artifacts", "episodic", "phase1"]:
        raise AssertionError(f"unexpected raw-artifact path: {path}")
    return "/".join(parts[4:])


def _hashes_for_post_comparable_artifacts(manifest: dict) -> dict[str, str]:
    result: dict[str, str] = {}
    for artifact in manifest["artifacts"]:
        suffix = _artifact_suffix(str(artifact["path"]))
        if suffix.endswith(".jsonl") or suffix == "shared/forced-unsupported.stderr.txt":
            result[suffix] = str(artifact["sha256"])
    return result


def _assert_manifest_binding(
    raw_root: Path,
    source_root: Path,
    expected_source_head: str,
    *,
    label: str,
    expected_probe_sha256: str | None = None,
) -> dict:
    manifest = _load_json(raw_root / "raw-artifact-manifest.json")
    if manifest.get("schema_version") != "ocgforge.episodic.phase1.raw_artifact_manifest.v1":
        raise AssertionError(f"{label} raw manifest has the wrong schema")
    actual_head = _git_head(source_root)
    if actual_head != expected_source_head:
        raise AssertionError(
            f"{label} source checkout is not at the requested head: {actual_head} != {expected_source_head}"
        )
    if manifest.get("source_base_commit") != actual_head:
        raise AssertionError(f"{label} raw manifest is not bound to the actual source head")
    _assert_build_binding(
        source_root,
        manifest,
        label=label,
        expected_probe_sha256=expected_probe_sha256,
    )
    return manifest


def _assert_post_binding(post_root: Path, expected_head: str, expected_probe_sha256: str) -> dict:
    manifest = _assert_manifest_binding(
        post_root,
        ROOT,
        expected_head,
        label="post-refactor",
        expected_probe_sha256=expected_probe_sha256,
    )
    return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description="Run the strict Phase-1 baseline/post-refactor acceptance gate")
    parser.add_argument("--baseline-raw-root", type=Path, required=True)
    parser.add_argument("--post-raw-root", type=Path, required=True)
    parser.add_argument("--expected-head", required=True)
    parser.add_argument("--expected-probe-sha256", required=True)
    args = parser.parse_args()

    if not args.baseline_raw_root.is_dir() or not args.post_raw_root.is_dir():
        raise AssertionError("both immutable baseline and post-refactor raw roots are required")
    if args.expected_head == BASELINE_COMMIT:
        raise AssertionError("post-refactor acceptance must be bound to a different final source head")

    from tools.episodic.capture_phase1_characterization import collect_characterization

    expected = _load_json(FIXTURE)
    baseline = collect_characterization(args.baseline_raw_root, require_baseline=True)
    if baseline != expected:
        raise AssertionError("immutable baseline collector output differs from the checked fixture")

    baseline_manifest = _assert_manifest_binding(
        args.baseline_raw_root,
        _source_root_for_raw_root(args.baseline_raw_root),
        BASELINE_COMMIT,
        label="baseline",
    )
    provenance = _load_json(ROOT / "tests" / "episodic" / "fixtures" / "phase1-pre-extraction-characterization.provenance.json")
    if provenance.get("fixture_sha256") != _sha256_file(FIXTURE):
        raise AssertionError("baseline provenance does not bind the checked fixture bytes")
    collector_path = ROOT / "tools" / "episodic" / "capture_phase1_characterization.py"
    if provenance.get("collector_source_sha256") != _sha256_file(collector_path):
        raise AssertionError("baseline provenance does not bind the collector source bytes")
    if provenance.get("raw_artifact_manifest") != _normalized_raw_manifest(baseline_manifest):
        raise AssertionError("baseline provenance does not bind the complete raw-artifact manifest")

    post_manifest = _assert_post_binding(args.post_raw_root, args.expected_head, args.expected_probe_sha256)
    post = collect_characterization(args.post_raw_root, require_baseline=False)
    if _semantic_projection(post) != _semantic_projection(expected):
        raise AssertionError("post-refactor semantic characterization differs from the immutable fixture")

    baseline_hashes = _hashes_for_post_comparable_artifacts(baseline_manifest)
    post_hashes = _hashes_for_post_comparable_artifacts(post_manifest)
    if baseline_hashes != post_hashes:
        raise AssertionError("post-refactor JSONL/diagnostic artifact hashes differ from the baseline")

    print(
        json.dumps(
            {
                "acceptance": "PASS",
                "source_base_commit": args.expected_head,
                "baseline_commit": BASELINE_COMMIT,
                "comparable_artifact_count": len(baseline_hashes),
                "job_count": post["summary"]["job_count"],
                "record_count": post["summary"]["record_count"],
                "candidate_count_max": post["summary"]["candidate_count_max"],
            },
            sort_keys=True,
        )
    )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (AssertionError, OSError, ValueError, json.JSONDecodeError) as error:
        print(f"PHASE1_ACCEPTANCE_FAILURE: {error}", file=sys.stderr)
        raise SystemExit(1)
