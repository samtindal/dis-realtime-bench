import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import verify  # noqa: E402

GOOD = """corpus_fnv1a64 8856ac750934ab0d
pdu idiomatic 0 1 2 0
edge idiomatic truncated_accepts 0
edge idiomatic full_accepts 1
edge idiomatic bad_type_accepts 0
edge idiomatic short_length_accepts 0
dr 0 412e84812979f431 412e8482c8c2950c 412e84850cdacfae
"""


class VerifyTests(unittest.TestCase):
    def write(self, d, name, text):
        p = Path(d) / name
        p.write_text(text)
        return p

    def test_identical_dumps_pass(self):
        with tempfile.TemporaryDirectory() as d:
            files = [self.write(d, f"{n}.dump", GOOD) for n in ("a", "b", "c")]
            self.assertEqual(verify.verify(files), [])

    def test_one_bit_difference_in_dr_fails_and_names_the_line(self):
        with tempfile.TemporaryDirectory() as d:
            a = self.write(d, "a.dump", GOOD)
            b = self.write(d, "b.dump", GOOD.replace("0cdacfae", "0cdacfaf"))
            errors = verify.verify([a, b])
            self.assertEqual(len(errors), 1)
            self.assertIn("line 7", errors[0])
            self.assertIn("b.dump", errors[0])

    def test_edge_line_with_wrong_count_fails_even_when_all_agree(self):
        bad = GOOD.replace("truncated_accepts 0", "truncated_accepts 3")
        with tempfile.TemporaryDirectory() as d:
            files = [self.write(d, f"{n}.dump", bad) for n in ("a", "b")]
            errors = verify.verify(files)
            self.assertTrue(any("truncated_accepts" in e for e in errors))

    def test_decode_failed_marker_fails(self):
        bad = GOOD.replace("pdu idiomatic 0 1 2 0", "pdu idiomatic 0 DECODE_FAILED")
        with tempfile.TemporaryDirectory() as d:
            files = [self.write(d, f"{n}.dump", bad) for n in ("a", "b")]
            self.assertTrue(verify.verify(files))

    def test_fewer_than_two_dumps_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            self.assertTrue(verify.verify([self.write(d, "a.dump", GOOD)]))

    def test_empty_dump_is_an_error(self):
        with tempfile.TemporaryDirectory() as d:
            files = [self.write(d, "a.dump", ""), self.write(d, "b.dump", "")]
            self.assertTrue(verify.verify(files))


if __name__ == "__main__":
    unittest.main()
