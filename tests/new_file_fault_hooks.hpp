#pragma once
// The fault state new_file_fault_tests arms and its hooks (new_file_fault_hooks.cpp) read.
namespace new_file_faults {
enum class Fault {
    None,
    NoSpace,
    ShortWrite,
    FileSync,
    Link,
    DirectorySync,
    RetrySync,
    RacingWriter,
    KillBeforePublication,
    KillAfterPublication
};
extern Fault fault;
extern int writes, syncs;
} // namespace new_file_faults
