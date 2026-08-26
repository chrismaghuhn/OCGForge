from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests" / "episodic" / "fixtures" / "phase1-pre-extraction-characterization.json"
BASELINE_COMMIT = "72c29009f107a2ebb172d85de1c70b38d2f007d8"

if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))


def _load_json(path: Path) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


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


def _assert_post_binding(post_root: Path, expected_head: str, expected_probe_sha256: str) -> dict:
    manifest = _load_json(post_root / "raw-artifact-manifest.json")
    if manifest.get("schema_version") != "ocgforge.episodic.phase1.raw_artifact_manifest.v1":
        raise AssertionError("post-refactor raw manifest has the wrong schema")
    if manifest.get("source_base_commit") != expected_head:
        raise AssertionError(
            "post-refactor raw manifest is not bound to the requested final source head: "
            f"{manifest.get('source_base_commit')} != {expected_head}"
        )
    build = manifest.get("build", {})
    if build.get("probe_sha256") != expected_probe_sha256:
        raise AssertionError("post-refactor raw manifest is not bound to the requested probe binary")
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

    post_manifest = _assert_post_binding(args.post_raw_root, args.expected_head, args.expected_probe_sha256)
    post = collect_characterization(args.post_raw_root, require_baseline=False)
    if _semantic_projection(post) != _semantic_projection(expected):
        raise AssertionError("post-refactor semantic characterization differs from the immutable fixture")

    baseline_manifest = _load_json(args.baseline_raw_root / "raw-artifact-manifest.json")
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
