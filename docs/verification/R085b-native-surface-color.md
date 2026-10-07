# R085.b — Eight-bit native compositor surfaces

2026-10-07. Product change `cb28929a4cebee1773c21d3137d939f2c9c29a13`;
integration source `c09035b` includes the diagnostics readiness fixture below.
This fixes the observed fractional-client failure without accepting R085/M9.

## Observed failure and correction

R085.a retains three failed Wayland 1.75 cases and their original compositor
crash evidence. A protocol diagnostic on the Pixman path showed EGL choosing
RGB565 buffers, including odd widths with strides not divisible by four.
Changing only the QOpenGLWidget format did not fix the top-level surface.
The application and 74 native fixture/benchmark entry points now request eight
bits per RGB/alpha channel before QApplication through one shared initializer.
Caller-selected GL version/profile, depth and samples are preserved.

The [buffer evidence](R085b-surface-buffer-evidence.json) records the earlier
low-depth dimensions/strides and aligned 32-bit buffers after the global change.
The viewport regression also checks negotiated RGB depth. See
[ADR 0157](../decisions/0157-native-surface-color.md) for the Qt initialization
contract. This avoids the observed buffer selection; it does not patch Weston
or establish that every compositor/driver defect is resolved.

## Native validation

| Matrix | Result | Total elapsed |
| --- | --- | ---: |
| [Five targets, Wayland/X11, client factors 1.5/1.75](R085b-fractional-matrix.json) | 20/20 | 70.478 s |
| [Viewport, text size and shortcuts under ASan/UBSan](R085b-sanitizer-matrix.json) | 12/12 | 71.751 s |

Targets cover drawing/picking/context recreation, keyboard measured construction,
responsive text sizing, Outliner keyboard work and shortcut setup. Sanitizers
use leak detection and halt-on-error; the documented [private Wayland client reference fix](R062b-wayland-proxy.md)
is retained without an application suppression. The
[actual application smoke](R085b-application-smoke.json) also passes at Wayland
client factor 1.75 with renderer/text ready and `glError: 0`.

Native sessions use the same isolated Weston 15.0.1, Qt 6.11.2, Mesa
26.2.3-arch1.2 and LLVM 23.1.1 llvmpipe environment as R085.a. Wayland fractional
cases use the GL renderer at output scale 1, with `QT_SCALE_FACTOR` applied to
the client after the wrapper. They do not substitute for physical fractional
output negotiation, output moves or hardware testing. CI adds the same twenty
normal cases while retaining its existing triggers and gates.

## Diagnostic report readiness

CI run `37607667846` on PR #188 failed a separate diagnostic fixture at Wayland
2×: it waited 250 ms after clearing an assistant preview, while report controls
refresh on a 200 ms timer. The test now waits up to five seconds for the repair
button to become enabled. The active-preview fence, exact geometry checks,
explicit Apply, Undo, staleness rejection and native-focus checks remain.
A failure identifies the disabled control by name.

The corrected fixture passes [four normal cases](R085c-normal-matrix.json)
(61.447 s) and [four sanitizer cases](R085c-sanitizer-matrix.json) (85.043 s),
on isolated Wayland and X11 at 1×/2×. Wayland uses compositor output scale;
X11 uses the Qt client factor. The fixture-only backport is commit `6fd6944`
on PR #188; that PR still requires its exact-head CI. No product availability
rule was relaxed.

## Integration and distribution inputs

The [complete regression run](R085b-full-ctest.txt) passes **181/181 in
569.81 s**, without skips. All eleven real-consumer checks ran, covering Blender,
measured PDF, DXF, STL, OBJ and glTF. CTest ran serially with private XDG paths,
`QT_QPA_PLATFORM=offscreen`, actual Blender/PDF executables and ezdxf 1.4.4.
Other build/provider work ran concurrently, so this is correctness evidence,
not a performance measurement.

The [installed application and three helpers](R085b-installed-smoke.json)
match the build byte for byte; all 138 installed decisions and 11 API documents
match their sources. The [source archive](R085b-source-package.json) contains
216 byte-exact install inputs, SHA-256
`d9839b3c6ff817b8b93d575b629c8ae4d8bc4600b42ce6b9d73a5b63d7f4af11`.

The [build-attempt record](R085b-build-attempts.json) retains an initial linker
SIGKILL and the successful serial continuation. Available logs/cgroup counters
do not establish an OOM cause. Native matrix/application logs remain private
with [content hashes](R085b-private-log-hashes.json); the failed widget-only
attempt and historical R085.a failures are retained.

Physical mixed-DPI moves, suspend/resume, input methods, supported hardware,
preceding milestone gates, remote CI, ordered merges and final release
acceptance remain open. Both sleeping physical displays were left unchanged.
