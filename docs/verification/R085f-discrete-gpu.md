# R085.f — Available AMD GPU and exploratory measurements

2026-10-07. The owner provided a second CachyOS system, and authenticated
inspection confirms accelerated Radeon OpenGL with 16 GiB VRAM. Discrete hardware
availability is no longer a blocker. The [environment and artifact record](R085f-discrete-hardware.json)
retains exact versions, identifiers, resource limits and report hashes.

The system reports **QEMU virtualization**, a Ryzen 9 9900X CPU with 12 exposed
single-threaded cores, XFCE/X11, Qt 6.11.2, running kernel 7.2.7-1-cachyos and
Mesa 26.3.0-devel (`8ea6e9dd41`). The installed kernel package is newer than the
running kernel; the PCI inspection's missing-libkmod warning is retained. Rendering
is accelerated through the already loaded `amdgpu` driver. PCI subsystem naming
says RX 6900 XT; Mesa reports RX 6800 XT (Navi 21). Both readings are recorded
without claiming a more precise physical board identity. Hypervisor host load and
GPU assignment configuration were not independently inspected.

The exact retained Release benchmark from the [R082.a baseline](R082a-real-model-baseline.md)
(source `b4652b96df6bd638a33273384b0602842fb8da02`, executable SHA-256
`e7d6a6f9fbe20683f62061b4dfa2bdad3ce2db2ef9c25e9e9ee56ea0572a5df5`)
was copied and verified on the second system. No build, package installation,
desktop configuration change or display wake was required. This is the earlier
baseline executable, not a current release-candidate binary or the subsequent
component/file-worker optimizations.

A [25-instance smoke test](R085f-amd-smoke.json) passed before three sequential
1,000-instance / 100,000-triangle measurements. Each asserts the complete
1920 × 1080 framebuffer, geometry picks, stationary mesh/upload reuse, 100
edit/undo/redo operations, stable cache capacity and no final OpenGL error.
The host window is not clipped. All full runs reproduce fixture SHA-256
`c874449f6143db01040c1b0df5f26dbc0f90daee66cd2635453eeac73c11376d`.

| Run | Scene GPU-complete p95 | Pick p95 | Edit + readback p95 | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| [1](R085f-amd-1000-1.json) | 2.244 ms | 0.060 ms | 30.863 ms | 459,374,592 B |
| [2](R085f-amd-1000-2.json) | 2.385 ms | 0.061 ms | 30.652 ms | 459,010,048 B |
| [3](R085f-amd-1000-3.json) | 2.969 ms | 0.063 ms | 30.435 ms | 459,403,264 B |

These are exploratory observations, not end-to-end frame feedback or a release
pass. Scene timing includes `glFinish` but excludes painter overlays/compositor
presentation; edit timing includes framebuffer readback. Each run takes 50 samples
after 10 warmups. The earlier Intel run used different display/driver conditions
and concurrent local compilation, so these results are not a controlled GPU-speed
comparison. The virtualized host's workload is unknown.

Each remote run uses a transient user service with a verified 4 GiB memory cap,
zero extra swap, one CPU quota and affinity, 128 tasks and a five-minute deadline.
Temporary files and shader cache live on persistent disk under a dedicated test
directory. The full run JSONs, stderr, exit status and kernel-controller evidence
are retained locally and remotely. The laptop package build continued on the
separate machine; no local benchmark competed with it. All four remote runs exited
zero, and the services ended. These explicit safety ceilings are part of the
measurement conditions, not newly frozen reference-performance budgets.

The unsupported 10,000-instance fixture was not rerun: its known document-limit
failure remains open and was not replaced by the smaller scene. Current-candidate
measurements, full fixture/timing coverage, controlled reference conditions,
frozen memory budgets, native Wayland/platform coverage, CI and ordered merges
remain required for R082/R085 and release acceptance.
