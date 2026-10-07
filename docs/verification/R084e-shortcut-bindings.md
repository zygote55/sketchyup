# R084.e — Shortcut preference policy

2026-10-07. Product `b515980`, Qt iterator correction `0104607`.
Contract: [0148](../decisions/0148-versioned-shortcut-bindings.md).

Normal tests pass **1/1 in 0.02 s**; ASan/UBSan with leak detection and
halt-on-error passes **1/1 in 0.05 s**. The suite checks persisted reassignment and
explicit unbinding across reconstruction, atomic conflict rejection, user choices
winning over new defaults, inactive duplicate saved choices, explicit resolution
of all conflicting owners, unknown setting preservation on edits/reset, reserved
keys, malformed/unsupported versions and the envelope byte limit.

No real user preference or provider credential is read or changed. This pure
binding model is not yet connected to native actions or a shortcut editor. Native
routing, dialogs and persistence checks follow; R084/M9 remain open.
