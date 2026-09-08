#!/usr/bin/env python3
"""Generate bounded, reproducible scaling inputs; does not run the packer.

Generated geometry follows tests/cpp/practical_packing_tests.cpp and the concave
star in tests/cpp/packing_tests.cpp. Only Python's standard library is required.
"""
from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
import struct
import sys
from pathlib import Path

VERSION = 2
ROOT = Path(__file__).resolve().parents[2]
MAX_BYTES = 64 * 1024 * 1024
MAX_TRIANGLES = 200_000
BOX_FACES = [
    (0, 2, 1),
    (0, 3, 2),
    (4, 5, 6),
    (4, 6, 7),
    (0, 1, 5),
    (0, 5, 4),
    (3, 7, 6),
    (3, 6, 2),
    (0, 4, 7),
    (0, 7, 3),
    (1, 2, 6),
    (1, 6, 5),
]


def f32(value):
    result = struct.unpack("<f", struct.pack("<f", value))[0]
    if not math.isfinite(result):
        raise ValueError("nonfinite float32 coordinate")
    return result


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def cross(a, b):
    return (
        a[1] * b[2] - a[2] * b[1],
        a[2] * b[0] - a[0] * b[2],
        a[0] * b[1] - a[1] * b[0],
    )


def dot(a, b):
    return math.fsum(x * y for x, y in zip(a, b))


def bounds(vertices):
    return (
        [min(p[k] for p in vertices) for k in range(3)],
        [max(p[k] for p in vertices) for k in range(3)],
    )


def centered(vertices):
    pivot = tuple(math.fsum(p[k] for p in vertices) / len(vertices) for k in range(3))
    return [sub(p, pivot) for p in vertices], pivot


def volume(vertices, faces):
    origin = vertices[0]
    return (
        math.fsum(
            dot(
                sub(vertices[a], origin),
                cross(sub(vertices[b], origin), sub(vertices[c], origin)),
            )
            for a, b, c in faces
        )
        / 6
    )


def validate_mesh(vertices, faces):
    if not 4 <= len(faces) <= MAX_TRIANGLES:
        raise ValueError("triangle count outside bounded fixture domain")
    if not all(math.isfinite(x) and abs(x) <= 1e12 for p in vertices for x in p):
        raise ValueError("invalid or excessive coordinate")
    edges = {}
    neighbours = [set() for _ in vertices]
    for face in faces:
        if len(set(face)) != 3 or any(i < 0 or i >= len(vertices) for i in face):
            raise ValueError("invalid triangle indices")
        a, b, c = (vertices[i] for i in face)
        if math.hypot(*cross(sub(b, a), sub(c, a))) == 0:
            raise ValueError("degenerate triangle")
        for a, b in zip(face, face[1:] + face[:1]):
            edge = tuple(sorted((a, b)))
            edges.setdefault(edge, []).append((a, b))
            neighbours[a].add(b)
            neighbours[b].add(a)
    if any(len(uses) != 2 or uses[0] != uses[1][::-1] for uses in edges.values()):
        raise ValueError("surface is not closed with consistent orientation")
    reached, pending = set(), [0]
    while pending:
        index = pending.pop()
        if index not in reached:
            reached.add(index)
            pending.extend(neighbours[index] - reached)
    if len(reached) != len(vertices) or len(vertices) - len(edges) + len(faces) != 2:
        raise ValueError("fixture must be one genus-zero closed surface")
    result = volume(vertices, faces)
    if not math.isfinite(result) or result <= 0:
        raise ValueError("fixture must have positive signed volume")
    return result


def stl_bytes(vertices, faces):
    vertices = [tuple(f32(x) for x in p) for p in vertices]
    validate_mesh(vertices, faces)
    data = bytearray(
        f"IROP scaling fixture v{VERSION}".encode("ascii").ljust(80, b"\0")
    )
    data.extend(struct.pack("<I", len(faces)))
    for a, b, c in faces:
        data.extend(
            struct.pack("<12fH", 0, 0, 0, *vertices[a], *vertices[b], *vertices[c], 0)
        )
    return bytes(data), vertices


def read_binary_stl(path):
    # Deliberately narrow fixture-tool input domain; production STL loading remains
    # authoritative and supports ASCII as well. Supplied repository inputs are binary.
    with path.open("rb") as stream:
        data = stream.read(MAX_BYTES + 1)
    if not 84 <= len(data) <= MAX_BYTES:
        raise ValueError("binary STL outside the 64 MiB fixture-tool byte bound")
    count = struct.unpack_from("<I", data, 80)[0]
    if count > MAX_TRIANGLES or len(data) != 84 + 50 * count:
        raise ValueError("expected bounded binary STL with exact record length")
    vertices, faces, indices = [], [], {}
    for i in range(count):
        values = struct.unpack_from("<12fH", data, 84 + 50 * i)
        face = []
        for offset in (3, 6, 9):
            point = tuple(values[offset : offset + 3])
            if point not in indices:
                indices[point] = len(vertices)
                vertices.append(point)
            face.append(indices[point])
        faces.append(tuple(face))
    validate_mesh(vertices, faces)
    return data, vertices, faces


