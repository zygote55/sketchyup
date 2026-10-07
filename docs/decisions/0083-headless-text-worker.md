# Bounded headless text geometry worker

R066.b, 2026-10-06. A QCoreApplication caller can request local font geometry without
opening a display or turning the CLI into a GUI application. The installed
`lib/sketchyup/sketchyup-text-worker` executable owns QGuiApplication and all font
objects. It forces Qt's offscreen platform, generic platform theme and compose
input module before application initialization, avoiding inherited desktop plugins
that try to connect to a display. It uses the same shaping library as R066.a.

The client discovers only the adjacent build helper or the installation-relative
helper. It does not search PATH. Explicit absolute executable paths and reduced
limits are available for fixture injection. Each invocation accepts one strict
version-1 JSON request on stdin and emits one response on stdout. Diagnostics are
separate. Requests are limited to 32 KiB, responses to 32 MiB, and the default
wall-clock deadline is 30 seconds. Timeout, malformed responses, failed startup,
crashes, unsupported protocol and nonzero success exits fail the operation. The
client kills an unfinished worker when rejecting its response. This is process
isolation with input/output/work bounds, not an operating-system sandbox.

The response contains shaped-font provenance, statistics and native geometry.
Transport reuses the validated native document codec, but accepts only generated
geometry with default metadata: no resources, hosted relationships, transforms,
appearance overrides or hidden/locked bodies. All geometry is decoded and checked
before returning to the caller. No caller document is modified on either success
or failure; later text commands must publish returned geometry atomically.

The worker reads local fonts. It neither embeds fonts nor accesses providers,
credentials or network services. This layer adds no persistent editable source
records or user commands. Reopening cached text without its original font and
explicit regeneration policy are handled by the subsequent persistence layer.
