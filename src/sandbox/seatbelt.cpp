// The Seatbelt layer (macOS): the policy's file rules and the kinds of call its syscall rules
// allow, written as one sandbox profile that denies everything else. The profile is compiled when
// the policy is prepared and applied by enter(), which only issues the call that installs it. An
// action outside the profile fails with EPERM; Seatbelt has no kill default (ADR 0183, macOS
// notes).
//
// sandbox_compile_string and sandbox_apply are libsandbox's calls behind the public sandbox_init.
// They are not in a header: the declarations below are the ones Chromium uses. The public
// sandbox_init compiles in the calling process, which a child of a multithreaded fork must not do.
#include "sandbox/layers.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <sys/stat.h>
#include <vector>

extern "C" {
struct sandbox_profile;
struct sandbox_params;
sandbox_profile *sandbox_compile_string(const char *profile, sandbox_params *params, char **error);
int sandbox_apply(sandbox_profile *profile);
void sandbox_free_profile(sandbox_profile *profile);
}

namespace sketchyup::sandbox {
namespace {

// A path as the profile's string: quoted, with backslash and quote escaped.
std::string quoted(const std::string &path) {
    std::string text = "\"";
    for (const char c : path) {
        if (c == '"' || c == '\\')
            text += '\\';
        text += c;
    }
    return text + '"';
}

// The filter for one resolved path: a directory grants what is beneath it.
std::string filterFor(const std::string &resolved, bool directory) {
    return std::string(directory ? "(subpath " : "(literal ") + quoted(resolved) + ")";
}

// The filters for one granted path, or none when the path does not exist (as Landlock, it cannot be
// opened, so it grants nothing). Seatbelt matches the path a call resolves to, so a rule names the
// canonical path (/tmp is /private/tmp), and a directory's grant also names the targets of the
// symbolic links beneath it: Homebrew's Qt plugin directory, for one, is a tree of links into each
// module's own directory. Landlock checks the resolved file too; Linux's granted trees have no such
// links. A grant that writes does not follow links: its rule is its own tree only, as Landlock's.
std::vector<std::string> pathFilters(const std::string &path, bool followLinks) {
    namespace fs = std::filesystem;
    std::vector<std::string> filters;
    std::set<std::string> visited;
    std::vector<std::string> pending{path};
    while (!pending.empty()) {
        const std::string next = std::move(pending.back());
        pending.pop_back();
        char resolved[PATH_MAX];
        struct stat status{};
        if (!::realpath(next.c_str(), resolved) || ::stat(resolved, &status) != 0)
            continue;
        const bool directory = S_ISDIR(status.st_mode);
        // A target inside a tree already granted needs no rule of its own.
        const std::string target = resolved;
        bool covered = false;
        for (const std::string &tree : visited)
            covered = covered || target == tree || target.starts_with(tree + "/");
        if (covered)
            continue;
        visited.insert(target);
        filters.push_back(filterFor(target, directory));
        if (!directory || !followLinks)
            continue;
        std::error_code error;
        for (fs::recursive_directory_iterator entry(target, error), end; !error && entry != end;
             entry.increment(error))
            if (entry->is_symlink(error))
                pending.push_back(entry->path().string());
    }
    return filters;
}

} // namespace

std::string profileText(const Policy &policy) {
    std::set<std::string_view> allowed;
    bool fork = false;
    for (const SyscallRule &rule : policy.syscalls) {
        if (rule.action == Action::allow)
            allowed.insert(rule.name);
        if (rule.action == Action::forkOrThread)
            fork = true;
    }
    fork = fork || allowed.count("fork") || allowed.count("vfork") || allowed.count("clone");

    // Metadata is not limited, as Landlock does not limit stat. sysctl reads are what the C
    // library asks for the processor count and page size. A process may signal itself (abort).
    std::string text = "(version 1)\n(deny default)\n"
                       "(allow file-read-metadata)\n"
                       "(allow sysctl-read)\n";
    text += allowed.count("kill") ? "(allow signal)\n" : "(allow signal (target self))\n";
    if (fork)
        text += "(allow process-fork)\n";
    // The loader of a program started under the profile reads the root directory (the directory
    // alone, not what is beneath it) before it maps the shared cache, and aborts without it.
    if (allowed.count("execve"))
        text += "(allow file-read* (literal \"/\"))\n";

    // Executable code may be mapped from any path the policy lets the process read, as on Linux,
    // where Landlock's execute right governs execve and not mmap. A path the policy writes may not
    // be mapped executable.
    for (const FileRule &file : policy.files) {
        const bool writes = file.access == FileAccess::readWrite;
        for (const std::string &filter : pathFilters(file.path, !writes))
            switch (file.access) {
            case FileAccess::readOnly:
                text += "(allow file-read* file-map-executable " + filter + ")\n";
                break;
            case FileAccess::readExecute:
                text += "(allow file-read* file-map-executable " + filter + ")\n";
                if (allowed.count("execve"))
                    text += "(allow process-exec " + filter + ")\n";
                break;
            case FileAccess::readWrite:
                text += "(allow file-read* file-write* " + filter + ")\n";
                break;
            }
    }

    // The network, only as far as the policy allows the socket calls. Every helper refuses them
    // (Action::denied), and the profile's default then refuses them too.
    if (allowed.count("socket") || allowed.count("socketpair"))
        text += "(allow system-socket)\n";
    if (allowed.count("connect"))
        text += "(allow network-outbound)\n";
    if (allowed.count("bind"))
        text += "(allow network-bind)\n";
    if (allowed.count("listen") || allowed.count("accept") || allowed.count("accept4"))
        text += "(allow network-inbound)\n";
    return text;
}

Profile::~Profile() {
    if (compiled)
        sandbox_free_profile(static_cast<sandbox_profile *>(compiled));
}

std::optional<Failure> buildProfile(const Policy &policy, Profile &profile) {
    // The names are checked as the seccomp layer checks them, so a policy that Linux refuses is
    // refused here too: a name not in the table, an argument past the sixth, or one call named
    // twice with two rules.
    std::map<std::string_view, const SyscallRule *> seen;
    for (const SyscallRule &rule : policy.syscalls) {
        if (!syscallNumber(rule.name) || rule.argumentIndex > 5)
            return Failure{Layer::seatbelt, EINVAL};
        const auto [entry, fresh] = seen.emplace(rule.name, &rule);
        const SyscallRule &previous = *entry->second;
        if (!fresh &&
            (previous.action != rule.action || previous.argumentIndex != rule.argumentIndex ||
             previous.argumentValue != rule.argumentValue || previous.values != rule.values))
            return Failure{Layer::seatbelt, EINVAL};
    }
    char *error = nullptr;
    profile.compiled = sandbox_compile_string(profileText(policy).c_str(), nullptr, &error);
    if (!profile.compiled) {
        std::free(error);
        return Failure{Layer::seatbelt, EINVAL};
    }
    return std::nullopt;
}

std::optional<Failure> applyProfile(Profile &profile) noexcept {
    if (!profile.compiled)
        return Failure{Layer::seatbelt, EBADF};
    if (sandbox_apply(static_cast<sandbox_profile *>(profile.compiled)) != 0)
        return Failure{Layer::seatbelt, errno ? errno : EPERM};
    return std::nullopt;
}

} // namespace sketchyup::sandbox