def source_mesh(shape, detail):
    n = 12
    vertices = []
    for sign in (-1, 1):
        for i in range(n):
            radius = (0.9 if shape != "concave" or i % 2 == 0 else 0.38) * (
                1 + 0.0025 * i
            )
            angle = 2 * math.pi * i / n
            point = (
                radius * math.cos(angle),
                radius * math.sin(angle),
                sign * (1.055 + 0.0001 * i * i),
            )
            if shape == "slender":
                point = (point[0] * 0.3, point[1] * 0.3, point[2] * 1.8)
            vertices.append(point)
    faces = []
    for i in range(n):
        j = (i + 1) % n
        faces.extend([(i, j, n + j), (i, n + j, n + i)])
    if shape == "concave":
        vertices.extend([(0, 0, -1.055), (0, 0, 1.055)])
        for i in range(n):
            j = (i + 1) % n
            faces.extend([(2 * n, j, i), (2 * n + 1, n + i, n + j)])
    else:
        for i in range(1, n - 1):
            faces.extend([(0, i + 1, i), (n, n + i, n + i + 1)])
    for _ in range(detail):
        midpoints, refined = {}, []

        def midpoint(a, b, edge_midpoints):
            edge = tuple(sorted((a, b)))
            if edge not in edge_midpoints:
                edge_midpoints[edge] = len(vertices)
                vertices.append(
                    tuple((x + y) / 2 for x, y in zip(vertices[a], vertices[b]))
                )
            return edge_midpoints[edge]

        for a, b, c in faces:
            ab, bc, ca = (
                midpoint(a, b, midpoints),
                midpoint(b, c, midpoints),
                midpoint(c, a, midpoints),
            )
            refined.extend([(a, ab, ca), (ab, b, bc), (ca, bc, c), (ab, bc, ca)])
        faces = refined
    return vertices, faces


def shear(point):
    x, y, z = point
    return (x + 0.04 * z, y + 0.025 * x, z)


def unshear(point):
    x, y, z = point
    original_x = x - 0.04 * z
    return (original_x, y - 0.025 * original_x, z)


def generated_case(vertices, faces, count, density):
    vertices, pivot = centered(
        vertices
    )  # Same mathematical vertex-centroid convention as the service.
    inverse = [unshear(p) for p in vertices]
    lo, hi = bounds(inverse)
    extent = sub(hi, lo)
    # Container shape is fixed within each source/detail series. Only uniform
    # count^(1/3) scaling changes between counts; unused witness cells are allowed.
    factor = (count * volume(vertices, faces) / (density * math.prod(extent))) ** (
        1 / 3
    )
    grid_side = 1
    while grid_side**3 < count:
        grid_side += 1
    if factor / grid_side <= 1.01:
        raise ValueError(
            "density leaves insufficient space for a separated grid witness"
        )
    dims = (grid_side, grid_side, grid_side)
    pitch = tuple(x * factor / grid_side for x in extent)
    half = tuple(x * factor / 2 for x in extent)
    corners = [
        (-1, -1, -1),
        (1, -1, -1),
        (1, 1, -1),
        (-1, 1, -1),
        (-1, -1, 1),
        (1, -1, 1),
        (1, 1, 1),
        (-1, 1, 1),
    ]
    container_data, container = stl_bytes(
        [shear(tuple(p[k] * half[k] for k in range(3))) for p in corners], BOX_FACES
    )
    planes = []
    for a, b, c in BOX_FACES:
        normal = cross(sub(container[b], container[a]), sub(container[c], container[a]))
        length = math.hypot(*normal)
        planes.append((container[a], tuple(x / length for x in normal)))
    translations = []
    minimum_cell_margin = math.inf
    minimum_container_clearance = math.inf
    cells = sorted(
        itertools.product(*(range(n) for n in dims)),
        key=lambda cell: (sum((2 * i - grid_side + 1) ** 2 for i in cell), cell),
    )
    for index in cells[:count]:
        cell_center = tuple(-half[k] + (index[k] + 0.5) * pitch[k] for k in range(3))
        translation = shear(
            tuple(cell_center[k] - (lo[k] + hi[k]) / 2 for k in range(3))
        )
        translations.append(list(translation))
        # The proof checks the actual float32 representation of every witness vertex.
        for p in vertices:
            output_point = tuple(f32(p[k] + translation[k]) for k in range(3))
            local = unshear(output_point)
            minimum_cell_margin = min(
                minimum_cell_margin,
                *(pitch[k] / 2 - abs(local[k] - cell_center[k]) for k in range(3)),
            )
            minimum_container_clearance = min(
                minimum_container_clearance,
                *(-dot(normal, sub(output_point, origin)) for origin, normal in planes),
            )
    if minimum_cell_margin <= 0 or minimum_container_clearance <= 0:
        raise ValueError(
            "float32 constructive witness lost strict separation or containment"
        )
    witness = {
        "schema_version": 1,
        "volume_scale": 1.0,
        "rotation_radians": [0, 0, 0],
        "source_vertex_centroid": list(pivot),
        "translations": translations,
        "grid_dimensions": list(dims),
        "grid_capacity": grid_side**3,
        "unused_grid_cells": grid_side**3 - count,
        "inverse_shear_cell_pitch": list(pitch),
        "pivot_arithmetic": "Explicit mathematical vertex centroid; not a bitwise C++ transform replay.",
        "minimum_float32_cell_margin": minimum_cell_margin,
        "minimum_float32_container_plane_clearance": minimum_container_clearance,
        "proof": "Each serialized full-size copy lies strictly inside its own disjoint inverse-shear "
        "grid cell and all outward container triangle halfspaces. Triangles lie in the "
        "convex hull of their vertices. This establishes a fit layout, not solver success.",
        "used_as_engine_initial_state": False,
    }
    return container_data, container, witness


