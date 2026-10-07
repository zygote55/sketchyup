# Portable CI disk report

2026-10-07. [PR #176 run 37577773406](https://github.com/zygote55/sketchyup/actions/runs/37577773406)
completed the test phases and removed the completed Debug/sanitizer trees, then
failed at `df -h . /work`: GitHub checked out the repository under `/__w`, and
`/work` exists only in the local verification container. The filesystem report
showed 80 GiB available after cleanup, but the nonexistent second argument returned
exit status 1 and prevented the package gate from starting.

The report now uses only `df -h .`. All workflow triggers, test commands, evidence
copying, cleanup paths and complete package build/test/install/upgrade/removal gate
remain unchanged. A disposable fixture executed the actual corrected cleanup block
under `sh -e`, verified both CTest logs survive, both completed build trees are removed,
unrelated evidence remains and the disk report exits successfully. No live build
tree was used for that fixture. Source archive inputs and their recorded hashes are
unchanged by this workflow/evidence-only correction. Fresh remote checks are required.
