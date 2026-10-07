# 0149 — Native shortcut editing

Edit → Keyboard shortcuts exposes public actions by label, retaining stable IDs
for storage. Search filters the list; Assign stages a binding, Reassign explicitly
unbinds conflicting owners, Clear unbinds the selected command and Reset defaults
restores known actions while preserving unknown settings. Save applies the draft;
Cancel leaves live actions and saved bytes unchanged. An edited but unassigned key
cannot silently disappear on Save.

Use the versioned policy in [0148](0148-versioned-shortcut-bindings.md). Settings
live in the single `shortcuts/v1` value alongside existing preferences. A private
advisory lock coordinates our editors; compare the original value before writing
to reject stale dialogs. Qt's atomic settings synchronization must succeed before
live bindings change. Unsupported or malformed records are reported without a
replacement write. Ordinary non-shortcut preferences remain untouched.

Plain and Shift-only text keys are restricted to the viewport so typing in fields
continues normally. Existing viewport-scoped commands remain local even with a
modifier binding. Menus and the palette use live QAction shortcuts; the command
button and tooltips update. A floating assistant mirrors only window-scoped toggle
bindings, preventing a plain-letter shortcut from consuming composer text. Explicit
assistant toggling activates the destination window before routing keyboard focus.

Preferences never edit model geometry/history or submit an assistant prompt. Native
checks use isolated settings and a noncontacted local-model fixture for composer
availability. This feature does not complete the independent text-scaling,
all-dialog, Outliner or assistive-technology portions of R084.
