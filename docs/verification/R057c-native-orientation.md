# R057.c — Native face orientation and direction preview

Date: 2026-10-05 UTC. Depends on R057.b. Native acceptance passes locally;
CI/dependency merges remain pending. Edge appearance and exported front/back
semantics remain later R057 work.

**Face orientation (Shift+O)** offers Reverse selected faces or Orient connected
faces to one selected reference. Preview arrows show the resulting physical
front direction on each changed face in the active editing context. The reference
keeps its direction. Enter/click applies, Escape cancels, Alt-drag orbits, and
measurements do not capture numeric input. Selection and physical materials stay
on their original sides; shared component edits update all instances in one Undo.

`orientation_input_tests` passes X11 and Wayland at DPR 1 and 2. Wayland DPR 2 also
passes ASan, UBSan and leak detection. Fixtures check:

- Actual mouse selection and Shift+O preview; native framebuffer contains visible
  amber preview geometry. Direction counts identify the changed faces.
- Front/back pixel colors before and after reversal, viewed from opposite sides,
  with and without a mirrored/nonuniform placement. Red and blue stay on the same
  physical sides. Selection retention is checked separately from material samples
  so selection stipple cannot replace the color probe at a different DPI.
- Immutable preview, orbit, cancellation, Enter/click Apply, one Undo item, exact
  original body restoration, Redo and native-container persistence.
- Connected orientation repairs one reversed cube face from an unchanged
  reference. A consistent component reports no change; multiple references reject.
- External document edits invalidate the preview before Apply.
- Shared component reversal changes both instances, draws direction arrows only
  for the active instance, retains selected scene-face identities, persists and
  restores both instances in one Undo.

Adjacent native solid tools, shell layouts (640/900/1200/1600 logical widths),
numeric entry and tool lifecycle suites pass on X11. CI includes all four display
runs of the new native suite. No live-provider acceptance is claimed.

Visually reviewed native framebuffer captures (whole-window Qt grabs omit the
OpenGL painter overlay, so these use `grabFramebuffer`):

- [Reverse preview](images/R057c-reverse-preview.png), downward new-front arrow:
  `5eecfb9a91a4d6ad11a2bd05863854f67e6d4fedcc845e8b740ce347c91debf5`
- [Connected orientation preview](images/R057c-orient-preview.png), repaired top-front arrow:
  `79a6afaa28e752aced8f0170335b851811eb7b6be832b7911d7bfdcc24ccdb44`

The owned, isolated native recording `build/evidence/r057c/native-orientation.mp4`
fully decodes with FFmpeg. SHA-256:
`69960e2769fdb9ee17be7539b2b7cae24afa27e33afa7b516e59f8419e921f34`.
Only the isolated test application was recorded.
