project (test_utilities)

add_executable(test_utilities test_utilities.cpp ../utilities.cpp)
target_link_libraries(test_utilities PRIVATE shlwapi.lib)
