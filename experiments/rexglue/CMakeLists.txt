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
    src/rex_nui_bridge.cpp
)

set(FNK_REPOSITORY_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
file(GLOB FNK_ONNXRUNTIME_ROOTS LIST_DIRECTORIES true
    "${FNK_REPOSITORY_ROOT}/tools/pose/onnxruntime-win-x64-*")
list(LENGTH FNK_ONNXRUNTIME_ROOTS FNK_ONNXRUNTIME_COUNT)
if(FNK_ONNXRUNTIME_COUNT EQUAL 0)
    message(FATAL_ERROR
        "ONNX Runtime is missing under tools/pose. Run scripts/Setup-Pose.ps1 first.")
endif()
list(GET FNK_ONNXRUNTIME_ROOTS -1 FNK_ONNXRUNTIME_ROOT)

add_library(fnk_rex_onnxruntime SHARED IMPORTED)
set_target_properties(fnk_rex_onnxruntime PROPERTIES
    IMPORTED_IMPLIB "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${FNK_ONNXRUNTIME_ROOT}/include"
)

add_library(fnk_rex_nui STATIC
    "${FNK_REPOSITORY_ROOT}/src/camera/media_foundation_camera.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/depth_synthesizer.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/dispatcher.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/frame_store.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/game_space_calibration.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/guest_marshaller.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/movenet_adapter.cpp"
    "${FNK_REPOSITORY_ROOT}/src/nui/skeleton_mapper.cpp"
    "${FNK_REPOSITORY_ROOT}/src/pose/movenet_onnx.cpp"
)
target_include_directories(fnk_rex_nui PUBLIC "${FNK_REPOSITORY_ROOT}/include")
target_compile_features(fnk_rex_nui PUBLIC cxx_std_20)
target_compile_definitions(fnk_rex_nui PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(fnk_rex_nui PRIVATE fnk_rex_onnxruntime
    mf mfplat mfreadwrite mfuuid ole32)

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
target_link_libraries(fruit_ninja_kinect_recomp PRIVATE fnk_rex_nui)

add_custom_command(TARGET fruit_ninja_kinect_recomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
        "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
        "${FNK_REPOSITORY_ROOT}/tools/pose/models/movenet-singlepose-lightning.onnx"
        $<TARGET_FILE_DIR:fruit_ninja_kinect_recomp>
    VERBATIM
)

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
