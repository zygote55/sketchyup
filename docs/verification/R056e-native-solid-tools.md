# R056.e — Native Trim, Split and Outer Shell

Date: 2026-10-05 UTC. Depends on R056.d. Local acceptance passes;
CI/dependency merges remain pending. This completes the local R056 implementation,
not its merged delivery or the M6 milestone.

**Solid tools (Shift+B)** uses the existing preview/Apply/Cancel session for Union,
Subtract, Intersection, Trim, Split and Outer Shell. Draw → Solid operation
options chooses an operation and swaps target/tool. For Trim, the retention action
reads **Keep target (tool always retained)**; its HUD and empty-result messages
never claim that the cutter will be removed. Other operations use Keep originals.
Applying selects every generated part. Shared component editing uses the same
command scope as existing native tools.

The extended `boolean_input_tests` passes X11 and Wayland at DPR 1 and 2.
Wayland DPR 2 also passes ASan, UBSan and leak detection. Fixtures exercise:

- Real face selection, Shift+B, visible preview, Escape, orbit, Enter/click Apply,
  swap, retain/replace, stale and invalid operands from the existing Boolean suite.
- Native Trim, Split and Outer Shell preview without mutation, cancel, one Undo
  item, exact operand restoration, Redo and native-container save/reopen.
- Trim preserves the exact cutter. An empty retained Trim rejects with guidance;
  replacing the target yields only the retained cutter and an empty result selection.
- Split selects all three 4 m³ regions. The shared-component fixture replaces two
  operands with three regions in each instance, selects active-context scene IDs,
  persists exactly and restores both instances in one Undo.
- Outer Shell fills a 7 m³ cavity body and removes its covered island, leaving one
  8 m³ solid; native Info reports that volume and six exterior faces.

Adjacent X11 shell layouts (640/900/1200/1600 widths), numeric entry and tool
lifecycle suites pass. The prior command layer's full 74 enabled CTest suites and
installed-example checks remain recorded in [R056.d](R056d-solid-commands.md).
No live-provider acceptance is claimed in this native slice.

Visually reviewed screenshots:

- [Split preview](images/R056e-split-preview.png):
  `7af70f6532cd04a77e68ffc65b8b1b8580e31c7fc5a0b9b46f51c85cbfa16445`
- [All Split regions selected](images/R056e-split-applied.png):
  `68d36f36426b245b20d588838681fe85b9dcdf8669af1091d4f079853414f550`
- [Filled Outer Shell Info](images/R056e-outer-shell-info.png):
  `6967daeaf1bfa96c86d6809572c3b81fcc39b8ea8a082e077797a5c7ac1f2687`

The owned, isolated X11 recording at `build/evidence/r056e/native-solid-tools.mp4`
fully decodes with FFmpeg. SHA-256:
`06ab79c7e9101876d23b43454da1ae316e6298bfbace0042c6e64d8cefa045df`.
It contains only the isolated test application, never the user's desktop.
