#!/usr/bin/env python3
"""Verify the checked-in parity corpus against the Python reference implementation."""

from __future__ import annotations

import argparse
import inspect
import json
from pathlib import Path
from unittest import mock

import numpy as np
import pyvista as pv

from irregular_object_packing.cat.chordal_axis_transform import process_cells_to_normals
from irregular_object_packing.cat.tetra_cell import TetraCell
from irregular_object_packing.packing import initialize as reference_initialize
from irregular_object_packing.packing.nlc_optimisation import (
    _compute_constraint_jacobian_dense,
    _compute_objective_gradient,
    construct_transform_matrix_from_array,
    local_constraint_for_vertex,
    objective,
    transform_v,
)
from irregular_object_packing.packing.optimizer import Optimizer
from irregular_object_packing.packing.optimizer_data import SimConfig


DEFAULT_FIXTURE = (
    Path(__file__).resolve().parents[1] / "fixtures" / "parity_reference_v1.json"
)


def _polydata(definition: dict) -> pv.PolyData:
    points = np.asarray(definition["vertices"], dtype=np.float64)
    triangles = np.asarray(definition["triangles"], dtype=np.int64)
    face_sizes = np.full((triangles.shape[0], 1), 3, dtype=np.int64)
    faces = np.hstack((face_sizes, triangles)).reshape(-1)
    return pv.PolyData(points, faces)


def _primitive(corpus: dict, name: str) -> pv.PolyData:
    if name in corpus["primitives"]:
        return _polydata(corpus["primitives"][name])

    definition = corpus["generated_primitives"][name]
    if definition["kind"] == "faceted_cylinder":
        radius = float(definition["radius"])
        half_height = float(definition["half_height"])
        segment_count = int(definition["segment_count"])
        points = []
        for height in (-half_height, half_height):
            for index in range(segment_count):
                angle = 2.0 * np.pi * index / segment_count
                points.append([radius * np.cos(angle), radius * np.sin(angle), height])

        triangles = []
        top_offset = segment_count
        for index in range(segment_count):
            next_index = (index + 1) % segment_count
            triangles.append([index, next_index, top_offset + next_index])
            triangles.append([index, top_offset + next_index, top_offset + index])
        for index in range(1, segment_count - 1):
            triangles.append([0, index + 1, index])
            triangles.append([top_offset, top_offset + index, top_offset + index + 1])
        return _polydata({"vertices": points, "triangles": triangles})

    if definition["kind"] == "box":
        half_x, half_y, half_z = map(float, definition["half_extents"])
        return _polydata(
            {
                "vertices": [
                    [-half_x, -half_y, -half_z],
                    [half_x, -half_y, -half_z],
                    [half_x, half_y, -half_z],
                    [-half_x, half_y, -half_z],
                    [-half_x, -half_y, half_z],
                    [half_x, -half_y, half_z],
                    [half_x, half_y, half_z],
                    [-half_x, half_y, half_z],
                ],
                "triangles": [
                    [0, 2, 1],
                    [0, 3, 2],
                    [4, 5, 6],
                    [4, 6, 7],
                    [0, 1, 5],
                    [0, 5, 4],
                    [3, 7, 6],
                    [3, 6, 2],
                    [0, 4, 7],
                    [0, 7, 3],
                    [1, 2, 6],
                    [1, 6, 5],
                ],
            }
        )

    raise AssertionError(f"unknown generated primitive: {name}")


def _assert_close(actual, expected, *, atol: float = 2.0e-12) -> None:
    np.testing.assert_allclose(
        np.asarray(actual, dtype=np.float64),
        np.asarray(expected, dtype=np.float64),
        rtol=0.0,
        atol=atol,
    )


def _verify_initialization(corpus: dict) -> None:
    for case in corpus["initialization"]["success_cases"]:
        object_mesh = _primitive(corpus, case["object"])
        container = _primitive(corpus, case["container"])
        count = int(case["object_count"])
        initial_scale = float(case["initial_volume_scale"])
        expected = case["expected"]

        # The reference initializer derives a count from coverage instead of
        # accepting one. Put the threshold inside the final object's volume
        # interval so repeated-sum rounding cannot request an extra copy.
        coverage = (count - 0.25) * object_mesh.volume / container.volume
        captured: dict[str, int] = {}
        original_generate = reference_initialize.generate_initial_coordinates

        def capture_generate(*args, **kwargs):
            coordinates, skipped = original_generate(*args, **kwargs)
            captured["skipped"] = int(skipped)
            return coordinates, skipped

        np.random.seed(int(case["seed"]))
        with mock.patch.object(
            reference_initialize,
            "generate_initial_coordinates",
            side_effect=capture_generate,
        ):
            transforms = reference_initialize.initialize_state(
                object_mesh,
                container,
                coverage,
                initial_scale,
            )

        assert transforms.shape == (count, 7), case["name"]
        assert captured["skipped"] == expected["rejected_candidate_count"], case["name"]
        uniform_draws = 3 * (count + captured["skipped"]) + 3 * count
        assert uniform_draws == expected["random_draw_count"], case["name"]
        radius = reference_initialize.get_max_radius(object_mesh) * np.cbrt(
            initial_scale
        )
        _assert_close(radius, expected["object_bounding_radius"], atol=2.0e-15)

        for sampled in expected["sampled_transforms"]:
            index = int(sampled["index"])
            expected_transform = [
                sampled["volume_scale"],
                *sampled["rotation"],
                *sampled["translation"],
            ]
            _assert_close(transforms[index], expected_transform, atol=2.0e-14)

    for case in corpus["initialization"]["failure_cases"]:
        object_mesh = _primitive(corpus, case["object"])
        container = _primitive(corpus, case["container"])
        assert not container.is_manifold, case["name"]
        try:
            reference_initialize.generate_initial_coordinates(
                container,
                object_mesh,
                coverage_rate=0.1,
                f_init=float(case["initial_volume_scale"]),
            )
        except AssertionError:
            pass
        else:
            raise AssertionError(
                f"{case['name']}: Python accepted a non-manifold container"
            )


