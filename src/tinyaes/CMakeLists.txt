cmake_minimum_required(VERSION 3.10)
project(tinyaes VERSION 0.1.0 LANGUAGES CXX)

include(CheckCXXCompilerFlag)

# Detect whether tinyaes is the top-level project or being consumed via
# add_subdirectory(). Only the top-level build exposes tests, benchmarks,
# fuzzers, and CTest. Downstream consumers must not inherit our dev targets.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(TINYAES_IS_TOP_LEVEL ON)
else()
    set(TINYAES_IS_TOP_LEVEL OFF)
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(BUILD_SHARED_LIBS          "Build shared library" OFF)
option(TINYAES_BUILD_TESTS        "Build tinyaes unit tests" OFF)
option(TINYAES_BUILD_BENCHMARKS   "Build tinyaes benchmarks" OFF)
option(TINYAES_BUILD_FUZZERS      "Build tinyaes fuzzers (Clang+Linux only)" OFF)
option(TINYAES_FORCE_PORTABLE     "Disable SIMD backends; use only portable code" OFF)
option(TINYAES_ENABLE_SANITIZERS  "Enable AddressSanitizer + UndefinedBehaviorSanitizer (GCC/Clang only)" OFF)

# When consumed as a subdirectory, force dev-only options OFF regardless of
# what the parent passed. Prevents a parent project that defines its own
# BUILD_TESTS (or similar) from accidentally turning ours on, and prevents
# sanitizer flags from leaking into consumer builds.
if(NOT TINYAES_IS_TOP_LEVEL)
    set(TINYAES_BUILD_TESTS       OFF CACHE BOOL "" FORCE)
    set(TINYAES_BUILD_BENCHMARKS  OFF CACHE BOOL "" FORCE)
    set(TINYAES_BUILD_FUZZERS     OFF CACHE BOOL "" FORCE)
    set(TINYAES_ENABLE_SANITIZERS OFF CACHE BOOL "" FORCE)
endif()

# --- Sanitizer support ---
# Defines a helper function tinyaes_apply_sanitizers(<target>) that applies
# ASan+UBSan compile and link flags to <target>. When sanitizers are OFF the
# helper is a no-op, so callers in subdirectories (tests, bench, fuzz) can
# invoke it unconditionally. MSVC is unsupported because /fsanitize=address
# does not combine cleanly with the existing /guard:cf /guard:ehcont /Qspectre
# hardening and MSVC has no UBSan — fail loudly rather than silently dropping
# the flags.
if(TINYAES_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR
            "TINYAES_ENABLE_SANITIZERS is not supported with MSVC. "
            "Use GCC or Clang for sanitized builds.")
    endif()
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        message(FATAL_ERROR
            "TINYAES_ENABLE_SANITIZERS requires GCC or Clang "
            "(got ${CMAKE_CXX_COMPILER_ID}).")
    endif()
    # Probe the toolchain for ASan+UBSan runtime availability at link time.
    # MinGW GCC on Windows, for example, ships libgcc but not libasan/libubsan
    # and would otherwise fail with a cryptic "cannot find -lasan" linker
    # error deep into the build. Catch it now with a clear message instead.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fsanitize=address,undefined")
    set(CMAKE_REQUIRED_LIBRARIES "-fsanitize=address,undefined")
    check_cxx_source_compiles("int main() { return 0; }"
        TINYAES_HAS_SANITIZER_RUNTIME)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LIBRARIES)
    if(NOT TINYAES_HAS_SANITIZER_RUNTIME)
        message(FATAL_ERROR
            "TINYAES_ENABLE_SANITIZERS=ON but the ASan/UBSan runtime "
            "libraries are not available to the linker. On MinGW you "
            "typically need libasan/libubsan (not shipped by default) or "
            "must switch to Clang. On Linux install the matching "
            "libasan/libubsan packages for your compiler.")
    endif()
    function(tinyaes_apply_sanitizers target)
        target_compile_options(${target} PRIVATE
            -fsanitize=address,undefined
            -fno-omit-frame-pointer
            -fno-sanitize-recover=undefined
        )
        set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS
            " -fsanitize=address,undefined")
    endfunction()
else()
    function(tinyaes_apply_sanitizers target)
        # no-op when TINYAES_ENABLE_SANITIZERS is OFF
    endfunction()
endif()

# --- Library sources ---
set(TINYAES_SOURCES
    src/secure_zero.cpp
    src/cpuid.cpp
    src/iv_generate.cpp
    src/keyschedule.cpp
    src/ecb.cpp
    src/cbc.cpp
    src/ctr.cpp
    src/gcm.cpp
    src/backend/aes_portable.cpp
    src/backend/ghash_portable.cpp
)

