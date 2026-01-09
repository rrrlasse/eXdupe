project (test)

add_executable(test systest.cpp test.cpp ../utilities.cpp ../ui.cpp ../io.cpp ../libexdupe/gxhash/gxhash.c)
if (MSVC)
	target_link_libraries(test PRIVATE shlwapi.lib)
else()
	target_compile_options(test PRIVATE -mvaes -maes -msse2)	
endif(MSVC)