# ADR 0037: assistant transactions in the native document

Status: accepted implementation contract. Panel and visual preview integration
follow this bridge; this decision does not claim that those UI flows exist yet.

The native editor already owns a `Document` whose address is retained by the
viewport, inspector and history controls. Assistant publication must update that
same object through the existing durable transaction path, preserving prior
manual history and adding one labeled assistant undo entry. It must not replace
the user's file or reconstruct the editor from a separate automation process.

`TransactionCoordinator` now supports an explicit trusted `BorrowedDocument`
constructor in addition to its original owning constructor. The owning path
retains its behavior. The borrowed path binds the existing object and publishes
the same preallocated candidate with the same no-throw move assignment after
durable acceptance. Its host must keep the document alive, serialize operations
on the owner thread, and prevent edits/replacement while an outcome is uncertain.
The coordinator cannot be moved or copied. Every operation checks the pinned
session identity as well as thread ownership; reopening the same document ID
into a new session closes the old binding. Recovery into a borrowed object is
rejected: explicitly recover an independent document before binding the editor.

`NativeAssistantSession` connects `AssistantTask` to this coordinator, the shared
transaction dispatcher and bounded inspection session. It supplies actual native
selection/context, rejects file/process operations and preserves the same schemas,
revision checks, budgets, staged drafts and durable identities. It exposes no
new provider command, network endpoint or mutation capability through native MCP.
The bridge accepts a host callback for active modeling gestures; staging/apply
wait for the gesture to finish, while abort/status/reconcile remain available.

Native temporary locks are additional host policy. Sealing, acquiring a preview
and committing recheck changed/deleted bodies, world transforms and additions
under locked ancestors. Locking after a preview without changing the model
revision still prevents publication. A rejected locked commit retires the sealed
request through the durable coordinator. Full commit-schema/document validation
precedes that retirement, so a malformed request cannot cancel another valid
proposal. Persistent engine authorization remains authoritative.

A trusted host may pin the exact sealed `PreparedEdit` for rendering. The handle
is immutable, bounded by existing staging admission, and cannot itself publish.
Acquiring it checks current revision, session, expiry and native lock policy.
The UI must release its displayed handle on stale/cancel/close and distinguish
preview measurements from live measurements. Holding an old immutable handle
never bypasses current commit checks.

A post-rename failure remains an unknown outcome: the bridge neither claims that
nothing was applied nor replays the modeling commands. Reconciliation uses the
same request/hash and publishes the verified retained candidate once. The UI
must prevent dependent edits while that outcome is uncertain and keep Reconcile
available. Explicit close is rejected until uncertainty is resolved; destruction
preserves the journal for later recovery. The provider must be destroyed before
its bridge, and the bridge before the native document. Ordinary provider waits,
failed requests and sealed previews do not lock manual modeling.
