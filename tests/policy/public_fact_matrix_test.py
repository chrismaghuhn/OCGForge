from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
MATRIX = ROOT / "docs" / "p4a" / "P4A_PUBLIC_FACT_MATRIX.md"
RESEARCH_ROOT = ROOT / "docs" / "research" / "teacher_strategy"

REQUIRED_ROWS = (
    "P4A-G00-01",
    "P4A-G00-02",
    "P4A-G00-03",
    "P4A-G00-04",
    "P4A-G00-05",
    "P4A-G00-06",
    "P4A-G00-07",
    "P4A-G00-08",
    "P4A-G00-09",
    "P4A-G00-10",
    "P4A-G00-11",
    "P4A-G00-12",
    "P4A-G00-13",
    "P4A-G00-14",
    "P4A-G00-15",
    "P4A-G00-16",
    "P4A-G00-17",
    "P4A-G00-18",
    "P4A-G00-19",
    "P4A-G00-20",
    "P4A-G00-21",
    "P4A-G00-22",
    "P4A-G00-23",
    "P4A-G00-24",
    "P4A-G00-25",
    "P4A-G00-26",
    "P4A-G00-27",
    "P4A-G00-28",
    "P4A-G00-29",
    "P4A-G00-30",
    "P4A-G00-31",
    "P4A-G00-32",
)
ALLOWED_AVAILABILITY = {"DIRECT", "SAFE_DERIVATION", "BLOCKED"}
EXPECTED_BLOCKED_ROWS = {
    "P4A-G00-19",
    "P4A-G00-28",
    "P4A-G00-29",
    "P4A-G00-30",
    "P4A-G00-31",
}


def fail(message: str) -> None:
    raise AssertionError(message)


def require(condition: bool, message: str) -> None:
    if not condition:
        fail(message)


def read_matrix_rows() -> list[dict[str, str]]:
    require(MATRIX.is_file(), f"required public-fact matrix is missing: {MATRIX.relative_to(ROOT)}")
    lines = MATRIX.read_text(encoding="utf-8").splitlines()
    header_index = next(
        (index for index, line in enumerate(lines) if line.startswith("| ID |")),
        None,
    )
    require(header_index is not None, "public-fact matrix table header is missing")
    expected_header = (
        "| ID | Teacher requirement | Exact public source | Availability | Evidence | Blocked reason |"
    )
    require(lines[header_index].strip() == expected_header, "public-fact matrix header changed")

    rows: list[dict[str, str]] = []
    for line in lines[header_index + 2 :]:
        if not line.startswith("|"):
            continue
        cells = [cell.strip() for cell in line.strip().strip("|").split("|")]
        require(len(cells) == 6, f"public-fact matrix row has the wrong column count: {line}")
        rows.append(dict(zip(("id", "requirement", "source", "availability", "evidence", "blocked"), cells)))
    return rows


def check_research_basis() -> None:
    expected = {
        "README.md",
        "OCGForge_Deterministic_Teacher_Architecture_2026-08-28.md",
        "WindBot_Strategy_Mining_2026-08-28.md",
        "Teacher_Research_Evidence_Index_2026-08-28.md",
        "Teacher_Combo_Planning_and_Recovery_2026-08-28.md",
        "Swordsoul_Tenyi_Teacher_Strategy_2026-08-28.md",
        "Salamangreat_Teacher_Strategy_2026-08-28.md",
    }
    actual = {path.name for path in RESEARCH_ROOT.glob("*.md")}
    require(expected <= actual, "one or more required Teacher research artifacts are missing")


def check_source_references(source: str) -> None:
    references = [item.strip() for item in source.split(";") if item.strip()]
    require(references, "matrix row has no exact public source reference")
    for reference in references:
        reference = reference.strip("`")
        path_text, separator, symbol = reference.partition(":")
        require(separator and symbol,
                f"matrix source is not an exact path/symbol reference: {reference}")
        path = ROOT / path_text
        require(path.is_file(), f"matrix source file is missing: {path_text}")
        leaf = symbol.rsplit(".", 1)[-1].replace("()", "")
        require(leaf in path.read_text(encoding="utf-8"),
                f"matrix source symbol is not present in {path_text}: {symbol}")


def check_evidence_reference(evidence: str) -> None:
    references = [item.strip() for item in evidence.split(";") if item.strip()]
    require(references, "matrix row has no executable evidence")
    for reference in references:
        reference = reference.strip("`")
        path_text, separator, symbol = reference.partition("::")
        require(separator and symbol,
                f"matrix evidence is not an exact path/symbol reference: {reference}")
        path = ROOT / path_text
        require(path.is_file(), f"matrix evidence file is missing: {path_text}")
        require(symbol in path.read_text(encoding="utf-8"),
                f"matrix evidence symbol is not present in {path_text}: {symbol}")


def check_blocked_reason(row: dict[str, str]) -> None:
    require(len(row["blocked"]) >= 20 and row["blocked"] != "-",
            f"blocked matrix row lacks an explicit missing-source reason: {row['id']}")


def main() -> None:
    check_research_basis()
    rows = read_matrix_rows()
    by_id = {row["id"]: row for row in rows}
    require(len(by_id) == len(rows), "public-fact matrix contains duplicate row IDs")
    missing = [row_id for row_id in REQUIRED_ROWS if row_id not in by_id]
    require(not missing, "public-fact matrix is missing rows: " + ", ".join(missing))
    require(set(by_id) == set(REQUIRED_ROWS),
            "public-fact matrix contains rows outside the frozen G00 row set")
    actual_blocked = {row["id"] for row in rows if row["availability"] == "BLOCKED"}
    require(actual_blocked == EXPECTED_BLOCKED_ROWS,
            "public-fact matrix blocked scope changed unexpectedly")

    for row in rows:
        require(row["requirement"], f"matrix row lacks a Teacher requirement: {row['id']}")
        require(row["availability"] in ALLOWED_AVAILABILITY,
                f"matrix row has an invalid availability: {row['id']}")
        check_source_references(row["source"])
        check_evidence_reference(row["evidence"])
        if row["availability"] == "BLOCKED":
            check_blocked_reason(row)
        else:
            require(row["blocked"] == "-",
                    f"non-blocked matrix row has a blocked reason: {row['id']}")

    print(f"public_fact_matrix=ok rows={len(rows)}")


if __name__ == "__main__":
    main()
