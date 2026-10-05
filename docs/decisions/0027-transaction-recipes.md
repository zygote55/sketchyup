# Bounded transaction recipes

R042.b adds `sketchyup-cli --recipe FILE` with the same explicit model, outcome
store, recovery and save scope as a headless session. It executes ordinary
registered session requests sequentially. There is no expression language, shell,
network access, loop or arbitrary file operation. The legacy `--script` command
array remains a separate local batch interface.

## Format and references

A recipe is an API-version-1 object containing `steps`, an array of 1–1,000 objects
with exactly `id` and `request`. Step IDs are unique ASCII letters, digits,
underscores or hyphens, at most 64 characters. Requests are object templates for
the shared session API. The complete recipe is parsed and checked before opening
or creating a model; malformed structure, duplicate IDs and forward references
cannot have model side effects.

An exact object `{"$ref":"previous-step#/result/documentId"}` substitutes a typed
value from an earlier complete response. The portion after `#` is a JSON pointer:
object keys use `~0` for tilde and `~1` for slash; array indexes are canonical
nonnegative decimals within bounds. An empty pointer selects the whole response.
References are at most 512 characters and cannot target the current/future step.
A missing field, invalid index or traversal through a scalar fails explicitly.

An exact object `{"$literal":VALUE}` inserts its value without expanding tags.
It can escape data containing `$ref` or `$literal`. Tagged objects cannot have
additional fields. Ordinary strings never interpolate. Substitution preserves
numbers, booleans, nulls, arrays and objects instead of converting them to strings.
Resolved requests still pass the authoritative operation/query schema and core
validation. Template checks do not claim that a future referenced value has the
correct type or that geometry will be valid.

## Effects and failure

Each successful step emits the normal compact `{id, ok, result}` JSON-line reply.
The first dispatch or reference failure emits a structured error, stops later
steps, closes uncommitted staging and exits nonzero. All steps succeeding exits
zero. The recipe as a whole is not atomic: every explicitly committed transaction
and confirmed save remains complete if a later step fails. A lost process/output
connection still requires durable outcome reconciliation; never infer absence of
a commit from missing stdout. Response correlation IDs are not durable retry keys.

The shipped [face recipe](../../examples/transaction-face-recipe.json) discovers
the bound identity/revision, begins a task, creates a 2 × 3 m face, inspects its
provisional ID, seals, commits once, saves to the bound destination and measures
the final world area. Both measurements are 6 m². A fresh model has a new identity;
geometry and measurements, rather than random document IDs, are deterministic.

```sh
sketchyup-cli --recipe examples/transaction-face-recipe.json \
  --new --output /tmp/face.sketchyup --outcomes /tmp/face-outcomes
```

The output must not already exist for `--new`. Use `--input` for an existing model.
Repeated modeling against an existing saved model is new work, not a retry of the
previous recipe. To resolve a prior accepted commit, use its durable request
ID/hash through the session API.

## Bounds and discovery

Input is at most 1 MiB with at most 64 nesting levels. A resolved request is at
most 64 KiB; inspection retains its stricter limit. Expansion charges keys,
structure and referenced values before inserting them into the request. Repeated
references cannot build an arbitrarily large intermediate object before a final
size check. A conservative construction charge may reject a request close to the
limit even when its final compact encoding would be slightly smaller.

The runner charges retained replies at twice their compact JSON size plus bounded
per-step overhead, with an 8 MiB aggregate limit. This is conservative accounting,
not a process RSS limit. Output is at most 8 MiB. Before each operation, reserve a
maximum 1 MiB reply against output capacity and twice that amount plus overhead
against retention. Keep 16 KiB for a terminal error. Budget failure therefore
happens before accepting the next operation; the runner does not commit a step
and then discover that it has no budget for its receipt. Smaller internal limits
are available for embedding and verification.

`--recipe-capabilities` prints the strict recipe envelope schema, reference rules,
limits and partial-effect semantics without opening any model. The generated
`recipe-v1.json`, this contract and example are installed with the application.
