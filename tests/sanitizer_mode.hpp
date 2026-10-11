#pragma once
// True when the test binary is built with AddressSanitizer (GCC defines
// __SANITIZE_ADDRESS__; Clang reports it through __has_feature). Long tests use
// it to run a reduced case set in the serial sanitized CI job.
#if defined(__SANITIZE_ADDRESS__)
#define SKETCHYUP_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define SKETCHYUP_TEST_SANITIZED 1
#endif
#endif
#ifndef SKETCHYUP_TEST_SANITIZED
#define SKETCHYUP_TEST_SANITIZED 0
#endif
namespace sketchy::test {
inline constexpr bool sanitized = SKETCHYUP_TEST_SANITIZED != 0;
}
