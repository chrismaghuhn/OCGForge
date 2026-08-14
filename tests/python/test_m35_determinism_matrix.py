import json
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class M35DeterminismMatrixTests(unittest.TestCase):
    def test_canonical_determinism_covers_both_start_players(self):
        path = ROOT / "artifacts" / "m3" / "canonical_mr5" / "determinism" / "m3_determinism_results.json"
        report = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(report["starting_player_partitions"], [0, 1])
        self.assertEqual(sorted(report["partitions"]), ["0", "1"])
        for player in ("0", "1"):
            partition = report["partitions"][player]
            self.assertTrue(partition["independent_process_match"])
            self.assertTrue(partition["semantic_action_reexecution_match"])
            self.assertTrue(partition["crlf_semantic_replay_match"])
            self.assertEqual(len(partition["semantic_gameplay_hash"]), 64)
            self.assertEqual(len(partition["trace_hash"]), 64)


if __name__ == "__main__":
    unittest.main()
