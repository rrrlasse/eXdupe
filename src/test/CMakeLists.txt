project (test)

add_executable(test systest.cpp test.cpp ../utilities.cpp ../ui.cpp ../bytebuffer.cpp ../libexdupe/gxhash/gxhash.c)
if (MSVC)
	target_link_libraries(test PRIVATE shlwapi.lib)
else()
	target_compile_options(test PRIVATE -mvaes -mavx512vl -maes -msse2)	
endif(MSVC)