def _verify_transform(corpus: dict) -> None:
    case = corpus["transform"]
    transform = case["transform"]
    tf_array = np.asarray(
        [transform["volume_scale"], *transform["rotation"], *transform["translation"]],
        dtype=np.float64,
    )
    matrix = construct_transform_matrix_from_array(tf_array)
    _assert_close(matrix, case["expected_matrix"])
    actual_point = transform_v(
        np.asarray(case["input_point"], dtype=np.float64), matrix
    )
    _assert_close(actual_point, case["expected_point"])


def _verify_cat(corpus: dict) -> None:
    case = corpus["cat"]
    points = np.asarray(case["points"], dtype=np.float64)
    cell = TetraCell(case["tetrahedron"], case["owners"], 0)
    normals, polygons, _ = process_cells_to_normals(
        points,
        [cell],
        int(case["participant_count"]),
    )
    expected = case["expected"]
    for participant in range(int(case["participant_count"])):
        expected_polygons = expected["polygons_by_participant"][str(participant)]
        assert len(polygons[participant]) == len(expected_polygons)
        for actual_polygon, expected_polygon in zip(
            polygons[participant], expected_polygons
        ):
            _assert_close(actual_polygon, expected_polygon)
        expected_constraints = expected["constraints_by_participant"][str(participant)]
        assert len(normals[participant]) == len(expected_constraints)
        assert (
            len(normals[participant])
            == expected["constraints_per_participant"][participant]
        )
        for actual_constraint, expected_constraint in zip(
            normals[participant], expected_constraints
        ):
            _assert_close(actual_constraint[0], expected_constraint["source_point"])
            _assert_close(actual_constraint[1], expected_constraint["plane_point"])
            _assert_close(
                actual_constraint[2], expected_constraint["inward_unit_normal"]
            )


def _verify_local_constraint(corpus: dict) -> None:
    case = corpus["local_constraint"]
    step = case["step"]
    tf_array = np.asarray(
        [
            step["volume_scale_multiplier"],
            *step["rotation_delta"],
            *step["translation_delta"],
        ],
        dtype=np.float64,
    )
    _assert_close(objective(tf_array), case["expected_objective"], atol=2.0e-15)
    _assert_close(
        _compute_objective_gradient(tf_array), case["expected_objective_gradient"]
    )
    matrix = construct_transform_matrix_from_array(tf_array)
    constraint = local_constraint_for_vertex(
        np.asarray(case["current_vertex"], dtype=np.float64),
        np.asarray(case["plane_point"], dtype=np.float64),
        np.asarray(case["inward_unit_normal"], dtype=np.float64),
        matrix,
        np.asarray(case["object_center"], dtype=np.float64),
        float(case["padding"]),
    )
    _assert_close(constraint, case["expected_constraint"])
    constraint_data = np.asarray(
        [[case["current_vertex"], case["plane_point"], case["inward_unit_normal"]]],
        dtype=np.float64,
    )
    gradient = _compute_constraint_jacobian_dense(
        tf_array,
        constraint_data,
        np.asarray(case["object_center"], dtype=np.float64),
        float(case["padding"]),
    )[0]
    # The live reference exposes this gradient as a forward-difference Jacobian
    # with eps=1e-7. Compare it with the analytic fixture using a tolerance that
    # covers the expected first-order truncation error.
    _assert_close(gradient, case["expected_gradient"], atol=6.0e-8)


