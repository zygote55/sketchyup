# Reference hardware availability

2026-10-07. Initially the project owner confirmed that only the current Linux
development machine, with Intel integrated graphics, was available. That blocked
the discrete-GPU portion of R082/R085; the requirement was not waived.

Later the same day, the owner supplied access to a CachyOS system with a discrete
AMD Navi 21 GPU and 16 GiB VRAM. Authenticated inspection confirms an active
XFCE/X11 session and accelerated OpenGL through `amdgpu`/Mesa. The
[discrete-GPU follow-up](R085f-discrete-gpu.md) records the exact environment,
retained benchmark binary and bounded measurements.

Hardware availability no longer blocks that work. The full fixture corpus,
controlled reference measurements, current release-candidate checks and acceptance
remain open. Existing Intel and software evidence retain their original scope.
