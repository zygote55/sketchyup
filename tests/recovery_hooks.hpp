#pragma once
// The fault state recovery_tests arms and its hooks (recovery_hooks.cpp) read.
namespace recovery_faults {
extern int syncs, failSync, killSync;
extern bool fullDisk, partialWrite;
} // namespace recovery_faults
