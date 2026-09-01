from pathlib import Path
import sys


FORBIDDEN_IDENTIFIERS = (
    "PlayerObservation",
    "CoreHost",
    "semantic_key",
    "SubmissionToken",
    "exact_response_bytes",
    "EnvironmentContinuationView",
    "raw_message_hash",
    "continuation_id",
    "ModelBatchLayout",
    "PyTorch",
    "JAX",
    "NumPy",
    "Trackio",
    "Accelerate",
    "neural_network",
    "Behavior_Cloning",
    "optimizer",
    "loss",
    "RL",
)


def main() -> int:
    roots = [Path(argument) for argument in sys.argv[1:]]
    violations = []
    for root in roots:
        for path in sorted(root.rglob("*")):
            if path.suffix not in {".cpp", ".hpp"}:
                continue
            text = path.read_text(encoding="utf-8")
            for identifier in FORBIDDEN_IDENTIFIERS:
                if identifier in text:
                    violations.append(f"{path}: {identifier}")
    if violations:
        for violation in violations:
            print(violation)
        return 1
    print("logical_model_public_boundary=passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
