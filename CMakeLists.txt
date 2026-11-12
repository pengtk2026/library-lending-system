cmake_minimum_required(VERSION 3.16)
project(LibraryLendingSystem LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(MSVC)
  add_compile_options(/utf-8)
else()
  add_compile_options(-Wall -Wextra -O2)
endif()

# ---- SQLite: compile the amalgamation directly, zero runtime DLL dependency ----
set(SQLITE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/sqlite-amalgamation-3540100")
add_library(sqlite3 STATIC "${SQLITE_DIR}/sqlite3.c")
target_include_directories(sqlite3 PUBLIC "${SQLITE_DIR}")
target_compile_definitions(sqlite3 PRIVATE SQLITE_THREADSAFE=1)

# ---- Core library shared by the GUI application and the demo-data tool ----
add_library(lls_core STATIC
  src/domain.cpp
  src/database.cpp
  src/daos.cpp
  src/service.cpp
  src/demo_data.cpp)
target_include_directories(lls_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(lls_core PUBLIC sqlite3)

# ---- FLTK: imported targets from the locally built static package ----
find_package(FLTK CONFIG REQUIRED)

add_executable(LibraryApp WIN32
  src/main.cpp
  src/ui.cpp)
target_link_libraries(LibraryApp PRIVATE lls_core fltk::fltk)

add_executable(DemoDataTool src/demo_tool_main.cpp)
target_link_libraries(DemoDataTool PRIVATE lls_core)

if(NOT MSVC)
  target_link_options(LibraryApp PRIVATE -static)
  target_link_options(DemoDataTool PRIVATE -static)
endif()

# Place both executables in build/ root.
set_target_properties(LibraryApp DemoDataTool PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")
