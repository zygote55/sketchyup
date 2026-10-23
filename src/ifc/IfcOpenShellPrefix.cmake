# The IfcOpenShell prefix (ADR 0171 sections 1.4 and 1.5): the trimmed, pinned tree in
# third_party/ifcopenshell built as shared libraries into one install prefix.
#
# The prefix depends only on the vendored files, the image's packages and the flags below, so
# CI caches it (a cache restores it into SKETCHYUP_IFC_PREFIX). This file is hashed into the
# cache key: change a flag here and the key changes. Bump SKETCHYUP_IFC_PREFIX_FORMAT for any
# change that must invalidate prefixes whose key cannot see it.
#
# Two ways to get the prefix, one result:
#   - SKETCHYUP_IFC_PREFIX names a directory that holds a prefix built from this pin and these
#     flags (its stamp file says so): it is used as is and nothing is built.
#   - otherwise an ExternalProject configures, builds and installs the vendored tree into
#     ${CMAKE_BINARY_DIR}/ifc-prefix when the main build runs (once; delete that directory to
#     rebuild after editing the vendored tree).

include(ExternalProject)

set(SKETCHYUP_IFC_PREFIX_FORMAT 1)
set(SKETCHYUP_IFC_PREFIX "" CACHE PATH
    "A prebuilt IfcOpenShell prefix (a CI cache restore); built from third_party/ifcopenshell when empty or stale")
set(SKETCHYUP_IFC_JOBS "" CACHE STRING
    "Compile jobs for the IfcOpenShell sub-project (default: CMAKE_BUILD_PARALLEL_LEVEL, else the CPU count, at most 4: a compiler process holds up to 0.9 GB)")

if(SKETCHYUP_IFC_PREFIX)
  set(sketchyup_ifc_prefix "${SKETCHYUP_IFC_PREFIX}")
else()
  set(sketchyup_ifc_prefix "${CMAKE_BINARY_DIR}/ifc-prefix")
endif()
set(sketchyup_ifc_stamp_file "${sketchyup_ifc_prefix}/sketchyup-ifc-prefix.txt")
set(sketchyup_ifc_stamp
    "ifcopenshell ${SKETCHYUP_IFC_PIN_COMMIT}\nprefix-format ${SKETCHYUP_IFC_PREFIX_FORMAT}\n")

# The kernel needs OpenCASCADE and every schema's typed API needs Boost headers and Eigen.
# Boost and OCCT libraries are found at run time (boost-libs, or the private OCCT copy).
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)  # find_package(Boost) uses Boost's own CMake config
endif()
find_package(Boost REQUIRED)
find_package(Eigen3 REQUIRED NO_MODULE)
get_target_property(sketchyup_ifc_eigen_include Eigen3::Eigen INTERFACE_INCLUDE_DIRECTORIES)
set(sketchyup_ifc_occt_include "/usr/include/opencascade" CACHE PATH "OpenCASCADE headers (Arch: /usr/include/opencascade)")
set(sketchyup_ifc_occt_lib "/usr/lib" CACHE PATH "OpenCASCADE libraries (Arch: /usr/lib)")
if(NOT EXISTS "${sketchyup_ifc_occt_include}/Standard_Version.hxx")
  message(FATAL_ERROR "SKETCHYUP_BUILD_IFC needs OpenCASCADE headers: ${sketchyup_ifc_occt_include}/Standard_Version.hxx is missing (Arch: pacman -S opencascade)")
endif()

set(sketchyup_ifc_prefix_ready FALSE)
if(EXISTS "${sketchyup_ifc_stamp_file}")
  file(READ "${sketchyup_ifc_stamp_file}" sketchyup_ifc_stamp_found)
  if(sketchyup_ifc_stamp_found STREQUAL sketchyup_ifc_stamp)
    set(sketchyup_ifc_prefix_ready TRUE)
  endif()
