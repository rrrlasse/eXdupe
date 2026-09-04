add_executable(tinyaes_tests
    test_main.cpp
    test_keyschedule.cpp
    test_ecb.cpp
    test_cbc.cpp
    test_padding.cpp
    test_ctr.cpp
    test_ctr_counter_wrap.cpp
    test_gcm.cpp
    test_gcm_extended.cpp
    test_gcm_auth_failure.cpp
    test_iv_generation.cpp
    test_cpuid.cpp
    test_capi.cpp
    test_utilities.cpp
    test_backend_differential.cpp
)

target_link_libraries(tinyaes_tests PRIVATE tinyaes)
tinyaes_apply_sanitizers(tinyaes_tests)
target_include_directories(tinyaes_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
set_target_properties(tinyaes_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Warning flags for tests
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(tinyaes_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_property(TARGET tinyaes_tests APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,-z,relro,-z,now -Wl,-z,noexecstack")
    endif()
    # macOS: -bind_at_load is deprecated on modern macOS (eager binding is the default)
    if(MINGW)
        set_property(TARGET tinyaes_tests APPEND_STRING PROPERTY LINK_FLAGS
            " -Wl,--nxcompat -Wl,--dynamicbase -Wl,--high-entropy-va")
    endif()
elseif(MSVC)
    target_compile_options(tinyaes_tests PRIVATE /W4 /WX)
    set_property(TARGET tinyaes_tests APPEND_STRING PROPERTY LINK_FLAGS
        " /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA")
endif()

add_test(NAME tinyaes_tests COMMAND tinyaes_tests)