def _verify_dense_initialization(corpus: dict) -> None:
    case = corpus["dense_initialization"]
    object_mesh = _primitive(corpus, case["object"])
    container = _primitive(corpus, case["container"])
    accepted_prefix = case["accepted_prefix"]
    policy = case["python_behavior"]
    count = int(accepted_prefix["object_count"])
    initial_scale = float(case["initial_volume_scale"])
    coverage = (count - 0.25) * object_mesh.volume / container.volume
    captured: dict[str, int] = {}
    original_generate = reference_initialize.generate_initial_coordinates

    def capture_generate(*args, **kwargs):
        coordinates, skipped = original_generate(*args, **kwargs)
        captured["skipped"] = int(skipped)
        return coordinates, skipped

    timeout_calls = []

    def bounded_timeout_loop(function, parameters, max_runs):
        timeout_calls.append((function, max_runs))
        return reference_initialize.generate_correct_coordinates(*parameters)

    np.random.seed(int(case["seed"]))
    # Capture the live timeout-loop branch and replace only its wait/retry
    # behavior with a bounded direct call. The placement function itself remains
    # the live reference implementation.
    with mock.patch.object(
        reference_initialize,
        "timeout_loop",
        side_effect=bounded_timeout_loop,
    ):
        with mock.patch.object(
            reference_initialize,
            "generate_initial_coordinates",
            side_effect=capture_generate,
        ):
            transforms = reference_initialize.initialize_state(
                object_mesh,
                container,
                coverage,
                initial_scale,
            )

    assert len(timeout_calls) == 1
    timeout_function, timeout_max_runs = timeout_calls[0]
    assert timeout_function is reference_initialize.generate_correct_coordinates_timeout
    assert timeout_max_runs == policy["timeout_loop_max_runs"]
    wrapper = getattr(timeout_function, "_self_wrapper")
    decorator_configuration = inspect.getclosurevars(wrapper).nonlocals
    assert decorator_configuration["dec_timeout"] == policy["timeout_seconds_per_run"]

    assert transforms.shape == (count, 7)
    assert captured["skipped"] == accepted_prefix["rejected_candidate_count"]
    uniform_draws = 3 * (count + captured["skipped"]) + 3 * count
    assert uniform_draws == accepted_prefix["random_draw_count"]
    radius = reference_initialize.get_max_radius(object_mesh) * np.cbrt(initial_scale)
    _assert_close(radius, case["expected_bounding_radius"], atol=2.0e-12)
    for expected in accepted_prefix["transforms"]:
        index = int(expected["index"])
        expected_transform = [
            expected["volume_scale"],
            *expected["rotation"],
            *expected["translation"],
        ]
        _assert_close(transforms[index], expected_transform, atol=2.0e-12)

    minimum_distance = 2.0 * radius
    assert minimum_distance > 0.25 * np.cbrt(container.volume)

    # Bound the otherwise unbounded Python loop at a deterministic seam. This
    # executes the live random-coordinate and acceptance functions and proves
    # that the same six-center prefix remains jammed throughout the probe.
    np.random.seed(int(case["seed"]))
    triangulated_container = reference_initialize.pyvista_to_trimesh(container)
    coordinates = []
    for _ in range(int(policy["bounded_probe_attempts"])):
        candidate = reference_initialize.random_coordinate_within_bounds(
            triangulated_container.bounds
        )
        if reference_initialize.coord_is_correct(
            candidate,
            triangulated_container,
            coordinates,
            minimum_distance,
        ):
            coordinates.append(candidate)
    assert len(coordinates) == policy["bounded_probe_expected_accepted_count"]
    expected_coordinates = [
        item["translation"] for item in accepted_prefix["transforms"]
    ]
    _assert_close(coordinates, expected_coordinates, atol=2.0e-12)


def _verify_no_growth_terminal(corpus: dict) -> None:
    case = corpus["no_growth_terminal"]
    object_mesh = _primitive(corpus, case["object"])
    container = _primitive(corpus, case["container"])
    config = SimConfig(
        init_f=float(case["initial_volume_scale"]),
        final_scale=float(case["final_volume_scale"]),
        n_scale_steps=1,
        itn_max=1,
        r=float(object_mesh.volume / container.volume),
        sampling_disabled=True,
        n_threads=1,
    )
    optimizer = Optimizer(object_mesh, container, config, seed=int(case["seed"]))
    optimizer.setup()

    expected = case["expected"]
    setup = expected["python_setup_transform"]
    expected_transform = [
        setup["volume_scale"],
        *setup["rotation"],
        *setup["translation"],
    ]
    assert optimizer.tf_arrays.shape == (1, 7)
    _assert_close(optimizer.tf_arrays[0], expected_transform)
    packing_fraction = object_mesh.volume * optimizer.tf_arrays[0, 0] / container.volume
    _assert_close(packing_fraction, expected["metrics"]["packing_fraction"])


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--fixture", type=Path, default=DEFAULT_FIXTURE)
    args = parser.parse_args()

    with args.fixture.open("r", encoding="utf-8") as input_file:
        corpus = json.load(input_file)
    assert corpus["schema_version"] == 1

    _verify_dense_initialization(corpus)
    _verify_initialization(corpus)
    _verify_transform(corpus)
    _verify_cat(corpus)
    _verify_local_constraint(corpus)
    _verify_no_growth_terminal(corpus)
    print(f"Python reference parity corpus passed: {args.fixture}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
