# fruit_ninja_kinect_recomp - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(fruit_ninja_kinect_recomp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Sources
set(FRUITNINJAKINECTRECOMP_SOURCES
    src/main.cpp
)

if(WIN32)
    add_executable(fruit_ninja_kinect_recomp WIN32 ${FRUITNINJAKINECTRECOMP_SOURCES})
else()
    add_executable(fruit_ninja_kinect_recomp ${FRUITNINJAKINECTRECOMP_SOURCES})
endif()

# The runtime loader uses Windows' no-prefix DLL convention. CMake otherwise
# adds the Unix-style "lib" prefix when targeting MinGW.
if(MINGW AND TARGET rexgpu-xenos)
    set_target_properties(rexgpu-xenos PROPERTIES PREFIX "")
endif()

rexglue_setup_target(fruit_ninja_kinect_recomp GPU_PLUGINS xenos)

# ReXGlue's SDL host uses wWinMain on Windows. LLVM-MinGW otherwise links the
# ANSI WinMain startup object for WIN32 executables.
if(MINGW)
    target_link_options(fruit_ninja_kinect_recomp PRIVATE -municode)

    get_filename_component(REX_MINGW_RUNTIME_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(TARGET fruit_ninja_kinect_recomp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${REX_MINGW_RUNTIME_DIR}/libc++.dll"
            "${REX_MINGW_RUNTIME_DIR}/libunwind.dll"
            $<TARGET_FILE_DIR:fruit_ninja_kinect_recomp>
        VERBATIM
    )
endif()
