# Python/C++ parity corpus

`parity_reference_v1.json` is a compact, deterministic cross-language corpus for
Milestone 6. It covers:

- successful seeded initialization, rejected-candidate work, and persisted transforms;
- the supplied full-scale cylinder/box proxy: its six-center greedy prefix, bounded
  seven/ten-object C++ exhaustion, and live Python timeout-loop selection and
  retry-count contract;
- a malformed-container initialization failure and its C++ error-category mapping;
- volume-scale, rotation-order, translation, CAT split polygons and per-source plane
  points/normals, objective/gradient, and local constraint/reference Jacobian behavior;
- a known-feasible no-growth terminal scene, its C++ outcome, packing metrics, and artifact set.

The no-growth case compares the final C++ transform with the feasible state produced
by `Optimizer.setup()`. The Python implementation has no canonical final outcome or
artifact contract, so those assertions intentionally apply to the C++ Milestone 5
contract rather than claiming byte-for-byte Python artifact parity.

Create a Python 3.9/3.10-compatible reference-oracle environment using the
checked-in direct requirement pins, then install this checkout without resolving
the application's broader dependency set:

```powershell
python -m venv build/python-parity
build/python-parity/Scripts/python.exe -m pip install --upgrade pip
build/python-parity/Scripts/python.exe -m pip install -r tests/parity/requirements.txt
build/python-parity/Scripts/python.exe -m pip install -e . --no-deps
build/python-parity/Scripts/python.exe tests/parity/verify_python_reference.py
```

The dedicated requirements file pins PyVista 0.38.4 with VTK 9.2.6. Newer VTK
releases remove symbols that this reference PyVista version imports, so using an
unconstrained current VTK does not provide a valid reference environment.
The file pins the oracle's direct requirements, not every transitive package, so it
is intentionally not a complete lock file.

After `parity_corpus_tests.cpp` is part of `irop_tests`, verify the C++ side with the
documented build/test preset, or run the focused executable filter:

```powershell
build/windows-vs2026/tests/cpp/Debug/irop_tests.exe "[parity]"
```

Do not update expected values from C++ output alone. A fixture change must first pass
the Python verifier, then the C++ parity tests. Numerical comparisons use explicit
tolerances; serialized STL bytes are not a parity contract.
