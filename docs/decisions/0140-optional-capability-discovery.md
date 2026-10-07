# Optional capability discovery without execution

R081.c, 2026-10-07. Standalone `--optional-capabilities` returns version-1 JSON
for the headless process. It distinguishes compiled support, executable presence,
unverified readiness and unavailable editor state. It checks Blender on PATH and
application-owned text/extension workers beside the executable or in the installed
lib directory. It does not execute helpers, inspect fonts, load packages, read
provider credentials/settings or contact the network. Desktop override paths are
not consulted, and paths/user configuration are not disclosed in the response.

Presence is not proof of compatibility or successful work. Provider readiness is
explicitly not probed. Headless selection and capture remain unavailable; native
editor binding is the documented alternative. Each missing optional dependency has
an actionable manual, geometry-query, direct-command or GLB alternative. No fallback
is silently substituted for the user's requested result. Core capabilities point
to this separate runtime discovery mode rather than embedding host-dependent data
in generated schema artifacts.

Acceptance relocates the real CLI, controls PATH, verifies missing, present and
nonexecutable files, and installs marker-writing fake helpers that must never run.
It checks both adjacent and installed layouts, absence of returned paths and
rejection of mixed discovery/mutation modes without producing files. Existing
worker tests retain actual protocol, compatibility, timeout and failure validation.
