# File: CMakeLists.txt
cmake_minimum_required(VERSION 3.14)
project(wled_rest CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- nlohmann/json (Header-only) ----
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

# ---- Pistache: try system package first, then FetchContent fallback ----
find_package(Pistache CONFIG QUIET)

if (NOT Pistache_FOUND)
  find_package(PkgConfig QUIET)
  if (PkgConfig_FOUND)
    pkg_check_modules(PISTACHE QUIET libpistache pistache)
  endif()
endif()

if (Pistache_FOUND)
  message(STATUS "Using system Pistache (CONFIG)")
  add_executable(wled_rest main.cpp)
  target_link_libraries(wled_rest PRIVATE Pistache::pistache nlohmann_json::nlohmann_json)

elseif (PISTACHE_FOUND)
  message(STATUS "Using system Pistache (pkg-config)")
  add_executable(wled_rest main.cpp)
  target_include_directories(wled_rest PRIVATE ${PISTACHE_INCLUDE_DIRS})
  target_link_libraries(wled_rest PRIVATE ${PISTACHE_LIBRARIES} nlohmann_json::nlohmann_json)
  find_package(Threads REQUIRED)
  target_link_libraries(wled_rest PRIVATE Threads::Threads)

else()
  message(STATUS "Pistache not found; fetching from source")

  include(FetchContent)
  # Use a real tag name (note the leading 'v')
  FetchContent_Declare(
    pistache
    GIT_REPOSITORY https://github.com/pistacheio/pistache.git
    GIT_TAG v0.4.26
    GIT_SHALLOW TRUE
  )

  # speed up & avoid extra components
  set(PISTACHE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(PISTACHE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(PISTACHE_USE_SSL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(pistache)

  add_executable(wled_rest main.cpp)
  if (TARGET Pistache::pistache)
    target_link_libraries(wled_rest PRIVATE Pistache::pistache nlohmann_json::nlohmann_json)
  elseif (TARGET pistache)
    target_link_libraries(wled_rest PRIVATE pistache nlohmann_json::nlohmann_json)
  else()
    message(FATAL_ERROR "Pistache target not found after FetchContent")
  endif()
endif()

# Warnings (optional)
if (MSVC)
  target_compile_options(wled_rest PRIVATE /W4)
else()
  target_compile_options(wled_rest PRIVATE -Wall -Wextra -Wpedantic)
endif()
