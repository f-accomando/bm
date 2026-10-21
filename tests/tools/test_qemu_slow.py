"""Every name in the SLOW table of tests/qemu_test.py (the shards' weights) must be a `def test_*` of that file.

The file is read with `ast`, not imported (an import would need QEMU, the tools folder and a build)."""
import ast, os, sys, unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
QEMU_TEST = os.path.join(ROOT, "tests", "qemu_test.py")


def read_tree():
    with open(QEMU_TEST, encoding="utf-8") as fh:
        return ast.parse(fh.read(), QEMU_TEST)


def slow_keys(tree):
    for node in tree.body:
        if isinstance(node, ast.Assign) and any(isinstance(t, ast.Name) and t.id == "SLOW" for t in node.targets):
            return [k.value for k in node.value.keys]
    return None


class QemuSlowTest(unittest.TestCase):
    def test_every_slow_name_is_a_test_function(self):
        tree = read_tree()
        keys = slow_keys(tree)
        self.assertIsNotNone(keys, "SLOW = {...} not found in tests/qemu_test.py")
        self.assertGreater(len(keys), 0)
        funcs = {n.name for n in ast.walk(tree) if isinstance(n, ast.FunctionDef) and n.name.startswith("test_")}
        missing = sorted(k for k in keys if k not in funcs)
        self.assertEqual(missing, [], "SLOW names without a `def test_*`: " + ", ".join(missing))


if __name__ == "__main__":
    unittest.main()
