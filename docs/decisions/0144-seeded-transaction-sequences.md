# Seeded transaction failure sequences

R083.c, 2026-10-07. Eight deterministic seeds each execute 32 transitions through
real staged transactions and the durable outcome store. The first eight transitions
cover every mode; subsequent choices and translation amounts are seeded. A failing
run reports seed, step and mode and accepts `--seed N` for reproduction.

The modes are commit, cancellation, intervening edit/undo, and interruptions before
write, after write, after file sync, after rename and after directory sync. Private
fault callbacks are existing test infrastructure; production behavior is unchanged.
Before replacement, an interrupted commit must remain pending and retryable. After
possible replacement, it must remain unknown, block mutation and reconcile without
command replay. Terminal receipts are immutable under duplicate commit, late cancel,
undo/redo and coordinator restart.

An independent summed-translation oracle and untouched neighboring geometry check
accepted edits. Every transition passes native container validation and exact
roundtrip. Each successful transaction undoes and redoes as one edit. Every eight
transitions, explicit save/reopen and coordinator restart preserve all earlier
terminal receipts and current content. A separately saved original remains byte
identical. Normal and sanitizer runs must produce the same mode/result counts.

This bounded corpus complements parser corruption, syscall publication faults,
existing geometry fuzzing, recovery corruption and process-kill tests. It is not
exhaustive state-space exploration or release acceptance. R083 remains open until
the release candidate completes the combined persistence/transaction failure matrix.
