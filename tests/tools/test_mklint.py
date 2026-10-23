"""The build lint of a .b16: one rule list for scripts/mkbm.py and the SDK export (src/bm/lint.c).

  python tests/tools/test_mklint.py [build/host/test_lint]

Always: scripts/mklint.py --check (the C table is current, every `api = true` name is in src/ai/api.tsv) and the
rules of the toml reach mkbm.py. With the path of the host program `test_lint` (make test-lint): the same
cartridges go through b16_lint of mkbm.py and through bm_b16_lint of lint.c, and the warnings must be equal."""
import contextlib
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import mkbm  # noqa: E402
import mklint  # noqa: E402

BIN = sys.argv.pop(1) if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]) else None

LUA = {
    "clean": b"local x = 1\nfunction _draw() cls(0) end\n",
    "all": b"a = math.random(3)\nb = time()\nsave(1)\nls('x')\ncart_read('a')\nudp_open(1)\nrequire 'x'\nload('y')\nscreen(320, 180)\n",
    "comment": b"-- math.random() time() save()\nlocal s = 1 -- screen(1)\n",
    "long_comment": b"--[[ time()\nsave() ]] x = 1\n--[==[ ls() ]] ]==] y = screen(2)\n",
    "long_string": b"local t = [[ time() ]]\nlocal u = [=[ save( ]] ]=] .. time()\n",
    "string": b"print('save(1)')\nprint(\"ls(x)\")\nlocal n = \"a\\\"time()\"\n",
    "member": b"obj.save(1)\nobj:time()\nmy_save(2)\nlocal timer = 1\nx = foo.require\n",
    "word": b"local r = require\nrequire.x = 1\nrequirement = 2\n",
    "dotted": b"local f = math.randomseed\nmymath.random(1)\nx = math.random\n",
    "prefix": b"cart_()\nudp_x (1)\ncart_ab(1)\n",
    "unclosed": b"print('abc\nsave(2)\n",
    "late": b"\n\n\nlocal a = 1\nlocal b = ls(2)\n",
    "utf8": "local étime = 1\nfooésave(2)\n".encode("utf-8"),
}


def python_warnings(data):
    err = io.StringIO()
    with contextlib.redirect_stderr(err):
        mkbm.b16_lint(data, "x.b16")
    out = []
    for line in err.getvalue().splitlines():
        m = re.match(r"warning: x\.b16:(?:(\d+):)? ?(.*)$", line)
        out.append(f"{m.group(1) or 0}|{m.group(2)}")
    return out


def c_warnings(data):
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "x.b16")
        with open(p, "wb") as f:
            f.write(data)
        r = subprocess.run([BIN, p], capture_output=True, text=True)
        assert r.returncode == 0, r.stderr
        return r.stdout.splitlines()


def carts():
    out = {name: mkbm.pack(lua, res=(360, 360)) for name, lua in LUA.items()}
    out["two_sections"] = mkbm.pack(b"x = 1\nsave(1)\n", res=(360, 360))
    out["screen256"] = mkbm.pack(b"x = 1\n", res=(256, 256))
    out["screen720"] = mkbm.pack(b"x = 1\n", res=(720, 720))
    out["screen_wide"] = mkbm.pack(b"x = 1\n", res=(640, 360))
    base = mkbm.pack(b"x = 1\n", res=(360, 360))
    out["pi_only"] = mkbm.mark_compat(base, 1)
    out["rgb30_only"] = mkbm.mark_compat(base, 2)
    out["both"] = mkbm.mark_compat(base, 3)
    out["mouse_hw"] = mkbm.mark_compat(base, 3, needs=mkbm.COMPAT_BITS["needs"]["mouse_hw"])
    out["all_header"] = mkbm.mark_compat(mkbm.pack(LUA["all"], res=(256, 256)), 2,
                                         needs=mkbm.COMPAT_BITS["needs"]["mouse_hw"])
    return out


class RulesTest(unittest.TestCase):
    def test_table_and_api_names(self):
        r = subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "mklint.py"), "--check"],
                           capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_mkbm_uses_the_toml(self):
        self.assertEqual(mkbm.B16_LUA_CHECKS, mklint.python_checks("b16"))
        self.assertGreaterEqual(len(mkbm.B16_LUA_CHECKS), 9)

    def test_bad_rule_is_refused(self):
        with tempfile.TemporaryDirectory() as d:
            p = os.path.join(d, "r.toml")
            with open(p, "w", encoding="utf-8") as f:
                f.write('[[rule]]\ntarget = "b16"\nkind = "regex"\nname = "x"\napi = false\nmsg = "m"\n')
            with self.assertRaises(SystemExit):
                mklint.load_rules("b16", p)

    def test_unknown_api_name_is_found(self):
        self.assertEqual(mklint.api_problems(), [])
        self.assertNotIn("math.random", mklint.api_names())


@unittest.skipUnless(BIN, "needs the path of build/host/test_lint")
class SameAsMkbmTest(unittest.TestCase):
    def test_every_cartridge(self):
        for name, data in carts().items():
            with self.subTest(name):
                self.assertEqual(c_warnings(data), python_warnings(data))

    def test_samples_warn_as_expected(self):
        self.assertEqual(c_warnings(carts()["clean"]), [])
        self.assertEqual(len(c_warnings(carts()["all"])), 9)
        self.assertEqual(c_warnings(carts()["late"]), ["5|ls() and files: no file system"])


if __name__ == "__main__":
    unittest.main()
