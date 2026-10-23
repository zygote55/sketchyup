# R097.a: IfcOpenShell and the helper that links it (ADR 0171). Added by the top-level build when
# SKETCHYUP_BUILD_IFC is ON. Only sketchyup-ifc-worker links the library (decision 19): the
# application, the CLI and every other executable stay free of it, so the LGPL attaches to the
# helper and to the libraries beside it.
#
# The pinned upstream commit. scripts/vendor-ifcopenshell.py checks that it equals the commit
# recorded in third_party/ifcopenshell/README.sketchyup.md and in dependencies.json.
set(SKETCHYUP_IFC_PIN_COMMIT 7ba701972f7182322e0a00db8f0e1f3de5f69840)

include("${CMAKE_CURRENT_LIST_DIR}/IfcOpenShellPrefix.cmake")

add_executable(sketchyup-ifc-worker
  worker_main.cpp probe_environment.cpp probe_geometry.cpp probe_writer.cpp
  access.cpp units.cpp placement.cpp model.cpp geometry.cpp account.cpp georeference.cpp package.cpp report.cpp import.cpp)
target_include_directories(sketchyup-ifc-worker PRIVATE "${PROJECT_SOURCE_DIR}/src")
target_compile_definitions(sketchyup-ifc-worker PRIVATE
  SKETCHYUP_IFC_PIN_COMMIT_STRING="${SKETCHYUP_IFC_PIN_COMMIT}")
target_link_libraries(sketchyup-ifc-worker PRIVATE sketchyup_ifc_libraries sketchyup_version)
target_compile_options(sketchyup-ifc-worker PRIVATE -Wall -Wextra -Wpedantic)
add_dependencies(sketchyup-ifc-worker sketchyup_ifcopenshell_prefix)
# The private directory is flat: the helper and every library sit in /usr/lib/sketchyup. A
# RUNPATH (not an RPATH) of $ORIGIN lets LD_LIBRARY_PATH override it, which is the "replace the
# library" path of LGPL-3.0 section 4 (check L2). The build tree finds the prefix's libraries.
set_target_properties(sketchyup-ifc-worker PROPERTIES
  BUILD_RPATH "${sketchyup_ifc_prefix}/lib"
  INSTALL_RPATH "$ORIGIN"
  BUILD_WITH_INSTALL_RPATH OFF)
target_link_options(sketchyup-ifc-worker PRIVATE -Wl,--enable-new-dtags)

# Package component `ifc` (decision 22: the split package sketchyup-ifc). Everything below
# installs into /usr/lib/sketchyup, the private directory the other workers already use.
install(TARGETS sketchyup-ifc-worker RUNTIME DESTINATION lib/sketchyup COMPONENT ifc)
# The libraries of ADR 0171 section 1.2 and nothing else: IfcParse, the plugin loader and IfcGeom
# with their soname links, the three schema libraries, the three geometry mapping plugins and
# the two kernels. Not the tree and serializer plugins, headers or CMake files.
install(DIRECTORY "${sketchyup_ifc_prefix}/lib/" DESTINATION lib/sketchyup COMPONENT ifc
  FILES_MATCHING
    PATTERN "libifcopenshell.parse.so*"
    PATTERN "libifcopenshell.plugin.so*"
    PATTERN "libifcopenshell.geometry.so*"
    PATTERN "ifcopenshell_parse_schema_ifc2x3.so"
    PATTERN "ifcopenshell_parse_schema_ifc4.so"
    PATTERN "ifcopenshell_parse_schema_ifc4x3_add2.so"
    PATTERN "ifcopenshell_geometry_mapping_ifc2x3.so"
    PATTERN "ifcopenshell_geometry_mapping_ifc4.so"
    PATTERN "ifcopenshell_geometry_mapping_ifc4x3_add2.so"
    PATTERN "ifcopenshell_geometry_kernel_opencascade.so"
    PATTERN "ifcopenshell_geometry_kernel_passthrough.so")
