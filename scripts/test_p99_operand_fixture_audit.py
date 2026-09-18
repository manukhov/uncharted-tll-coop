"""Pure regression for the tracked conditional oracle; no retail calls."""
import re
import unittest
import generate_p99_native_operand_fixture as generator


class FixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.header = generator.DEFAULT_OUTPUT.read_text(encoding="utf-8")
        cls.data = bytes(int(value, 16) for value in re.findall(r"0x([0-9A-F]{2})", cls.header))

    def test_exact_size_hash_and_regeneration(self):
        self.assertEqual(len(self.data), 3948)
        self.assertEqual(generator.render(self.data), self.header)

    def test_every_source_byte_is_pinned_including_record_padding(self):
        for index in range(len(self.data)):
            changed = bytearray(self.data)
            changed[index] ^= 1
            with self.subTest(index=index), self.assertRaises(ValueError):
                generator.render(changed)

    def test_wrong_size_refused(self):
        for value in (b"", self.data[:-1], self.data + b"x"):
            with self.assertRaises(ValueError):
                generator.render(value)


if __name__ == "__main__":
    unittest.main()