def encoded(value):
    return (json.dumps(value, indent=2, sort_keys=True, allow_nan=False) + "\n").encode(
        "utf-8"
    )


def metadata(name, data, vertices, faces):
    lo, hi = bounds(vertices)
    return {
        "path": name,
        "sha256": hashlib.sha256(data).hexdigest(),
        "size_bytes": len(data),
        "vertices": len(vertices),
        "triangles": len(faces),
        "volume": volume(vertices, faces),
        "bounds": {"minimum": lo, "maximum": hi},
    }


def build_artifacts(args):
    manifest = {
        "schema_version": 1,
        "generator_version": VERSION,
        "generator_sha256": hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
        "counts": args.counts,
        "seeds": args.seeds,
        "primary_seed": args.seeds[0],
        "results": "unmeasured; generation is not packing acceptance",
        "workloads": {
            "primary_growth": {
                "initial_volume_scale": 0.1,
                "final_volume_scale": 1.0,
                "scale_steps": 9,
            },
            "near_target_diagnostic": {
                "initial_volume_scale": 0.9,
                "final_volume_scale": 1.0,
                "scale_steps": 1,
            },
            "direct_placement": {
                "initial_volume_scale": 1.0,
                "final_volume_scale": 1.0,
                "scale_steps": 1,
            },
        },
        "settings": {
            "adaptive_sampling": False,
            "structured_initialization_fallback": False,
            "engine_timeout_ms": 300000,
            "local_timeout_ms": 30000,
            "local_iterations": 1000,
        },
        "timing_repeats": 3,
        "input_validation": "Structural mesh checks only; production geometry validation remains required.",
        "performance_targets": None,
        "target_registration_rule": "Record practical wall/memory targets after the first baseline and "
        "before selecting an optimization. Preserve failed measurements.",
        "cases": [],
    }
    artifacts = {}
    if args.object:
        object_data, vertices, faces = read_binary_stl(args.object)
        container_data, container, container_faces = read_binary_stl(args.container)
        manifest["family"] = "supplied-scaled-container"
        manifest["source_inputs"] = {
            "object": {
                "path": str(args.object.resolve()),
                "sha256": hashlib.sha256(object_data).hexdigest(),
            },
            "container": {
                "path": str(args.container.resolve()),
                "sha256": hashlib.sha256(container_data).hexdigest(),
            },
        }
        artifacts["object.stl"] = object_data
        artifacts["fixed-container.stl"] = container_data
        fixed_volume = volume(container, container_faces)
        for count in args.counts:
            factor = (count / args.reference_count) ** (1 / 3)
            scaled_data, scaled = stl_bytes(
                [tuple(x * factor for x in p) for p in container], container_faces
            )
            name = f"container-{count}.stl"
            artifacts[name] = scaled_data
            ratio = count * volume(vertices, faces) / fixed_volume
            manifest["cases"].append(
                {
                    "id": f"supplied-{count}",
                    "count": count,
                    "object": metadata("object.stl", object_data, vertices, faces),
                    "container": metadata(name, scaled_data, scaled, container_faces),
                    "container_linear_scale_about_origin": factor,
                    "reference_count": args.reference_count,
                    "target_volume_fraction": args.reference_count
                    * volume(vertices, faces)
                    / fixed_volume,
                    "actual_volume_fraction": count
                    * volume(vertices, faces)
                    / volume(scaled, container_faces),
                    "feasibility": "unproven: volume fraction alone is not a constructive fit witness",
                    "fit_witness": None,
                    "fixed_container_control": {
                        "container": "fixed-container.stl",
                        "volume_fraction": ratio,
                        "classification": "volume-non-fit-if-valid-solids"
                        if ratio > 1
                        else "feasibility-unproven",
                        "part_of_fixed_density_series": False,
                    },
                }
            )
    else:
        manifest["family"] = "generated-known-fit"
        manifest[
            "container_scaling"
        ] = "Fixed aspect and shear per source/detail; uniform count^(1/3) scale."
        manifest["geometry_sources"] = [
            "tests/cpp/practical_packing_tests.cpp:packing_cylinder",
            "tests/cpp/packing_tests.cpp:physical_step_recovery_meshes",
            "tests/cpp/support/test_support.hpp:box_mesh/cylinder_mesh",
        ]
        for shape, detail in itertools.product(args.shapes, args.details):
            vertices, faces = source_mesh(shape, detail)
            source_data, vertices = stl_bytes(vertices, faces)
            source_name = f"{shape}-detail{detail}.stl"
            artifacts[source_name] = source_data
            for count in args.counts:
                case_id = f"{shape}-detail{detail}-{count}"
                container_data, container, witness = generated_case(
                    vertices, faces, count, args.density
                )
                container_name, witness_name = (
                    f"{case_id}-container.stl",
                    f"{case_id}-witness.json",
                )
                artifacts[container_name] = container_data
                artifacts[witness_name] = encoded(witness)
                manifest["cases"].append(
                    {
                        "id": case_id,
                        "count": count,
                        "shape": shape,
                        "detail": detail,
                        "object": metadata(source_name, source_data, vertices, faces),
                        "container": metadata(
                            container_name, container_data, container, BOX_FACES
                        ),
                        "target_volume_fraction": args.density,
                        "uniform_container_scale_from_count_one": count ** (1 / 3),
                        "actual_volume_fraction": count
                        * volume(vertices, faces)
                        / volume(container, BOX_FACES),
                        "feasibility": "constructive full-size fit witness",
                        "fit_witness": witness_name,
                    }
                )
    artifacts["manifest.json"] = encoded(manifest)
    return artifacts


