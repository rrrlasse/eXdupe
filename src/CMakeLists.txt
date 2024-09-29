cmake_minimum_required(VERSION 3.6)

project (exdupe)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT exdupe)

set(BUILD_SHARED_LIBS OFF)
if(MSVC)
else()
	set(CMAKE_EXE_LINKER_FLAGS "-static")
endif()
set(CMAKE_CXX_STANDARD 20)

function(apply_compile_options_to_files files_list options)
    foreach(file ${files_list})
        set_property(SOURCE ${file} APPEND PROPERTY COMPILE_OPTIONS ${options})
    endforeach()
endfunction()

file(GLOB MODULES *.cppm)
file(GLOB EXDUPE *.cppm *.h *.hpp *.cpp)
file(GLOB TREX ${CMAKE_CURRENT_SOURCE_DIR}/trex/*.cpp  ${CMAKE_CURRENT_SOURCE_DIR}/trex/*.h)
file(GLOB LUA ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.c ${CMAKE_CURRENT_SOURCE_DIR}/lua/*.h)

if(MSVC)
	add_definitions(-DUNICODE -D_UNICODE -D_CRT_SECURE_NO_WARNINGS -D_CRT_NON_CONFORMING_SWPRINTFS)
	set(flags_to_apply /W3)
	file (GLOB SHADOW shadow/*.h shadow/*.cpp)
	list (APPEND EXDUPE ${SHADOW})
	source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${LUA})
	# Make VS not use Debug and Release sub dirs
	set(CMAKE_RUNTIME_OUTPUT_DIRECTORY $<1:${CMAKE_BINARY_DIR}/..>)
	
else()
	set(flags_to_apply -fmodules-ts) # FIXME  -Wextra -pedantic
	add_definitions(-DLUA_USE_MKSTEMP)
endif()

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${TREX})
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${SHADOW})

list (APPEND SOURCES ${EXDUPE} ${LUA} ${TREX})

apply_compile_options_to_files("${EXDUPE}" "${flags_to_apply}")
apply_compile_options_to_files("${MODULES}" "-xc++")

add_executable(exdupe ${SOURCES})

#add_subdirectory(libexdupe/xxHash/cmake_unofficial)

add_subdirectory(libexdupe)
add_subdirectory(lua)
add_subdirectory(test)

target_link_libraries(exdupe PRIVATE libexdupe)
target_link_libraries(exdupe PRIVATE lua_static)

execute_process(
    COMMAND git rev-parse --abbrev-ref HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_BRANCH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(
    COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
FILE (WRITE ${CMAKE_BINARY_DIR}/timestamp.cmake "STRING(TIMESTAMP TIMEZ %Y-%m-%dT%H:%M:%S%z)\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(WRITE timestamp.h \"#pragma once\\n\")\n")
FILE (APPEND ${CMAKE_BINARY_DIR}/timestamp.cmake "FILE(APPEND timestamp.h \"#define _TIMEZ_ \\\"\${TIMEZ}\\\"\\n\\n\")\n")
ADD_CUSTOM_TARGET (
    timestamp
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_BINARY_DIR}/timestamp.cmake
    ADD_DEPENDENCIES ${CMAKE_BINARY_DIR}/timestamp.cmake)
ADD_DEPENDENCIES (exdupe timestamp)
target_include_directories(exdupe PRIVATE ${CMAKE_BINARY_DIR})
target_compile_definitions(${PROJECT_NAME} PRIVATE "-DGIT_COMMIT_HASH=\"${GIT_COMMIT_HASH}\"")
