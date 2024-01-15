project (test)

add_executable(test systest.cpp test.cpp ../utilities.cpp ../ui.cpp)
if (MSVC)
	target_link_libraries(test PRIVATE shlwapi.lib)
endif(MSVC)