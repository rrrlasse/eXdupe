cmake_minimum_required(VERSION 3.6)

project (exdupe)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT exdupe)

set(CMAKE_CXX_STANDARD 20)

function(apply_compile_options_to_files files_list options)
    foreach(file ${files_list})
        set_source_files_properties(${file} PROPERTIES COMPILE_OPTIONS ${options})
    endforeach()
endfunction()

file(GLOB EXDUPE *.h *.hpp *.cpp)
file(GLOB TREX ${CMAKE_CURRENT_SOURCE_DIR}/trex/*.cpp  ${CMAKE_CURRENT_SOURCE_DIR}/trex/*.h)
file(GLOB LUA ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.h)

if(MSVC)
	add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -D_CRT_NON_CONFORMING_SWPRINTFS)
	set(flags_to_apply /W3)
	file (GLOB SHADOW shadow/*.h shadow/*.cpp)
	list (APPEND EXDUPE ${SHADOW})
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LUA})
else()
	set(flags_to_apply -Wall -Wextra -pedantic)
	add_definitions(-DLUA_USE_MKSTEMP)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TREX})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SHADOW})

list (APPEND SOURCES ${EXDUPE} ${LUA} ${TREX})

apply_compile_options_to_files("${EXDUPE}" "${flags_to_apply}")

add_executable(exdupe ${SOURCES})

set(BUILD_SHARED_LIBS OFF)

add_subdirectory(libexdupe)
add_subdirectory(test)

target_link_libraries(exdupe PRIVATE libexdupe)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)

execute_process(
    COMMAND git rev-parse HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
	

if (WIN32)
    execute_process(
        COMMAND cmd /c "echo %date% %time%"
        OUTPUT_VARIABLE BUILD_TIME
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
else()
    execute_process(
        COMMAND date "+%Y-%m-%d %H:%M:%S"
        OUTPUT_VARIABLE BUILD_TIME
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE "-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")
target_compile_definitions(${PROJECT_NAME} PRIVATE "-DBUILD_TIME=\"${BUILD_TIME}\"")
	