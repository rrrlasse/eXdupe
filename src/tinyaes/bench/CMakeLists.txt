add_executable(tinyaes_benchmarks
    bench_all.cpp
)

target_link_libraries(tinyaes_benchmarks PRIVATE tinyaes)
tinyaes_apply_sanitizers(tinyaes_benchmarks)
target_include_directories(tinyaes_benchmarks PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(tinyaes_benchmarks PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Warning flags for benchmarks
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tinyaes_benchmarks PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET tinyaes_benchmarks APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,-z,relro,-z,now -Wl,-z,noexecstack")
    endif()
    # macOS: -bind_at_load is deprecated on modern macOS (eager binding is the default)
    if(MINGW)
        set_property(TARGET tinyaes_benchmarks APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,--nxcompat -Wl,--dynamicbase -Wl,--high-entropy-va")
    endif()
elseif(MSVC)
    target_compile_options(tinyaes_benchmarks PRIVATE /W4 /WX)
    set_property(TARGET tinyaes_benchmarks APPEND_STRING PROPERTY LINK_FLAGS
        " /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA")
endif()
