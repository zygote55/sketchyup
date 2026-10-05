# Native MCP inspection binding

R043.b binds MCP to a deliberately opened native window. Launch with an existing
model and a socket in an existing user-owned directory with mode 0700:

```sh
mkdir -m 700 /tmp/sketchyup-mcp-example
sketchyup --mcp-inspection-socket /tmp/sketchyup-mcp-example/editor.sock model.sketchyup
sketchyup-cli --mcp-connect /tmp/sketchyup-mcp-example/editor.sock
```

The second command is the stdio executable/arguments to configure in a local MCP
client. `sketchyup --mcp-inspection-capabilities` prints the exact native catalog.
The protocol and per-request metadata are the same MCP 2026-07-28 contract as
[the headless binding](0028-local-mcp.md). The bridge opens no model and accepts no
other model, save, recipe or session options.

## Scope and capabilities

The service pins the original document **session**, including its in-memory
ownership stamp. Normal edits, selection changes and saves are permitted. Opening
another document, including reopening the same file with the same persistent
identity, closes the listener and connected clients. A new explicit launch is
required to expose that document. It never attaches to an ambient window.

The native catalog contains `session.describe`, shared bounded inspection and
snapshot queries, and `view.describe`. Selection queries and measurements use the
actual viewport selection and context. Each client owns its own snapshot retention.
The document-state resource contains identity, revision, dirty state, capability
flags and an opaque hash of selection, hidden/locked entities, context and view
state. A 50 ms GUI timer coalesces external changes into subscription notifications.
Equal-sized selections still change the hash. Notifications signal that clients
should requery; they do not contain geometry or an unbounded event history.

This binding advertises no mutation, save or transaction tools. Native transaction
publication requires the assistant preview/coordinator ownership integration in
R046. The headless binding already exposes staged transactions. `view.capture` is
also excluded because its existing 4 MiB envelope exceeds the MCP reply budget.
The backend enforces its catalog independently of the protocol tool name; mixed
`operation` and `query` discriminators are rejected by the shared dispatcher.

Explicit MCP launch suppresses startup units/recovery dialogs that could replace
the requested model or obstruct automation. Ordinary desktop startup is unchanged.
Automatic recovery still starts for subsequent human edits. Client disconnect
never closes the window or discards human edits.

## Transport and bounds

Only Linux named Unix sockets are supported: no TCP, abstract namespace or remote
listener. The canonical socket parent must be user-owned with permissions 0700.
Existing endpoints, including symlinks, are rejected without removal. Qt creates a
user-access-only endpoint. Both service and bridge check Linux `SO_PEERCRED` for
the current effective UID. This provides a local account boundary, not isolation
from other programs running under that same account.

At most two clients are admitted, each with the existing 32 MiB snapshot budget
and eight subscription limit. The service bounds buffered input to 66 KiB plus
framing detection bytes, each response to 1 MiB and queued output to 2 MiB. It
processes one input line at a time and waits for pending socket writes to drain
before dispatching more or publishing coalesced changes. Qt socket I/O is
asynchronous on the document's GUI thread; no blocking wait or worker accesses the
model. The CLI bridge uses nonblocking descriptors and `poll`, with bounded input
and a 1 MiB stdout buffer. Kernel socket/pipe buffers are additional finite OS
buffers, not part of the application retention budget. A slow reader applies
backpressure without growing a GUI response queue.

On stdin EOF, the bridge completes any final unterminated line and sends a private
transport notification `notifications/sketchyup/close`. The service completes any
remaining subscriptions, flushes queued output asynchronously, then disconnects.
This notification is a local bridge control, not a public modeling tool. Abrupt
client death discards only its private read-only inspection snapshots. A replaced
document closes the socket immediately; the bridge exits unsuccessfully rather
than pretending that the old scope remains available.

The Qt socket implementation follows the official [QLocalServer](https://doc.qt.io/qt-6/qlocalserver.html)
and [QLocalSocket](https://doc.qt.io/qt-6/qlocalsocket.html) contracts. The executable
and installed catalog are the source of truth for this binding's narrower tools.
