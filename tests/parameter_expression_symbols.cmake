# R093.a (ADR 0165 §7): the parameter expression library must not reference any
# process, file, network or loader call, nor Qt. Lists the archive's undefined
# symbols with nm and fails on any match.
#   cmake -DNM=<nm> -DLIBRARY=<libsketchyup_parameter_expression.a> -P this-file
if(NOT NM OR NOT LIBRARY)
  message(FATAL_ERROR "Usage: cmake -DNM=<nm> -DLIBRARY=<archive> -P parameter_expression_symbols.cmake")
endif()
if(NOT EXISTS "${LIBRARY}")
  message(FATAL_ERROR "Library not found: ${LIBRARY}")
endif()
execute_process(COMMAND "${NM}" -u "${LIBRARY}" OUTPUT_VARIABLE raw RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "nm -u failed (${status}) on ${LIBRARY}")
endif()
execute_process(COMMAND "${NM}" -u -C "${LIBRARY}" OUTPUT_VARIABLE demangled RESULT_VARIABLE status)
if(NOT status EQUAL 0)
  message(FATAL_ERROR "nm -u -C failed (${status}) on ${LIBRARY}")
endif()

set(forbidden
  # process
  system popen pclose fork vfork clone clone3 execl execlp execle execv execvp execvpe
  execve execveat fexecve posix_spawn posix_spawnp kill raise ptrace syscall wait waitpid
  # loader
  dlopen dlmopen dlsym dlvsym dlclose dladdr
  # file
  fopen fopen64 freopen freopen64 fdopen open open64 openat openat64 creat creat64 read
  write pread pread64 pwrite pwrite64 close mkfifo unlink unlinkat remove rename renameat
  mkdir rmdir chdir stat stat64 lstat fstat opendir readdir mmap
  # network
  socket socketpair connect bind listen accept accept4 getaddrinfo gethostbyname
  gethostbyname2 send sendto sendmsg recv recvfrom recvmsg
  # host environment
  getenv secure_getenv setenv putenv)

set(count 0)
set(failures "")
set(sawLibm OFF)
string(REPLACE "\n" ";" lines "${raw}")
foreach(line IN LISTS lines)
  string(STRIP "${line}" line)
  # "                 U symbol@GLIBC_2.2.5" or "U symbol"
  if(NOT line MATCHES "^U[ \t]+([^ \t@]+)")
    continue()
  endif()
  set(symbol "${CMAKE_MATCH_1}")
  math(EXPR count "${count} + 1")
  if(symbol STREQUAL "sin")
    set(sawLibm ON)
  endif()
  list(FIND forbidden "${symbol}" index)
  if(NOT index EQUAL -1)
    string(APPEND failures "  ${symbol}\n")
  endif()
endforeach()

string(REPLACE "\n" ";" lines "${demangled}")
foreach(line IN LISTS lines)
  if(line MATCHES "std::filesystem|basic_filebuf|basic_ifstream|basic_ofstream|basic_fstream|std::system|Q[A-Z][A-Za-z]+::|qt_")
    string(STRIP "${line}" line)
    string(APPEND failures "  ${line}\n")
  endif()
endforeach()

if(count EQUAL 0)
  message(FATAL_ERROR "nm listed no undefined symbols in ${LIBRARY}; the scan read the wrong file")
endif()
if(NOT sawLibm)
  message(FATAL_ERROR "sin is not among the undefined symbols; the scan read the wrong file")
endif()
if(failures)
  message(FATAL_ERROR "sketchyup_parameter_expression references forbidden symbols:\n${failures}")
endif()
message(STATUS "sketchyup_parameter_expression: ${count} undefined symbols, none forbidden")
