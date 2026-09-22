import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import summarize  # noqa: E402

HEADER = "harness,lang,toolchain,kernel,variant,round,rep,ns_per_op\n"


def raw(rows):
    return HEADER + "".join(",".join(map(str, r)) + "\n" for r in rows)


class SummarizeTests(unittest.TestCase):
    def run_summary(self, raw_text, alloc_text=None, ghz=4.0):
        with tempfile.TemporaryDirectory() as d:
            rp = Path(d) / "raw.csv"
            rp.write_text(raw_text)
            ap = None
            if alloc_text is not None:
                ap = Path(d) / "alloc.csv"
                ap.write_text(alloc_text)
            return summarize.summarize(rp, ghz, ap)

    def test_median_iqr_min_and_cycles(self):
        rows = [("simple", "cpp", "gcc", "decode", "idiomatic", 0, i, v) for i, v in enumerate([10, 11, 12, 13, 50])]
        md, ok = self.run_summary(raw(rows))
        self.assertTrue(ok)
        line = next(l for l in md.splitlines() if "| gcc |" in l)
        cells = [c.strip() for c in line.strip("|").split("|")]
        # harness | lang | toolchain | median | p25-p75 | min | cycles | n
        self.assertEqual(cells[3], "12.00")
        self.assertEqual(cells[4], "11.00–13.00")
        self.assertEqual(cells[5], "10.00")
        self.assertEqual(cells[6], "48.0")  # 12 ns * 4 GHz
        self.assertEqual(cells[7], "5")

    def test_rows_sorted_fastest_first_within_kernel(self):
        rows = [("simple", "rust", "rustc", "dr", "default", 0, 0, 30.0),
                ("simple", "cpp", "clang", "dr", "default", 0, 0, 20.0)]
        md, _ = self.run_summary(raw(rows))
        self.assertLess(md.index("| clang |"), md.index("| rustc |"))

    def test_implausibly_fast_rows_are_flagged(self):
        rows = [("simple", "rust", "rustc", "decode", "idiomatic", 0, 0, 0.3)]  # 1.2 cycles
        md, _ = self.run_summary(raw(rows))
        self.assertIn("DCE?", md)

    def test_nonzero_allocation_fails(self):
        rows = [("bdn", "csharp", "jit", "decode", "idiomatic", 0, 0, 20.0)]
        alloc = "lang,toolchain,kernel,variant,bytes_per_op,gen0\ncsharp,jit,decode,idiomatic,24,1\n"
        md, ok = self.run_summary(raw(rows), alloc)
        self.assertFalse(ok)
        self.assertIn("24", md)

    def test_zero_allocation_passes(self):
        rows = [("bdn", "csharp", "jit", "decode", "idiomatic", 0, 0, 20.0)]
        alloc = "lang,toolchain,kernel,variant,bytes_per_op,gen0\ncsharp,jit,decode,idiomatic,0,0\n"
        _, ok = self.run_summary(raw(rows), alloc)
        self.assertTrue(ok)

    def test_harness_agreement_ratio(self):
        rows = [("simple", "cpp", "gcc", "dr", "default", 0, 0, 50.0),
                ("gbench", "cpp", "gcc", "dr", "default", 0, 0, 55.0)]
        md, _ = self.run_summary(raw(rows))
        self.assertIn("1.10", md)


if __name__ == "__main__":
    unittest.main()