# Decision 18: a private copy of the OpenCASCADE libraries the kernel needs.
option(SKETCHYUP_IFC_BUNDLE_OCCT "Install a private copy of the OpenCASCADE libraries the kernel plugin needs (needs patchelf)" ON)
if(SKETCHYUP_IFC_BUNDLE_OCCT)
  install(CODE "set(SKETCHYUP_IFC_PREFIX_LIB \"${sketchyup_ifc_prefix}/lib\")
set(SKETCHYUP_IFC_OCCT_LIB \"${sketchyup_ifc_occt_lib}\")
set(SKETCHYUP_IFC_PRIVATE_DIR \"\${CMAKE_INSTALL_PREFIX}/lib/sketchyup\")" COMPONENT ifc)
  install(SCRIPT "${CMAKE_CURRENT_LIST_DIR}/InstallOcct.cmake" COMPONENT ifc)
endif()
# The notices and the written source offer are installed by the top-level CMakeLists.txt, where
# scripts/release-artifacts.sh collects them into THIRD_PARTY_NOTICES.

if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  # The API probe: what the pinned library does with units, voids, colours, per-element
  # failures and the writer, checked against hand-computed values.
  add_test(NAME ifc_worker_probe COMMAND "${Python3_EXECUTABLE}" -I
    "${PROJECT_SOURCE_DIR}/tests/ifc_worker_probe_tests.py"
    "$<TARGET_FILE:sketchyup-ifc-worker>" "${PROJECT_SOURCE_DIR}/tests/fixtures/ifc")
  set_tests_properties(ifc_worker_probe PROPERTIES TIMEOUT 600)
  if(SKETCHYUP_SANITIZE)
    # IfcParse leaks the instances and header entities of every file it parses (upstream turns leak
    # detection off in its own fuzz harness for the same reason); the rest of ASan and UBSan stay on.
    # An UBSan report is a failure, not a line on stderr.
    set_tests_properties(ifc_worker_probe PROPERTIES
      ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
  endif()
  # R097.b: the worker's import at the package level against a raw-text scan of the fixtures (units,
  # hierarchy, properties, accounting, determinism), and the damaged-file corpus of check J2 (64 mutations
  # per certification file here; 512 in the `full` test).
  add_test(NAME ifc_import_worker COMMAND "${Python3_EXECUTABLE}" -I
    "${PROJECT_SOURCE_DIR}/tests/ifc_import_worker_tests.py"
    "$<TARGET_FILE:sketchyup-ifc-worker>" "${PROJECT_SOURCE_DIR}/tests/fixtures/ifc")
  # R097.c: georeferencing at the package level (F1 to F4), against the raw-text scan and hand-computed values.
  add_test(NAME ifc_georeference COMMAND "${Python3_EXECUTABLE}" -I
    "${PROJECT_SOURCE_DIR}/tests/ifc_georeference_tests.py"
    "$<TARGET_FILE:sketchyup-ifc-worker>" "${PROJECT_SOURCE_DIR}/tests/fixtures/ifc")
  add_test(NAME ifc_worker_fault_corpus COMMAND "${Python3_EXECUTABLE}" -I
    "${PROJECT_SOURCE_DIR}/tests/ifc_worker_fault_corpus.py"
    "$<TARGET_FILE:sketchyup-ifc-worker>" "${PROJECT_SOURCE_DIR}/tests/fixtures/ifc" 64 2)
  add_test(NAME ifc_worker_fault_corpus_full COMMAND "${Python3_EXECUTABLE}" -I
    "${PROJECT_SOURCE_DIR}/tests/ifc_worker_fault_corpus.py"
    "$<TARGET_FILE:sketchyup-ifc-worker>" "${PROJECT_SOURCE_DIR}/tests/fixtures/ifc" 512 2)
  set_tests_properties(ifc_import_worker ifc_georeference ifc_worker_fault_corpus PROPERTIES TIMEOUT 900)
  set_tests_properties(ifc_worker_fault_corpus_full PROPERTIES TIMEOUT 3000 LABELS full)
  if(SKETCHYUP_SANITIZE)
    set_tests_properties(ifc_import_worker ifc_georeference ifc_worker_fault_corpus ifc_worker_fault_corpus_full PROPERTIES
      ENVIRONMENT "IFC_NO_ADDRESS_LIMIT=1;ASAN_OPTIONS=detect_leaks=0;UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1")
  endif()
  # Checks L1 to L6 of ADR 0171 section 6.3 on a scratch install of the ifc component.
  add_test(NAME ifc_library_install COMMAND "${Python3_EXECUTABLE}" -I
    "${PROJECT_SOURCE_DIR}/scripts/verify-ifc-library.py" install
    --cmake "${CMAKE_COMMAND}" --build-dir "${CMAKE_BINARY_DIR}")
  set_tests_properties(ifc_library_install PROPERTIES TIMEOUT 600)
  # L3 (the pin) and L4 (dependencies.json, the PKGBUILD licences, the notice lines) read repository
  # files the source archive does not carry, so this test exists in a git checkout only.
  if(EXISTS "${PROJECT_SOURCE_DIR}/packaging/arch/PKGBUILD" AND EXISTS "${PROJECT_SOURCE_DIR}/dependencies.json")
    add_test(NAME ifc_library_source COMMAND "${Python3_EXECUTABLE}" -I
      "${PROJECT_SOURCE_DIR}/scripts/verify-ifc-library.py" source --source-dir "${PROJECT_SOURCE_DIR}")
  endif()
endif()
