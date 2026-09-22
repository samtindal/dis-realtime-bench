import csv
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ingest  # noqa: E402

GBENCH = {
    "context": {},
    "benchmarks": [
        {"name": "decode/idiomatic", "run_type": "iteration", "repetition_index": 0,
         "real_time": 10.24, "time_unit": "us"},
        {"name": "decode/idiomatic", "run_type": "iteration", "repetition_index": 1,
         "real_time": 20480.0, "time_unit": "ns"},
        {"name": "decode/idiomatic_median", "run_type": "aggregate", "aggregate_name": "median",
         "real_time": 15.0, "time_unit": "us"},
        {"name": "dr/default", "run_type": "iteration", "repetition_index": 0,
         "real_time": 0.03072, "time_unit": "ms"},
    ],
}

BDN = {
    "Benchmarks": [
        {"Method": "DecodeIdiomatic", "Statistics": {"OriginalValues": [22.5, 23.0]},
         "Memory": {"Gen0Collections": 0, "BytesAllocatedPerOperation": 0}},
        {"Method": "Dr", "Statistics": {"OriginalValues": [48.0]},
         "Memory": {"Gen0Collections": 2, "BytesAllocatedPerOperation": 24}},
    ]
}


def read_rows(path):
    with open(path, newline="") as f:
        return list(csv.DictReader(f))


class IngestTests(unittest.TestCase):
    def test_gbench_keeps_iterations_drops_aggregates_and_normalizes_units(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "g.json", Path(d) / "raw.csv"
            src.write_text(json.dumps(GBENCH))
            ingest.ingest_gbench(src, "cpp", "gcc-16.2.0", out)
            rows = read_rows(out)
            self.assertEqual(len(rows), 3)
            self.assertEqual(rows[0]["harness"], "gbench")
            self.assertEqual((rows[0]["kernel"], rows[0]["variant"]), ("decode", "idiomatic"))
            self.assertAlmostEqual(float(rows[0]["ns_per_op"]), 10.0)  # 10.24 us / 1024
            self.assertAlmostEqual(float(rows[1]["ns_per_op"]), 20.0)
            self.assertEqual(rows[1]["rep"], "1")
            self.assertAlmostEqual(float(rows[2]["ns_per_op"]), 30.0)  # 0.03072 ms / 1024

    def test_criterion_reads_every_sample_as_time_over_iters_over_batch(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d) / "criterion"
            new = root / "decode" / "shift" / "new"
            new.mkdir(parents=True)
            (new / "benchmark.json").write_text(json.dumps({"group_id": "decode", "function_id": "shift"}))
            (new / "sample.json").write_text(json.dumps({"iters": [10.0, 20.0], "times": [102400.0, 409600.0]}))
            # A 'report' directory with no sample.json must be ignored.
            (root / "report").mkdir()
            out = Path(d) / "raw.csv"
            ingest.ingest_criterion(root, "rust", "rustc-1.95.0", out)
            rows = read_rows(out)
            self.assertEqual([float(r["ns_per_op"]) for r in rows], [10.0, 20.0])
            self.assertTrue(all(r["harness"] == "criterion" and r["variant"] == "shift" for r in rows))

    def test_bdn_maps_methods_and_writes_alloc_rows(self):
        with tempfile.TemporaryDirectory() as d:
            src, out, alloc = Path(d) / "b.json", Path(d) / "raw.csv", Path(d) / "alloc.csv"
            src.write_text(json.dumps(BDN))
            ingest.ingest_bdn(src, "csharp", "dotnet-10.0.12-jit", out, alloc)
            rows = read_rows(out)
            self.assertEqual([(r["kernel"], r["variant"]) for r in rows],
                             [("decode", "idiomatic"), ("decode", "idiomatic"), ("dr", "default")])
            self.assertEqual(float(rows[1]["ns_per_op"]), 23.0)
            a = read_rows(alloc)
            self.assertEqual(a[1]["bytes_per_op"], "24")
            self.assertEqual(a[1]["gen0"], "2")

    def test_appending_writes_header_once(self):
        with tempfile.TemporaryDirectory() as d:
            src, out = Path(d) / "g.json", Path(d) / "raw.csv"
            src.write_text(json.dumps(GBENCH))
            ingest.ingest_gbench(src, "cpp", "a", out)
            ingest.ingest_gbench(src, "cpp", "b", out)
            self.assertEqual(out.read_text().count("harness,"), 1)
            self.assertEqual(len(read_rows(out)), 6)

    def test_unknown_benchmark_name_is_an_error(self):
        bad = {"benchmarks": [{"name": "mystery", "run_type": "iteration", "real_time": 1, "time_unit": "ns"}]}
        with tempfile.TemporaryDirectory() as d:
            src = Path(d) / "g.json"
            src.write_text(json.dumps(bad))
            with self.assertRaises(ValueError):
                ingest.ingest_gbench(src, "cpp", "x", Path(d) / "raw.csv")


if __name__ == "__main__":
    unittest.main()
