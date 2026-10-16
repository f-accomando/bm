"""Tests for scripts/psf2c.py: the script runs as a subprocess on PSF fonts built in a temporary folder."""
import ast, gzip, os, struct, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "psf2c.py")

A_ROWS = bytes([0x18, 0x24, 0x42, 0x42, 0x7E, 0x42, 0x42, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00])


def psf1(height=16, glyph_a=A_ROWS):
    """PSF1, mode 2 (256 glyphs, with a Unicode table): glyph i is U+0020..U+007E for i in that range"""
    glyphs = b"".join(glyph_a if i == 0x41 else bytes(height) for i in range(256))
    table = b""
    for i in range(256):
        if 0x20 <= i <= 0x7E:
            table += struct.pack("<H", i)
        table += struct.pack("<H", 0xFFFF)
    return b"\x36\x04" + bytes([2, height]) + glyphs + table


def psf2_wide():
    """PSF2 with 1 glyph, 9 pixels wide (2 bytes per row, 16 rows)"""
    return struct.pack("<8I", 0x864AB572, 0, 32, 0, 1, 32, 16, 9) + bytes(32)


class Psf2cTest(unittest.TestCase):
    def run_psf(self, data, name="font.psf"):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, name)
            with open(path, "wb") as fh:
                fh.write(data)
            return subprocess.run([sys.executable, SCRIPT, path], capture_output=True, text=True)

    def test_psf1_glyph_a_and_font_name(self):
        res = self.run_psf(psf1())
        self.assertEqual(res.returncode, 0, res.stderr)
        row = "    { " + ", ".join(f"0x{b:02x}" for b in A_ROWS) + " }, /* A */"
        self.assertIn(row, res.stdout.splitlines())
        self.assertIn("font_console_8x16", res.stdout)
        self.assertIn("static const uint8_t glyphs[256][16]", res.stdout)

    def test_psf1_gzip_gives_the_same_output(self):
        plain = self.run_psf(psf1())
        packed = self.run_psf(gzip.compress(psf1()), "font.psf.gz")
        self.assertEqual(packed.returncode, 0, packed.stderr)
        self.assertEqual(packed.stdout, plain.stdout)

    def test_unmapped_codes_are_reported_on_stderr(self):
        res = self.run_psf(psf1())
        self.assertIn("missing glyphs:", res.stderr)
        # the table maps only U+0020..U+007E: code 1 and code 255 are missing, 'A' (65) is not
        missing = ast.literal_eval(res.stderr.split("missing glyphs:")[1].strip())
        self.assertIn(1, missing)
        self.assertIn(255, missing)
        self.assertNotIn(65, missing)
        self.assertNotIn(0, missing)

    def test_psf2_wider_than_8_pixels_is_refused(self):
        res = self.run_psf(psf2_wide())
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("only fonts up to 8 pixels wide", res.stderr)

    def test_not_a_font_is_refused(self):
        res = self.run_psf(b"this is not a font at all")
        self.assertNotEqual(res.returncode, 0)
        self.assertIn("not a PSF font", res.stderr)


if __name__ == "__main__":
    unittest.main()
