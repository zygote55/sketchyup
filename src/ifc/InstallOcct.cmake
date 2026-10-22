# Install-time step of the ifc component (ADR 0171 decision 18): a private copy of the OCCT
# libraries the kernel plugin records, beside the IfcOpenShell libraries, so the package does not
# depend on Arch's opencascade (which pulls in VTK, Tcl and Tk, 0.55 GiB). Run by
# install(SCRIPT ...) with SKETCHYUP_IFC_PREFIX_LIB, SKETCHYUP_IFC_OCCT_LIB and
# SKETCHYUP_IFC_PRIVATE_DIR set.
#
# The set is the closure of libTK* libraries that ifcopenshell_geometry_kernel_opencascade.so
# records (it is linked with --as-needed, so only the toolkits it really uses), found by reading
# DT_NEEDED with readelf. Each copy gets RUNPATH $ORIGIN: a library finds its dependencies by its
# own RUNPATH, never by the executable's.
# file(INSTALL) prepends DESTDIR to an absolute destination itself; the other commands below
# (MAKE_DIRECTORY, REAL_PATH, patchelf) work on the staged copy and need it spelled out.
set(install_destination "${SKETCHYUP_IFC_PRIVATE_DIR}")
set(destination "$ENV{DESTDIR}${SKETCHYUP_IFC_PRIVATE_DIR}")
set(kernel "${SKETCHYUP_IFC_PREFIX_LIB}/ifcopenshell_geometry_kernel_opencascade.so")
find_program(readelf_program readelf REQUIRED)
find_program(patchelf_program patchelf REQUIRED)

function(needed_libraries file out)
  execute_process(COMMAND "${readelf_program}" -d "${file}" OUTPUT_VARIABLE dynamic RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "readelf -d ${file} failed")
  endif()
  string(REGEX MATCHALL "\\(NEEDED\\)[^\n]*\\[[^]]*\\]" lines "${dynamic}")
  set(names)
  foreach(line IN LISTS lines)
    string(REGEX REPLACE ".*\\[([^]]*)\\]" "\\1" name "${line}")
    list(APPEND names "${name}")
  endforeach()
  set(${out} "${names}" PARENT_SCOPE)
endfunction()

set(pending)
needed_libraries("${kernel}" initial)
foreach(name IN LISTS initial)
  if(name MATCHES "^libTK")
    list(APPEND pending "${name}")
  endif()
endforeach()
set(closure)
while(pending)
  list(POP_FRONT pending name)
  if(name IN_LIST closure)
    continue()
  endif()
  list(APPEND closure "${name}")
  if(NOT EXISTS "${SKETCHYUP_IFC_OCCT_LIB}/${name}")
    message(FATAL_ERROR "OpenCASCADE library ${name} (needed by the kernel plugin) is not in ${SKETCHYUP_IFC_OCCT_LIB}")
  endif()
  needed_libraries("${SKETCHYUP_IFC_OCCT_LIB}/${name}" more)
  foreach(dependency IN LISTS more)
    if(dependency MATCHES "^libTK")
      list(APPEND pending "${dependency}")
    endif()
  endforeach()
endwhile()
list(SORT closure)
list(LENGTH closure count)
message(STATUS "Installing ${count} OpenCASCADE libraries beside the IfcOpenShell libraries")
foreach(name IN LISTS closure)
  # FOLLOW_SYMLINK_CHAIN copies the soname link and the file it points at.
  file(INSTALL "${SKETCHYUP_IFC_OCCT_LIB}/${name}" DESTINATION "${install_destination}" FOLLOW_SYMLINK_CHAIN)
endforeach()
foreach(name IN LISTS closure)
  file(REAL_PATH "${destination}/${name}" real)
  execute_process(COMMAND "${patchelf_program}" --set-rpath "$ORIGIN" "${real}" RESULT_VARIABLE status)
  if(NOT status EQUAL 0)
    message(FATAL_ERROR "patchelf failed on ${real}")
  endif()
endforeach()
