// The Landlock layer (ADR 0183 sections 3 and 4.2): a ruleset that grants the listed paths and handles
// every filesystem right, so an access outside the grants fails with EACCES. The kernel's ABI is in
// Documentation/userspace-api/landlock.rst; the constants are written here so no header is needed.
#include "sandbox/layers.hpp"
#include "sandbox/syscalls_x86_64.hpp"

#include <cerrno>
#include <cstdint>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

namespace sketchyup::sandbox {
namespace {

constexpr unsigned kCreateRulesetVersion = 1U << 0;
constexpr std::uint32_t kRulePathBeneath = 1;

// The filesystem rights, by bit (ABI 1 has bits 0 to 12; ABI 2 adds refer, ABI 3 truncate). Device
// ioctl (ABI 5) is not handled: ADR 0183 section 4.2 leaves it to a later change.
constexpr std::uint64_t kAccessExecute = 1ULL << 0;
constexpr std::uint64_t kAccessWriteFile = 1ULL << 1;
constexpr std::uint64_t kAccessReadFile = 1ULL << 2;
constexpr std::uint64_t kAccessReadDir = 1ULL << 3;
constexpr std::uint64_t kAccessAbi1 = (1ULL << 13) - 1;
constexpr std::uint64_t kAccessRefer = 1ULL << 13;
constexpr std::uint64_t kAccessTruncate = 1ULL << 14;

// The scopes (ABI 6): abstract UNIX sockets and signals to processes outside the domain. Used when the
// kernel has them; the design does not depend on them (ADR 0183 decision 11).
constexpr std::uint64_t kScopeAbstractUnixSocket = 1ULL << 0;
constexpr std::uint64_t kScopeSignal = 1ULL << 1;

// struct landlock_ruleset_attr: the three words of ABI 6. Older kernels take a shorter prefix.
struct RulesetAttr {
    std::uint64_t handledAccessFs;
    std::uint64_t handledAccessNet;
    std::uint64_t scoped;
};

// struct landlock_path_beneath_attr (packed, as the kernel declares it).
struct __attribute__((packed)) PathBeneathAttr {
    std::uint64_t allowedAccess;
    std::int32_t parentFd;
};

std::uint64_t handledFilesystemRights(int abi) {
    std::uint64_t rights = kAccessAbi1;
    if (abi >= 2) rights |= kAccessRefer;
    if (abi >= 3) rights |= kAccessTruncate;
    return rights;
}

// The rights a rule grants on a path. A file takes only the file rights (the kernel refuses a
// directory right on a file with EINVAL); a directory takes the directory rights as well.
std::uint64_t grantedRights(FileAccess access, bool directory, int abi) {
    const std::uint64_t handled = handledFilesystemRights(abi);
    if (access == FileAccess::readOnly)
        return directory ? (kAccessReadFile | kAccessReadDir) : kAccessReadFile;
    if (directory) return handled & ~kAccessExecute;
    return kAccessReadFile | kAccessWriteFile | (abi >= 3 ? kAccessTruncate : 0);
}

}  // namespace

std::optional<Failure> installLandlock(const std::vector<FileRule>& files) {
    const int abi = landlockAbiVersion();
    if (abi < 1) return Failure{Layer::landlock, errno};

    RulesetAttr attr{handledFilesystemRights(abi), 0, 0};
    std::size_t attrSize = sizeof(std::uint64_t);  // handled_access_fs only (ABI 1 to 3)
    if (abi >= 4) attrSize = 2 * sizeof(std::uint64_t);  // plus handled_access_net (ABI 4 and 5)
    if (abi >= 6) {
        attr.scoped = kScopeAbstractUnixSocket | kScopeSignal;
        attrSize = sizeof(RulesetAttr);
    }
    const long ruleset = syscall(kSysLandlockCreateRuleset, &attr, attrSize, 0L);
    if (ruleset < 0) return Failure{Layer::landlock, errno};

    for (const FileRule& file : files) {
        const int fd = ::open(file.path.c_str(), O_PATH | O_CLOEXEC);
        if (fd < 0) {
            if (errno == ENOENT) continue;  // not there, so nothing is granted
            const int saved = errno;
            ::close(static_cast<int>(ruleset));
            return Failure{Layer::landlock, saved};
        }
        struct stat info {};
        if (::fstat(fd, &info) != 0) {
            const int saved = errno;
            ::close(fd);
            ::close(static_cast<int>(ruleset));
            return Failure{Layer::landlock, saved};
        }
        const PathBeneathAttr rule{grantedRights(file.access, S_ISDIR(info.st_mode), abi), fd};
        const long added = syscall(kSysLandlockAddRule, ruleset, static_cast<long>(kRulePathBeneath), &rule, 0L);
        const int saved = errno;
        ::close(fd);
        if (added != 0) {
            ::close(static_cast<int>(ruleset));
            return Failure{Layer::landlock, saved};
        }
    }

    const long restricted = syscall(kSysLandlockRestrictSelf, ruleset, 0L);
    const int saved = errno;
    ::close(static_cast<int>(ruleset));
    if (restricted != 0) return Failure{Layer::landlock, saved};
    return std::nullopt;
}

int landlockAbiVersion() {
    return static_cast<int>(syscall(kSysLandlockCreateRuleset, nullptr, 0L,
                                    static_cast<long>(kCreateRulesetVersion)));
}

}  // namespace sketchyup::sandbox