# Conditionally add SIMD backend sources on x86_64
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT TINYAES_FORCE_PORTABLE)
    list(APPEND TINYAES_SOURCES
        src/backend/aes_aesni.cpp
        src/backend/aes_vaes.cpp
        src/backend/ghash_pclmulqdq.cpp
        src/backend/ghash_vpclmulqdq.cpp
    )
endif()

# Conditionally add ARM64 crypto extension backend sources
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64" AND NOT TINYAES_FORCE_PORTABLE)
    list(APPEND TINYAES_SOURCES
        src/backend/aes_arm_ce.cpp
        src/backend/ghash_arm_ce.cpp
    )
endif()

add_library(tinyaes ${TINYAES_SOURCES})

# --- Shared library symbol visibility ---
if(BUILD_SHARED_LIBS)
    set_target_properties(tinyaes PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
    target_compile_definitions(tinyaes PUBLIC TINYAES_SHARED=1)
    target_compile_definitions(tinyaes PRIVATE TINYAES_BUILDING=1)
endif()

if(TINYAES_FORCE_PORTABLE)
    target_compile_definitions(tinyaes PRIVATE TINYAES_FORCE_PORTABLE=1)
endif()

target_include_directories(tinyaes PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# Consumers get SYSTEM includes so tinyaes headers don't trigger their warnings
target_include_directories(tinyaes SYSTEM INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
# Internal headers (src/) for the library itself
target_include_directories(tinyaes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

set_target_properties(tinyaes PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

# --- Link bcrypt on Windows for BCryptGenRandom ---
if(WIN32)
    target_link_libraries(tinyaes PRIVATE bcrypt)
endif()

# --- Detect MinGW environment ---
# Only use CMake's built-in MINGW (set for GCC with MinGW). Clang on Windows can
# target either MinGW or MSVC ABI, and the linker varies (GNU ld vs lld-link) —
# applying the wrong linker flags breaks the build. We let Clang on Windows rely
# on the OS defaults for DEP/ASLR (enabled by default on modern Windows) rather
# than guessing the linker style.

# --- Hardening flags ---
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tinyaes PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -Wconversion -Wsign-conversion -Wshadow
        -Wcast-align -Wundef
        -Wformat=2 -Wformat-security
        -fno-strict-overflow
    )
    # Stack protector: requires libssp or compiler-rt at link time.
    # Probe with try_compile to catch Clang+MinGW toolchains that lack libssp.
    include(CheckCXXSourceCompiles)
    set(CMAKE_REQUIRED_FLAGS "-fstack-protector-strong")
    check_cxx_source_compiles("int main() { char buf[64]; buf[0] = 0; return buf[0]; }" HAS_STACK_PROTECTOR_STRONG)
    unset(CMAKE_REQUIRED_FLAGS)
    if(HAS_STACK_PROTECTOR_STRONG)
        target_compile_options(tinyaes PRIVATE -fstack-protector-strong)
    endif()
    # -fPIC is implied on Windows PE; only needed on ELF/Mach-O
    if(NOT WIN32)
        target_compile_options(tinyaes PRIVATE -fPIC)
        # Stack clash protection (Linux only — not supported on macOS/Apple Clang)
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            target_compile_options(tinyaes PRIVATE -fstack-clash-protection)
        endif()
    endif()
    # Intel CET control flow integrity (x86_64 only)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        check_cxx_compiler_flag("-fcf-protection=full" HAS_CF_PROTECTION)
        if(HAS_CF_PROTECTION)
            target_compile_options(tinyaes PRIVATE -fcf-protection=full)
        endif()
    endif()
    # -Wstrict-aliasing=2 only for GCC (Clang doesn't support the =2 level)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_compile_options(tinyaes PRIVATE -Wstrict-aliasing=2)
        # libstdc++ container assertions in Debug (bounds checks on operator[], etc.)
        target_compile_options(tinyaes PRIVATE
            $<$<CONFIG:Debug>:-D_GLIBCXX_ASSERTIONS>
        )
    endif()
    # _FORTIFY_SOURCE requires optimization; not reliably supported on MinGW
    if(NOT WIN32)
        target_compile_options(tinyaes PRIVATE
            $<$<NOT:$<CONFIG:Debug>>:-D_FORTIFY_SOURCE=2>
        )
    endif()
    # Linker hardening (use LINK_FLAGS for CMake 3.10 compat)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET tinyaes APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,-z,relro,-z,now -Wl,-z,noexecstack")
        # Reject undefined symbols in shared libs (catches missing TINYAES_EXPORT)
        if(BUILD_SHARED_LIBS)
            set_property(TARGET tinyaes APPEND_STRING PROPERTY LINK_FLAGS " -Wl,-z,defs")
        endif()
    endif()
    # macOS: -bind_at_load is deprecated on modern macOS (eager binding is the default)
    # MinGW linker hardening (DEP + ASLR) — only for GCC+MinGW (known GNU ld)
    if(MINGW)
        set_property(TARGET tinyaes APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,--nxcompat -Wl,--dynamicbase -Wl,--high-entropy-va")
    endif()
elseif(MSVC)
    target_compile_options(tinyaes PRIVATE
        /W4 /WX
        /sdl
        /GS
        /guard:cf
    )
    check_cxx_compiler_flag("/Qspectre" HAS_QSPECTRE)
    if(HAS_QSPECTRE)
        target_compile_options(tinyaes PRIVATE /Qspectre)
    endif()
    set_property(TARGET tinyaes APPEND_STRING PROPERTY LINK_FLAGS
        " /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA")
    # CET shadow stack compatibility
    check_cxx_compiler_flag("/guard:ehcont" HAS_GUARD_EHCONT)
    if(HAS_GUARD_EHCONT)
        target_compile_options(tinyaes PRIVATE /guard:ehcont)
        set_property(TARGET tinyaes APPEND_STRING PROPERTY LINK_FLAGS " /guard:ehcont /CETCOMPAT")
    endif()
endif()

# --- Per-backend SIMD compile flags ---
# MinGW GCC + Debug: Windows x64 ABI only guarantees 16-byte stack alignment,
# but YMM/ZMM registers need 32/64-byte alignment. At -O0, GCC spills AVX
# registers to the stack with aligned moves (vmovdqa), which SIGSEGV on the
# misaligned stack. Force -O1 for SIMD backends in Debug mode so GCC keeps
# values in registers. -mpreferred-stack-boundary=5 is blocked by the ABI.
set(_MINGW_SIMD_FIX "")
if(MINGW AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(_MINGW_SIMD_FIX " $<$<CONFIG:Debug>:-O1>")
endif()

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64" AND NOT TINYAES_FORCE_PORTABLE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(src/backend/aes_aesni.cpp PROPERTIES
            COMPILE_FLAGS "-maes -msse4.1")
        set_source_files_properties(src/backend/aes_vaes.cpp PROPERTIES
            COMPILE_FLAGS "-mavx512f -mvaes -maes -msse4.1${_MINGW_SIMD_FIX}")
        set_source_files_properties(src/backend/ghash_pclmulqdq.cpp PROPERTIES
            COMPILE_FLAGS "-mpclmul -msse4.1")
        set_source_files_properties(src/backend/ghash_vpclmulqdq.cpp PROPERTIES
            COMPILE_FLAGS "-mavx512f -mvpclmulqdq -mpclmul -msse4.1${_MINGW_SIMD_FIX}")
    elseif(MSVC)
        # MSVC: AES-NI and PCLMULQDQ available without extra flags on x64
        set_source_files_properties(src/backend/aes_vaes.cpp PROPERTIES
            COMPILE_FLAGS "/arch:AVX512")
        set_source_files_properties(src/backend/ghash_vpclmulqdq.cpp PROPERTIES
            COMPILE_FLAGS "/arch:AVX512")
    endif()
endif()

# --- Per-backend ARM64 crypto extension compile flags ---
if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|arm64|ARM64" AND NOT TINYAES_FORCE_PORTABLE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        set_source_files_properties(src/backend/aes_arm_ce.cpp PROPERTIES
            COMPILE_FLAGS "-march=armv8-a+crypto")
        set_source_files_properties(src/backend/ghash_arm_ce.cpp PROPERTIES
            COMPILE_FLAGS "-march=armv8-a+crypto")
    endif()
    # MSVC on ARM64: crypto extensions are enabled by default
endif()

# Apply sanitizers to the library itself (no-op if disabled).
tinyaes_apply_sanitizers(tinyaes)

# --- Tests / Benchmarks / Fuzzing ---
# Only build in a top-level tinyaes build. The guards above force all three
# options OFF when consumed via add_subdirectory(), so downstream consumers
# never inherit our dev targets or their link-time constraints.
if(TINYAES_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(TINYAES_BUILD_BENCHMARKS)
    add_subdirectory(bench)
endif()

if(TINYAES_BUILD_FUZZERS
        AND CMAKE_CXX_COMPILER_ID MATCHES "Clang"
        AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_subdirectory(fuzz)
endif()
