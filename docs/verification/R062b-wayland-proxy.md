# Wayland destroyed-proxy leak isolated from SketchyUp

2026-10-06. Styles PR head `3662b9e` failed run `37474927891` during sanitized
`style_input_tests`, with four direct 96-byte allocations from
`wl_proxy_marshal_flags` → Qt Wayland surface creation. The corresponding library
build ID is `811f309c6b797aa9eaec17093b01cd525c6f4679` (Wayland 1.26.0).
A second run, `37474870646`, failed with two allocations (192 bytes) at the same
style/picker lifecycle. Both logs were inspected before changing the harness.
This investigation does not treat prior passing runs as a fix.

The retained [standalone reproducer](probes/wayland-destroyed-proxy.cpp) uses only
Wayland client/server libraries, a socket pair and one server thread. It uses no
Qt, SketchyUp or external display server. The server queues `wl_keyboard.enter`
with a surface argument. The client reads that event into a separate queue, destroys
the surface, and then dispatches the queued event. The destroyed argument is
correctly delivered as null, but a direct **96-byte proxy allocation leaks** even
after every owned proxy, queue, client and server display is destroyed.

The control changes only the ordering: dispatch the event before destroying the
surface. It exits successfully with **zero leaked allocations** under the same
ASan settings and the same installed library. [Results](R062b-wayland-proxy.json).

This matches the reference accounting in the upstream
[Wayland client source](https://cgit.freedesktop.org/wayland/wayland/tree/src/wayland-client.c?h=1.26.0):
`increase_closure_args_refcount` retains object arguments when the event is queued;
`validate_closure_objects` clears a destroyed argument; `destroy_queued_closure`
then cannot release the reference through that cleared pointer. The isolated
experiment establishes this leak independently of application dialog ownership.

Build and run the diagnostic outside the product build:

```sh
c++ -std=c++20 -g -fsanitize=address -fno-omit-frame-pointer \
  docs/verification/probes/wayland-destroyed-proxy.cpp -o /tmp/wayland-proxy-repro \
  $(pkg-config --cflags --libs wayland-client wayland-server) -pthread
ASAN_OPTIONS=detect_leaks=1 /tmp/wayland-proxy-repro
ASAN_OPTIONS=detect_leaks=1 /tmp/wayland-proxy-repro --dispatch-first
```

The first invocation intentionally exits 1 with the 96-byte report; the control
exits 0. The reproducer is diagnostic evidence, not an application dependency or
an expected-failure test in the normal suite.

The style input harness now waits for native exposure/focus before accepting each
color picker, and for focus to return before accepting its parent dialog. This
models actual keyboard/mouse use instead of closing a client-visible widget while
native focus events remain queued. Leak detection remains enabled without any
suppression or system-library modification. The changed harness passed six ASan/UBSan native checks in **32.633 seconds**:
Wayland at scales 1 and 2 (three scale-2 repetitions), and X11 at scales 1 and 2.
An initial additional Wayland scale-2 check also passed. All used leak detection,
with no suppressions. [Native results](R062b-focus-native.json). These focused
checks supplement the existing full product checks; remote CI remains a separate
gate, and this does not claim every possible downstream Wayland lifecycle is fixed.

## Remaining failure and private CI client fix

The focus change was insufficient. Scene head `97154583a9652ab55148a4d9593941c51b23df20`
failed run `37484748047` with one 96-byte proxy allocation at the cancellation of
the Styles dialog (`style_input_tests.cpp:162`). The failure log was inspected;
the run was not blindly retried. It exercises the same independently reproduced
destroyed-argument accounting defect.

The [local source patch](../../scripts/wayland-1.26-destroyed-proxy.patch) releases
the queued reference when `validate_closure_objects` clears its destroyed argument.
The receiver reference and other queued argument references retain their existing
lifetimes. The standalone failing case and dispatch-first control both pass with
zero leaks after this change. The changed client also passes the reproducer with
the client library itself instrumented by ASan/UBSan. All **26 upstream Wayland
tests pass**. [Results](R062b-wayland-client-fix.json).

The [CI build script](../../scripts/ci-wayland-client.sh) downloads the exact
Wayland 1.26.0 release, verifies its SHA-256, applies the patch without fuzz, runs
the upstream tests and both standalone probe orderings, then copies only the
client library and its original license into a private build directory. It
requires the pinned system version and fails if that prerequisite changes. This
is a local patch, not a claim of upstream acceptance.

Only sanitized native test processes opt into this client through
`SKETCHYUP_WAYLAND_CLIENT_LIBRARY`. Weston, the Wayland server, regular native
checks, product packaging and the user's desktop continue using system libraries.
Leak detection remains enabled; no leak suppression or system installation is
used. This keeps the application sanitizer gate useful without treating the
known system-library defect as an application allocation. The system Wayland
1.26 dialog lifecycle defect remains a documented external limitation until a
fixed distribution library is available.

Six native ASan/UBSan checks pass with the private client: Styles at scale 1 and
three scale-2 runs, assistant preview at scale 2, and current native MCP at scale 2.
[Native results](R062b-fixed-client-native.json). The initial MCP probes were rejected
by a stale catalog and then a relocated executable missing its adjacent CLI;
rebuilding and running from its actual build directory passed. Neither rejection
was a sanitizer failure. The complete private-client build script also passed
end-to-end, including the source checksum, 26 upstream tests and both leak probes.
