# 0148 — Versioned shortcut bindings

Status: accepted implementation contract; native editor integration follows.

Store shortcut choices by stable public action identity and Qt PortableText in a
version-1 JSON envelope. An absent setting uses current defaults. Empty bindings
explicitly unbind an action. Labels and list order are never persistence keys.
Only single key combinations are supported initially. Super/Meta remains owned by
the compositor; focus traversal, Escape, completion and inference keys cannot be
reassigned. Plain keys must retain viewport-only routing in the native integration.

Loading is read-only. Explicit choices take priority over newly introduced default
bindings, with a notice for the disabled default. Conflicting saved choices are
retained but inactive, with a notice; resolving them requires explicit reassignment.
Reassignment records every displaced known action as unbound, so restart cannot
silently reintroduce the conflict. Rejected changes preserve the original model.

Unknown action entries and additional envelope fields survive edits and reset of
known actions. Malformed data, unsupported versions and oversized records reject
without rewriting the original setting. Limits: 128 KiB envelope, 1,024 bindings,
128-character identities and sequences. This is the first binding schema; existing
unversioned non-shortcut preferences are unrelated and must remain unchanged.

The native editor must stage changes until Save, report persistence failures,
update live action routing and displayed shortcuts, and reject stale settings.
This policy alone does not claim that UI integration or R084 acceptance is complete.
