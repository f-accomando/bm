"""Tests for scripts/cindex.py: the script runs as a subprocess on temporary C and Lua files."""
import os, subprocess, sys, tempfile, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "cindex.py")

C_SRC = """\
#include <stdio.h>
/* ---- Drawing */
static int l_cls(lua_State *L)
{
    if (x) {
        return 0;
    }
    while (y) {
        y--;
    }
    return 1;
}

void foo(void)
{
}
"""

LUA_SRC = """\
function a()
end
local function b()
end
P.c = function(x)
end
  local d = 1
"""


class CindexTest(unittest.TestCase):
    def run_index(self, name, text, *extra):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, name)
            with open(path, "w", encoding="utf8", newline="\n") as fh:
                fh.write(text)
            res = subprocess.run([sys.executable, SCRIPT, path, *extra],
                                 capture_output=True, text=True)
        self.assertEqual(res.returncode, 0, res.stderr)
        return res.stdout.splitlines()

    def test_c_banner_and_functions(self):
        lines = self.run_index("x.c", C_SRC)
        # banner on line 2, l_cls on line 3, foo on line 14
        self.assertEqual(lines, ["     2 # Drawing", "     3   l_cls", "    14   foo"])

    def test_c_skips_if_while_and_indented_lines(self):
        lines = self.run_index("x.c", C_SRC)
        text = "\n".join(lines)
        self.assertNotIn("if", text)
        self.assertNotIn("while", text)
        self.assertEqual(len(lines), 3)

    def test_lua_functions(self):
        lines = self.run_index("x.lua", LUA_SRC)
        self.assertEqual(lines, ["     1   a", "     3   b", "     5   P.c"])

    def test_word_filter_is_case_insensitive(self):
        lines = self.run_index("x.c", C_SRC, "CLS")
        self.assertEqual(lines, ["     3   l_cls"])
        lines = self.run_index("x.c", C_SRC, "drawing")
        self.assertEqual(lines, ["     2 # Drawing"])

    def test_line_numbers_right_aligned_6_columns(self):
        for line in self.run_index("x.c", C_SRC):
            self.assertEqual(line[6], " ")
            self.assertTrue(line[:6].strip().isdigit(), line)
            self.assertEqual(line[:6], f"{int(line[:6]):6d}")


if __name__ == "__main__":
    unittest.main()
