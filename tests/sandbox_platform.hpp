#pragma once
// What the helper sandbox tests expect of each platform's layers (ADR 0183; src/sandbox). On Linux,
// Landlock refuses a path with EACCES and the seccomp filter kills an unlisted call (an exec, a
// ptrace) with SIGSYS. On macOS the policy is one Seatbelt profile: it refuses a path, an exec and
// every other action with EPERM, and never kills. The attack probes check the refusal each
// platform makes.
#include <cerrno>
#include <csignal>
#include <string>

#ifdef __APPLE__
#include <sandbox.h>
#endif

namespace sandbox_platform {

#ifdef __APPLE__
inline constexpr int refusedPath = EPERM;
inline constexpr const char *refusedPathName = "EPERM";
#else
inline constexpr int refusedPath = EACCES;
inline constexpr const char *refusedPathName = "EACCES";
#endif

// A call the policy does not name (an exec, a fork without a clone rule, a ptrace): on Linux the
// seccomp filter kills the process with SIGSYS; on macOS Seatbelt refuses it with EPERM, and the
// probe returns 0 when the call failed with EPERM.
inline bool unlistedRefused(bool signaled, int signal, int code) {
#ifdef __APPLE__
    (void)signal;
    return !signaled && code == 0;
#else
    (void)code;
    return signaled && signal == SIGSYS;
#endif
}

#ifdef __APPLE__
inline constexpr const char *unlistedRefusal = "refused with EPERM";
#else
inline constexpr const char *unlistedRefusal = "killed with SIGSYS";
#endif

// The C.UTF-8 locale data the helpers that read a locale are granted.
#ifdef __APPLE__
inline constexpr const char *localeFile = "/usr/share/locale/C.UTF-8/LC_CTYPE";
#else
inline constexpr const char *localeFile = "/usr/lib/locale/C.utf8/LC_CTYPE";
#endif

#ifdef __APPLE__
// macOS: makes the helper's own install fail for real, as the Linux tests' filter that refuses the
// layers' calls does (ADR 0183 section 6.7). A process inside a Seatbelt profile cannot install
// another (EPERM), so the child that will exec the helper enters a profile that allows everything
// but one write. Call it in the child, before exec. Returns false when the profile was not entered.
inline bool enterOuterProfile() {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
    char *error = nullptr;
    const bool entered =
        sandbox_init("(version 1)(allow default)"
                     "(deny file-write* (literal \"/private/var/empty/sketchyup-outer-profile\"))",
                     0, &error) == 0;
    if (error)
        sandbox_free_error(error);
#pragma clang diagnostic pop
    return entered;
}

// The refusal the helper names under the outer profile.
inline const std::string outerRefusal = "sandboxUnavailable: seatbelt (Operation not permitted)";
#endif

} // namespace sandbox_platform
