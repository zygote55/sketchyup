# R085.h — Actual mixed-scale output transitions

2026-10-07. Two native Wayland attempts pass on the Intel Arc integrated GPU,
reparenting the viewport onto fullscreen test hosts on both already-awake displays:
2880×1800 eDP-1 at 2× and 3840×2160 DP-7 at 1.6×. Both preserve rendered sample
pixels, aligned picking, recreated contexts, eight-bit RGBA and zero GL errors.
The existing depth, clipping, transparency, cache, resize and hide/show checks also
pass. [Reports, resource limits and failure logs](R085h-physical-outputs.json).
No display settings, window rules or DPMS state are changed.

The fixture now records actual per-output renderer, context generation, framebuffer
size and effective widget scale, and refuses `--screens` with fewer than two outputs.
QScreen's reported scale for the external display is 2 while the widget's effective
scale is 1.6; both are retained. The viewport occupies the host layout's content
area, so its framebuffer dimensions are smaller than the full monitor resolution.
This check does not claim an exact 1920×1080 performance framebuffer.

Both X11/Xwayland attempts **fail the requested-output assertion** before output
qualification. The first used `QWindow::setScreen`; the follow-up also positions
the test host within the target screen's geometry before fullscreen. Qt documents
that [setScreen alone does not move a window within a virtual desktop](https://doc.qt.io/qt-6/qwindow.html#setScreen).
The second request still did not select the requested screen in this compositor.
This is a placement failure in the hardware fixture; no conclusion about rendered
pixels on that requested X11 output follows. Failures are retained and X11 physical
output transitions remain unqualified. Existing headless X11 checks are separate.

The first runtime/test source is `ed7fbec`, artifact
`9bd1bc3bd222ef4cea7b4003766f03a538ce46b841643f8daec8bb494e573b42`.
The second fixture includes the X11 positioning change `25fdc2e`, built from
`d7085e8` with the same four optimized viewport files. The latter tree also contains
file-workflow test instrumentation, which does not affect this executable. All
compiled overlay inputs are captured in the private build manifests. Both Release
builds pass the default viewport fixture at headless Wayland scales 1 and 2.

The laptop runs kernel 7.2.5-3-omarchy, Hyprland 0.56.2 at `efb50993780079460b0cbed1363e2166a2de1d9f`,
Mesa 26.2.2 and Qt 6.11.2. Actual desktop cases use eight distinct CPU cores,
4 GiB RAM, zero extra swap, a 120-second process deadline and private disk-backed
temporary/config/cache directories. Local work is serialized. The two output
contexts report the Intel renderer, not software fallback. Each attempt completes
its Wayland checks in about three seconds; X11 exits with the assertion failure.

This closes these recorded native Wayland transitions only. It does not simulate
hotplug, suspend/resume, input methods, other GPU vendors, X11 multi-output acceptance
or complete R085/release qualification. CI and ordered integration remain open.
