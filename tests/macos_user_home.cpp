// macOS only: each test process gets its own empty home for CoreFoundation, so QStandardPaths
// (Application Support, Caches, Documents and the rest) never reaches the developer's ~/Library.
// The Linux tests keep those folders out of the real home with XDG_DATA_HOME and HOME, which
// QStandardPaths ignores on macOS; it follows CFFIXED_USER_HOME instead. A child process inherits
// its parent's home, and the process that made the home removes it when it exits.
#include <cstdlib>
#include <filesystem>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {
struct TestUserHome {
    std::string path;
    pid_t owner{};
    TestUserHome() {
        if (std::getenv("CFFIXED_USER_HOME"))
            return;
        std::string pattern =
            (std::filesystem::temp_directory_path() / "sketchyup-test-home-XXXXXX").string();
        if (!::mkdtemp(pattern.data()))
            std::abort();
        path = pattern;
        owner = ::getpid();
        ::setenv("CFFIXED_USER_HOME", path.c_str(), 1);
    }
    ~TestUserHome() {
        if (path.empty() || ::getpid() != owner)
            return;
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
} testUserHome;
} // namespace
