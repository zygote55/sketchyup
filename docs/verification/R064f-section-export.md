# R064.f — Section-aware GLB and Blender export

2026-10-06, implementation `5efd450` with native result-contract/text follow-ups
`7a60179` and `021fee4`; integrated head `b8d5787`.
[Contract](../decisions/0077-section-export.md). Export derives the same scoped
world-space section mesh as the viewport, retaining source face IDs and independent
front/back texture mapping. Cap entries carry plane identity separately.

All **117 CTest suites passed in 112.92 seconds**, with real Blender explicitly
enabled. Focused ASan/UBSan suites for section export, GLB and the worker passed
in **5.48 seconds**, with leak detection. Both builds emitted no compiler warnings.
Logs: `build/r064f-complete-{build,ctest}.log` and
`build/r064f-sanitize-{build,ctest}.log`.

The native Render workflow passed **eight Wayland/X11 checks at scales 1 and 2**
(**37.998 seconds**), including four ASan/UBSan variants. Checks load the installed
example's source fixture and preserve captured activation while modeling continues.
The result labels omitted section cut-edge lines. Sanitized Wayland uses the
[isolated client reference fix](R062b-wayland-proxy.md), without leak suppressions.
[Native results](R064f-native.json).

A separate actual native render passed on Wayland scale 2 with Blender 5.2.2,
producing a 512×512 verified PNG. Tests check green cap pixels and unchanged
activation. Raw [native window](R064f-native-render.png),
[rendered image](R064f-native-render-image.png) and
[verified worker result](R064f-native-render-manifest.json) were inspected.
Log: `build/r064f-native-render-final.log` in the implementation worktree.

Two fixtures passed the pinned glTF validator with **zero errors and warnings**.
Actual Blender 5.2.1 import/Cycles checks confirm the nested/mirrored quarter,
sibling scope, outward cap normals, independent total top-cap area of 5 m²,
green cap pixels, transparent removed regions, reflected clipped UVs and unchanged
input bytes. [glTF results](R064f-glb-validation.json),
[Blender results](R064f-blender-validation.json).

Core export fixtures also cover hidden source faces opening the contour before
fill, sibling caps remaining intact, explicit unfilled diagnostics, cap fill off,
fully clipped rejection, preserved immutable snapshot state, retained bounds and
independent physical-side UV/normal interpolation. Native geometry is unchanged.
The native free clipping override retains its explicit render rejection contract.

Installed CLI checks export the packaged three-plane example with separate cap
provenance, verified GLB hash and clipped bounds; deactivation exports the original
height with zero caps. Input bytes and geometry remain unchanged. Contract and
desktop binary match the build. [Installed results](R064f-installed-smoke.json).
All **116 install inputs** match the source archive byte-for-byte, excluding build
and Git metadata. [Package results](R064f-source-package.json).

All R064 implementation layers now have local acceptance evidence. Remote CI and
merging the dependency stack remain required before R064 delivery is accepted.
