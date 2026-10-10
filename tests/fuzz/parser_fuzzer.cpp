#include "parser_harness.hpp"
#include <cstdint>

namespace {
struct Runtime {
    int argc{1};
    char name[18]{"sketchyup-fuzzer"};
    char *argv[2]{name, nullptr};
    QCoreApplication app{argc, argv};
    QTemporaryDir scratch;
    Runtime() { sketchy::fuzz::require(scratch.isValid(), "private parser scratch"); }
};
} // namespace
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, size_t size) {
    if (size > sketchy::fuzz::inputLimit)
        return 0;
    static Runtime runtime;
    sketchy::fuzz::parseInput(QStringLiteral(SKETCHYUP_FUZZ_FORMAT),
                              QByteArray(reinterpret_cast<const char *>(data), qsizetype(size)),
                              runtime.scratch.filePath("input.glb"));
    // Rejections also contribute coverage, including unsafe paths and resource guards.
    return 0;
}
