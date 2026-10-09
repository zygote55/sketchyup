# New-file publication fault matrix

R083.b, 2026-10-07. The shared publisher used by native migration, STL, DXF,
measured drawings and library bundles now has a dedicated syscall fault matrix.
Link-time interposition exists only in the test executable and its explicit child
processes; no production fault switch or machine-wide filesystem change is added.

The matrix injects full and partial ENOSPC writes, failed file fsync, failed link,
failed directory fsync, interrupted fsync with successful retry, and a competing
writer that creates the destination immediately before publication. It verifies
that prepublication failures expose no destination, handled errors clean staging
files, and a competing writer's bytes are never replaced. Postpublication fsync
failure retains the complete output and reports uncertain durability explicitly.
Unrelated original bytes remain intact throughout.

Two child processes are intentionally killed at file-sync and directory-sync
boundaries. Before publication no partial destination appears; after publication
the complete file survives process interruption. These checks model process death,
not sudden power loss or storage-controller guarantees. Existing files and symlink
targets are also protected. The test uses private temporary directories and bounded
synthetic bytes, with leak-detecting ASan/UBSan coverage.

The matrix supplements existing native-save, recovery-journal and durable-outcome
fault injection. It does not replace format-specific validation, migration source
checks, real package upgrade acceptance or the complete R083 release gate.
