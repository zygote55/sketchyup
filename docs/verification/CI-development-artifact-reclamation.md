# Reclaim completed normal CI builds before sanitizers

PR247's native sanitizer check failed when saving its fixture with `No space left on device`. PR252's corresponding step exhausted disk during the final `texture_viewport_tests` link. Both workflows retained the entire completed normal build while building the sanitizer matrix; their original failures and exact captured log hashes remain recorded.

After all normal tests, consumers, recordings and recipes finish, CI now keeps the five normal executables required by the later fractional-scale matrix and their text/extension workers. It archives the normal CTest evidence and SHA-256 identities, removes the completed normal build, restores those seven executables to their original paths and verifies their bytes. Later sanitizer and fractional checks remain in their original order, with identical commands, limits and deadlines. The package cleanup remains after the final consumers. Application and test source bytes are unchanged.

Four isolated cleanup checks passed under 512 MiB, one CPU and no swap (13.2 MiB measured peak). A complete synthetic build retains every later consumer byte for byte, archives the original CTest log and leaves an unrelated sanitizer directory untouched. Missing workers, preexisting staging and a symlinked build each fail before any file mutation. The declared later workflow consumers match the retained executable set. These checks validate cleanup and preservation; they do not replace fresh full CI or claim an exact space saving on a hosted runner.

[Qualification, unchanged source inputs and original failure identities](CI-development-artifact-reclamation.json).
