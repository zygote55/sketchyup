# Bounded extension action worker

R080.c, 2026-10-07. The application ships `sketchyup-extension-worker`, a headless
helper that resolves one declarative command action per process. Discovery uses
only the adjacent build helper or the installed `lib/sketchyup` helper. Extension
manifests cannot select a binary, arguments, working directory, environment or shell.
The helper receives the installed manifest, action identity and scalar parameters;
it receives no document snapshot, model path, provider configuration or credentials.

Protocol version 1 accepts one bounded JSON request on stdin: `protocol`, canonical
Base64 `manifest`, `action` and `parameters`. Maximum request size is 384 KiB,
parameters 16 KiB, manifest 256 KiB and action identity 64 characters. Successful
output contains exactly `protocol`, `ok`, `manifestSha256`, `action` and `commands`.
Errors contain `protocol`, `ok` and a bounded error. Input is read in bounded chunks;
output is limited to 96 KiB and diagnostic stderr to 4096 bytes. The default deadline
is ten seconds with an implementation maximum of thirty seconds. Timeout,
interruption, failed start, abnormal/nonzero exit, malformed output and budget
violations terminate the attempt without returning commands.

The parent independently validates response identity and compares the returned
batch with deterministic resolution of the retained manifest and parameters.
A compromised/mismatched helper response cannot substitute undeclared commands.
The child uses a minimal environment containing PATH/LANG and explicit sanitizer
options for verification; it inherits neither provider credentials nor desktop
plugin configuration. No extension-supplied executable code is run. This is a
process-failure and command-validation boundary, not an OS sandbox for the installed
application binary.

The caller must retain the document identity/revision across the asynchronous
attempt, confirm the package remains enabled, and publish the verified batch via
the existing public atomic command API. Worker completion alone is not a commit.
Native lifecycle integration follows in R080.d.

Tests run the actual helper and compare resolved commands, then inject failed
start, nonzero exit, abrupt SIGKILL termination, malformed JSON, stdout/stderr
floods, timeout, QThread cancellation and a forged successful response. All return
an error without executable commands. Controlled SIGKILL tests produce no core dump.
