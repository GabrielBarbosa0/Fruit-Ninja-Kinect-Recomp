cmake_minimum_required(VERSION 3.20)

project(FruitNinjaKinectRecomp LANGUAGES CXX)

option(FNK_BUILD_TESTS "Build the native compatibility-layer tests" ON)
option(FNK_BUILD_CAMERA "Build the Windows webcam capture backend" ON)
option(FNK_BUILD_POSE "Build the ONNX MoveNet pose-estimation backend" ON)
option(FNK_BUILD_REVERSE_TOOLS "Build local XEX reverse-engineering probes" OFF)
option(FNK_BUILD_XENON_HOOKS "Build NUI overrides against generated XenonRecomp output" OFF)
option(FNK_BUILD_XENON_RUNTIME_BRIDGE
    "Build the NUI GuestMemory bridge for tools/Xenon-Recomp" OFF)

add_library(fnk_nui STATIC
    src/nui/depth_synthesizer.cpp
    src/nui/dispatcher.cpp
    src/nui/flat_guest_memory.cpp
    src/nui/frame_store.cpp
    src/nui/game_space_calibration.cpp
    src/nui/guest_marshaller.cpp
    src/nui/mediapipe_adapter.cpp
    src/nui/movenet_adapter.cpp
    src/nui/skeleton_mapper.cpp
)

