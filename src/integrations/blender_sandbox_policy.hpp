#pragma once
// R109 S5 (ADR 0183 sections 4.4, 5 and 11): the sandbox of the Blender job's child. The job
// prepares this policy in the parent before it forks, and the child installs it and then execs
// Blender. The policy is one for every mode the job runs (probe, render, preview, encode and
// handoff), because the attempt directory and the inputs differ only by path.
#include "sandbox/sandbox.hpp"

#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <vector>

namespace sketchy::blender_sandbox {

// The paths of one attempt. Empty paths grant nothing.
struct Layout {
    QString executable;       // Blender's binary, canonical: the only program the child may execute
    QString attemptDirectory; // read and write: HOME, the XDG directories, TMPDIR and the results
    QString inputDirectory; // read: the render snapshot (scene.glb, manifest.json, environment.hdr)
    QStringList inputFiles; // read: the frames of a video encode
};

// The exit code of a child whose sandbox layer could not be installed (ADR 0183 section 7).
inline constexpr int kRefusedExit = 70;

// The Landlock rules and the seccomp allow-list of one attempt (ADR 0183 section 5, Blender).
sketchyup::sandbox::Policy policy(const Layout &layout);

// The allow-list alone: the calls the trace of the real headless renders needs, with the sockets
// and the calls a helper has no need of refused (EPERM). A call that is not named is killed
// (SIGSYS).
std::vector<sketchyup::sandbox::SyscallRule> syscalls();

// The environment the child runs with: the caller's environment without the variables that name
// the user's session, the loader or Blender's own paths, plus HOME, the XDG directories and TMPDIR
// inside the attempt directory, and ALSOFT_DRIVERS=null (no OpenAL backend, so no audio socket).
// The directories are created by the caller (sandboxDirectories lists them).
QProcessEnvironment environment(QProcessEnvironment base, const QString &attemptDirectory);

// The directories inside the attempt directory that environment() names, relative to it.
QStringList sandboxDirectories();

} // namespace sketchy::blender_sandbox
