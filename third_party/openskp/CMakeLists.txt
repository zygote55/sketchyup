# R102.a (ADR 0176): the vendored OpenSKP parser subset (VENDOR.txt and the patches/ directory
# record the pinned commit, the SHA-256 of every file, and the patches; scripts/vendor-openskp.py
# fails the build if this tree has drifted from the pinned commit).
#
# The build mirrors the upstream CMakeLists (packages/cpp) for the parser subset only: no
# exporters, no writer, no scene builder, no tinygltf, no flatbuffers.
# miniz is C; the top-level project enables only CXX. enable_language(C) is idempotent: without
# it, a re-run of CMake (with CMAKE_C_COMPILER already in the cache) drops the .c sources again.
enable_language(C)
add_library(sketchyup_openskp ${SKETCHYUP_LIBRARY_TYPE}
  src/core.cpp
  src/errors.cpp
  src/geometry.cpp
  src/legacy.cpp
  src/model.cpp
  src/observability.cpp
  src/pages_dimensions.cpp
  src/parser.cpp
  src/tlv.cpp
  src/transforms.cpp
  src/vff.cpp
  miniz/miniz.c
  miniz/miniz_tdef.c
  miniz/miniz_tinfl.c
  miniz/miniz_zip.c)
target_compile_features(sketchyup_openskp PUBLIC cxx_std_17)
target_include_directories(sketchyup_openskp
  PUBLIC include
  # PUBLIC: the public OpenSKP headers include openskp/export.hpp, which the consumer compiles.
  PUBLIC "${CMAKE_BINARY_DIR}/generated"
  PRIVATE src miniz)
# The upstream generates openskp/export.hpp for a target named OpenSkp (the OPENSKP_EXPORT macro);
# generate it the same way here (BASE_NAME keeps the macro names identical to upstream).
include(GenerateExportHeader)
generate_export_header(sketchyup_openskp BASE_NAME OpenSkp EXPORT_FILE_NAME "${CMAKE_BINARY_DIR}/generated/openskp/export.hpp")
# miniz.h takes its export macro from a header configured from the upstream template.
configure_file(cmake/miniz_export.h.in "${CMAKE_BINARY_DIR}/generated/miniz_export.h" COPYONLY)
if(NOT BUILD_SHARED_LIBS)
  target_compile_definitions(sketchyup_openskp PUBLIC OPENSKP_STATIC_DEFINE)
endif()
set_target_properties(sketchyup_openskp PROPERTIES CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN YES)
target_compile_options(sketchyup_openskp PRIVATE -Wall -Wextra -Wpedantic)