target_include_directories(fnk_nui PUBLIC include)
target_compile_features(fnk_nui PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(fnk_nui PRIVATE /W4 /permissive-)
else()
    target_compile_options(fnk_nui PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32 AND FNK_BUILD_CAMERA)
    add_library(fnk_camera STATIC
        src/camera/media_foundation_camera.cpp
    )
    target_include_directories(fnk_camera PUBLIC include)
    target_compile_features(fnk_camera PUBLIC cxx_std_20)
    target_compile_definitions(fnk_camera PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(fnk_camera PRIVATE mf mfplat mfreadwrite mfuuid ole32)

    if(MSVC)
        target_compile_options(fnk_camera PRIVATE /W4 /permissive-)
    else()
        target_compile_options(fnk_camera PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    add_executable(fnk_camera_test apps/fnk_camera_test.cpp)
    target_link_libraries(fnk_camera_test PRIVATE fnk_camera)
    target_compile_features(fnk_camera_test PRIVATE cxx_std_20)

    if(FNK_BUILD_POSE)
        file(GLOB FNK_ONNXRUNTIME_ROOTS LIST_DIRECTORIES true
            "${CMAKE_SOURCE_DIR}/tools/pose/onnxruntime-win-x64-*")
        list(LENGTH FNK_ONNXRUNTIME_ROOTS FNK_ONNXRUNTIME_COUNT)
        if(FNK_ONNXRUNTIME_COUNT GREATER 0)
            list(GET FNK_ONNXRUNTIME_ROOTS -1 FNK_ONNXRUNTIME_ROOT)
            add_library(fnk_onnxruntime SHARED IMPORTED)
            set_target_properties(fnk_onnxruntime PROPERTIES
                IMPORTED_IMPLIB "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.lib"
                IMPORTED_LOCATION "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                INTERFACE_INCLUDE_DIRECTORIES "${FNK_ONNXRUNTIME_ROOT}/include"
            )

            add_library(fnk_pose STATIC src/pose/movenet_onnx.cpp)
            target_include_directories(fnk_pose PUBLIC include)
            target_compile_features(fnk_pose PUBLIC cxx_std_20)
            target_link_libraries(fnk_pose PUBLIC fnk_camera fnk_nui PRIVATE fnk_onnxruntime)
            if(MSVC)
                target_compile_options(fnk_pose PRIVATE /W4 /permissive-)
            else()
                target_compile_options(fnk_pose PRIVATE -Wall -Wextra -Wpedantic)
            endif()

            add_executable(fnk_pose_test apps/fnk_pose_test.cpp)
            target_link_libraries(fnk_pose_test PRIVATE fnk_pose)
            target_compile_features(fnk_pose_test PRIVATE cxx_std_20)
            add_custom_command(TARGET fnk_pose_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                    "$<TARGET_FILE_DIR:fnk_pose_test>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
                    "$<TARGET_FILE_DIR:fnk_pose_test>"
            )

            add_executable(fnk_calibration_test apps/fnk_calibration_test.cpp)
            target_link_libraries(fnk_calibration_test PRIVATE fnk_pose)
            target_compile_features(fnk_calibration_test PRIVATE cxx_std_20)
            add_custom_command(TARGET fnk_calibration_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime.dll"
                    "$<TARGET_FILE_DIR:fnk_calibration_test>"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${FNK_ONNXRUNTIME_ROOT}/lib/onnxruntime_providers_shared.dll"
                    "$<TARGET_FILE_DIR:fnk_calibration_test>"
            )
        else()
            message(WARNING
                "ONNX Runtime not found under tools/pose; fnk_pose is disabled. "
                "Run scripts/Setup-Pose.ps1 and configure again.")
        endif()
    endif()
endif()

if(FNK_BUILD_TESTS)
    enable_testing()

    add_executable(fnk_nui_tests tests/nui_tests.cpp)
    target_link_libraries(fnk_nui_tests PRIVATE fnk_nui)
    target_compile_features(fnk_nui_tests PRIVATE cxx_std_20)

    add_test(NAME fnk_nui_tests COMMAND fnk_nui_tests)

    # Compile the PPC hook translation unit against XenonRecomp's real context
    # definition even before a complete game-code generation is available.
    add_library(fnk_xenon_hook_compile_test STATIC src/nui/xenon_hooks.cpp)
    target_compile_features(fnk_xenon_hook_compile_test PRIVATE cxx_std_20)
    target_compile_definitions(fnk_xenon_hook_compile_test PRIVATE PPC_CONFIG_H_INCLUDED)
    target_include_directories(fnk_xenon_hook_compile_test PRIVATE
        include
        tools/XenonRecomp/XenonUtils
        tools/XenonRecomp/thirdparty/simde
    )
    target_link_libraries(fnk_xenon_hook_compile_test PRIVATE fnk_nui)
endif()

if(FNK_BUILD_XENON_HOOKS)
    if(EXISTS "${CMAKE_SOURCE_DIR}/generated/ppc/ppc_context.h")
        add_library(fnk_xenon_nui_hooks STATIC src/nui/xenon_hooks.cpp)
        target_compile_features(fnk_xenon_nui_hooks PRIVATE cxx_std_20)
        target_include_directories(fnk_xenon_nui_hooks PRIVATE
            include
            generated/ppc
            tools/XenonRecomp/thirdparty/simde
        )
        target_link_libraries(fnk_xenon_nui_hooks PUBLIC fnk_nui)
    else()
        message(FATAL_ERROR "Generate generated/ppc before enabling FNK_BUILD_XENON_HOOKS.")
    endif()
endif()

if(FNK_BUILD_XENON_RUNTIME_BRIDGE)
    # Only import the CPU and memory layers needed by AddressSpace. Importing
    # the runtime's root project would also build its launcher, recompiler and
    # preparation tools, none of which are dependencies of this bridge.
    set(FNK_XENON_RUNTIME_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/tools/Xenon-Recomp"
        CACHE PATH "Path to the modern Xenon-Recomp source tree")
    if(NOT TARGET Xenon::Memory)
        set(XENON_SOURCE_DIR "${FNK_XENON_RUNTIME_ROOT}")
        set(XENON_ENABLE_MEMORY ON)
        set(XENON_MEMORY_DEFAULT_DIRECT_APERTURE OFF)
        include("${FNK_XENON_RUNTIME_ROOT}/cmake/CompilerOptions.cmake")
        include("${FNK_XENON_RUNTIME_ROOT}/cmake/Warnings.cmake")
        add_subdirectory("${FNK_XENON_RUNTIME_ROOT}/src/cpu"
                         "${CMAKE_BINARY_DIR}/xenon-runtime/cpu")
        if(WIN32)
            # Xenon's diagnostic paths use DbgHelp. MSVC obtains it from
            # #pragma comment, while clang/MinGW needs an explicit link edge.
            target_link_libraries(xenon_cpu PUBLIC dbghelp)
        endif()
        add_subdirectory("${FNK_XENON_RUNTIME_ROOT}/src/memory"
                         "${CMAKE_BINARY_DIR}/xenon-runtime/memory")
    endif()

    add_library(fnk_nui_xenon_runtime STATIC
        src/nui/xenon_address_space_memory.cpp
        src/nui/xenon_module_bridge.cpp
        src/nui/xenon_registry_overlay.cpp)
    target_include_directories(fnk_nui_xenon_runtime PUBLIC include)
    target_compile_features(fnk_nui_xenon_runtime PUBLIC cxx_std_20)
    target_link_libraries(fnk_nui_xenon_runtime PUBLIC fnk_nui Xenon::Memory)

    add_executable(fnk_xenon_memory_probe apps/fnk_xenon_memory_probe.cpp)
    target_compile_features(fnk_xenon_memory_probe PRIVATE cxx_std_20)
    target_link_libraries(fnk_xenon_memory_probe PRIVATE fnk_nui_xenon_runtime)
    if(FNK_BUILD_TESTS)
        add_test(NAME fnk_xenon_memory_probe COMMAND fnk_xenon_memory_probe)
    endif()
endif()

if(FNK_BUILD_REVERSE_TOOLS)
    set(FNK_XENON_ROOT "${CMAKE_SOURCE_DIR}/tools/XenonRecomp")
    set(FNK_XENON_BUILD "${FNK_XENON_ROOT}/build-fnk2")
    if(EXISTS "${FNK_XENON_BUILD}/XenonUtils/libXenonUtils.a")
        add_executable(fnk_nui_abi_probe analysis/nui_abi_probe.cpp)
        target_compile_features(fnk_nui_abi_probe PRIVATE cxx_std_20)
        target_include_directories(fnk_nui_abi_probe PRIVATE
            "${FNK_XENON_ROOT}/XenonUtils"
            "${FNK_XENON_ROOT}/thirdparty/disasm"
            "${FNK_XENON_ROOT}/thirdparty/simde"
        )
        target_link_libraries(fnk_nui_abi_probe PRIVATE
            "${FNK_XENON_BUILD}/XenonUtils/libXenonUtils.a"
            "${FNK_XENON_BUILD}/thirdparty/disasm/libdisasm.a"
        )
    else()
        message(FATAL_ERROR "Build XenonRecomp first; libXenonUtils.a is missing.")
    endif()
endif()
