# R056.b — Cavity-aware Boolean solids

Date: 2026-10-05 UTC. Depends on R056.a; CI/dependency merges pending.
[Adapter contract](../decisions/0046-solid-booleans.md) and
[native shell classification](../decisions/0047-shell-containment.md).

Enclosed negative boundaries attach to their smallest enclosing positive material
shell. Every candidate pair is checked by native containment under a shared work
budget; each assembled material body and aggregate volume are revalidated.
Disconnected positive solids and islands inside cavities receive separate output
bodies. Cavity boundaries retain native face IDs and source provenance after
remapping, without welding independent shell vertices. One body with cavities is
now a valid measured material solid; one body containing disconnected material
components still reports no single-solid volume.

Kernel fixtures cover:

- An 8 m³ cube minus an enclosed 1 m³ cutter becomes one 7 m³ material body with
  two native boundaries. Reusing it in union/intersection/subtraction with the
  original cutter yields 8/0/7 m³ respectively.
- Two enclosed voids attach to one body (6.992 m³). Repeated reconstruction has
  deterministic geometry and provenance.
- Subtracting a hollow tool yields a 4.625 m³ cavity body and a separate 0.125 m³
  solid island. Operations between two hollow operands have independently known
  7.875/2.375/4.625 m³ union/intersection/subtraction volumes.
- Reversed hollow inputs, eight large-origin oblique placements, a mirrored
  nonuniform placement, centimeter geometry, and an independent corner-touching
  positive body retain expected volumes and native topology.
- Every cavity tool face retains reversed source-face provenance; existing
  invalid, contact, near-coplanar, size and precision cases continue to pass.

The shared command fixture checks preview/commit identity, operand consumption,
six reversed cutter faces with distinct front/back materials, a 7 m³ Entity Info
measurement, native save/reopen and Undo/Redo. An independent inverse-transpose
normal oracle checks physical material sides when the target, tool, or both are
reflected. That fixture exposed an operand-frame parity bug: baked reflected
placements now reverse loop order both into world space and back into the target
frame, matching rendering/consolidation and preserving physical front/back.
The targeted fixture failed before this fix and passes afterward, including
ASan/UBSan/leak detection.

Native Boolean interaction passes on isolated X11 and Wayland at DPR 1 and 2.
The cavity fixture previews without publication, consumes both originals in one
Undo item, selects the result and verifies the Info panel reads exactly 7 m³.
Persistence and native Undo/Redo pass. The final native fixture also passes
ASan/UBSan/leak checks on Wayland at DPR 2. Kernel, shell, solid and command
sanitizers pass; the added multiple-cavity kernel case and reflected command
cases were rechecked after their addition.

The final full development build succeeds; all 72 enabled CTest suites pass in
40.91 s (73 entries, opt-in real Blender skipped).

A temporary-prefix install verifies the installed CLI and
`examples/enclosed-cavity.json`: it saves/reopens one twelve-face body,
reports 7 m³ and preserves all six reversed cutting-face mappings. CI includes
that example. Published transaction/session/MCP schemas match the live registry;
no public command schema changed. No live-provider quality trial is claimed.

[Native Info capture](images/R056b-cavity-info.png) was visually inspected:
SHA-256 `3417d8d78f2724f7b3808d5447ba1ab221c20bdb7f103e2d82f9296bb1d44c56`.
The final isolated X11 recording fully decodes:
`build/evidence/r056b/native-cavity.mp4`, SHA-256
`eeeaceec8dcf303a9ca28c041ab9811e3c25d5c9b4f4a71a7b860cc2ec800ac1`.
The enclosed void is not visible through its opaque outer faces; the native
volume, face/edge counts, topology tests and material-side checks establish it.

Trim, split and outer-shell semantics and controls remain subsequent R056 work.
