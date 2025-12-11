cmake_minimum_required(VERSION 3.6)

if(MSVC)
    # Force static CRT via flags
    set(CMAKE_C_FLAGS_RELEASE   "${CMAKE_C_FLAGS_RELEASE} /MT")
    set(CMAKE_C_FLAGS_DEBUG     "${CMAKE_C_FLAGS_DEBUG} /MTd")
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /MT")
    set(CMAKE_CXX_FLAGS_DEBUG   "${CMAKE_CXX_FLAGS_DEBUG} /MTd")
endif()

project (libexdupe)

set(CMAKE_CXX_STANDARD 20)

ADD_LIBRARY(libexdupe STATIC libexdupe.cpp gxhash/gxhash.c)

set(BUILD_SHARED_LIBS OFF)
option(XXHASH_BUILD_XXHSUM "" OFF)
option(ZSTD_PROGRAMS_LINK_SHARED "" OFF)
option(ZSTD_BUILD_SHARED "" OFF)

if (MSVC)
	set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH}" "${CMAKE_CURRENT_SOURCE_DIR}/pthread/cmake")
	include_directories(pthread)
	add_subdirectory(pthread)
else()
	target_compile_options(libexdupe PRIVATE -mno-avx2)
	set(flags_to_apply "-maes" "-msse2" "-pedantic" "-Wextra" "-Wall" "-Wno-gnu-zero-variadic-macro-arguments")
endif(MSVC)

apply_compile_options_to_files("libexdupe.cpp" "${flags_to_apply}")
apply_compile_options_to_files("gxhash/gxhash.c" "${flags_to_apply}")

# Todo, is all this really needed for libzstd?
set(ZSTD_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/zstd")
set(LIBRARY_DIR ${ZSTD_SOURCE_DIR}/lib)
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/zstd/build/cmake/CMakeModules")
include(GetZstdLibraryVersion)
GetZstdLibraryVersion(${LIBRARY_DIR}/zstd.h zstd_VERSION_MAJOR zstd_VERSION_MINOR zstd_VERSION_PATCH)
include(GNUInstallDirs)
add_subdirectory(zstd/build/cmake/lib)

target_link_libraries(libexdupe PRIVATE libzstd_static)

if(${CMAKE_SYSTEM_NAME} MATCHES "FreeBSD")
    target_link_libraries (libexdupe PRIVATE "pthread")
endif()

if (MSVC)
	target_link_libraries(libexdupe PRIVATE libpthreadVC3)
	target_link_libraries(libexdupe PRIVATE vssapi.lib) # Volume Shadow Copy Service
endif(MSVC)
