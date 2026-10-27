# 0182 — Multi-user document collaboration: feasibility and semantics

**Status: Accepted (2026-10-27): deferred indefinitely.** The owner works alone or hands files back and forth and does not need concurrent editing, so R108 is deferred indefinitely. C07 stays at "Investigate", no slice of the implementation split is scheduled, and the network-stance amendments of §5 are not adopted: ADR 0179's N1 to N9 stand unchanged. The analysis of the current model (§2) stays as a record. Originally proposed 2026-10-26. This is the feasibility design for [R108](../PR_ROADMAP.md#r108) ("Specify and prototype multi-user document collaboration"), scope row C07, which [SCOPE](../SCOPE.md) keeps at "Investigate" for M10. It is research and design only: no product code, no dependency, no server, no socket and no network call. No code was run. Every code claim below comes from reading the source at `e51f5959` (origin/main, read 2026-10-26), and each cites a file and line. The prototype (R108.a) waits for the owner's decisions in [Owner decisions](#owner-decisions).

## Verdict in brief

- **Verdict: viable as server-ordered transactions with ownership leases, hosted by one member's own SketchyUp process. Prototype next, with no sockets until its fixtures pass.** The host runs the same commit path that R041 delivered, so the design reuses most of it.
- **The commit path exists.** R041 provides a revision compare-and-swap, one actor that serializes commits, request identity with a payload hash, co-recorded outcomes, and unknown-to-reconciled transitions ([ADR 0005](0005-document-protocol.md), [0022](0022-private-staging.md)–[0025](0025-transaction-dispatch.md)). R042 runs the same coordinator headless ([ADR 0026](0026-headless-session.md)).
- **Four things stop replication today, and each is a bounded change.** Change preconditions compare pointers, not values (`src/core/model.cpp:1617`). New IDs come from document-wide counters, and the batch language names entities only by absolute ID (`src/automation/commands.cpp:68`). The durable path rewrites a full checkpoint per commit under a 64 MiB ledger ([ADR 0023](0023-durable-outcomes.md)). Undo is one last-in-first-out stack (`src/core/model.cpp:2582`).
- **Rejected:** operational transformation. I found no published transform set for topology-changing commands. CRDTs on geometry, because they promise convergence and no lost updates, not validity. Per-property last-writer-wins, because it drops changes silently on geometry and settings. Branch and merge is deferred: it is an asynchronous exchange feature, and it needs ID remapping that does not exist.
- **The network stance changes.** A session opens an inbound listener and sends document content to invited members. That breaks N9 ("nothing about the document is sent") and changes N1. [Section 5](#5-network-stance) lists each change for owner acceptance.
- **Not claimed:** a hosted service, a relay, accounts, NAT traversal, or any measured number for the proposed protocol.

## Context

- **What exists.**
  - R041 (complete; PRs #60–63): private staging, revision compare-and-swap, durable outcomes and incremental dispatch ([ADR 0022](0022-private-staging.md)–[0025](0025-transaction-dispatch.md)). R042 (PRs #64–65): [headless sessions](0026-headless-session.md).
  - R038 (PRs #50–51, [ADR 0015](0015-recovery-storage.md), [0016](0016-native-recovery.md)): checksummed checkpoints and an append-only journal. Each frame is a complete native container. A process lock guards each recovery session (`src/io/recovery.cpp:289–291`).
  - [ADR 0005](0005-document-protocol.md): a random 128-bit document identity, a monotonic revision, idempotent commit reconciliation, and allocator high-water marks that survive undo and save.
  - The native format is document version 31 with container version 2 (`src/io/native_format.hpp:10`). The root field list is a strict whitelist (`src/io/document_io.cpp:942–943`), and unknown required fields are rejected ([ADR 0108](0108-public-native-format.md)). ADR 0108's text still says version 24; the code is authoritative.
  - The outbound network surface is the assistant providers and a loopback Ollama endpoint ([ADR 0179](0179-hosted-asset-access-feasibility.md) §6). The only listener is the local MCP socket, a `QLocalServer` with user-only access (`src/app/native_mcp.cpp:57`, `:191–193`; [ADR 0028](0028-local-mcp.md), [0029](0029-native-mcp.md)). No TCP listener exists.
  - Qt 6.8 (`Qt6::Network`) and OpenSSL 3 are already linked for the provider transport (`CMakeLists.txt`, target `sketchyup_providers`). The Qt 6.8.9 QtNetwork class list includes `QSslServer`, `QSslSocket` and `QTcpServer`, and no QUIC class (read 2026-10-26).
- **Roadmap constraints.** R108 requires R089 and R041. Its Build line asks for conflict and ownership semantics, identity, transport and service operations before any prototype, and it says "this item does not promise a complete hosted service." Its Verify line asks for two-client fixtures that show "defined conflict handling, disconnection and recovery without lost updates." C07 in [SCOPE](../SCOPE.md) reads "cannot be inferred from single-user AI support." [BUILD_PLAN](../BUILD_PLAN.md) line 342 already lists "ID remapping, repeated request deduplication and concurrent-revision rejection" among the transaction requirements. M10 is the Extended milestone ([PR_ROADMAP](../PR_ROADMAP.md) milestone table).

**Alternatives considered:**

| Approach | What it would mean here | Evidence | Verdict |
|---|---|---|---|
| Operational transformation | Transform each pair of concurrent commands so they commute | Each command pair needs a transform, including commands that create or split topology (split edge, heal face, component create). The abstract of Sun et al. (2019) notes that CRDT work has "routinely portrayed OT as an incorrect, complex and inefficient technique". The abstracts I read give no transform set for B-rep editing. | Rejected. |
| CRDTs on the document | Each replica merges by a convergence rule, with no coordinator | The Kleppmann and Beresford abstract promises that "no updates are lost" and that replicas converge. It says nothing about application invariants. A body is a validated topology ([ADR 0004](0004-topology.md), [ADR 0053](0053-geometry-diagnostics.md)), so a converged merge can still be an invalid body. Libraries: Automerge (MIT; Rust core with a C API), Yjs (MIT; JavaScript), yrs (MIT; Rust port with a C binding), json-joy (AGPL-3.0-only in its latest release). | Rejected for geometry. Not needed for R108. |
| Per-property last-writer-wins | The server's arrival order decides each property (Figma's model) | Figma uses server order and per-property last-writer-wins, and says it does not use OT (Figma, 2019-10-16). Here a write can be overwritten by a write its author never saw. That breaks the Verify line for settings and topology. | Rejected for geometry and settings. |
| Server-ordered transactions with ownership leases | One host serializes commits. A commit is accepted only at the host's current revision. Leases mark who is editing what. | R041 and R042 provide most of the commit path. Figma orders all edits through one server authority (Figma, 2019-10-16). | **Chosen.** |
| Branch and merge (copy, then merge deltas) | Each user works on a copy, and merges apply a base-to-source delta | Onshape merges base-to-source deltas per tab with Merge, Keep or Replace, and does not merge while the document is in use (Onshape help, read 2026-10-26). Here IDs come from document-wide counters, so two copies allocate the same IDs. A merge needs ID remapping and a closure rule that do not exist. | Deferred: a later asynchronous exchange feature, not R108. |

## 1. Precedents and libraries

**Products.** Public sources only. Each claim cites a page read on 2026-10-26.

| Product | Model | Concurrency and conflicts | Source |
|---|---|---|---|
| Figma | Browser clients; a central server is the authority for ordering and validation | Per-property last-writer-wins; fractional indexing for insert positions. "Figma isn't using true CRDTs though." Figma says it did not use OT. Post dated 2019-10-16. | [Figma blog](https://www.figma.com/blog/how-figmas-multiplayer-technology-works/) |
| Onshape | Cloud documents with workspaces (branches) and versions | Merges a source version or workspace into the target. Strategy per tab: Merge changes (default), Keep or Replace. "Branches cannot be merged if the document is in use." "Changes from different branches can be incompatible, causing errors." No selection of individual changes inside a tab. No date shown. | [Onshape help, Merging](https://cad.onshape.com/help/Content/Document/merging.htm) |
| SketchUp and Trimble Connect | Shared models with access levels | The official SketchUp help page I read covers sharing with view or comment access and pinned comments. A search summary of SketchUp's help says recipients see the owner's changes in real time; I did not verify that by fetching. I found no official statement of check-out, locks or simultaneous co-editing. Trimble Connect's concurrency model is unverified. | [SketchUp help, Connect and collaborate](https://help.sketchup.com/en/connect-and-collaborate) (last updated 2026-08-21 per the page) |
| Blender Multi-User | Client/server add-on that replicates data-blocks | Per-data-block access rights and session roles. The features page describes no ownership lock or conflict handling. "The addon is still in development." GPL v3 or later per the Blender Extensions listing. | [Multi-User docs](https://multi-user.readthedocs.io/en/latest/about/features.html); [Blender Extensions](https://extensions.blender.org/add-ons/multi-user/) |
| FreeCAD | Local files; version control through Git | I found no official statement of real-time multi-user editing. CADCloud, a third-party platform, was presented at FOSDEM 2021 as self-hostable, with "full lifecycle management" under implementation. Its current status is unverified. | [FOSDEM 2021, CADCloud](https://archive.fosdem.org/2021/schedule/event/cadcloud/) |

Figma's post verifies a single server authority. Onshape's merge page does not say how its service orders edits. The other three do not describe concurrent editing with a verified source, so they decide nothing here.

**Libraries.** Licence and size come from the npm registry and the repositories. Size is the package's unpacked size, which is not the same as the runtime cost of a model.

| Library | Licence (read 2026-10-26) | Language and C interface | Package size | Fit for R108 |
|---|---|---|---|---|
| Automerge 3.5.0 (`@automerge/automerge`) | MIT (LICENSE: "Copyright (c) 2019-2021 the Automerge contributors") | Rust core, compiled to WebAssembly for JavaScript; C API in `rust/automerge-c` | 47,529,812 bytes unpacked, 58 files (npm) | Merges JSON trees; not validated B-rep records. Not chosen. |
| Yjs 13.6.33 | MIT (npm) | JavaScript | 2,308,376 bytes unpacked, 135 files (npm) | Same objection. Not chosen. |
| yrs / y-crdt | MIT (LICENSE: "Copyright (c) 2020", Bartosz Sypytkowski) | Rust port of Yjs; C foreign interface in `yffi` | Not measured | Same objection, plus a Rust toolchain. Not chosen. |
| json-joy 18.30.0 | **AGPL-3.0-only** (npm, latest) | TypeScript (inferred from the repository files; not stated) | 2,205,014 bytes unpacked, 1,159 files (npm) | Licence is incompatible with an MIT product in the latest release. Older releases are reported as Apache-2.0 by a search summary, not verified. Not chosen. |

## 2. The current model

### 2.1 The commit path that exists

- `Document::apply(Edit, expectedRevision)` (`src/core/model.cpp:3026`) checks the revision in `applyEdit`, and throws `STALE_REVISION` when it differs (`src/core/model.cpp:1140–1141`).
- `TransactionCoordinator` is the only committer. `prepare` stages a batch against the current document (`src/automation/transaction_coordinator.cpp:224`). `commit` applies the staged proposal to a candidate and writes it through the outcome store before it publishes (`:310`, `:301`). `status` and `reconcile` resolve pending and unknown outcomes (`:366`, `:379`).
- The batch request already carries `documentId` and `expectedRevision`. The executor rejects a wrong identity and a stale revision (`src/automation/commands.cpp:583–586`). A batch holds 1–100 commands (`:588`).
- The outcome store has one exclusive writer per document and epoch (`src/io/outcome_store.hpp:16`). Request identities are server-issued ([ADR 0023](0023-durable-outcomes.md)).

So the single-user path already runs a batch against a revision, refuses it when stale, and records its outcome once. A host-ordered design reuses that path.

### 2.2 Change sets

- An `Edit` is a list of per-record before and after pointers, one list per table, with per-table allocator floors (`src/core/model.hpp:99`; the per-record `Change` at `:23`). A batch becomes one `Edit` (`src/automation/commands.cpp:1744–1746`), and is applied at `:1884`.
- The safety check against the current state compares pointers. `(it == bodies_.end() ? nullptr : it->second) != c.before` throws "Change precondition failed" (`src/core/model.cpp:1617–1618`). Two replicas loaded from one file hold different `BodyPtr` values, so the same edit fails on one of them. Content, not identity, must be compared before an edit can cross a process boundary.
- `PreparedEdit` binds to a `SaveStamp` (session and state pointers), and `isCurrentSnapshot` compares them (`src/core/prepared_edit.cpp:25–27`, `src/core/model.hpp:349`). Staleness is an in-process fact today.
- A change set can be derived from two snapshots. `recovered()` rebuilds an `Edit` by diffing a before container against an after container (`src/automation/transaction_coordinator.cpp:27`). That gives a state-diff replication path, at the cost of two full snapshots.
- A change is a whole record. A body carries its whole topology and surface (`src/core/body.hpp`). [ADR 0005](0005-document-protocol.md) says: "Current changed-body copying is an interim implementation; large topology edits need granular deltas."

### 2.3 IDs and allocators

- Each kind has a document-wide counter: bodies (`nextId_`), definitions, tags, materials, assets, scenes, camera paths, render presets, sections, annotations, drawing sheets, views, items and component drivers (accessors at `src/core/model.hpp:229–274`). Topology IDs are per body (`topology.nextId`, `surface.nextId`), and member IDs are per definition (`nextMemberId`).
- Creating a record below its counter is refused. `if (!c.before && c.id < nextId_)` throws "Retired ID cannot be reused" (`src/core/model.cpp:1619–1620`). Floors survive undo and save ([ADR 0005](0005-document-protocol.md)).
- **Consequence.** If two clients each created a body from the same base revision, both would allocate the same next ID. A host that received the second client's `Edit` would refuse it at `:1619–1620`, because its counter had already passed that ID. A host-ordered design must not ship client-allocated IDs. The host allocates them.
- The batch language addresses entities by absolute ID. `id()` accepts only canonical decimal strings (`src/automation/commands.cpp:68–74`), and a command can name only an entity that already exists. So a batch cannot refer to a body it creates earlier in the same batch. Under strict revision matching, a client can predict the IDs its preview will get, and the host's `createdIds` must match them. A replayed batch on a newer base needs a batch-local reference form that does not exist yet (§3.3).
- The document identity is a random 128-bit value generated at construction (`src/core/model.cpp:613–618`) and stored as `documentId`. It names a lineage: Save As keeps it, and fork or Save Copy creates a new one ([ADR 0005](0005-document-protocol.md)). Two independent opens of one file share it, so it cannot name a replica. A session needs its own session identity, bound to the host's lineage.

### 2.4 Undo and history

- Undo and redo form one stack, last in first out (`src/core/model.cpp:2582`, `:2602`). Each undo is a new revision and reverses the newest edit. With two authors, a LIFO undo reverts whichever edit came last, which may be the other author's. [ADR 0163](0163-documentation-sheets.md) states the same for sheet and model edits.
- History is capped at 64 MiB (`src/core/model.hpp:280`). Terrain edits have their own 512 MiB budget ([ADR 0168](0168-terrain-import-and-surfaces.md)).
- Per-member undo is therefore a new semantic (§4, rule R7). It does not change the single-user stack.

### 2.5 Recovery and durability

- The R038 journal writes a complete replacement state per frame, and does not write a command log: "It references a complete replacement state rather than replaying commands" ([ADR 0015](0015-recovery-storage.md)). [ADR 0023](0023-durable-outcomes.md) says the outcome store has "no executable command replay."
- The outcome store rewrites a complete checkpoint on each transition, with a 64 MiB limit that covers both containers ([ADR 0023](0023-durable-outcomes.md)). R041b measured this on the development host it records (Linux, Qt 6.11.2, Debug build, median of five). A commit on the 1000-face fixture took 88 ms and left a 1,141,500-byte checkpoint. The 8 MiB asset fixture took 108 ms and left a 16,782,380-byte checkpoint ([R041b evidence](../verification/R041b-durable-outcomes.md)).
- So the durable store cannot be the replication log, and it cannot serve client catch-up. It holds at most 10,000 outcomes and 64 MiB, and a large model fills that in a few commits. The replication feed needs its own change-only log (§3.5).
- Recovery restores identity, revision, records and allocator floors, but not the undo stack ([ADR 0015](0015-recovery-storage.md)).

### 2.6 What can be replicated

| Item | Replicable as-is? | What is needed |
|---|---|---|
| Native container (document state) | Yes, as canonical bytes | Nothing, for snapshots |
| `Edit` (change set) | No: preconditions are pointers | Canonical per-record encoding and content-hash preconditions |
| Batch (command JSON) | Yes, with `documentId` and `expectedRevision` | Host-side execution; batch-local references for replay (P2 only) |
| Allocators and IDs | No: process-local counters | Host-only allocation |
| `PreparedEdit`, `SaveStamp` | No: pointer identity | Replaced on the wire by revision and hash |
| Undo and redo stacks | No: pointers, and LIFO order | Per-member undo as inverse commits (§4, R7) |
| Derived state (variants, totals, indices) | Rebuilt on each replica | Nothing; never a conflict unit (R6) |
| Outcome records | Yes, but at full-container cost | A separate change-only log |
| Leases and presence | Not document state | Host memory only |

## 3. The design: host-ordered commits

### 3.1 Roles and service operations

- A session has one host: the SketchyUp process of the member who starts it. The host owns the document, runs batches, allocates IDs, keeps the outcome store and writes the replication log. No project-operated server takes part.
- The host's operations are: start a session (which opens a listener on an interface the user chooses); invite a member (one-time code); remove a member; end the session; force-release a lease; restart (recovery from the R038 journal and the outcome store); and save a copy, which creates a new identity and ends nothing.
- On restart, `initialize` runs `abortOrphans`, which aborts pending requests the host cannot find, with `STAGING_UNAVAILABLE` (`src/automation/transaction_coordinator.cpp:158`). This is the existing path.

### 3.2 Commit rule

A batch is accepted only if its base revision equals the host's current revision. Acceptance and durable recording happen in the host's single actor turn, as R041 already does. So no commit can overwrite a change its author did not see. This rule holds whatever the leases say. Leases are policy.

### 3.3 Phases

- **P1 (first).** Strict. A batch on a stale base is not applied. The client holds it and shows the conflict (§4). A held batch is re-run on the new base locally, and the user confirms the new preview before it is sent. P1 needs no footprint analysis, because any intervening commit blocks the batch.
- **P2 (later, gated).** Footprint-disjoint replay. The host re-runs a stale batch only when no intervening commit touched any record the batch reads or writes. P2 needs the closure rules (§4.4), batch-local references for created entities, and a test that the closure is complete. Until then there is no automatic replay.

### 3.4 Wire operations (proposed)

| Operation | Purpose | Reply |
|---|---|---|
| `prepare {batch, baseRevision, documentId}` | Issue the request identity; stage at the base | `{requestId, payloadHash, preview, createdIds}`; or `STALE_REVISION`, `LOCKED` (holder named), or a validation code |
| `commit {requestId, payloadHash}` | Make the batch durable and broadcast it | Committed, with revision and record hashes; or aborted, with the named reason |
| `status {requestId, payloadHash}` | Resolve a lost reply | The outcome `OutcomeStore::lookup` returns |
| `lease {footprint}`, `release`, `heartbeat` | Ownership policy | Granted; held by a named member; or expired |
| `catchUp {fromRevision}` | Reconnect | Committed change records, or a snapshot if the gap is outside retention |
| Event stream (host to members) | Committed records; lease and presence changes | — |
| `digest {revision}` | Divergence check | Per-record hash map, at every 100th revision |

Each commit takes two round trips, `prepare` then `commit`, as in R041. A lost `prepare` reply leaves at most an orphan request, which expires as aborted ([ADR 0023](0023-durable-outcomes.md)). A lost `commit` reply is resolved by `status`.

### 3.5 Replication log

- Each committed `Edit` is encoded as its label and metadata, the host's allocator floors after the commit, and each changed record: its ID, the hash of its before state, and its after bytes (or a null marker for a delete). The encoding is canonical: sorted maps and canonical decimal strings, as the native format already requires.
- A replica applies a record only if the hash of its local before state matches. A mismatch means divergence, and the replica requests a snapshot.
- The batch's preview is checked against the host's result: `createdIds` and the per-record after hashes must match (§3.6).

### 3.6 Determinism check

Client and host run the same batch on the same base, so the results must be identical. The client compares its preview with the host's committed record hashes. A mismatch is logged and forces a resync. This tests the identical-build assumption on every commit. Two builds may join only if version, platform and architecture all match (§6.2).

### 3.7 Serial outbox

A client keeps at most one batch in flight. Later edits queue behind it. This keeps batch-local IDs away from unconfirmed state and keeps ordering simple.

## 4. Conflict and ownership semantics

### 4.1 Rules

- **R1. No lost update.** A batch applies only at the revision it was composed on (P1), or after a host-verified disjoint replay (P2). So no commit overwrites a change its author did not see.
- **R2. Stale work is held, never dropped.** A held batch stays in its author's outbox, with the conflicting commit named. The user re-applies it (the preview is recomputed on the new base) or discards it explicitly. A discard is recorded as a user action.
- **R3. Deleted IDs stay deleted.** No resurrection. An edit to a deleted record fails and names the record.
- **R4. Settings compare by value.** A setting changed since the base is a conflict. There is no last-writer-wins.
- **R5. Whole records are the conflict unit.** Topology and component members are never merged element by element. Validation runs on the whole record.
- **R6. Derived state is never a conflict unit.** Variants, placements and totals are recomputed inside the commit. Their inputs define the closure.
- **R7. Undo is per member, as an inverse commit.** A member's undo applies the inverse of that member's latest committed edit, if no later commit touched the same footprint. Otherwise it is refused and names the commit. Single-user undo is unchanged.

### 4.2 Per record kind

| Record kind | Conflict unit (lease key) | Same record, concurrent edit | Delete versus edit | Notes |
|---|---|---|---|---|
| Geometry body and its topology | The body, with its parent chain and, for moves and deletes, its descendants | Held under P1, or `LOCKED` while a lease is held | The first commit wins; the other batch fails and names the body (R3) | Topology is never merged element by element (R5). |
| Component definition (members, references, glue, parameters, drivers) | The definition, every placement, and their members | Serialized: a definition edit takes every placement's lease | A definition in use is refused by existing validation | Regeneration of placements happens in the same commit (R6). |
| Component placement (transform, overrides) | The placement root and its members | Instance-only edits take the placement lease only | Deleting the placement fails the other batch by name | An override whose parameter a commit removed is refused, and the parameter is named (ADR 0165; overrides are on top-level placements only). |
| Tag (and folder) | The tag | Rename or visibility: per-tag lease | Refused while bodies or definition members use it (`src/core/tags.cpp:38–45`); the racing assignment wins and the delete fails by name | The folder rule is at `tags.cpp:46–48`. |
| Material | The material | Per-material lease | Refused while assigned (`src/core/materials.cpp:34–39`) | Assigning a material to a face is a body edit. |
| Managed asset ([ADR 0011](0011-managed-assets.md), immutable) | The asset ID | Never conflicts; creation only | Follows the existing reference rules | No edit path exists. |
| Saved scene ([ADR 0070](0070-saved-scene-records.md)) | The scene | An update is an explicit new capture, under the scene's lease | Deleting a scene leaves dependent sheet views in the broken-reference state [ADR 0163](0163-documentation-sheets.md) already exposes | A scene and a sheet edit do not conflict by themselves. |
| Section plane | The section | Lease | Named failure | Active sections are a setting (R4). |
| Settings: active sections, units, precision, style, solar, site origin, hosted settings | The setting, one key each | Value comparison (R4) | Not applicable | A site re-anchor moves geometry, so it takes every body ([ADR 0168](0168-terrain-import-and-surfaces.md) §4). |
| Drawing sheet, view and item ([ADR 0163](0163-documentation-sheets.md), [0164](0164-sheet-layout-interface.md)) | The sheet, with its views and items | Sheet lease | A model edit that moves or deletes an anchored body or scene takes the sheet in its closure | Sheet-only edits never change model records (ADR 0163). |
| Terrain ([ADR 0168](0168-terrain-import-and-surfaces.md), [0169](0169-terrain-editing-drape-stamp.md)) | The terrain and its tiles | Whole-terrain lease | Named failure | Excluded from sessions in M10 (owner decision 10). |
| Camera path, render preset ([ADR 0173](0173-camera-paths-batch-output.md)) | The record | Lease | Named failure | — |
| Allocators, floors, document identity | Host only | Never a client write | Not applicable | — |

### 4.3 Leases

- Leases are advisory policy. Correctness does not depend on them (R1).
- A lease covers a batch's footprint. `prepare` takes it, and `commit`, `abort` or expiry releases it. A member may also take an explicit lease on a selection while editing it.
- A lease expires after 30 seconds without a heartbeat. The holder's outbox is kept.
- The host can force-release a lease, with notice to the holder. The holder's batch becomes held, not discarded.
- Leases live only in host memory. They are never written to the document, the native file or the recovery journal, and they clear when the host restarts.

### 4.4 Footprint closure (required before P2)

These links must be enumerated and tested, in one reverse index (R108.b):

- Body: parent chain and descendants (for moves and deletes).
- Definition: every placement, and every member record of each placement (regeneration in `src/core/component_regeneration.cpp`; [ADR 0165](0165-constrained-component-parameters.md) §6).
- Tag: bodies and definition members that use it (`src/core/tags.cpp:38–45` already refuses such deletes).
- Material: bodies, faces and definitions that use it (`src/core/materials.cpp:34–39`).
- Scene: sheet views that reference it ([ADR 0163](0163-documentation-sheets.md)).
- Sheet item: the bodies and scenes it anchors to (the anchor rewrite inside model edits, [ADR 0164](0164-sheet-layout-interface.md)).
- Terrain: its tiles and terrain edits ([ADR 0168](0168-terrain-import-and-surfaces.md), [0169](0169-terrain-editing-drape-stamp.md)).
- Site origin: every body.

### 4.5 Scenarios

- **Same body, concurrent.** With A's lease held, B's `prepare` fails `LOCKED` and names A. Without a lease, A commits first. B's batch is held under P1, with A's commit named. B re-applies or discards.
- **Delete versus edit.** A deletes body X. B's batch edits X and is held under P1. On re-apply, X is missing, so the re-run fails and names X. B's batch is kept, with the deletion as the reason. Nothing is re-created.
- **Definition versus instance.** A edits definition D, which regenerates placements P1 and P2. B changes P1's transform, which is instance-owned. The revision rule holds B's batch. On re-apply, the preview shows the regenerated members of P1. An override that names a parameter A removed is refused, and the parameter is named ([ADR 0165](0165-constrained-component-parameters.md)).
- **Two new bodies from one base.** Both clients compose batches on the same counter. The host runs the first batch and allocates the next ID. The second batch is held under P1. On re-apply, the host allocates again. No ID is reused, and no client's preview is trusted over the host's.

## 5. Network stance

The stance in [ADR 0179](0179-hosted-asset-access-feasibility.md) §6 is N1–N9. Collaboration keeps most of it and must change three rules. These are for owner acceptance (owner decision 5).

| Stance (ADR 0179) | Today | With a session | Change |
|---|---|---|---|
| N1. Only on user action; no background requests | Requests only after a user action | A session the user starts keeps one connection open, with heartbeats, for as long as it is open | **Amend.** No traffic without an open session. |
| N2. No telemetry | Holds | Holds. Presence and display names go only to members. | No |
| N3. In the app process; the provider transport pattern (`src/integrations/provider_transport.hpp`, 30 s bound) | Holds | The listener and client run in the app process. Heartbeats replace the request timeout for sessions. | **Amend** the timeout rule for sessions. |
| N4. No hosted bytes to a network-capable helper | Holds | Holds. Session traffic never reaches a helper. | No |
| N5. The User-Agent names the application and carries no user identifier | Holds | No User-Agent, since there is no HTTP. The session hello names the application and protocol version. | Adapted |
| N6. Sign-in only where a service requires it; tokens in the OS store | Holds | A device key in the OS secret store ([ADR 0034](0034-openai-provider.md)). No account. | No |
| N7. Named failure states | Holds | Holds: offline, refused, stale, locked, unknown, expired. | No |
| N8. Nothing core depends on a hosted service | Holds | Holds. Single-user editing needs no session. | No |
| N9. Nothing about the document is sent | Holds | **Cannot hold.** Document content goes to the members the user invited, over TLS 1.3 with pinned identities, only while the session is open. | **Replace** with N9′ (below). |
| N10 (new). No TCP listener without a host session; the user picks one interface. The local MCP socket (`QLocalServer`, user-only) is unchanged. | — | The first TCP listener in the product | **New** |
| N11 (new). No relay or service operated by the project | — | The host is a member's own process | **New** |

**N9′.** Document content leaves the machine only to members the user invited, over TLS 1.3 with pinned identities, and only while a session is open. Presence and display names go only to the same members.

## 6. Identity, transport and security

### 6.1 Identity without an account

- On first use of collaboration, the app creates a device key pair and keeps the private key in the OS secret store. [ADR 0034](0034-openai-provider.md) requires that store, with no plaintext fallback.
- The device ID is the SHA-256 of the DER-encoded certificate, encoded in base32. Syncthing forms its device IDs this way, and its devices authenticate each other by presenting certificates in TLS and checking the expected ID ([Syncthing device IDs](https://docs.syncthing.net/dev/device-ids.html), read 2026-10-26). The certificate exists to carry the key. Trust comes from the pinned ID.
- A member chooses a display name. Nothing else about the member is shared.
- Trust comes from an invite. The host creates a one-time invite with its device ID and an expiry (default 24 hours). The user passes it on out of band. A joiner connects only to the pinned device ID, and refuses any other. An invite is spent on first use.
- The key type (ECDSA P-256 is the candidate, for interoperation with the existing Qt and OpenSSL stack) is to be verified in R108.c. This ADR does not verify it.

### 6.2 Transport

- TLS 1.3 over TCP, through `QSslServer` and `QSslSocket` (both in the Qt 6.8.9 QtNetwork class list, read 2026-10-26). TLS 1.3 is specified by RFC 9846, which obsoletes RFC 8446 ([RFC 9846](https://www.rfc-editor.org/rfc/rfc9846), read 2026-10-26).
- Frames: a 4-byte length prefix, then UTF-8 JSON, with a 1 MiB limit (proposed).
- **Not WebSocket.** RFC 6455 is framing on top of a connection that already has TLS ([RFC 6455](https://www.rfc-editor.org/rfc/rfc6455), read 2026-10-26). Qt's WebSocket module is a new Qt component (LGPL v3 or GPL v2; [Qt WebSockets](https://doc.qt.io/qt-6/qtwebsockets-index.html), read 2026-10-26, which documents Qt 6.12.0, not 6.8). The build does not link it. Not needed.
- **Not QUIC.** RFC 9000 ([RFC 9000](https://www.rfc-editor.org/rfc/rfc9000), read 2026-10-26) is an option. The Qt 6.8.9 QtNetwork class list has no QUIC class, so QUIC would add a dependency. Not for M10.
- **Not a relay the project runs.** That would be a hosted service, which is out of scope. A relay a member runs is the host with a public address, and NAT traversal is not promised. M10 supports LAN and VPN addresses only (owner decision 13).
- **Not Noise.** The Noise framework (revision 34, 2018-07-11, [noiseprotocol.org](https://noiseprotocol.org/noise.html), read 2026-10-26) describes a handshake without certificates. It would add a second cryptographic stack that the project does not use.
- **Version handshake.** Protocol version, document version (31), application version, platform and architecture must all match. The host runs the batches, and the client's preview must match the host's (§3.6), so a mismatched build does not join. It gets a named message.

### 6.3 Authorization

- Roles: host (the local member who started the session); editor (may `prepare`, `commit` and take leases); viewer (receives events and catch-up, and may not `prepare`).
- The host enforces roles on every operation. A remote member cannot open or save files, export, change settings the host did not expose, or invoke the assistant on the host.

### 6.4 Limits and abuse

- Handshake deadline: 10 seconds. Failed handshakes count per address and back off.
- Frame: at most 1 MiB. Batch: at most 100 commands (`src/automation/commands.cpp:588`) and at most 256 KiB encoded. The 256 KiB commit-result cap (`src/automation/transaction_coordinator.cpp:331`, `:338`) applies to results.
- At most 8 members, one listener, and one session per application.
- The wire carries only the operations in §3.4. Parser helpers never see session bytes (N4).
- Messages use the same strict decoders as the native format. Unknown fields are rejected.

### 6.5 Secrets and privacy

- The device private key stays in the OS store ([ADR 0034](0034-openai-provider.md)). Nothing about the key appears in documents, logs or screenshots (UX_DESIGN §8.5).
- Before the first host or join, the UI states what is sent. Document content goes to the invited members. The display name and the IP address go to the same members (UX_DESIGN §8.5 requires consent text for network features).

## 7. Disconnection and recovery

### 7.1 Client outbox

Each client writes its outbox (request identity, payload hash, base revision and batch) as checksummed frames, under the rules of the R038 journal. It lives in the client's own recovery directory, not in the document. A batch leaves the outbox only on a terminal outcome.

### 7.2 Host durability order

The host writes a committed record durably before it acknowledges the commit, and before it broadcasts it. [ADR 0005](0005-document-protocol.md) requires "fsync before publishing committed state or acknowledging success." So a member never holds a commit the host does not hold.

### 7.3 Reconnect

1. Open the session connection. The host returns its current revision.
2. Replay the committed log from the client's revision. If the gap is outside retention, take a snapshot instead. Retention is 10,000 outcomes or 64 MiB ([ADR 0023](0023-durable-outcomes.md)), matched by the log in §3.5.
3. For the in-flight batch, call `status`:
   - committed: apply the committed record, once;
   - aborted: keep the batch and show the reason;
   - pending: `commit` again with the same identity and hash. The host commits at most once;
   - unknown or expired: the explicit reconciliation error ([ADR 0005](0005-document-protocol.md)). The batch is held for the user.
4. Re-run held batches only through P1.

### 7.4 Cases

| Event | What happens | Lost update? |
|---|---|---|
| Client offline with edits | Edits queue in the outbox; they are sent after reconnect through P1 | No. Held batches are shown. |
| `prepare` reply lost | The orphan request expires as aborted. The client re-prepares under a new identity after reading the aborted outcome. | No. Nothing was applied. |
| `commit` reply lost after the host committed | `status` returns committed, with the original result. The record is applied once. | No. Applied once. |
| `commit` reply lost before the host committed | `status` returns pending or aborted. A pending request is committed once, by re-commit. | No |
| Host crashes after a durable commit, before broadcast | Restart replays the R038 journal and the outcome store. Members catch up. | No |
| Host crashes before a commit | Restart aborts the orphan (`src/automation/transaction_coordinator.cpp:158`). Members hold the batch. | No. Nothing was applied. |
| Client crashes with an outbox | Restart reads the outbox and reconciles each entry | No |
| Host journal lost | Acknowledged commits are held by the submitting client and by any member that received them. A replacement host can be rebuilt from the most advanced replica (owner decision 12). | Only if the host journal and every replica holding a commit are lost |
| Two copies with one `documentId` | `initialize` refuses a diverged input with `RECONCILIATION_REQUIRED` (`src/automation/transaction_coordinator.cpp:158`). A session binds to its own session identity as well. | No |

### 7.5 Invariant

Every acknowledged commit exists in the host journal and in the submitting client's replica. Every submitted batch ends in exactly one terminal outcome, or stays in an outbox. No batch is applied twice. §8.1 checks these after every event.

## 8. Prototype and fixtures

### 8.1 Harness

- Two in-process replicas, one host, and a simulated network. The network runs on a virtual clock, with seeded random delay, loss, duplication and reordering, plus scripted partitions and crashes (restart from the R038 journal). It opens no socket.
- Each run has a seed, and a failure reproduces from its seed. This follows the seeded sequences of [ADR 0144](0144-seeded-transaction-sequences.md) (eight seeds, 32 transitions each).
- Oracles checked after every event: revision monotonicity; each replica's state equals the host's state at the same revision (digest); no acknowledged commit is missing; no batch is applied twice; every batch is in exactly one terminal state or in an outbox; and the full validation oracle (`SKETCHYUP_FULL_VALIDATION_ORACLE=1`, `src/core/model.hpp`) runs on every apply.

### 8.2 Fixtures

| ID | Scenario | What it proves |
|---|---|---|
| F1 | Two clients edit disjoint bodies | The second batch is held, re-applied, and both edits are present. No loss. |
| F2 | Two clients edit one body; a lease is held by one | `LOCKED` names the holder. Nothing held is lost. |
| F3 | One body, no lease | The second batch is held with the commit named. Re-apply or discard. |
| F4 | Delete versus edit | The edit fails and names the record. Nothing is re-created. The batch is kept. |
| F5 | Definition edit versus instance transform | Regeneration happens in one commit. An override refusal names its parameter. |
| F6 | Two new bodies from one base | The host allocates distinct IDs. No retired-ID error. Previews reconcile. |
| F7 | Concurrent settings (units, style) | Value comparison holds the second batch. |
| F8 | Undo by one member after another's later commit on the same footprint | Refused, naming the commit. A disjoint undo succeeds as an inverse commit. |
| F9 | Disconnect with an outbox; reconnect | Catch-up and held batches. Digests equal on every replica. |
| F10 | `commit` reply lost after the host committed | `status` returns committed. Applied once. |
| F11 | Host crash after durable commit, before broadcast | Restart; members catch up; nothing is lost. |
| F12 | Host crash before commit | Aborted on restart. No effect applied. |
| F13 | Client crash with an outbox | Reconciled on restart. |
| F14 | Duplicate and reordered messages | Exactly once. Digests equal. |
| F15 | Lease holder silent | The lease expires after 30 s of simulated time. No deadlock. |
| F16 | Replica loaded from the same file | Hash preconditions pass where pointer preconditions fail today. This is the regression R108.a must fix. |
| F17 | Seeded random schedules: 1,000 seeds in the CI tier, more locally | Every oracle holds. |
| F18 | Codec round trip and tamper | A hash mismatch is detected. Canonical bytes are equal after the round trip. |

### 8.3 Out of scope for the prototype

Sockets, identity, UI, P2 rebase, terrain (excluded from sessions), large imports and assistant batches.

## 9. Verdict

The verdict is the one in [Verdict in brief](#verdict-in-brief), and the evidence is this:

1. **The commit path is in the code.** Revision compare-and-swap (`src/core/model.cpp:1140–1141`), a single committer (`src/automation/transaction_coordinator.cpp:310`), request identity with a payload hash ([ADR 0005](0005-document-protocol.md)), and a headless host (R042).
2. **The blockers are bounded.** Pointer preconditions (`src/core/model.cpp:1617–1618`) need content hashes. Allocation (`src/core/model.cpp:1619–1620`, `src/automation/commands.cpp:68–74`) moves to the host. The durable store's full-checkpoint rewrite (R041b) is avoided by a change-only log. Undo becomes per-member. None of these needs a change to the native file schema.
3. **Operational transformation and CRDTs on geometry fail on evidence.** Neither the OT nor the CRDT sources read give a transform set or an invariant guarantee for B-rep editing. The CRDT libraries that are MIT-licensed merge JSON trees.
4. **Branch and merge is real but later.** Onshape's merge page describes a shipped model, with per-tab strategies and a use-lock. This code has no ID remapping and no closure rules for it.
5. **The network change is real.** N9 cannot hold for a session, and the first TCP listener is new. The owner must accept N9′ and N10.

This ADR does not claim that collaboration is viable for 1.x. It claims that a host-ordered prototype can be built and tested with no socket, and that the network step waits for that test to pass.

## Verification

This ADR was not checked by running code. The code claims are from reading `e51f5959`. The checks below belong to the implementation slices.

- **V1. Codec.** Round trip, canonical bytes, and content-hash preconditions (F16, F18).
- **V2. Two-client fixtures.** F1–F17 pass under the harness, with seeds recorded.
- **V3. Full validation oracle.** On for every fixture.
- **V4. Single-user unchanged.** The existing automation and recovery suites pass without changed output.
- **V5. Network (R108.c).** A wrong device ID is refused. An invite expires and is single use. Oversize and malformed frames are refused. The handshake deadline holds. No listener is bound unless a session is open, which a test checks at startup.
- **V6. Recovery drills.** A fault at each `OutcomeStore::Phase` (`BeforeWrite`, `AfterWrite`, `AfterFileSync`, `AfterRename`, `AfterDirectorySync`, `src/io/outcome_store.hpp`), and at the journal tail, as [ADR 0005](0005-document-protocol.md) requires.
- **V7. Measurements.** Host commit latency and checkpoint rewrite on the R041b fixtures (empty, room, 1000 faces, 8 MiB asset) and on a 10,000-body fixture. Replication frame sizes.
- **V8. Two processes (R108.c).** A host and a joiner as separate processes on loopback, sharing no memory.

## Budgets (proposed; the prototype measures)

| Item | Budget | Basis |
|---|---|---|
| Network traffic with no session open | 0 bytes | N1, as amended |
| Members per session | At most 8 | Proposed; owner decision 9 |
| Frame size | At most 1 MiB | Proposed |
| Batch | At most 100 commands; at most 256 KiB encoded | `src/automation/commands.cpp:588`; `src/automation/transaction_coordinator.cpp:331` |
| Replication frame per commit | At most 4 MiB. A larger commit is refused in a session with `LIMIT_EXCEEDED`. | Proposed. The R041b checkpoint (16.8 MB for an 8 MiB asset) shows whole-checkpoint cost is too high. |
| Heartbeat | 5 s | Proposed |
| Lease expiry | 30 s without a heartbeat | Proposed; owner decision 8 |
| Handshake deadline | 10 s | Proposed |
| Invite | 24 h, single use | Proposed |
| Outbox | At most 1,000 batches or 64 MiB | Proposed |
| Catch-up retention | 10,000 outcomes or 64 MiB | [ADR 0023](0023-durable-outcomes.md) |
| Commit latency, host side | No worse than the single-user path, which measured 88 ms at 1000 faces, plus one round trip | R041b; V7 |
| Host memory above single-user | At most 256 MiB | Proposed |
| Digest | Per-record hash map every 100 commits; full container digest on reconnect only | Proposed |

## Risks

1. **Hidden pointer assumptions.** Code outside `apply` may rely on pointer identity, in staging or previews. Mitigation: F16 and a pointer-versus-hash equivalence test in R108.a.
2. **Closure omissions.** A missed link lets P2 replay two conflicting batches. Mitigation: P1 first. P2 is gated on the reverse-index tests (F5).
3. **Commit cost.** A commit rewrites a full checkpoint today (R041b: 88 ms at 1000 faces; 16.8 MB at an 8 MiB asset). Many edits per minute will feel it. Mitigation: a change-only log for replication. The append design [ADR 0023](0023-durable-outcomes.md) mentions is a prerequisite for large models.
4. **Build divergence.** A weak handshake lets two builds diverge silently. Mitigation: the exact-match handshake (§6.2) and the determinism check (§3.6).
5. **Network surface.** This is the product's first TCP listener. Mitigation: the limits in §6.4, strict decoders, the V5 tests, and a fuzzing pass in the style of R083.e.
6. **Consent and disclosure.** Members see document content, names and addresses. Mitigation: the consent text in §6.5 and owner decision 5.
7. **Undo confusion.** Per-member undo differs from the single-user LIFO. Mitigation: R7, tested by F8, and user-guide wording in R108.e.
8. **Lost work when the host is lost.** A commit held only by the host and by a member who did not submit it is lost if both disks fail. Mitigation: the §7.5 invariant, and manual rebuild from a replica (owner decision 12).
9. **Two copies, one identity.** Two opens of one file share a `documentId`. `RECONCILIATION_REQUIRED` covers diverged inputs, and not every duplicate open. Mitigation: session identity bound to the host's lineage, and a test in R108.c.
10. **Terrain and large commits.** Terrain edits replace whole tile sets, and imports can be large. Both can exceed the 256 KiB result cap and the replication budget. Mitigation: exclusion from sessions in M10 (owner decision 10).
11. **Licence.** The latest json-joy release is AGPL-3.0-only (npm, read 2026-10-26). Mitigation: excluded (owner decision 14).
12. **Scope creep.** Sessions imply sync, accounts and presence. Mitigation: the Build line, owner decisions 2 and 4.
13. **Source uncertainty.** Some precedents rest on search summaries: Trimble Connect's locking, Blender Collab Pro's locks, and FreeCAD's status. The TP1 and TP2 text in Imine et al. could not be extracted. No decision here depends on those claims. Re-read them before R108.e.
14. **Single-machine lock.** The outcome store's exclusive writer is a `QLockFile` (`src/io/outcome_store.cpp:342`, `writer.lock`). That lock does not protect across machines, for example on a shared network filesystem. Mitigation: the host records its session in the recovery directory, and refuses to start while a live session holds it.

## Implementation split

- **R108.a: prototype.** Canonical change codec; content-hash preconditions in `apply` (keeping the pointer fast path); the deterministic two-client harness; fixtures F1–F18. No sockets, no UI. Gate: owner decisions 1–3. Tests: V1–V4 and V6 in-process, and V7 measurements.
- **R108.b: ownership policy.** Leases, the footprint reverse index, and per-member undo as inverse commits. Gate: R108.a merged and its fixtures passing.
- **R108.c: transport and identity.** TLS host and join; device keys in the OS store; invites; catch-up and snapshot; the persisted outbox; the basic session UI. Gate: R108.b; owner decisions 4–7; and the N-stance amendment (decision 5). Tests: V5 and V8.
- **R108.d: P2 footprint-disjoint replay.** Batch-local references for created entities, and closure-complete replay. Gate: R108.b closure tests complete, and owner decision 3.
- **R108.e: presence and conflict UI.** Held batches, re-apply, discard, and presence. Gate: a UX section in `UX_DESIGN.md`, approved by the owner.
- **R108.f: claims and scope.** SCOPE C07 stays "Investigate" until R108.c passes on two machines. SUPPORT_MATRIX and USER_GUIDE make no collaboration claim before then.

## Owner decisions

1. **Direction: host-ordered transactions with ownership leases.** Not OT, not CRDTs on geometry, not per-property last-writer-wins. *Recommended: yes.* The commit path exists (§9), and the alternatives fail on the evidence.
2. **Scope: M10 only, as an Extended feature.** No collaboration claim in 1.x, and no support-matrix row until R108.c passes. *Recommended: yes.*
3. **Phasing: P1 (strict, held for the user) first. P2 (disjoint replay) only after the closure tests pass.** *Recommended: yes.* P1 needs no footprint analysis, so it can ship safely first.
4. **No project-run service.** The host is a member's own process. No relay, no account and no hosted directory. *Recommended: yes.* This is the "no hosted service" boundary of R108.
5. **Network stance: accept N1 as amended, N9′, and the new N10 and N11 (§5).** *Recommended: yes.* The alternative is no sessions at all.
6. **Transport: TLS 1.3 over TCP through `QSslServer` and `QSslSocket`.** No WebSocket and no QUIC in M10. *Recommended: yes.*
7. **Identity: a device key in the OS store; device ID as the SHA-256 of the certificate; single-use invites pinned to the device ID; no accounts.** *Recommended: yes.*
8. **Leases: advisory; 30 seconds without a heartbeat expires them; the host can force release with notice; the outbox is kept.** *Recommended: yes.*
9. **Session limits: at most 8 members; one batch in flight per client; editor and viewer roles.** *Recommended: yes.*
10. **Excluded from M10 sessions: terrain edits, imports above the replication budget, and assistant batches.** Each is refused with a named error. *Recommended: yes.* They are whole-tile or whole-document replacements, and they exceed the result cap.
11. **Undo: per member, as inverse commits. Single-user undo is unchanged.** *Recommended: yes.*
12. **Host loss: no automatic host migration in M10.** A replacement host can be rebuilt from a member's replica by a deliberate user action, designed in R108.c. *Recommended: yes.*
13. **Reach: LAN and VPN addresses only in M10. No NAT traversal and no relay.** *Recommended: yes.*
14. **Dependencies: no CRDT library, no Rust toolchain and no new Qt module in R108.a–c. json-joy is excluded.** *Recommended: yes.* json-joy's latest release is AGPL-3.0-only.

## Sources

Each page below was read on 2026-10-26, unless marked otherwise. "Search summary" means I did not fetch the page; the claim is from a search result and is not relied on for a decision.

**Repository**

- Code at `e51f5959` (origin/main), read 2026-10-26: `src/core/model.cpp`, `src/core/model.hpp`, `src/core/prepared_edit.cpp`, `src/core/component_records.hpp`, `src/core/tags.cpp`, `src/core/materials.cpp`, `src/automation/commands.cpp`, `src/automation/transaction_coordinator.cpp`, `src/io/outcome_store.hpp`, `src/io/recovery.cpp`, `src/io/document_io.cpp`, `src/io/native_format.hpp`, `src/app/native_mcp.cpp`, `CMakeLists.txt`.
- ADRs: [0004](0004-topology.md), [0005](0005-document-protocol.md), [0011](0011-managed-assets.md), [0015](0015-recovery-storage.md), [0016](0016-native-recovery.md), [0022](0022-private-staging.md)–[0026](0026-headless-session.md), [0034](0034-openai-provider.md), [0053](0053-geometry-diagnostics.md), [0070](0070-saved-scene-records.md), [0108](0108-public-native-format.md), [0144](0144-seeded-transaction-sequences.md), [0163](0163-documentation-sheets.md), [0164](0164-sheet-layout-interface.md), [0165](0165-constrained-component-parameters.md), [0168](0168-terrain-import-and-surfaces.md), [0169](0169-terrain-editing-drape-stamp.md), [0173](0173-camera-paths-batch-output.md), [0179](0179-hosted-asset-access-feasibility.md).
- [R041b evidence](../verification/R041b-durable-outcomes.md): measured commit and checkpoint costs.
- [PR_ROADMAP](../PR_ROADMAP.md), [SCOPE](../SCOPE.md), [BUILD_PLAN](../BUILD_PLAN.md), UX_DESIGN.

**Products**

- Figma, "How Figma's multiplayer technology works", dated 2019-10-16. <https://www.figma.com/blog/how-figmas-multiplayer-technology-works/>
- Onshape Help, "Merging" (English; no date shown). <https://cad.onshape.com/help/Content/Document/merging.htm>
- SketchUp Help, "Connect and collaborate" (the page shows last updated 2026-08-21). <https://help.sketchup.com/en/connect-and-collaborate>. The real-time-viewing statement is a search summary, not fetched.
- Blender Multi-User, "Key Features". <https://multi-user.readthedocs.io/en/latest/about/features.html>. No version shown on the page.
- Blender Extensions, Multi-User listing (GPL v3 or later; version 0.8.2). <https://extensions.blender.org/add-ons/multi-user/>. The listing's dates disagree with each other.
- FOSDEM 2021, "CADCloud". <https://archive.fosdem.org/2021/schedule/event/cadcloud/>

**Libraries and licences**

- Automerge repository. <https://github.com/automerge/automerge>. LICENSE (raw): <https://raw.githubusercontent.com/automerge/automerge/main/LICENSE>. npm, `@automerge/automerge` latest: <https://registry.npmjs.org/@automerge/automerge/latest>.
- y-crdt repository. <https://github.com/y-crdt/y-crdt>. LICENSE (raw): <https://raw.githubusercontent.com/y-crdt/y-crdt/main/LICENSE>.
- npm, `yjs` latest. <https://registry.npmjs.org/yjs/latest>
- npm, `json-joy` latest. <https://registry.npmjs.org/json-joy/latest>. Repository: <https://github.com/streamich/json-joy> (licence text not captured). `master` LICENSE returned 404.

**Literature**

- Kleppmann and Beresford, "A Conflict-Free Replicated JSON Datatype", submitted 2016-08-13. <https://arxiv.org/abs/1608.03960>
- Sun, Sun, Agustina and Cai, "Real Differences between OT and CRDT under a General Transformation Framework for Consistency Maintenance in Co-Editors", submitted 2019-05-02. <https://arxiv.org/abs/1905.01518>. Abstract only.
- Ink & Switch, "Local-first software", dated April 2019. <https://www.inkandswitch.com/essay/local-first/>
- Imine et al., ECSCW 2003 (TP1 and TP2). Not read: the PDF returned no extractable text. No claim here relies on it.
- Shapiro et al., INRIA RR-7506 (2011). Not read: access denied on 2026-10-26. No claim here relies on it.
- Sun and Ellis, CSCW 1998. Not read. No claim here relies on it.

**Standards and platform**

- Syncthing, "Device IDs". <https://docs.syncthing.net/dev/device-ids.html>
- RFC 9846, TLS 1.3 (obsoletes RFC 8446). <https://www.rfc-editor.org/rfc/rfc9846>
- RFC 6455, The WebSocket Protocol (December 2011). <https://www.rfc-editor.org/rfc/rfc6455>
- RFC 9000, QUIC (2021). <https://www.rfc-editor.org/rfc/rfc9000>
- Noise Protocol Framework, revision 34 (2018-07-11). <https://noiseprotocol.org/noise.html>
- Qt 6.8.9, Qt Network C++ classes. <https://doc.qt.io/qt-6.8/qtnetwork-module.html>
- Qt WebSockets module (documents Qt 6.12.0). <https://doc.qt.io/qt-6/qtwebsockets-index.html>
