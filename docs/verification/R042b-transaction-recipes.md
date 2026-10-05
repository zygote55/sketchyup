# R042.b deterministic transaction recipes

Date: 2026-10-05 UTC. Local verification passed; CI acceptance pending. Requires
[PR #64](https://github.com/zygote55/sketchyup/pull/64).
[Recipe contract](../decisions/0027-transaction-recipes.md).

The shipped recipe runs twice in fresh headless CLI processes with display
variables removed. Each run discovers its new document identity, creates a 2 × 3 m
face privately, measures the provisional ID at 6 m², seals and commits once, saves
and measures the live result at 6 m². A separate CLI process reopens each native
file and verifies the same area. Saved models contain one body at revision one.

Invalid envelopes/versions, duplicate IDs, forward references, bad pointer escapes,
input bytes and step-count limits fail before execution. A malformed recipe passed
to `--new` leaves no model file. A later stale-revision step stops the recipe while
preserving its earlier committed and saved face. A missing referenced response
field after sealing stops the recipe and durably aborts the uncommitted request;
reopening confirms the original revision remains unchanged.

Typed references include array indexing and provisional IDs. Literal escapes
suppress template expansion. Repeating a bounded registry fragment 1,000 times
fails during request construction instead of materializing an oversized object.
Separate lowered-budget fixtures verify retained-response and output reservations
reject the next step before accepting effects. Error replies remain structured.

All 54 development CTest suites and five targeted ASan/UBSan suites pass: recipes,
headless sessions, CLI inspection, CLI history and CLI recovery. The unchanged
transaction/core matrices passed in preceding slices. Builds report no compiler
warnings. No desktop or provider is required for the new acceptance workflow.

A temporary install reports recipe discovery exactly equal to its installed JSON
artifact. The installed CLI executes the installed example without display
variables and reports the expected 6 m² measurement. The source archive includes
the recipe, schema, contract and tests. Disposable package CI repeats installed
schema equality, the actual recipe and artifact removal after uninstall. Parent
R042 remains pending until its PR stack passes CI and merges.
