# R082.d — Native file workers preserve responsiveness and newer edits

2026-10-07. Desktop native open/save previously decoded or serialized and wrote
the whole file on the GUI thread. The [file-operation contract](../decisions/0158-responsive-native-file-operations.md)
moves that work to owned worker state while continuing GUI event processing.
Longer operations show indeterminate progress. Short operations avoid dialog
flicker. In-progress publication is not cancelled by closing the progress dialog.

Save captures immutable document state and its stamp/revision. Later edits remain
unsaved, retain recovery data and get explicit status. Save-before-replace checks
for these newer edits before discarding anything. Open validates a separate
Document before replacement; failure preserves the current model. Reentrant
file operations and close/replacement requests are fenced while work is active.
Durability, backup, source-preservation and file-size/format contracts are unchanged.

[All 18 native cases pass](R082d-file-worker-matrix.json): new file-worker and
existing recovery fixtures on Wayland/X11 at 1×/2×, normal and ASan/UBSan, plus
normal/sanitized Wayland runs using the exact 100,671,415-byte R082.c fixture.
The new checks deliver timers during real file operations, preserve exact saved
bytes, reject reentrant save/open/close, preserve the active document after read
failure, retain edits arriving during save and decline save-before-replace when
newer edits appear. Existing recovery tests preserve their failure-banner, retry,
source-copy, settings and close/discard checks. Sanitizers use leak detection and
halt-on-error, with the established private Wayland client lifetime correction.
A user-message interruption left an empty fourth sanitizer case log; it is retained
privately, and that case plus the remaining cases completed after resumption.

The [three registered desktop regressions](R082d-desktop-ctest.txt) also pass in
11.08 seconds. Earlier [178 non-desktop checks](R082b-full-headless-ctest.txt)
cover the unchanged core/IO implementation at their recorded source; they are
not relabeled as rerun after this UI change.

[Installation verification](R082d-installed-smoke.json) matches all four installed
executables to the current build and all installed contracts/catalogs to source.
Both user/support guides are byte-exact. The [installed application](R082d-installed-large-app.json)
opens and renders the 100 MB fixture on isolated native Wayland with 1,000 bodies,
ready renderer/text and zero GL errors. The [source archive check](R082d-source-package-final.json)
verifies all 219 installed inputs, including the new contract.

These checks establish event delivery and state preservation, not a controlled
maximum event-loop latency or reference-hardware performance result. Recovery
checkpoint capture and unrelated imports/exports are outside this worker change.
The separately running clean package lifecycle uses its earlier stated source;
it does not count as packaging verification of these new workers. Final CI,
ordered merges, remaining R082 scenarios and release acceptance remain open.
The required discrete-GPU benchmark is [blocked by unavailable hardware](R085e-hardware-availability.md),
as confirmed by the project owner; this requirement is not waived.
