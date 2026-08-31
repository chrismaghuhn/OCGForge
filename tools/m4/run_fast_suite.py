"""Run the M4 Python suite without the separately orchestrated stress case.

The result-then-exit behavior remains covered by the single-run test in
``test_failure_isolation.py``.  Only its exact 100-repetition scheduling
stress test is reserved for the Heavy H_exec evidence command.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import sys
import unittest


ROOT = Path(__file__).resolve().parents[2]
TEST_ROOT = ROOT / "tests" / "m4"
HEAVY_TEST_ID = (
    "tests.m4.test_failure_isolation.FailureIsolationTests."
    "test_result_then_exit_never_publishes_passed_under_repeated_scheduling"
)


def _iter_test_cases(suite: unittest.TestSuite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _iter_test_cases(item)
        else:
            yield item


def _discovered_cases() -> list[unittest.TestCase]:
    if str(ROOT) not in sys.path:
        sys.path.insert(0, str(ROOT))
    loader = unittest.TestLoader()
    discovered = loader.discover(
        str(TEST_ROOT), pattern="test_*.py", top_level_dir=str(ROOT)
    )
    return list(_iter_test_cases(discovered))


def _fast_cases() -> list[unittest.TestCase]:
    cases = _discovered_cases()
    heavy = [case for case in cases if case.id() == HEAVY_TEST_ID]
    if len(heavy) != 1:
        raise RuntimeError(
            "expected exactly one registered M4 scheduling stress test, "
            f"found {len(heavy)}: {HEAVY_TEST_ID}"
        )
    return [case for case in cases if case.id() != HEAVY_TEST_ID]


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("-v", "--verbose", action="store_true")
    parser.add_argument(
        "--list",
        action="store_true",
        help="list the normal-suite test IDs without executing them",
    )
    args = parser.parse_args(argv)

    try:
        cases = _fast_cases()
    except RuntimeError as error:
        print(str(error), file=sys.stderr)
        return 2

    if args.list:
        for case in cases:
            print(case.id())
        return 0

    suite = unittest.TestSuite(cases)
    verbosity = 2 if args.verbose else 1
    result = unittest.TextTestRunner(verbosity=verbosity).run(suite)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    raise SystemExit(main())
