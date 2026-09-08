"""Small structural/artifact checks; never runs slow packing measurements."""
import hashlib
import importlib.util
import json
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

GENERATOR = Path(__file__).with_name("generate_scaling.py")
spec = importlib.util.spec_from_file_location("generate_scaling", GENERATOR)
generator = importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class ScalingFixtures(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        build = generator.ROOT / "build"
        build.mkdir(exist_ok=True)
        cls.scratch = tempfile.TemporaryDirectory(prefix="fixture-check-", dir=build)
        cls.directory = Path(cls.scratch.name).resolve()

    @classmethod
    def tearDownClass(cls):
        cls.directory.relative_to((generator.ROOT / "build").resolve())
        cls.scratch.cleanup()

    def run_generator(self, name, *arguments):
        destination = self.directory / name
        result = subprocess.run(
            [sys.executable, str(GENERATOR), "--output", str(destination), *arguments],
            capture_output=True,
            text=True,
            check=False,
        )
        return destination, result

    def test_surface_topology_and_detail_preserves_volume(self):
        for shape in ("irregular", "slender", "concave"):
            base_volume = None
            for detail in (0, 1, 2):
                vertices, faces = generator.source_mesh(shape, detail)
                data, vertices = generator.stl_bytes(vertices, faces)
                self.assertEqual(
                    len(faces), (48 if shape == "concave" else 44) * 4**detail
                )
                self.assertEqual(len(data), 84 + 50 * len(faces))
                result = generator.validate_mesh(vertices, faces)
                if base_volume is None:
                    base_volume = result
                self.assertAlmostEqual(result / base_volume, 1, places=7)

    def test_manifest_has_constant_density_and_explicit_witnesses(self):
        directory, result = self.run_generator("density", "--details", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((directory / "manifest.json").read_text())
        self.assertEqual(len(manifest["cases"]), 9)
        self.assertEqual(manifest["generator_version"], 2)
        normalized_dimensions = {}
        self.assertEqual(manifest["seeds"], [1918, 0, 12345])
        self.assertEqual(
            manifest["workloads"]["primary_growth"]["initial_volume_scale"], 0.1
        )
        self.assertIsNone(manifest["performance_targets"])
        for case in manifest["cases"]:
            self.assertAlmostEqual(case["actual_volume_fraction"], 0.05, places=7)
            witness = json.loads((directory / case["fit_witness"]).read_text())
            self.assertEqual(len(witness["translations"]), case["count"])
            self.assertEqual(witness["volume_scale"], 1)
            self.assertGreaterEqual(witness["grid_capacity"], case["count"])
            self.assertEqual(
                witness["grid_capacity"] - witness["unused_grid_cells"], case["count"]
            )
            self.assertEqual(len(set(witness["grid_dimensions"])), 1)
            box = case["container"]["bounds"]
            dimensions = [
                (b - a) / case["count"] ** (1 / 3)
                for a, b in zip(box["minimum"], box["maximum"])
            ]
            series = (case["shape"], case["detail"])
            if series in normalized_dimensions:
                for actual, expected in zip(dimensions, normalized_dimensions[series]):
                    # Each count serializes its scaled bounds independently to float32.
                    self.assertAlmostEqual(actual / expected, 1, delta=3 * 2**-23)
            else:
                normalized_dimensions[series] = dimensions
            self.assertGreater(witness["minimum_float32_cell_margin"], 0)
            self.assertGreater(witness["minimum_float32_container_plane_clearance"], 0)
            self.assertFalse(witness["used_as_engine_initial_state"])
            for key in ("object", "container"):
                data = (directory / case[key]["path"]).read_bytes()
                self.assertEqual(hashlib.sha256(data).hexdigest(), case[key]["sha256"])

    def test_independent_serialized_witness_geometry(self):
        directory, result = self.run_generator(
            "independent", "--counts", "100", "--shapes", "concave", "--details", "0"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((directory / "manifest.json").read_text())
        case = manifest["cases"][0]
        witness = json.loads((directory / case["fit_witness"]).read_text())

        def read_triangles(name):
            data = (directory / name).read_bytes()
            count = struct.unpack_from("<I", data, 80)[0]
            self.assertEqual(len(data), 84 + count * 50)
            triangles = []
            for index in range(count):
                coords = struct.unpack_from("<9f", data, 84 + index * 50 + 12)
                triangles.append([coords[offset : offset + 3] for offset in (0, 3, 6)])
            return triangles

        source = {
            point
            for triangle in read_triangles(case["object"]["path"])
            for point in triangle
        }
        container = read_triangles(case["container"]["path"])
        object_bounds = []
        for translation in witness["translations"]:
            transformed = [
                tuple(
                    struct.unpack(
                        "<f",
                        struct.pack(
                            "<f",
                            point[k]
                            - witness["source_vertex_centroid"][k]
                            + translation[k],
                        ),
                    )[0]
                    for k in range(3)
                )
                for point in source
            ]
            # Check all actual STL triangle planes, without trusting stored margins.
            for a, b, c in container:
                ab = [b[k] - a[k] for k in range(3)]
                ac = [c[k] - a[k] for k in range(3)]
                normal = [
                    ab[1] * ac[2] - ab[2] * ac[1],
                    ab[2] * ac[0] - ab[0] * ac[2],
                    ab[0] * ac[1] - ab[1] * ac[0],
                ]
                for point in transformed:
                    self.assertLess(
                        sum(normal[k] * (point[k] - a[k]) for k in range(3)), 0
                    )
            inverse = [
                (x - 0.04 * z, y - 0.025 * (x - 0.04 * z), z) for x, y, z in transformed
            ]
            object_bounds.append(
                [
                    (min(p[k] for p in inverse), max(p[k] for p in inverse))
                    for k in range(3)
                ]
            )
        # Independently compare all pairs, rather than relying on the grid metadata.
        for index, first in enumerate(object_bounds):
            for second in object_bounds[index + 1 :]:
                self.assertTrue(
                    any(
                        first[k][1] < second[k][0] or second[k][1] < first[k][0]
                        for k in range(3)
                    )
                )

    def test_generation_is_byte_reproducible(self):
        first, result = self.run_generator("first", "--counts", "10", "--details", "0")
        self.assertEqual(result.returncode, 0, result.stderr)
        second, result = self.run_generator(
            "second", "--counts", "10", "--details", "0"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        for path in first.iterdir():
            self.assertEqual(path.read_bytes(), (second / path.name).read_bytes())

    def test_existing_output_is_preserved(self):
        directory, result = self.run_generator(
            "occupied", "--counts", "1", "--details", "0"
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        before = {p.name: p.read_bytes() for p in directory.iterdir()}
        _, result = self.run_generator("occupied", "--counts", "1", "--details", "0")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(before, {p.name: p.read_bytes() for p in directory.iterdir()})

    def test_bad_configuration_creates_no_output(self):
        for index, arguments in enumerate(
            (
                ("--density", "nan"),
                ("--counts", "0"),
                ("--counts", "301"),
                ("--counts", "10,10"),
                ("--details", "3"),
                ("--density", ".4"),
                ("--object", "missing.stl"),
            )
        ):
            directory, result = self.run_generator(f"invalid-{index}", *arguments)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertFalse(directory.exists())

    def test_supplied_scaling_retains_hashes_and_separates_non_fit(self):
        original, result = self.run_generator(
            "supplied-source",
            "--counts",
            "1",
            "--details",
            "0",
            "--shapes",
            "irregular",
            "--density",
            ".05",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        directory, result = self.run_generator(
            "supplied",
            "--object",
            str(original / "irregular-detail0.stl"),
            "--container",
            str(original / "irregular-detail0-1-container.stl"),
            "--reference-count",
            "1",
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        manifest = json.loads((directory / "manifest.json").read_text())
        self.assertEqual(manifest["family"], "supplied-scaled-container")
        for case in manifest["cases"]:
            self.assertAlmostEqual(case["actual_volume_fraction"], 0.05, places=7)
            self.assertIsNone(case["fit_witness"])
            control = case["fixed_container_control"]
            self.assertFalse(control["part_of_fixed_density_series"])
            if case["count"] >= 100:
                self.assertEqual(
                    control["classification"], "volume-non-fit-if-valid-solids"
                )

    def test_bad_binary_stl_is_rejected_before_output(self):
        damaged = self.directory / "damaged.stl"
        damaged.write_bytes(b"not a binary STL")
        directory, result = self.run_generator(
            "bad-input", "--object", str(damaged), "--container", str(damaged)
        )
        self.assertEqual(result.returncode, 2)
        self.assertFalse(directory.exists())

    def test_outside_build_output_is_rejected(self):
        forbidden = generator.ROOT / "fixture-generator-must-not-create"
        self.assertFalse(forbidden.exists())
        result = subprocess.run(
            [sys.executable, str(GENERATOR), "--output", str(forbidden)],
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(result.returncode, 2)
        self.assertFalse(forbidden.exists())


if __name__ == "__main__":
    unittest.main()
