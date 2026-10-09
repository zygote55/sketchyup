# PR223 package build budget

The previous head `4c468c42af795de1ba3eaf94afe057c692b833dc` passed every normal, sanitizer, native interaction and independent consumer step before its [full workflow](https://github.com/zygote55/sketchyup/actions/runs/37725359536) was canceled after six hours during the package rebuild. Its original metadata, final excerpt and exact full-log hash remain recorded. This is not a passing package or release result.

Backport the package configuration already qualified in PR200 (`41e38367ca5f142fd37a594a5474de3059d28adb`) to this later checkpoint. Default development builds still include the native display fixtures, and the explicit native CI matrices still build and run them. Package ALL excludes only its 78 opt-in display programs. All 181 registered tests retain equivalent names, properties and commands, and every registered executable plus all four installed programs remains in ALL. Package outer jobs default to one and inherited LTO uses one compiler worker per link. The package check still runs the complete registered suite; the workflow deadline is unchanged.

Two isolated CMake/Ninja configurations passed under 512 MiB, one CPU and no swap, with compiler capability probes only. Default ALL contains 267 targets; package ALL contains 189. The exact difference is the 78 declared native fixtures. Application and test sources remain byte-identical to the previous head; only CMake, the package recipe and the branch workflow trigger changed. Branch checks run on pull_request, with main pushes retained. These configuration results require a fresh full CI build before merge.

[Source, configuration and retained artifact identities](CI-PR223-package-budget.json).
