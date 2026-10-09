# Deterministic parser corruption corpus

R083.a, 2026-10-07. A fixed xorshift seed drives 512 mutations for each of ten
formats: native JSON/container, GLB, binary/ASCII STL, OBJ, DXF, template/component
bundles and extension manifests. Four mutation families exercise truncation,
individual bits, appended bytes and overwritten byte spans. Original seeds must
parse first; each result records its SHA-256, size and accepted/rejected counts.
Every run uses synthetic input and private temporary files, never user documents.

Component capture deliberately creates a fresh standalone identity. The harness
normalizes that synthetic document identity and rebuilds its envelope lengths/hash
before mutating the seed. This changes only the test fixture, so normal/sanitized
runs exercise identical bytes. The original normalized seed must still pass the
real bundle decoder before any corruption begins.

Accepting a mutated input is not automatically an error: changing a name, valid
coordinate or advisory STL normal can leave a valid document. Accepted native,
GLB and library documents must survive canonical container validation/round trip.
Pure geometry parsers must retain finite bounded coordinates and valid vertex
references. Extension parsing must preserve exact validated source and valid
actions; geometry validation remains an execution-stage responsibility.

The harness distinguishes expected parser rejection from failure after acceptance.
Invariant failures and allocation exhaustion fail the test; they are never counted
as successful rejection. Native source bytes must remain unchanged. Normal and
ASan/UBSan runs must reproduce the same corpus outcomes. CTest and CI include the
corpus; sanitizer runs retain leak detection and halt-on-error.

This bounded reproducible corpus supplements format-specific malicious-path,
resource-limit and failure tests. It is not coverage-guided fuzzing and does not
claim exhaustive parser security. Any future finding must be minimized and kept
as an explicit regression. Disk exhaustion, interrupted saves/journals and edit
sequence fault injection remain separate R083 layers.
