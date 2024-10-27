cmake_minimum_required(VERSION 3.6)

project (libexdupe)

set(CMAKE_CXX_STANDARD 20)

ADD_LIBRARY(libexdupe STATIC libexdupe.cpp)

set(BUILD_SHARED_LIBS OFF)
option(XXHASH_BUILD_XXHSUM "" OFF)
option(ZSTD_PROGRAMS_LINK_SHARED "" OFF)
option(ZSTD_BUILD_SHARED "" OFF)

if (MSVC)
	set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/pthread/cmake")
	include_directories(pthread)
	add_subdirectory(pthread)
else()
	target_compile_options(libexdupe PRIVATE -mavx2)
	set(flags_to_apply "-pedantic" "-Wextra" "-Wall" "-Wno-gnu-zero-variadic-macro-arguments")
endif(MSVC)

apply_compile_options_to_files("libexdupe.cpp" "${flags_to_apply}")

add_subdirectory(blake3/c)
add_subdirectory(xxHash/cmake_unofficial)

# Todo, is all this really needed for libzstd?
set(ZSTD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zstd")
set(LIBRARY_DIR ${ZSTD_SOURCE_DIR}/lib)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/zstd/build/cmake/CMakeModules")
include(GetZstdLibraryVersion)
GetZstdLibraryVersion(${LIBRARY_DIR}/zstd.h zstd_VERSION_MAJOR zstd_VERSION_MINOR zstd_VERSION_PATCH)
include(GNUInstallDirs)
add_subdirectory(zstd/build/cmake/lib)

target_link_libraries(libexdupe PRIVATE libzstd_static)
target_link_libraries(libexdupe PRIVATE blake3)
target_link_libraries(libexdupe PUBLIC xxhash)

if (MSVC)
	target_link_libraries(libexdupe PRIVATE libpthreadVC3)
	target_link_libraries(libexdupe PRIVATE vssapi.lib) # Volume Shadow Copy Service
endif(MSVC)
