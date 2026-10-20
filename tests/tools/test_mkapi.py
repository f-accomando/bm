"""Tests for scripts/mkapi.py and runtime vs docs API comparison."""
import io
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SCRIPT = os.path.join(ROOT, "scripts", "mkapi.py")
RUNTIME_C = os.path.join(ROOT, "src", "bm", "runtime.c")
API_TSV = os.path.join(ROOT, "src", "ai", "api.tsv")

UNDOCUMENTED = set()
NOT_IN_RUNTIME = {"a", "instrument", "instruments", "nnet"}


class MkapiTest(unittest.TestCase):
    def test_fixture_parser(self):
        sys.path.insert(0, os.path.join(ROOT, "scripts"))
        import mkapi

        with tempfile.TemporaryDirectory() as tmp:
            fixture_md = os.path.join(tmp, "sample.md")
            content = (
                "# Sample\n"
                "| `func_a(x)` / `func_b(y)` | description for both |\n"
                "| `not_a_func` | no parentheses here |\n"
                "| `func_a(z)` | duplicate entry |\n"
                "| `s, port = res_open([port])` | result prefix |\n"
                "| `data, address, port = res_recv(s)` | three results |\n"
            )
            with open(fixture_md, "w", encoding="utf-8") as f:
                f.write(content)

            entries = {}
            err_buf = io.StringIO()
            mkapi.parse_md_file(fixture_md, entries, err_stream=err_buf, rel_path="docs/api/sample.md")

            self.assertIn("func_a", entries)
            self.assertIn("func_b", entries)
            self.assertNotIn("not_a_func", entries)
            self.assertIn("res_open", entries)
            self.assertIn("res_recv", entries)
            self.assertEqual(entries["res_open"]["signature"], "s, port = res_open([port])")
            self.assertEqual(entries["func_a"]["text"], "description for both")
            self.assertEqual(entries["func_b"]["text"], "description for both")

            err_output = err_buf.getvalue()
            self.assertIn("dup func_a", err_output)
            self.assertIn("docs/api/sample.md:4", err_output)

    def test_mkapi_check_subprocess(self):
        res = subprocess.run(
            [sys.executable, SCRIPT, "--check"],
            cwd=ROOT,
            capture_output=True,
            text=True,
        )
        self.assertEqual(res.returncode, 0, f"stdout: {res.stdout}\nstderr: {res.stderr}")

    def test_runtime_vs_docs(self):
        with open(RUNTIME_C, "r", encoding="utf-8") as f:
            c_text = f.read()

        m_block = re.search(
            r"static const luaL_Reg api\[\] = \{(.*?)\{\s*NULL,\s*NULL\s*\}",
            c_text,
            re.S,
        )
        self.assertIsNotNone(m_block, "api[] table not found in runtime.c")
        block = m_block.group(1)
        runtime_names = set(re.findall(r'\{\s*"([A-Za-z_0-9]+)",\s*l_', block))

        doc_global_names = set()
        with open(API_TSV, "r", encoding="utf-8") as f:
            for line in f:
                if line.startswith("#"):
                    continue
                parts = line.split("\t")
                if not parts:
                    continue
                name = parts[0]
                if "." not in name and ":" not in name:
                    doc_global_names.add(name)

        diff_undoc = runtime_names - doc_global_names
        diff_not_in_rt = doc_global_names - runtime_names

        self.assertEqual(diff_undoc, UNDOCUMENTED)
        self.assertEqual(diff_not_in_rt, NOT_IN_RUNTIME)


if __name__ == "__main__":
    unittest.main()