endif()
if(SKETCHYUP_IFC_PREFIX AND NOT sketchyup_ifc_prefix_ready AND EXISTS "${SKETCHYUP_IFC_PREFIX}/lib")
  message(WARNING "SKETCHYUP_IFC_PREFIX=${SKETCHYUP_IFC_PREFIX} holds no prefix for this pin and these flags; building it there from third_party/ifcopenshell")
endif()

if(sketchyup_ifc_prefix_ready)
  message(STATUS "IfcOpenShell: using the prefix in ${sketchyup_ifc_prefix}")
  add_custom_target(sketchyup_ifcopenshell_prefix)
else()
  if(SKETCHYUP_IFC_JOBS)
    set(sketchyup_ifc_jobs "${SKETCHYUP_IFC_JOBS}")
  elseif(DEFINED ENV{CMAKE_BUILD_PARALLEL_LEVEL} AND NOT "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}" STREQUAL "")
    set(sketchyup_ifc_jobs "$ENV{CMAKE_BUILD_PARALLEL_LEVEL}")
  else()
    cmake_host_system_information(RESULT sketchyup_ifc_jobs QUERY NUMBER_OF_LOGICAL_CORES)
    if(sketchyup_ifc_jobs GREATER 4)
      set(sketchyup_ifc_jobs 4)
    endif()
  endif()
  # The package build's flags (makepkg's CXXFLAGS and LDFLAGS) reach the sub-project through
  # the environment, as for the main project. Linking with --as-needed records only the OCCT
  # libraries the kernel really uses (check L6); RUNPATH (new dtags) keeps LD_LIBRARY_PATH
  # able to override the private directory (check L2).
  set(sketchyup_ifc_ldflags "$ENV{LDFLAGS} -Wl,--as-needed -Wl,--enable-new-dtags")
  ExternalProject_Add(sketchyup_ifcopenshell_prefix
    SOURCE_DIR "${CMAKE_SOURCE_DIR}/third_party/ifcopenshell/cmake"
    BINARY_DIR "${CMAKE_BINARY_DIR}/ifc-build"
    INSTALL_DIR "${sketchyup_ifc_prefix}"
    CMAKE_GENERATOR "${CMAKE_GENERATOR}"
    CMAKE_ARGS
      -DCMAKE_BUILD_TYPE=Release
      -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
      -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
      "-DCMAKE_SHARED_LINKER_FLAGS=${sketchyup_ifc_ldflags}"
      # Three schemas (IFC2X3, IFC4, IFC4X3_ADD2), IfcGeom with the OpenCASCADE kernel and the
      # pass-through kernel; no CGAL (GPL parts), no converter, Python, server, example,
      # viewer or COLLADA code, and no upstream tests (they fetch Catch2 over the network).
      -DSCHEMA_VERSIONS=2x3|4|4x3_add2
      -DBUILD_IFCGEOM=ON
      -DWITH_OPENCASCADE=ON
      -DWITH_CGAL=OFF
      -DWITH_MANIFOLD=OFF
      -DOCC_INCLUDE_DIR=${sketchyup_ifc_occt_include}
      -DOCC_LIBRARY_DIR=${sketchyup_ifc_occt_lib}
      -DEIGEN_DIR=${sketchyup_ifc_eigen_include}
      -DBUILD_CONVERT=OFF
      -DBUILD_IFCPYTHON=OFF
      -DBUILD_GEOMSERVER=OFF
      -DBUILD_EXAMPLES=OFF
      -DBUILD_BONSAIVIEWER=OFF
      -DBUILD_FUZZERS=OFF
      -DBUILD_IFCOPENSHELL_PARSE_TESTS=OFF
      -DBUILD_IFCOPENSHELL_GEOMETRY_TESTS=OFF
      -DBUILD_BONSAIVIEWER_TESTS=OFF
      -DCOLLADA_SUPPORT=OFF
    LIST_SEPARATOR |
    BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel ${sketchyup_ifc_jobs}
    INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
    COMMAND "${CMAKE_COMMAND}" "-DSTAMP_FILE=${sketchyup_ifc_stamp_file}"
            "-DPIN=${SKETCHYUP_IFC_PIN_COMMIT}" "-DFORMAT=${SKETCHYUP_IFC_PREFIX_FORMAT}"
            -P "${CMAKE_CURRENT_LIST_DIR}/WriteStamp.cmake"
    BUILD_ALWAYS OFF
    USES_TERMINAL_BUILD OFF)
