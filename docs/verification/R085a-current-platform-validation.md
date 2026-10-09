# R085.a — Current integration and fractional client scaling

2026-10-07. Integration source `893fe3b1c44d5a745844c474f059d0c3be671359`,
including the texture-publication correction carried by
[PR #220](https://github.com/zygote55/sketchyup/pull/220). This is overlapping
validation preparation; **R085 and M9 are not accepted**.

## Integration

The [complete CTest run](R085a-full-ctest.txt) passes **179/179 in 199.63 s**,
without skips. All eleven real-consumer checks ran, including Blender, measured
PDF, DXF, STL, OBJ and glTF. The installed application and three helpers match
the build; [137 decisions and 11 API documents](R085a-installed-smoke.json) match
their sources. The [source archive](R085a-source-package.json) includes
215 byte-exact installed inputs, SHA-256
`edc6de4d4c0d16671580b141c9460a097aeb2165d27029a223d148687404a9a4`.

Build: `cmake --build build/dev --parallel 2`. CTest ran serially with
`QT_QPA_PLATFORM=offscreen`, the real Blender/PDF executables and the retained
ezdxf 1.4.4 Python environment. Native checks below ran serially in isolated
display sessions, independently of the real desktop and provider credentials.

## Fractional client-scale matrix

The [matrix](R085a-fractional-matrix.json) retains **21 attempts for 20 cases**:
17 cases pass and three fail. The viewport case failed twice at Wayland 1.75;
neither attempt is removed. Targets cover viewport drawing/picking/context
recreation, keyboard measured construction, text-size/responsive layout,
keyboard Outliner editing and all-keyboard shortcut setup.

| Backend / Qt client factor | Passed | Failed |
| --- | ---: | --- |
| Wayland / 1.5 | 5/5 | None |
| Wayland / 1.75 | 2/5 | Viewport, text size, shortcuts |
| X11 / 1.5 | 5/5 | None |
| X11 / 1.75 | 5/5 | None |

Wayland uses `scripts/test-wayland.sh env QT_SCALE_FACTOR=FACTOR TARGET`,
`SKETCHYUP_TEST_SCALE=1`, generic Qt platform theme, Fusion style, compose input
and disabled client decorations. X11 uses `xvfb-run -a` with `QT_QPA_PLATFORM=xcb`,
the same client factor and software GL. Successful viewport reports verify the
actual device-pixel ratio. These factors emulate fractional client scaling;
they do **not** exercise physical fractional-output negotiation or output moves.

Container versions: Weston 15.0.1, Qt 6.11.2, Mesa 26.2.3-arch1.2 and LLVM 23.1.1
llvmpipe. Weston uses its headless backend and GL renderer at output scale 1.

## Open compositor failure

Weston receives SIGSEGV before the failed clients return results. Both the
systemd crash record and an independent debugger reproduction show frames in
libc, Mesa Gallium and Weston's GL renderer. The [retained stack](R085a-compositor-stack.txt)
has unresolved internal function names; it establishes the crashing process
and libraries, not the exact defect or which component produced invalid data.
The timestamped kernel record has no corresponding OOM kill. No core was
extracted and no user model was involved.

Changing only the isolated compositor renderer to Pixman lets all three client
tests finish ([diagnostic results](R085a-pixman-diagnostic.json)), but Pixman
prints `create_bits_image_internal` buffer row-stride alignment warnings.
These are **diagnostic results, not replacement acceptance passes**. The GL
matrix remains failed. No application workaround, system configuration change,
CI skip or compositor-default change was made. Original logs remain under
gitignored `build/`, with [content hashes](R085a-private-log-hashes.json).

## Hardware and remaining work

The [host inventory](R085a-host-environment.json) records installed versions and
connected output dimensions/scales only. Both physical outputs were DPMS asleep;
they were left unchanged. This run makes no claim for physical mixed-DPI moves,
suspend/resume, international input methods, a discrete GPU, or reference-machine
performance. Those checks, the compositor failure, preceding milestone gates,
remote CI and ordered PR merges remain open.
