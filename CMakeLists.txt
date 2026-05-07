cmake_minimum_required(VERSION 3.22)

option(QUEST "Build for quest" ON)

# Globals
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Load QPM-generated defines (sets EXTERN_DIR, SHARED_DIR, NDK path, RECURSE_FILES, etc.)
include(${CMAKE_CURRENT_SOURCE_DIR}/qpm_defines.cmake)

add_compile_options(-frtti -fexceptions -fvisibility=hidden -fPIE -fPIC)

if(${CMAKE_BUILD_TYPE} STREQUAL "RELEASE" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo" OR ${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
    add_compile_options(-O3)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    add_compile_options(-flto)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "DEBUG" OR ${CMAKE_BUILD_TYPE} STREQUAL "RelWithDebInfo")
    add_compile_options(-g)
endif()

project(vivify VERSION 0.3.0)

add_compile_definitions(MOD_ID="vivify")
add_compile_definitions(VERSION="0.3.0")

set(COMPILE_ID ${CMAKE_PROJECT_NAME})

# Recursively gather sources
RECURSE_FILES(cpp_file_list ${SOURCE_DIR}/*.cpp)
RECURSE_FILES(c_file_list ${SOURCE_DIR}/*.c)

add_library(
    ${CMAKE_PROJECT_NAME}
    SHARED
    ${cpp_file_list}
    ${c_file_list}
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -llog)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${INCLUDE_DIR})
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${SHARED_DIR})

# Load QPM-generated extern includes and link libraries
include(${CMAKE_CURRENT_SOURCE_DIR}/extern.cmake)

# beatsaber-hook inline hook sources
RECURSE_FILES(src_inline_hook_c   ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.c)
RECURSE_FILES(src_inline_hook_cpp ${EXTERN_DIR}/includes/beatsaber-hook/shared/inline-hook/*.cpp)
target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_c})
target_sources(${COMPILE_ID} PRIVATE ${src_inline_hook_cpp})

# Strip debug symbols in release
if(${CMAKE_BUILD_TYPE} STREQUAL "RELEASE")
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_STRIP} --strip-debug --strip-unneeded
            $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
        COMMENT "Stripping debug symbols"
    )
endif()
