#pragma once
// The fault state persistence_tests arms and its hooks (persistence_hooks.cpp) read.
namespace persistence_faults {
extern int failSync, syncError, directorySyncs, killDirectory;
extern bool failWrite, killFile, failFileSync;
} // namespace persistence_faults
