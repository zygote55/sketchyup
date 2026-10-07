# Versioned declarative extension manifests

R080.a, 2026-10-07. The initial public extension format is a self-contained UTF-8
JSON `.sketchyext` command package. It declares manifest version 1, command API
version 1 and execution model `command-batch-v1`. It contains metadata, explicit
command capabilities and named actions with typed parameters. Existing extensions
for other applications, native modules, Ruby, arbitrary scripts and executable
entrypoints are outside this format's compatibility claim.

Required fields are exactly `manifestVersion`, `commandApiVersion`, `execution`,
`id`, `name`, `version`, `description`, `capabilities` and `actions`. Identities are
bounded lowercase reverse-domain names; versions use three bounded numeric
components. Capabilities contain exactly a nonempty `commands` array drawn from the
live public command registry. Unknown API/format/execution versions or unavailable
required commands report incompatibility. Duplicate declarations and unknown fields
reject. Additive command discovery is compatible with API v1; removing commands or
incompatibly changing their parameter/transaction meaning requires a new major
command API version. Extension package versions are author-owned and do not select
an application binary version.

Each action contains exactly `id`, `name`, `description`, `parameters` and `commands`.
Each parameter declares `name`, `label`, `type` and `default`; numbers additionally
require finite `minimum` and `maximum`. Supported types are number, boolean and
UTF-8 string up to 256 bytes. Unknown inputs and out-of-range values reject. Exact
`{"$parameter":"name"}` objects substitute typed scalar values; no expressions,
loops, dynamic command names, model references, file paths or executable code are
interpreted. Ordinary command strings remain ordinary data. Nested command objects
must also declare their capability; command selectors cannot be parameters.

Limits: 256 KiB manifest, 32 actions, 32 parameters per action, 100 command
capabilities, 1–100 top-level commands per action, 10,000 expansion nodes, depth 32
and 64 KiB expanded commands. Metadata is bounded and control-free. Retained source
bytes are authoritative even if a caller modifies the parsed public struct.

Resolved actions use the existing versioned public batch API and its document
identity/revision, topology/resource validation and single-edit undo contract.
Manifest parsing does not prove geometry is valid for a particular document.
The sample `examples/extensions/panel.sketchyext` creates a named rectangular face
with width/height in metres, using only `geometry.face`. Tests measure its area,
verify undo/redo and atomic failure, and exercise incompatible versions, undeclared
nested/dynamic commands, parameter types/bounds and malformed manifests.

Installation, enable/disable/error persistence and bounded worker lifecycle follow
in separate R080 layers. The planned worker is the application's own declarative
resolver, not extension-supplied code. Process failure must leave an uncommitted
action unapplied. This format is a command-validation boundary, not a claim that
the application binary itself runs in an operating-system security sandbox.
