"""Tests for scripts/bm_weight.py: weight report, performance plan, compat policy table."""
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "scripts"))
import bm_weight  # noqa: E402
import bmmesh  # noqa: E402
import mkbm  # noqa: E402

LUA = b"function _draw()\n  cls(0)\nend\n" * 50


def model(name, nfaces):
    verts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
    faces = [(0, 1, 2, 0xFF0000FF, None)] * nfaces
    return {"name": name, "verts": verts, "faces": faces}


def build(extra=(), mesh=None, res=(640, 360)):
    return mkbm.pack(LUA, title="Fixture", author="test", res=res, mesh=mesh, extra=list(extra))


class WeightTest(unittest.TestCase):
    def setUp(self):
        self.mesh = bmmesh.encode([model("big", 200), model("small", 10)])
        self.data = build(mesh=self.mesh, extra=[(10, b"name: Fixture\n")])
        self.info = bm_weight.parse(self.data, "fixture.bm")
        self.w = bm_weight.weight(self.info)

    def test_sections_sum_to_total(self):
        self.assertEqual(sum(s["bytes"] for s in self.w["sections"]), len(self.data))
        self.assertEqual(self.w["total"], len(self.data))

    def test_biggest_first_and_percent(self):
        sizes = [s["bytes"] for s in self.w["sections"]]
        self.assertEqual(sizes, sorted(sizes, reverse=True))
        self.assertAlmostEqual(sum(s["percent"] for s in self.w["sections"]), 100.0, delta=0.1)
        self.assertEqual(self.w["sections"][0]["name"], "MESH")

    def test_models_are_listed_one_by_one(self):
        models = [a for a in self.w["assets"] if a["section"] == "MESH"]
        self.assertEqual([m["name"] for m in models], ["big", "small"])
        self.assertEqual(models[0]["faces"], 200)
        self.assertEqual(models[0]["bytes"], 16 + 8 + 3 * 12 + 200 * 24)
        self.assertEqual(sum(m["bytes"] for m in models), len(self.mesh) - 8)

    def test_lua_lines_and_category(self):
        lua = next(s for s in self.w["sections"] if s["name"] == "LUA")
        self.assertEqual(lua["facts"]["lines"], 150)
        self.assertIn("lua", [c["category"] for c in self.w["categories"]])

    def test_edition_from_extension_then_flags(self):
        self.assertEqual(bm_weight.parse(self.data, "x.b16")["edition"], "b16")
        self.assertEqual(bm_weight.parse(self.data, "x.bme")["edition"], "project")
        self.assertEqual(bm_weight.parse(mkbm.mark_project(self.data))["edition"], "project")
        self.assertEqual(bm_weight.parse(mkbm.mark_b16(self.data))["edition"], "b16")

    def test_not_a_cartridge(self):
        with self.assertRaises(ValueError):
            bm_weight.parse(b"x" * 200)
        with self.assertRaises(ValueError):
            bm_weight.parse(b"short")


class PlanTest(unittest.TestCase):
    def test_every_estimate_has_source_and_is_marked(self):
        data = build(mesh=bmmesh.encode([model("m", 100)]))
        info = bm_weight.parse(data, "a.bm")
        p = bm_weight.plan(info, bm_weight.weight(info))
        self.assertTrue(p["estimates"])
        for e in p["estimates"]:
            self.assertEqual(e["kind"], "estimate")
            self.assertIn("docs/", e["source"])
        read = next(e for e in p["estimates"] if e["what"].startswith("load"))
        self.assertAlmostEqual(read["value"], len(data) / 5.14e6, places=3)
        self.assertTrue(any("3D Bench" in b for b in p["next_benchmarks"]))

    def test_heavy_model_is_flagged(self):
        data = build(mesh=bmmesh.encode([model("huge", 11000)]))
        info = bm_weight.parse(data, "a.bm")
        p = bm_weight.plan(info, bm_weight.weight(info))
        self.assertTrue(any("huge" in f and "11000" in f for f in p["flags"]))

    def test_light_cart_has_no_flags(self):
        info = bm_weight.parse(build(), "a.bm")
        self.assertEqual(bm_weight.plan(info, bm_weight.weight(info))["flags"], [])


class PolicyTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.policy = bm_weight.load_policy()

    def facts(self, **kw):
        f = {"edition": "bm", "res": "640x360", "uses_3d": False, "audio_bytes": 0, "size_bytes": 1000}
        f.update(kw)
        return f

    def test_table_parses_with_both_kernels(self):
        self.assertEqual(set(self.policy["kernels"]), {"pi-zero-w", "rgb30"})
        self.assertTrue(self.policy["rules"])

    def test_kernel_names_exist_in_platforms_toml(self):
        import tomllib
        with open(os.path.join(ROOT, "docs", "spec", "platforms.toml"), "rb") as f:
            plat = tomllib.load(f)["platform"]
        for k in self.policy["kernels"]:
            self.assertIn(k, plat)

    def test_sample_cart_is_classified(self):
        data = build(mesh=bmmesh.encode([model("m", 5)]))
        info = bm_weight.parse(data, "sample.bm")
        facts = bm_weight.facts_of(info, bm_weight.weight(info))
        self.assertEqual(bm_weight.classify(facts, "pi-zero-w", self.policy)[0], "show")
        act, why, row = bm_weight.classify(facts, "rgb30", self.policy)
        self.assertEqual(act, "grey")
        self.assertIn("Pi", why)
        self.assertGreater(row, 0)

    def test_b16_shows_on_both_and_project_is_hidden(self):
        for k in ("pi-zero-w", "rgb30"):
            self.assertEqual(bm_weight.classify(self.facts(edition="b16"), k, self.policy)[0], "show")
            self.assertEqual(bm_weight.classify(self.facts(edition="project"), k, self.policy)[0], "hide")

    def test_market_can_differ_from_menu(self):
        big = self.facts(edition="b16", size_bytes=101 << 20)
        self.assertEqual(bm_weight.classify(big, "rgb30", self.policy, "menu")[0], "show")
        self.assertEqual(bm_weight.classify(big, "rgb30", self.policy, "market")[0], "hide")

    def test_unknown_kernel_and_default_row(self):
        with self.assertRaises(ValueError):
            bm_weight.classify(self.facts(), "nope", self.policy)
        policy = {"kernels": {"k": {"target": 1, "default_action": "grey", "default_reason": "d"}}, "rules": []}
        self.assertEqual(bm_weight.classify(self.facts(), "k", policy), ("grey", "d", 0))

    def test_declared_header_and_kernel_not_listed(self):
        """a cartridge with the compatibility block: only the kernels it lists run it; the old-.bm rule does not apply"""
        both = self.facts(declared=True, targets=3)
        pi_only = self.facts(declared=True, targets=1)
        rgb_only = self.facts(declared=True, targets=2)
        for k in ("pi-zero-w", "rgb30"):
            self.assertEqual(bm_weight.classify(both, k, self.policy)[0], "show")
        self.assertEqual(bm_weight.classify(pi_only, "pi-zero-w", self.policy)[0], "show")
        act, why, _ = bm_weight.classify(pi_only, "rgb30", self.policy)
        self.assertEqual((act, why), ("grey", "made for the Pi Zero W only"))
        act, why, _ = bm_weight.classify(rgb_only, "pi-zero-w", self.policy)
        self.assertEqual((act, why), ("grey", "made for the RGB30 only"))
        self.assertEqual(bm_weight.classify(self.facts(declared=False, targets=1), "rgb30", self.policy)[0], "grey")

    def test_reason_is_short(self):
        for r in self.policy["rules"]:
            self.assertLessEqual(len(r["reason"]), bm_weight.REASON_MAX)

    def test_bad_tables_are_refused(self):
        bad = {
            "action": '[[kernel]]\nid="k"\ntarget=1\nname="K"\ndefault_action="maybe"\ndefault_reason="r"\n',
            "kernel": '[[kernel]]\nid="k"\ntarget=1\nname="K"\ndefault_action="show"\ndefault_reason="r"\n'
                      '[[rule]]\nkernel="zz"\naction="hide"\nreason="r"\n',
            "key": '[[kernel]]\nid="k"\ntarget=1\nname="K"\ndefault_action="show"\ndefault_reason="r"\n'
                   '[[rule]]\nkernel="k"\naction="hide"\nreason="r"\ncolour="red"\n',
        }
        with tempfile.TemporaryDirectory() as tmp:
            for name, text in bad.items():
                path = os.path.join(tmp, name + ".toml")
                with open(path, "w") as f:
                    f.write(text)
                with self.assertRaises(ValueError, msg=name):
                    bm_weight.load_policy(path)

    def test_every_rule_row_uses_known_keys_only(self):
        for r in self.policy["rules"]:
            self.assertLessEqual(set(r) - {"kernel", "action", "market_action", "reason"}, set(bm_weight.MATCH_KEYS))


class CliTest(unittest.TestCase):
    def run_cli(self, *args):
        return subprocess.run([sys.executable, os.path.join(ROOT, "scripts", "bm_weight.py"), *args],
                              capture_output=True, text=True)

    def test_json_and_text_on_a_cart_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "t.bm")
            with open(path, "wb") as f:
                f.write(build(mesh=bmmesh.encode([model("m", 20)])))
            r = self.run_cli(path, "--json")
            self.assertEqual(r.returncode, 0, r.stderr)
            out = json.loads(r.stdout)
            self.assertIn("MESH", [x["name"] for x in out["weight"]["sections"]])
            self.assertEqual(out["compat"]["rgb30"]["menu"], "grey")
            r = self.run_cli(path)
            self.assertEqual(r.returncode, 0)
            self.assertIn("Sections, biggest first", r.stdout)
            self.assertIn("estimate", r.stdout)
            r = self.run_cli(path, "--weight-only")
            self.assertNotIn("Performance plan", r.stdout)

    def test_bad_file_is_an_error(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = os.path.join(tmp, "bad.bm")
            with open(path, "wb") as f:
                f.write(b"x" * 300)
            r = self.run_cli(path)
            self.assertEqual(r.returncode, 1)
            self.assertIn("not a .bm", r.stderr)

    def test_a_cart_in_the_repo(self):
        path = os.path.join(ROOT, "carts", "herocart", "assets", "models.bm")
        if not os.path.exists(path):
            self.skipTest("fixture missing")
        out = json.loads(self.run_cli(path, "--json").stdout)
        self.assertEqual(out["weight"]["sections"][0]["name"], "MESH")


if __name__ == "__main__":
    unittest.main()