endif()

# What the worker compiles and links against: headers and libraries in the prefix. The schema
# library of IFC4 is linked directly because the writer uses the typed IFC4 classes
# (hierarchy_helper<Ifc4>); the other schemas and every geometry plugin are loaded by the
# library from the directory of libifcopenshell.plugin.so at run time.
add_library(sketchyup_ifc_libraries INTERFACE)
add_dependencies(sketchyup_ifc_libraries sketchyup_ifcopenshell_prefix)
target_include_directories(sketchyup_ifc_libraries SYSTEM INTERFACE "${sketchyup_ifc_prefix}/include")
target_compile_definitions(sketchyup_ifc_libraries INTERFACE IFOPSH_WITH_OPENCASCADE)
target_link_directories(sketchyup_ifc_libraries INTERFACE "${sketchyup_ifc_prefix}/lib")
target_link_libraries(sketchyup_ifc_libraries INTERFACE
  ifcopenshell.geometry ifcopenshell.parse ifcopenshell.plugin
  -l:ifcopenshell_parse_schema_ifc4.so Boost::headers Eigen3::Eigen)

# The worker's direct OpenCASCADE use (ADR 0172, R098.a): the basic STEP reader and writer.
# The version is the configured one (the headers' spelling differs between 7.8 and 7.9: the
# build selects it, see step_probe.cpp). The libraries are found in the system directory; the
# private copy the `ifc` component installs is found through the worker's RUNPATH.
# 7.9 consolidated the toolkits: the STEP stack (STEPControl, the 209 and 242 protocols,
# the DESTEP writer) went into TKDESTEP, the topology (TopoDS, TopExp) into TKBRep and the
# tessellator (BRepMesh) into TKMesh; the 7.8 TKSTEP, TKSTEPAttr, TKSTEP209, TKTopoDS,
# TKXDESTEP and TKTessellate are gone.
set(SKETCHYUP_IFC_OCCT_VERSION "7.9.3" CACHE STRING "The OpenCASCADE version the worker's headers and toolkits are selected against")
if(SKETCHYUP_IFC_OCCT_VERSION MATCHES "^7\.9")
  set(sketchyup_ifc_occt_libs TKernel TKMath TKBRep TKMesh TKDESTEP TKXSBase TKXCAF)
else()
  set(sketchyup_ifc_occt_libs
    TKernel TKMath TKTopoDS TKBRep TKPrim TKSTEP TKSTEPAttr TKSTEP209
    TKXSBase TKDESTEP TKXDESTEP TKXCAF TKG3d TKBool TKPrism TKShHealing TKTessellate)
endif()
foreach(sketchyup_ifc_occt_lib_name IN LISTS sketchyup_ifc_occt_libs)
  if(NOT EXISTS "${sketchyup_ifc_occt_lib}/lib${sketchyup_ifc_occt_lib_name}.so")
    message(FATAL_ERROR "SKETCHYUP_BUILD_IFC needs the OpenCASCADE library ${sketchyup_ifc_occt_lib}/lib${sketchyup_ifc_occt_lib_name}.so (Arch: pacman -S opencascade)")
  endif()
endforeach()
add_library(sketchyup_ifc_occt INTERFACE)
target_include_directories(sketchyup_ifc_occt SYSTEM INTERFACE "${sketchyup_ifc_occt_include}")
target_link_directories(sketchyup_ifc_occt INTERFACE "${sketchyup_ifc_occt_lib}")
# The basic reader (the STEP control and data, the AP203/214/242 protocol, the XDE
# selection), the basic writer (the DESTEP writer and the XCAF document it writes) and the
# geometry the probe walks (the B-rep, the primitives, the tessellator and its helpers,
# for the tessellated import of R098.b).
target_link_libraries(sketchyup_ifc_occt INTERFACE ${sketchyup_ifc_occt_libs})
