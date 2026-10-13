"""CLI for mesh, skel, anim subcommands."""
import json
import os
import struct
import sys
import tempfile
import unittest

import _common as C
import bm_core
from bm_core import ValidationError, container, sections as S


class CliBuild(unittest.TestCase):
    """Test the bm-forge CLI mesh, skel, anim subcommands."""

    def _synthetic_glb(self):
        """Build a small synthetic .glb: quad under a rotated, scaled node (z flip),
        mirrored copy (det < 0), vertex colours, duplicate node names.

        Returns the .glb bytes (compatible with test_crosscheck.py test_synthetic_glb).
        """
        pos = struct.pack("<12f", 0, 0, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0)
        col = struct.pack("<16f", 1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 1, 1)
        idx = struct.pack("<6H", 0, 1, 2, 0, 2, 3)
        bin_ = pos + col + idx
        js = {"asset": {"version": "2.0"}, "scene": 0, "scenes": [{"nodes": [0, 1]}],
              "nodes": [{"name": "quad", "mesh": 0, "rotation": [0, 0.7071068, 0, 0.7071068], "scale": [2, 2, 2],
                         "children": [2]},
                        {"name": "quad", "mesh": 0, "scale": [-1, 1, 1], "translation": [3, 0, 0]},
                        {"mesh": 0, "translation": [0, 5, 0]}],
              "meshes": [{"name": "m", "primitives": [{"attributes": {"POSITION": 0, "COLOR_0": 1}, "indices": 2,
                                                       "material": 0}]}],
              "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.5, 0.8, 1, 1]}}],
              "buffers": [{"byteLength": len(bin_)}],
              "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 48},
                              {"buffer": 0, "byteOffset": 48, "byteLength": 64},
                              {"buffer": 0, "byteOffset": 112, "byteLength": 12}],
              "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
                            {"bufferView": 1, "componentType": 5126, "count": 4, "type": "VEC4"},
                            {"bufferView": 2, "componentType": 5123, "count": 6, "type": "SCALAR"}]}
        j = json.dumps(js).encode()
        j += b" " * ((-len(j)) % 4)
        b = bin_ + b"\0" * ((-len(bin_)) % 4)
        data = (struct.pack("<4sII", b"glTF", 2, 12 + 8 + len(j) + 8 + len(b)) + struct.pack("<I4s", len(j), b"JSON")
                + j + struct.pack("<I4s", len(b), b"BIN\0") + b)
        return data

    def test_mesh_boxes_bm(self):
        """mesh with .json (boxes) outputs .bm with correct model counts and face counts."""
        mesh, anim = C.forge_assets()
        with tempfile.TemporaryDirectory() as tmpdir:
            boxes_path = os.path.join(C.SKILL, "examples", "humanoid_boxes.json")
            ground_path = os.path.join(C.SKILL, "examples", "game_demo", "ground_boxes.json")
            out_path = os.path.join(tmpdir, "out.bm")

            code, stdout, stderr = C.run_cli(["mesh", boxes_path, ground_path, "-o", out_path])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path), "output file not created")

            # Check output
            cart = container.read_file(out_path)
            m = cart.mesh()
            self.assertEqual(len(m.models), 2)
            model_names = [mo.name for mo in m.models]
            self.assertIn("humanoid", model_names)
            self.assertIn("ground", model_names)

            # Check face counts
            humanoid = m.get("humanoid")
            ground = m.get("ground")
            self.assertEqual(len(humanoid.faces), 156)
            self.assertEqual(len(ground.faces), 12)

    def test_mesh_bmm(self):
        """mesh outputs .bmm with BMRES header (RES_MODEL)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            boxes_path = os.path.join(C.SKILL, "examples", "humanoid_boxes.json")
            out_path = os.path.join(tmpdir, "out.bmm")

            code, stdout, stderr = C.run_cli(["mesh", boxes_path, "-o", out_path])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            cart = container.read_file(out_path)
            self.assertTrue(cart.header.is_res)
            self.assertEqual(cart.header.kind, container.RES_MODEL)

    def test_mesh_max_tris(self):
        """mesh --max-tris 100 rejects input with more triangles."""
        with tempfile.TemporaryDirectory() as tmpdir:
            boxes_path = os.path.join(C.SKILL, "examples", "humanoid_boxes.json")
            out_path = os.path.join(tmpdir, "out.bm")

            code, stdout, stderr = C.run_cli(["mesh", boxes_path, "-o", out_path, "--max-tris", "100"])

            self.assertEqual(code, 1, f"expected code 1, got {code}: stderr={stderr}")
            self.assertFalse(os.path.exists(out_path), "output file should not be created on budget failure")
            self.assertIn("triangles", stderr)

    def test_mesh_bad_ext(self):
        """mesh -o x.b16 exits with code 2 (invalid extension)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            boxes_path = os.path.join(C.SKILL, "examples", "humanoid_boxes.json")
            out_path = os.path.join(tmpdir, "out.b16")

            code, stdout, stderr = C.run_cli(["mesh", boxes_path, "-o", out_path])

            self.assertEqual(code, 2)
            self.assertFalse(os.path.exists(out_path))

    def test_mesh_glb(self):
        """mesh with synthetic .glb outputs code 0 with correct models."""
        with tempfile.TemporaryDirectory() as tmpdir:
            glb_path = os.path.join(tmpdir, "test.glb")
            with open(glb_path, "wb") as f:
                f.write(self._synthetic_glb())
            out_path = os.path.join(tmpdir, "out.bm")

            code, stdout, stderr = C.run_cli(["mesh", glb_path, "-o", out_path])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            cart = container.read_file(out_path)
            m = cart.mesh()
            model_names = [mo.name for mo in m.models]
            self.assertEqual(model_names, ["quad", "m", "quad2"])

    def test_skel(self):
        """skel binds skeleton to model with correct bone count."""
        with tempfile.TemporaryDirectory() as tmpdir:
            # Create input .bm with humanoid model
            mesh, _ = C.forge_assets()
            in_path = os.path.join(tmpdir, "in.bm")
            cart = bm_core.export.models_cart(mesh, None, "Test", "Author")
            container.write_file(in_path, cart)

            skel_path = os.path.join(C.SKILL, "examples", "humanoid.json")
            out_path = os.path.join(tmpdir, "out.bm")

            code, stdout, stderr = C.run_cli(["skel", skel_path, "--in", in_path, "-o", out_path])

            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

            # Check rig
            cart_out = container.read_file(out_path)
            anim_obj = cart_out.anim()
            self.assertIsNotNone(anim_obj)
            self.assertEqual(len(anim_obj.rigs), 1)
            rig = anim_obj.rigs[0]

            # Expected: 7 bones from humanoid.json
            self.assertEqual(len(rig.bones), 7)
            self.assertEqual(rig.bones[0].name, "hips")

            # Check vertex count (humanoid_boxes has 104 vertices)
            mesh_obj = cart_out.mesh()
            humanoid_model = mesh_obj.get("humanoid")
            self.assertEqual(len(humanoid_model.verts), 104)

    def test_skel_wrong_model(self):
        """skel with nonexistent model in skeleton exits with code 1."""
        with tempfile.TemporaryDirectory() as tmpdir:
            # Create input .bm
            mesh, _ = C.forge_assets()
            in_path = os.path.join(tmpdir, "in.bm")
            cart = bm_core.export.models_cart(mesh, None, "Test", "Author")
            container.write_file(in_path, cart)

            # Create skeleton with wrong model name
            skel_path = os.path.join(tmpdir, "skel.json")
            with open(skel_path, "w") as f:
                json.dump({
                    "model": "nobody",
                    "bones": [{"name": "root", "parent": None, "head": [0, 0, 0], "tail": [0, 1, 0]}]
                }, f)

            out_path = os.path.join(tmpdir, "out.bm")
            code, stdout, stderr = C.run_cli(["skel", skel_path, "--in", in_path, "-o", out_path])

            self.assertEqual(code, 1, f"expected code 1, got {code}: stderr={stderr}")
            self.assertIn("nobody", stderr)

    def test_anim(self):
        """anim adds clips to rig and reports them."""
        with tempfile.TemporaryDirectory() as tmpdir:
            # mesh -> skel -> anim, the chain of the CLI
            ex = os.path.join(C.SKILL, "examples")
            m, s_, out_path = (os.path.join(tmpdir, n) for n in ("m.bm", "s.bm", "out.bm"))
            self.assertEqual(C.run_cli(["mesh", os.path.join(ex, "humanoid_boxes.json"), "-o", m])[0], 0)
            self.assertEqual(C.run_cli(["skel", os.path.join(ex, "humanoid.json"), "--in", m, "-o", s_])[0], 0)
            code, stdout, stderr = C.run_cli(["anim"] + [os.path.join(ex, n + ".json") for n in ("idle", "walk", "jump")]
                                             + ["--in", s_, "-o", out_path])
            names = [c.name for c in container.read_file(out_path).anim().rigs[0].clips]
            self.assertEqual(names, ["idle", "walk", "jump"])
            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertIn("clip walk:", stdout)
            self.assertIn("frames", stdout)

    def test_anim_duplicate_and_replace(self):
        """anim without --replace rejects duplicate clip names; with --replace succeeds."""
        with tempfile.TemporaryDirectory() as tmpdir:
            # Create input with rig that already has idle clip
            mesh, anim = C.forge_assets()
            in_path = os.path.join(tmpdir, "in.bm")
            cart = bm_core.export.models_cart(mesh, anim, "Test", "Author")
            container.write_file(in_path, cart)

            idle_path = os.path.join(C.SKILL, "examples", "idle.json")
            out_path = os.path.join(tmpdir, "out.bm")

            # Without --replace: should fail
            code, stdout, stderr = C.run_cli(["anim", idle_path, "--in", in_path, "-o", out_path])
            self.assertEqual(code, 1, f"expected code 1 without --replace, got {code}")

            # With --replace: should succeed
            code, stdout, stderr = C.run_cli(["anim", idle_path, "--in", in_path, "-o", out_path, "--replace"])
            self.assertEqual(code, 0, f"stderr: {stderr}")
            self.assertTrue(os.path.exists(out_path))

    def test_export_kernel_limits_api(self):
        """export_models / export_bmm with kernel_limits=True write a 5000-vertex model; the default refuses it."""
        from bm_core import shapes, errors
        verts, faces = [], []
        for i in range(625):
            vs, fs = shapes.box([float(i), 0.0, 0.0], [float(i) + 0.5, 0.5, 0.5], 0xFFFFFF, len(verts))
            verts += vs
            faces += fs
        big = bm_core.Mesh([bm_core.Model("big", verts, faces)])
        with tempfile.TemporaryDirectory() as tmpdir:
            with self.assertRaises(errors.ValidationError):
                bm_core.export.export_models(os.path.join(tmpdir, "a.bm"), big)
            for fn, ext in ((bm_core.export.export_models, ".bm"), (bm_core.export.export_bmm, ".bmm")):
                out = os.path.join(tmpdir, "k" + ext)
                fn(out, big, kernel_limits=True)
                self.assertEqual(len(container.read_file(out, kernel_limits=True).mesh(kernel_limits=True).models), 1)

    def test_edition_kept(self):
        """mesh --in .bm -o .bmm exits code 2 (editions not converted)."""
        with tempfile.TemporaryDirectory() as tmpdir:
            # Create a .bm input
            mesh, _ = C.forge_assets()
            in_path = os.path.join(tmpdir, "in.bm")
            cart = bm_core.export.models_cart(mesh, None, "Test", "Author")
            container.write_file(in_path, cart)

            out_path = os.path.join(tmpdir, "out.bmm")

            # skel should reject edition change
            skel_path = os.path.join(C.SKILL, "examples", "humanoid.json")
            code, stdout, stderr = C.run_cli(["skel", skel_path, "--in", in_path, "-o", out_path])

            self.assertEqual(code, 2)
            self.assertIn("edition", stderr.lower())

    def test_same_path_refused(self):
        """mesh/skel/anim with same input/output path exits code 2."""
        with tempfile.TemporaryDirectory() as tmpdir:
            boxes_path = os.path.join(C.SKILL, "examples", "humanoid_boxes.json")
            tmp_path = os.path.join(tmpdir, "same.bm")

            # First create a file to use as input
            code, _, _ = C.run_cli(["mesh", boxes_path, "-o", tmp_path])
            self.assertEqual(code, 0)

            # Now try to use it as both input and output for skel
            skel_path = os.path.join(C.SKILL, "examples", "humanoid.json")
            code, stdout, stderr = C.run_cli(["skel", skel_path, "--in", tmp_path, "-o", tmp_path])

            self.assertEqual(code, 2)
            self.assertIn("input and output", stderr.lower())

    def test_missing_input(self):
        """mesh/skel/anim with missing input file exits code 2."""
        with tempfile.TemporaryDirectory() as tmpdir:
            out_path = os.path.join(tmpdir, "out.bm")
            code, stdout, stderr = C.run_cli(["mesh", "/nonexistent.json", "-o", out_path])

            self.assertEqual(code, 2)
            self.assertIn("not found", stderr.lower())

    def test_no_stray_files(self):
        """A full mesh/skel/anim run creates only output files."""
        # Get initial state
        repo_before = set(os.listdir(C.REPO))
        skill_before = set(os.listdir(C.SKILL))

        try:
            with tempfile.TemporaryDirectory() as tmpdir:
                boxes_path = os.path.join(C.SKILL, "examples", "humanoid_boxes.json")
                ground_path = os.path.join(C.SKILL, "examples", "game_demo", "ground_boxes.json")
                mesh_out = os.path.join(tmpdir, "mesh.bm")

                code, _, stderr = C.run_cli(["mesh", boxes_path, ground_path, "-o", mesh_out])
                self.assertEqual(code, 0, f"mesh failed: {stderr}")

                skel_path = os.path.join(C.SKILL, "examples", "humanoid.json")
                skel_out = os.path.join(tmpdir, "skel.bm")

                code, _, stderr = C.run_cli(["skel", skel_path, "--in", mesh_out, "-o", skel_out])
                self.assertEqual(code, 0, f"skel failed: {stderr}")

                walk_path = os.path.join(C.SKILL, "examples", "walk.json")
                anim_out = os.path.join(tmpdir, "anim.bm")

                code, _, stderr = C.run_cli(["anim", walk_path, "--in", skel_out, "-o", anim_out])
                self.assertEqual(code, 0, f"anim failed: {stderr}")

            # Check state after (should be unchanged outside tmpdir)
            repo_after = set(os.listdir(C.REPO))
            skill_after = set(os.listdir(C.SKILL))

            self.assertEqual(repo_before, repo_after, "REPO directory was modified")
            self.assertEqual(skill_before, skill_after, "SKILL directory was modified")

        finally:
            pass


if __name__ == "__main__":
    unittest.main()
