if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    return()
endif()

# libFuzzer is only reliably available on Linux Clang.
# macOS: Apple Clang lacks the runtime; Homebrew LLVM has ABI mismatches
# with macOS system libc++. Windows: CRT mismatches in Debug/Release.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    return()
endif()

set(FUZZ_TARGETS fuzz_ecb fuzz_cbc fuzz_ctr fuzz_gcm fuzz_ghash fuzz_keyschedule fuzz_padding)

# libFuzzer always includes ASan. When TINYAES_ENABLE_SANITIZERS is ON we
# additionally enable UBSan on the fuzz binaries so nightly runs catch UB
# as well as memory errors.
if(TINYAES_ENABLE_SANITIZERS)
    set(_TINYAES_FUZZ_SAN "fuzzer,address,undefined")
else()
    set(_TINYAES_FUZZ_SAN "fuzzer,address")
endif()

foreach(target ${FUZZ_TARGETS})
    add_executable(${target} ${target}.cpp)
    target_link_libraries(${target} PRIVATE tinyaes)
    target_include_directories(${target} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src
    )
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/fuzz
    )
    target_compile_options(${target} PRIVATE
        -Wall -Wextra -Wpedantic -Werror
        -fsanitize=${_TINYAES_FUZZ_SAN}
        -fno-omit-frame-pointer
    )
    set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS
        " -fsanitize=${_TINYAES_FUZZ_SAN}")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET ${target} APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,-z,relro,-z,now -Wl,-z,noexecstack")
    endif()
endforeach()
