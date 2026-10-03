# Persistence migration fixtures

`container-scene-v2.sketchyup` is the actual R014 package acceptance wall-ring
save, written by the R012 schema-v2/container-v2 implementation. Its document ID
is `bb542e3223382cc3380b8bb8c1c9bb6a` and revision is 1. Raw v2 contains the same
payload; raw v1 removes revision and later transform/property fields, matching the
original experimental schema. All geometry is the project's own room recipe.

R015 must read these without source mutation, allocate deterministic initial edge
IDs, and preserve those edge records across the current schema roundtrip.
