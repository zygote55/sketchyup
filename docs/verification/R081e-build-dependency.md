# R081.e — Declare the build-time verification dependency

2026-10-07. The default CLI test configuration introduced by R081 requires Python
at CMake configure time for generated-reference, executable-recipe and optional
capability checks. Arch packaging now explicitly lists `python` in `makedepends`,
including builds made with `makepkg --nocheck`; it is not an application runtime
dependency. README build prerequisites match that configuration.

Validation: `bash -n` accepts PKGBUILD, and `makepkg --printsrcinfo` on the generated
source package contains `makedepends = python`. The [source package](R081e-source-package.json)
retains 196 byte-exact installed inputs. The recipe/reference/installed tests recorded
in R081.a–d remain applicable because executable source and test scripts are unchanged.
This declaration closes the missing direct build dependency; clean package acceptance
on the final integrated source remains a separate CI gate.
