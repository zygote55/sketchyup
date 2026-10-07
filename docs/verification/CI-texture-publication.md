# CI correction — consume decoded textures before fresh frames

2026-10-07. Regression `210f15a`; correction `e86f6da` (backports `0ad24ad`, `58ac44c`).

[PR #177 run 37586631216](https://github.com/zygote55/sketchyup/actions/runs/37586631216)
failed the solar alpha-cutout check at head `b7fb21370b58484aecc1d3ea43ad8727be85ebd9`.
The decoder had published its complete image, but the viewport's 25 ms polling timer
had not yet invalidated the appearance cache. A directly requested fresh frame could
therefore use stale opaque fallback geometry despite decoding being complete.

The regression queues decoding behind a controlled worker, renders the fallback,
releases the worker and requests a fresh frame without pumping GUI timers. It fails
before the correction with “Fresh frame consumes texture alpha before the refresh
timer” (`build/ci-texture-before.log`, native verification worktree). The original
solar fixture is byte-identical between PR #164 and the failing PR #177.

Every fresh scene render now compares the published image snapshot before deciding
whether to rebuild appearances. Texture synchronization also invalidates appearance
and picking when it adopts a new snapshot, covering raster export's direct sync path.
Geometry remains immutable and unchanged images retain their caches. The polling
timer still schedules unattended repaint; it no longer controls fresh-frame readiness.

The [eight-case solar matrix](CI-texture-publication-matrix.json) passes Wayland/X11
at 1×/2×, normal **19.085 s**, ASan/UBSan **26.776 s**, with leak detection and
halt-on-error. [Five regressions](CI-texture-publication-regressions.json) pass native
Wayland 2× in **21.295 s**: textures and raster export in both configurations, plus
normal viewport rendering, picking, lifecycle and incremental-cache checks.

This correction is propagated through the pending dependency stack. Earlier feature
package hashes remain historical integration evidence; remote CI must build/package
each corrected head. The broader integration is revalidated on the corrected source.
This record does not claim hardware, provider, or milestone acceptance.
