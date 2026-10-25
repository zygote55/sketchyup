# R102.a (ADR 0176): the .skp helper. Built by default, with no Qt: it is the one executable that
# links the vendored OpenSKP parser subset (decision 2), and the application talks to it through
# the P / R protocol and the SKSKPP01 package (io/skp_package.hpp), never through the parser.
# The package writer and the import are a library so the tests can run the writer and the
# checked decoder (sketchyup_io) against it; the executable is the worker around them.
add_library(sketchyup_skp ${SKETCHYUP_LIBRARY_TYPE}
  import.cpp
  package.cpp
  report.cpp
  sha256.cpp)
target_include_directories(sketchyup_skp PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sketchyup_skp PUBLIC sketchyup_openskp)
target_compile_options(sketchyup_skp PRIVATE -Wall -Wextra -Wpedantic)
add_executable(sketchyup-skp-worker worker_main.cpp)
target_include_directories(sketchyup-skp-worker PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(sketchyup-skp-worker PRIVATE sketchyup_skp sketchyup_version)
target_compile_options(sketchyup-skp-worker PRIVATE -Wall -Wextra -Wpedantic)
if(SKETCHYUP_SANITIZE)
  # The sanitizer's reservations exceed the 4 GiB address ceiling; the tests run the helper
  # without it (the IFC tests do the same with IFC_NO_ADDRESS_LIMIT).
  target_compile_definitions(sketchyup-skp-worker PRIVATE SKP_WORKER_NO_ADDRESS_LIMIT=1)
endif()
install(TARGETS sketchyup-skp-worker RUNTIME DESTINATION lib/sketchyup)
