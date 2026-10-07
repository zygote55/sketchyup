# 0153 — Protect panel-local shortcuts from global overrides

A global custom shortcut must not shadow an existing model-panel action. The
native shortcut editor checks the panel's widget-scoped QAction sequences before
assignment, including explicit Reassign. Its explanation names the panel command;
Reassign never clears a panel binding. Viewport-only actions can share a key with
a disjoint model-panel scope, using the same scope rule as actual action routing.

The versioned binding policy accepts an optional host guard. On restore, a newly
reserved combination becomes inactive with a visible notice, while its original
serialized choice and opaque entries remain intact. The user can resolve it by
assigning a different key. Guards participate in assignment before any candidate
mutation; rejection is atomic. Existing conflicts between editable public commands
retain their prior explicit reassignment behavior.

The current reservation source is the model-panel action tree. This is not a
compositor shortcut registry or an editor for panel-local bindings. The existing
focus/completion and Meta reservations remain in force. A future panel action
introduced after initialization must be included before binding resolution.

The audit also found a pre-existing default collision between Model panel and
Create 3D text, both Ctrl+Shift+T. The global panel toggle retains that established
key; local text creation uses Ctrl+Alt+Shift+T, shown in its button tooltip. Fresh
preferences must retain every declared public default without an inactive notice.
