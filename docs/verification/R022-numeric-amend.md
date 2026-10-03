# R022: numeric entry and guarded operation revision

Date: 2026-10-03. Local checks passed; PR merge pending.

The Measurements parser supports implicit document-unit values, explicit m/mm/cm/
ft/in, compound feet/inches, signed and mixed fractions, locale decimal separators,
absolute `[x,y,z]` and relative `<x,y,z>` coordinates. Comma-decimal locales separate
values with semicolons; digit grouping is rejected rather than guessed. Angles have
a shared deg/rad parser. Segment/copy/divide syntax is recognized for later tools;
current drawing tools explain that those count operations are unavailable.

Digits and coordinate prefixes transfer keyboard ownership from an active drawing
tool to Measurements. Coordinates can establish the first point without a pointer,
then dimensions or a second coordinate complete the shape. Line re-entry retains
the previous direction. Live previews show measured values without taking focus.
Invalid values remain selected, receive a visible border and accessible error
text, and do not change the document. Escape returns focus to the viewport.
Drawing remains on Z=0 until R023; the default document unit remains meters.

An opaque amendment stamp binds the document session, exact content state and
revision of the last committed operation. A replacement is evaluated on a private
copy rewound to the operation's prior state, with allocator floors preserved. It
must publish one atomic operation, preserve the context-creation count and leave
other editing contexts untouched. Successful publication increments the visible
revision once and replaces the last undo item. Rejection leaves document/history
unchanged. Newly created contexts and regenerated topology receive fresh IDs;
retired IDs are never reused.

Camera movement and saving do not invalidate the token. Intervening manual or
command edits, undo/redo and document replacement do. Prior save completion cannot
mark revised content clean. Tokens are in-process/session-local, not durable remote
transaction credentials. `executeAmend` and `previewAmend` provide the shared
command path, with prospective geometry and current-to-replacement topology maps.

Validation:

- All 15 development CTest suites and ten ASan/UBSan core suites pass.
- Parser fixtures include 12'6", 3/4", negative mixed fractions, explicit/implicit
  units, angles, comma decimals, coordinates and invalid/oversized input.
- Core/command fixtures verify exact rollback, one-step undo/redo, fresh allocator
  floors, save-state handling, context guards, stale/foreign tokens and preview/
  commit/save/reopen agreement.
- Native numeric-input tests pass on Wayland and pinned Arch/Xvfb: keyboard-only
  imperial rectangle, repeated replacement, camera-safe re-entry, one undo item,
  retained invalid input, intervening command rejection, locale dimensions and
  revised line length.
- Lifecycle and interaction suites pass under pinned Arch/Xvfb. Wayland lifecycle,
  interaction and responsive-shell tests also pass. One interaction run immediately
  after another native test failed its existing focus assertion; an isolated run
  passed, consistent with the previously recorded host focus intermittency.

```sh
ctest --preset dev
ctest --preset sanitize
QT_QPA_PLATFORM=wayland build/dev/numeric_input_tests
QT_QPA_PLATFORM=wayland build/dev/tool_lifecycle_tests
QT_QPA_PLATFORM=wayland build/dev/interaction_tests
```
