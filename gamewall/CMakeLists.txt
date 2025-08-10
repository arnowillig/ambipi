# CMakeLists.txt — Linux-only (Raspberry Pi / GCC 8 friendly)
cmake_minimum_required(VERSION 3.14)
project(wled_rest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(wled_rest main.cpp)

# =======================
# nlohmann/json (header-only)
# =======================
find_package(nlohmann_json QUIET)
if (NOT nlohmann_json_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
  )
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# =======================
# Pistache (system -> pkg-config -> FetchContent)
# =======================
set(_have_pistache_config OFF)
find_package(Pistache CONFIG QUIET)
if (Pistache_FOUND)
  set(_have_pistache_config ON)
endif()

if (NOT _have_pistache_config)
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(PISTACHE QUIET libpistache pistache)
  endif()
endif()

if (_have_pistache_config)
  message(STATUS "Using system Pistache (CONFIG)")
  target_link_libraries(wled_rest PRIVATE Pistache::pistache nlohmann_json::nlohmann_json)
elseif (PISTACHE_FOUND)
  message(STATUS "Using system Pistache (pkg-config)")
  target_include_directories(wled_rest PRIVATE ${PISTACHE_INCLUDE_DIRS})
  target_link_libraries(wled_rest PRIVATE ${PISTACHE_LIBRARIES} nlohmann_json::nlohmann_json)
else()
  message(STATUS "Pistache not found; fetching from source")
  include(FetchContent)
  FetchContent_Declare(
    pistache
    GIT_REPOSITORY https://github.com/pistacheio/pistache.git
    GIT_TAG v0.4.26
    GIT_SHALLOW TRUE
  )
  set(PISTACHE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(PISTACHE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PISTACHE_USE_SSL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(pistache)

  if (TARGET Pistache::pistache)
    target_link_libraries(wled_rest PRIVATE Pistache::pistache nlohmann_json::nlohmann_json)
  elseif (TARGET pistache)
    target_link_libraries(wled_rest PRIVATE pistache nlohmann_json::nlohmann_json)
  else()
    message(FATAL_ERROR "Pistache target not found after FetchContent")
  endif()
endif()

# =======================
# Linux link bits (Pi / GCC 8.x)
# =======================

# pthreads (required by Pistache)
find_package(Threads REQUIRED)
target_link_libraries(wled_rest PRIVATE Threads::Threads)

# libdl (dladdr, etc.)
find_library(DL_LIBRARY dl)
if (DL_LIBRARY)
  target_link_libraries(wled_rest PRIVATE ${DL_LIBRARY})
endif()

# ---- std::filesystem for GCC 8: prefer absolute static archive and keep it late
execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libstdc++fs.a
  OUTPUT_VARIABLE STDCXXFS_ABSPATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (STDCXXFS_ABSPATH AND NOT STDCXXFS_ABSPATH STREQUAL "libstdc++fs.a")
  message(STATUS "Linking std::filesystem via: ${STDCXXFS_ABSPATH}")
  target_link_libraries(wled_rest PRIVATE ${STDCXXFS_ABSPATH})
else()
  message(WARNING "Using -lstdc++fs fallback (absolute archive not resolved)")
  target_link_libraries(wled_rest PRIVATE stdc++fs)
endif()

# ---- libatomic: FORCE it to stay, keep it ABSOLUTELY LAST ----
# Try to resolve an absolute path to libatomic (so the linker can't skip it).
# Prefer .so (runtime), but .a is fine too.
execute_process(
  COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libatomic.so
  OUTPUT_VARIABLE ATOMIC_SO_ABSPATH
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if (NOT ATOMIC_SO_ABSPATH OR ATOMIC_SO_ABSPATH STREQUAL "libatomic.so")
  execute_process(
    COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libatomic.a
    OUTPUT_VARIABLE ATOMIC_A_ABSPATH
    OUTPUT_STRIP_TRAILING_WHITESPACE
  )
endif()

# Push --no-as-needed, link atomic, then pop back to default
if (ATOMIC_SO_ABSPATH AND NOT ATOMIC_SO_ABSPATH STREQUAL "libatomic.so")
  message(STATUS "Forcing libatomic via: ${ATOMIC_SO_ABSPATH}")
  target_link_options(wled_rest PRIVATE "SHELL:-Wl,--push-state,--no-as-needed")
  target_link_libraries(wled_rest PRIVATE ${ATOMIC_SO_ABSPATH})
  target_link_options(wled_rest PRIVATE "SHELL:-Wl,--pop-state")
elseif (ATOMIC_A_ABSPATH AND NOT ATOMIC_A_ABSPATH STREQUAL "libatomic.a")
  message(STATUS "Forcing libatomic via: ${ATOMIC_A_ABSPATH}")
  target_link_options(wled_rest PRIVATE "SHELL:-Wl,--push-state,--no-as-needed")
  target_link_libraries(wled_rest PRIVATE ${ATOMIC_A_ABSPATH})
  target_link_options(wled_rest PRIVATE "SHELL:-Wl,--pop-state")
else()
  message(WARNING "Falling back to -latomic (install libatomic1 if link fails)")
  target_link_options(wled_rest PRIVATE "SHELL:-Wl,--push-state,--no-as-needed")
  target_link_libraries(wled_rest PRIVATE atomic)
  target_link_options(wled_rest PRIVATE "SHELL:-Wl,--pop-state")
endif()

# ---- Warnings ----
target_compile_options(wled_rest PRIVATE -Wall -Wextra -Wpedantic)