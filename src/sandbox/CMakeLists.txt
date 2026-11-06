# R109.a (ADR 0183): the helper process sandbox. PR_SET_NO_NEW_PRIVS, a Landlock ruleset and a raw
# seccomp-bpf allow-list with a kill default, all written in-tree against the kernel's ABI. No
# dependency, and no Qt: the helpers that link it are plain C++.
# On macOS the policy is one Seatbelt profile instead (libsandbox). Elsewhere every layer fails
# closed with ENOSYS, and the helpers refuse.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_library(sketchyup_sandbox ${SKETCHYUP_LIBRARY_TYPE}
    sandbox.cpp
    landlock.cpp
    seccomp.cpp)
elseif(APPLE)
  add_library(sketchyup_sandbox ${SKETCHYUP_LIBRARY_TYPE}
    sandbox.cpp
    seatbelt.cpp
    unsupported.cpp)
  target_link_libraries(sketchyup_sandbox PRIVATE sandbox)
else()
  add_library(sketchyup_sandbox ${SKETCHYUP_LIBRARY_TYPE}
    sandbox.cpp
    unsupported.cpp)
endif()
target_include_directories(sketchyup_sandbox PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_options(sketchyup_sandbox PRIVATE -Wall -Wextra -Wpedantic)
