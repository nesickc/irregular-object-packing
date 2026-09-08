# Pryanik placement and growth investigation

Date: 2026-09-08. Investigation only; production code and policies are unchanged.

## Immediate failure

The maintainer identified run 20 and supplied its exact message:
`initial placement exhausted 1000000 candidate attempts after placing 27 of 50 objects`.
This is the random initializer's candidate-attempt limit, before TetGen/CAT/Ipopt.
The run number was reserved, but pre-engine initialization failure publishes no
summary under the existing contract. The omitted structured-search suffix also
identifies the fallback-disabled path for this message.

An existing-binary reproduction with pryanik_simplified.stl, 10_kg_np.stl,
count 50, seed 1918 and initial/target scales 1 reproduces the same message in
0.216664 seconds. Enabling structured fallback succeeds in 0.576817 seconds,
passes physical and serialized-output validation, exports and loads. It performs
zero local solves. These are single diagnostic observations, not a timing study.

The random initializer enforces disjoint bounding spheres: the source radius is
46.550839, requiring centers over 93.101678 apart. It never rearranges its accepted
prefix. This is particularly conservative for a roughly 78.02 by 62.04 by 28.30
object. After 27 accepted centers, repeated random proposals fail that search;
the attempt count is not evidence of actual shape infeasibility.

## What fits

Saved run `artifacts/run-000001/run-000012` contains 250 full-size objects, structured
grid initialization, both physical gates and 34.5546% volume fraction. Its source
triangle count, volume and byte size match the current input metadata. At 300 the
object-volume fraction would be 41.4655%; volume alone does not rule out a fit.

The six current uniform-grid orientations have capacities 240, 216, 224, 250,
240 and 210. The 250 case is a 5 by 5 by 10 grid with 90-degree Z rotation.
A current direct300 attempt fails after random exhaustion and the structured
capacity check in 0.216481 seconds. A separate arithmetic study of 1,000 further
deterministic PCA/perturbed/random orientations finds no 300 uniform rectangular
grid. It is a bounded negative search, not a general packing bound, and did not
produce a new physically validated artifact. Staggered rows, mixed orientations
and shape-aware local rearrangement remain untested alternatives.

## Growth is a separate weakness

Saved run 16 (30 objects, .1 to 1, adaptive sampling on) fails immediately because
surface resampling produces an invalid closed mesh. Saved run 17 disables adaptive
sampling and reaches the 300-second engine deadline after completing one of nine
barriers; its last committed scale is .2.

For run 17, local solves consume 298.588 of 301.266 engine seconds. Collision
correction takes 0.136 seconds, TetGen 1.809 and CAT 0.694. The dependency solve
accounts for 298.257 seconds; callback evaluation totals about 2.347 seconds
inside that interval. There are 87 actual local solves and 2,505,390 prepared
constraint rows across them. None of the immediate retry caches is used.
Consequently collision acceleration or faster callback loops alone cannot resolve
this particular growth bottleneck. Other supplied inputs have a different profile.

Pryanik has 3,796 source triangles; the registered tranche-3 primary object has 44.
That tranche's known-fit 100/300 scope remains valid, but is insufficient evidence
of practical growth for this mesh. The present method rebuilds global geometry,
solves local CAT-constrained problems, freezes already-complete objects and has no
general coordinated rearrangement or search restart. A single failed local solve
also aborts the run before physical-trial recovery. These are fixable design and
robustness limitations, not a demonstrated fundamental performance asymptote.

The source paper combines continuous optimization with combinatorial swapping,
replacement and insertion. The current C++ scope omits that discrete stage; its
performance cannot be treated as a full implementation of the published hybrid
method. See [Ma et al., 2018](https://doi.org/10.1111/cgf.13490) and
[PROJECT](PROJECT.md#initial-product-scope).

## Recommended next scope

Use actual pryanik250/300 and the supplied ulamok cases as the next practical
acceptance corpus before extending to 1,000. Prioritize:

1. Make constructive full-size placement an explicit first strategy and preserve
   useful pre-engine failure records. Establish 250 as a positive regression.
2. Compare bounded staggered/mixed-orientation placement and local rearrangement
   for 300 against the current six-grid ceiling. Retain failed searches and require
   full-resolution and serialized validation for any successful candidate.
3. Repair adaptive sampling robustness and measure conservative simplified geometry
   or reduced/reformulated local constraints with independent full-resolution checks.
   Use exact failure replay for numerical recovery when an Ipopt failure is present.
4. Select collision acceleration or a replacement growth strategy from measured
   successful end-to-end outcomes on those actual meshes. More iteration budget or
   parallel workers alone is not an established remedy.

This is a recommendation, not authorization or implementation of a new architecture.
Evidence paths and hashes are retained in `build/pryanik-20260908-investigation.json`;
the grid-study script/results and all three new benchmark reports remain in `build/`.
