# Package CI budget backport

2026-10-08 UTC. The earlier PR200 job explicitly exceeded GitHub's six-hour
limit during package acceptance: [run 37607638849](https://github.com/zygote55/sketchyup/actions/runs/37607638849).
That is a timeout, not a passing package check or an inferred product crash.

Backport the existing package build option and serial LTO control from source
`64e6e5edbd8d76c8c535fcd6ff76930ff1f3c90c`. Native display fixtures remain in the
default development build and every explicit CI interaction matrix. Only package
builds exclude them from ALL; they are not registered package CTests. Package
acceptance still builds all registered tests and installed executables. Outer
package jobs default to one and remain explicitly configurable; inherited LTO
uses one worker per link. No application or test source changes.

Both CMake configurations generate successfully. Actual Ninja ALL dependencies
contain 250 targets with the default option and 180 for packaging. The difference
is exactly the 70 opt-in display programs. All 174 CTest registrations, names and
properties are identical; all four installed executables remain in ALL.
[Input hashes and configuration results](CI-package-budget-backport.json).
This is configuration evidence, not a full build or execution of 174 tests.

The configuration check takes 5.281 seconds and reports a 199.6 MiB peak under
512 MiB, one CPU, zero swap and disk-backed scratch. The existing heavy clean
package build continues without interruption. Earlier preparation placed the
wrapper inside the CLI guard; review corrected its placement before either
configuration, commit or publication. No failed build result was discarded.

The workflow now runs branch work through pull_request and retains main push
checks. Parsed jobs, steps and permissions remain identical. Existing runs were
not canceled, the six-hour GitHub limit is unchanged, and fresh full CI and
ordered merges remain required. The feature and texture evidence describe their
retained runtime revisions; this backport changes build scheduling only.
