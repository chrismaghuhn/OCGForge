from __future__ import annotations

import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
INCLUDE_ROOT = ROOT / "include"

PUBLIC_DECISION = INCLUDE_ROOT / "ygo" / "environment" / "public_decision.hpp"
PUBLIC_OBSERVATION = INCLUDE_ROOT / "ygo" / "environment" / "public_environment_observation.hpp"
EPISODIC_ENVIRONMENT = INCLUDE_ROOT / "ygo" / "environment" / "episodic_environment.hpp"
PLAYER_OBSERVATION = INCLUDE_ROOT / "ygo" / "observation" / "player_observation.hpp"
POLICY_HEADERS = [INCLUDE_ROOT / "ygo" / "policy" / "policy.hpp"]
SELECTOR_HEADERS = [PUBLIC_DECISION, *POLICY_HEADERS]
POLICY_RUNNER = INCLUDE_ROOT / "ygo" / "policy" / "runner.hpp"

FORBIDDEN_SELECTOR_SYMBOLS = (
    "DecisionFrame",
    "SubmissionToken",
    "CoreHost",
    "PlayerObservation",
    "engine_step_index",
    "semantic_key",
    "raw_message",
    "response_bytes",
)
TRANSITIVE_FORBIDDEN_SYMBOLS = tuple(
    symbol for symbol in FORBIDDEN_SELECTOR_SYMBOLS if symbol != "PlayerObservation"
)


def fail(message: str) -> None:
    raise AssertionError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def strip_comments(source: str) -> str:
    source = re.sub(r"/\*.*?\*/", "", source, flags=re.DOTALL)
    return re.sub(r"//[^\r\n]*", "", source)


def read(path: Path) -> str:
    require(path.is_file(), f"required header is missing: {path.relative_to(ROOT)}")
    return path.read_text(encoding="utf-8")


def resolve_quoted_include(includer: Path, include_name: str) -> Path | None:
    candidates = (
        includer.parent / include_name,
        INCLUDE_ROOT / include_name,
        ROOT / include_name,
    )
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    return None


def transitive_local_headers(start: Path) -> set[Path]:
    pending = [start.resolve()]
    seen: set[Path] = set()
    include_pattern = re.compile(r'^\s*#include\s+"([^"]+)"\s*$', re.MULTILINE)
    while pending:
        current = pending.pop()
        if current in seen:
            continue
        seen.add(current)
        source = read(current)
        for match in include_pattern.finditer(source):
            resolved = resolve_quoted_include(current, match.group(1))
            if resolved is not None and resolved not in seen:
                pending.append(resolved)
    return seen


def check_selector_headers() -> None:
    for header in SELECTOR_HEADERS:
        source = strip_comments(read(header))
        for symbol in FORBIDDEN_SELECTOR_SYMBOLS:
            require(
                symbol not in source,
                f"selector-facing header exposes forbidden symbol {symbol}: "
                f"{header.relative_to(ROOT)}",
            )
        require(
            '"ygo/environment/episodic_environment.hpp"' not in source,
            f"selector-facing header includes lifecycle metadata: {header.relative_to(ROOT)}",
        )

        transitive = transitive_local_headers(header)
        for transitive_header in transitive:
            transitive_source = strip_comments(read(transitive_header))
            for symbol in TRANSITIVE_FORBIDDEN_SYMBOLS:
                require(
                    symbol not in transitive_source,
                    f"transitive selector header exposes forbidden symbol {symbol}: "
                    f"{transitive_header.relative_to(ROOT)} (from {header.relative_to(ROOT)})",
                )
        require(
            PLAYER_OBSERVATION.resolve() not in transitive,
            f"PlayerObservation is transitively reachable from selector header: "
            f"{header.relative_to(ROOT)}",
        )


def check_public_observation_boundary() -> None:
    source = strip_comments(read(PUBLIC_OBSERVATION))
    for symbol in FORBIDDEN_SELECTOR_SYMBOLS:
        if symbol == "PlayerObservation":
            continue
        require(
            symbol not in source,
            f"public observation header exposes forbidden symbol {symbol}",
        )
    require(
        '"ygo/observation/player_observation.hpp"' not in source,
        "public observation header directly includes PlayerObservation",
    )
    require(
        "struct PlayerObservation;" in source,
        "public observation header no longer forward-declares PlayerObservation",
    )
    require(
        '"ygo/observation/observed_zone.hpp"' in source,
        "public observation header lost its narrow locator dependency",
    )


def check_extracted_dtos() -> None:
    public_source = strip_comments(read(PUBLIC_DECISION))
    episodic_source = strip_comments(read(EPISODIC_ENVIRONMENT))
    for declaration in (
        "EnvironmentDecisionKind",
        "EnvironmentActionKind",
        "EnvironmentActionCandidate",
        "EnvironmentContinuationView",
        "EnvironmentDecisionRequest",
    ):
        require(
            re.search(rf"\b(?:enum class|struct)\s+{declaration}\b", public_source),
            f"public DTO declaration is missing from public_decision.hpp: {declaration}",
        )
        require(
            not re.search(rf"\b(?:enum class|struct)\s+{declaration}\b", episodic_source),
            f"public DTO declaration remains in episodic_environment.hpp: {declaration}",
        )
    require(
        '"ygo/environment/public_decision.hpp"' in read(EPISODIC_ENVIRONMENT),
        "episodic_environment.hpp does not include public_decision.hpp",
    )
    require(
        '"ygo/observation/observed_player_globals.hpp"' in read(PLAYER_OBSERVATION),
        "player_observation.hpp does not include observed_player_globals.hpp",
    )


def check_forged_selector_is_not_a_production_input() -> None:
    source = strip_comments(read(POLICY_RUNNER))
    require(
        "RandomLegalPolicySession" in source and "sessions" in source,
        "production runner does not own concrete RandomLegal sessions",
    )
    for forbidden in ("PolicySelector", "std::function", "selectors", "execution_bindings"):
        require(
            forbidden not in source,
            f"production runner exposes an arbitrary selector/binding seam: {forbidden}",
        )


def main() -> None:
    check_selector_headers()
    check_public_observation_boundary()
    check_extracted_dtos()
    check_forged_selector_is_not_a_production_input()
    print("policy_boundary=ok")


if __name__ == "__main__":
    main()
