# R084.k — Keyboard-only Outliner workflow

2026-10-07. Fixture `433acbc`; focus correction `c647d7b`.
Contract: [0154](../decisions/0154-keyboard-outliner-workflow.md).

The native task uses only actual keys after seeding a small hierarchy and assigning
initial viewport focus. F6 reaches Outliner; Home/Down select the intended group;
F2, typed text and Enter rename it; undo/redo restores the exact names. Local keys
lock/unlock and hide/show it. Enter opens its context; the same keyboard flow
renames a child without changing ownership, then Escape leaves the context. An
untouched neighboring geometry record remains identical.

The first matrix passed Wayland but failed X11 after Rename: the parent window
remained inactive. Organization dialogs now explicitly reactivate the parent and
return focus to the originating panel's current proxy. The fixture does not patch
focus itself after setup.

The final [matrix](R084k-platform-matrix.json) passes **8/8** across Wayland/X11
at 1×/2×: normal **14.545 s**, ASan/UBSan **22.112 s**, with leak detection and
halt-on-error. The broader organization interaction suite also passes Wayland 2×.
This branch includes the integrated M7 navigation, scene timing and naming fix;
it preserves existing public defaults after the separate text-key correction.

This establishes one representative keyboard task, not every Outliner size, tag
form, assistive-technology workflow or window-manager focus policy. Actual user
settings, documents and providers are untouched. Full R084/M9 acceptance remains open.
