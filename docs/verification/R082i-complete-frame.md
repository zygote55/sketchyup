# R082.i — Complete viewport timing and portable texture fixtures

2026-10-07. The manual benchmark now times the entire `Viewport::paintGL`, including
QPainter overlays, followed by `glFinish`. The old scene timing retains its own
intermediate `glFinish`. Neither measurement includes child widgets or compositor
presentation; framebuffer readback is included only in the separate edit metric.
Temporary fixture JSON/canonical buffers are released before viewport construction,
while construction peak RSS remains reported. Application rendering is unchanged.

There are **20 qualified scene runs per machine**, plus one repeated-scene control
after the texture correction: one 25-placement smoke and three 1,000-placement /
100,000-triangle samples for each of five scenarios. Each run retains ten warmups,
fifty frames/picks, one hundred edits/undo/redo, bounded inference checks, unchanged
stationary geometry uploads and zero GL errors. All five full-scene canonical hashes
agree with the earlier AMD evidence and now agree across both machines.
[Reports, exact source/artifact mapping, resource limits and environment](R082i-complete-frame.json).

| Machine | Scenario | Complete viewport p95 (ms) | Scene p95 (ms) | Edit/readback p95 (ms) |
| --- | --- | ---: | ---: | ---: |
| AMD | repeated | 2.971–3.470 | 2.361–2.672 | 18.540–19.119 |
| AMD | unique | 2.621–2.717 | 2.218–2.306 | 16.441–17.188 |
| AMD | deep | 6.093–7.187 | 2.396–2.801 | 56.463–57.471 |
| AMD | far | 2.870–3.037 | 2.172–2.289 | 17.846–31.725 |
| AMD | textures | 2.920–3.051 | 2.320–2.462 | 17.570–22.072 |
| Intel | repeated | 15.445–17.690 | 11.825–14.376 | 67.191–69.933 |
| Intel | unique | 13.512–15.403 | 11.445–12.937 | 51.839–57.468 |
| Intel | deep | 25.637–27.092 | 12.404–13.100 | 169.965–174.460 |
| Intel | far | 15.206–19.074 | 11.605–13.517 | 75.622–76.202 |
| Intel | textures | 16.338–18.632 | 13.353–15.296 | 72.764–73.359 |

These are observed ranges across three runs, not confidence intervals or release
acceptance. Intel still exceeds the 16.7 ms frame target in some scenes, especially
the deeply nested fixture. No budget or supported model limit was relaxed.

The AMD machine is the previously documented CachyOS QEMU guest with Navi21 graphics
(Mesa reports RX 6800 XT; the PCI board identification differs). It uses X11 and one
pinned guest CPU. Intel is a Core Ultra 7 155H / Arc integrated GPU, native Wayland
under Hyprland, with an eight-CPU quota and affinity to eight distinct cores. Both
use 4 GiB RAM, zero swap, fresh processes, exact 1920×1080 framebuffers and already
awake displays. Intel uses only the benchmark's own fullscreen window to prevent
tiling from clipping that framebuffer; desktop configuration is unchanged. The
resource profiles differ, so the table is not a controlled GPU-only comparison.
Guest load is captured; hypervisor activity, host scheduling and presentation latency
are not controlled. The extra inference index contributes to RSS; body cache counters
still exclude maps, textures, driver and allocator overhead.

## Texture fixture qualification

Generating the same 4096×4096 checker pixels through Qt produced different compressed
PNG bytes on the two machines. A first standalone capture also used 3937 pixels/metre
rather than the GUI's 3780 pixels/metre. Byte comparison found only density metadata
and its CRC differed between the core-only and GUI captures on AMD; RGBA hashes were
identical. The final resources embed the GUI-generated AMD bytes with fixed SHA-256
checks. [Asset provenance and pixel hashes](../../tests/fixtures/performance/README.md).
The original full texture-container hash `370b777e583aa817345ff5bae0e1e0f6c1b33299066f833178b5fcf8b413d84c`
is preserved, with no reduction in dimensions or decoded memory.

Sixteen non-texture cases per machine retain source `7e493ae`; texture cases and the
additional repeated control use `ca9ea93`. The latter changes only resource bytes,
their expected hashes and asset documentation. The detailed report identifies both
artifacts explicitly. Rejected mismatching texture reports are not counted as passes.
Earlier clipped-window and one-CPU Intel attempts remain private diagnostic evidence;
they are not substituted for the qualified eight-CPU results. An interrupted initial
eight-CPU attempt was stopped after discovering the encoding discrepancy.

Compilation overlays the listed files on the completed Release cache. Other runtime
inputs match `29fb31b`; the inference fix is `88f0a75`. The helper restores original
sources on exit and leaves package archives untouched. Each local build uses at most
eight CPUs, two compiler jobs, serial LTO per link, 4 GiB RAM and disk-backed temporary
storage. Local builds and measurements are serialized under a shared lock.

Complete paint timing is useful profiling evidence. It does not close end-to-end
input feedback, full memory accounting, long sessions, cold native UI persistence,
the unsupported 10,000-instance requirement or final reference/release acceptance.