def integer_list(text, minimum, maximum, limit):
    if len(text) > 128:
        raise argparse.ArgumentTypeError("numeric list exceeds its bounded text length")
    values = [int(x) for x in text.split(",")]
    if (
        not 1 <= len(values) <= limit
        or len(set(values)) != len(values)
        or any(not minimum <= x <= maximum for x in values)
    ):
        raise argparse.ArgumentTypeError(
            "list has duplicates or exceeds its bounded domain"
        )
    return values


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--output",
        required=True,
        type=Path,
        help="new directory below repository build/",
    )
    parser.add_argument(
        "--counts", default=[10, 100, 300], type=lambda s: integer_list(s, 1, 300, 8)
    )
    parser.add_argument(
        "--seeds",
        default=[1918, 0, 12345],
        type=lambda s: integer_list(s, 0, 2**32 - 1, 8),
    )
    parser.add_argument(
        "--details", default=[0, 1], type=lambda s: integer_list(s, 0, 2, 3)
    )
    parser.add_argument(
        "--shapes",
        nargs="+",
        choices=["irregular", "slender", "concave"],
        default=["irregular", "slender", "concave"],
    )
    parser.add_argument("--density", type=float, default=0.05)
    parser.add_argument(
        "--object",
        type=Path,
        help="optional supplied binary STL (both inputs required)",
    )
    parser.add_argument("--container", type=Path)
    parser.add_argument("--reference-count", type=int, default=10)
    args = parser.parse_args(argv)
    try:
        destination = args.output.resolve()
        destination.relative_to((ROOT / "build").resolve())
        if destination == (ROOT / "build").resolve() or destination.exists():
            raise ValueError("output must be a new child directory below build/")
        if bool(args.object) != bool(args.container):
            raise ValueError("--object and --container must be supplied together")
        if not math.isfinite(args.density) or not 0.001 <= args.density <= 0.4:
            raise ValueError("density must be finite and between .001 and .4")
        if not 1 <= args.reference_count <= 300 or len(set(args.shapes)) != len(
            args.shapes
        ):
            raise ValueError("reference count or shape list outside bounded domain")
        artifacts = build_artifacts(args)
        if sum(map(len, artifacts.values())) > 128 * 1024 * 1024:
            raise ValueError("generated artifacts exceed the 128 MiB output bound")
        destination.mkdir(parents=True, exist_ok=False)
        for name, data in artifacts.items():
            with (destination / name).open("xb") as stream:
                stream.write(data)
        print(destination / "manifest.json")
        return 0
    except (ValueError, OSError, OverflowError) as error:
        parser.exit(2, f"fixture generation failed: {error}\n")


if __name__ == "__main__":
    sys.exit(main())
