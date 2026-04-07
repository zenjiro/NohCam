cmake_minimum_required(VERSION 3.21)

project(NohCam VERSION 0.1.0 LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "NohCam Phase 1 currently supports Windows desktop builds only.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(NOHCAM_ENABLE_CUBISM_FRAMEWORK "Enable Cubism Framework integration hooks." ON)
option(NOHCAM_ENABLE_ONNXRUNTIME "Enable ONNX Runtime integration hooks." ON)
option(NOHCAM_ENABLE_DIRECTML "Enable DirectML linkage for Windows GPU inference." ON)

set(NOHCAM_CUBISM_SDK_ROOT
    "${CMAKE_SOURCE_DIR}/CubismSdkForNative-5-r.5"
    CACHE PATH
    "Path to the local Cubism SDK for Native checkout."
)

set(NOHCAM_ONNX_MODEL_DIR
    "${CMAKE_SOURCE_DIR}/assets/onnx"
    CACHE PATH
    "Path to the ONNX model directory used by NohCam."
)

set(NOHCAM_ONNXRUNTIME_ROOT
    "${CMAKE_SOURCE_DIR}/third_party/onnxruntime"
    CACHE PATH
    "Path to the unpacked prebuilt ONNX Runtime distribution."
)

find_package(imgui CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(directx-headers CONFIG REQUIRED)

if(NOHCAM_ENABLE_ONNXRUNTIME)
    find_path(NOHCAM_ONNXRUNTIME_INCLUDE_DIR
        NAMES onnxruntime_cxx_api.h
        HINTS "${NOHCAM_ONNXRUNTIME_ROOT}"
        PATH_SUFFIXES
            include
            build/native/include
    )

    find_library(NOHCAM_ONNXRUNTIME_LIBRARY
        NAMES onnxruntime
        HINTS "${NOHCAM_ONNXRUNTIME_ROOT}"
        PATH_SUFFIXES
            lib
            lib/x64
            build/native/lib
            build/native/lib/x64
            runtimes/win-x64/native
    )

    if(NOHCAM_ONNXRUNTIME_INCLUDE_DIR AND NOHCAM_ONNXRUNTIME_LIBRARY)
        add_library(nohcam_onnxruntime INTERFACE)
        target_include_directories(nohcam_onnxruntime INTERFACE "${NOHCAM_ONNXRUNTIME_INCLUDE_DIR}")
        target_link_libraries(nohcam_onnxruntime INTERFACE "${NOHCAM_ONNXRUNTIME_LIBRARY}")
        message(STATUS "Using prebuilt ONNX Runtime from ${NOHCAM_ONNXRUNTIME_ROOT}")
    else()
        message(STATUS "Prebuilt ONNX Runtime not found at ${NOHCAM_ONNXRUNTIME_ROOT}; building without ONNX Runtime linkage.")
    endif()
endif()

if(NOHCAM_ENABLE_DIRECTML)
    set(NOHCAM_WINDOWS_KITS_ROOT "$ENV{WindowsSdkDir}")
    if(NOT NOHCAM_WINDOWS_KITS_ROOT)
        set(NOHCAM_WINDOWS_KITS_ROOT "C:/Program Files (x86)/Windows Kits/10")
    endif()

    set(NOHCAM_WINDOWS_KITS_LIB_HINTS "")
    if(NOHCAM_WINDOWS_KITS_ROOT)
        list(APPEND NOHCAM_WINDOWS_KITS_LIB_HINTS
            "${NOHCAM_WINDOWS_KITS_ROOT}/Lib/${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION}/um/x64"
            "${NOHCAM_WINDOWS_KITS_ROOT}/Lib/10.0.26100.0/um/x64"
        )
    endif()

    find_library(NOHCAM_DIRECTML_LIBRARY
        NAMES directml
        HINTS ${NOHCAM_WINDOWS_KITS_LIB_HINTS}
    )
    if(NOHCAM_DIRECTML_LIBRARY)
        message(STATUS "Using DirectML library: ${NOHCAM_DIRECTML_LIBRARY}")
    else()
        message(STATUS "DirectML library not found; building without DirectML linkage.")
    endif()
endif()

set(NOHCAM_SOURCES
    src/main.cpp
    src/app/Application.cpp
    src/capture/CameraCapture.cpp
    src/render/D3D11Renderer.cpp
    src/ui/ImGuiLayer.cpp
    src/ui/MainWindow.cpp
)

set(NOHCAM_HEADERS
    src/app/Application.h
    src/capture/CameraCapture.h
    src/render/D3D11Renderer.h
    src/ui/ImGuiLayer.h
    src/ui/MainWindow.h
)

add_executable(NohCam WIN32
    ${NOHCAM_SOURCES}
    ${NOHCAM_HEADERS}
)

target_include_directories(NohCam
    PRIVATE
        "${CMAKE_SOURCE_DIR}/src"
)

target_compile_definitions(NohCam
    PRIVATE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        UNICODE
        _UNICODE
        NOHCAM_ONNX_MODEL_DIR=L"${NOHCAM_ONNX_MODEL_DIR}"
)

target_compile_options(NohCam
    PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /EHsc>
)

target_link_libraries(NohCam
    PRIVATE
        imgui::imgui
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        glm::glm
        Microsoft::DirectX-Headers
        d3d11
        dxgi
        d3dcompiler
        mf
        mfplat
        mfreadwrite
        mfuuid
)

if(TARGET nohcam_onnxruntime)
    target_link_libraries(NohCam PRIVATE nohcam_onnxruntime)
    target_compile_definitions(NohCam PRIVATE NOHCAM_HAS_ONNXRUNTIME=1)
endif()

if(NOHCAM_ENABLE_DIRECTML AND NOHCAM_DIRECTML_LIBRARY)
    target_link_libraries(NohCam PRIVATE "${NOHCAM_DIRECTML_LIBRARY}")
    target_compile_definitions(NohCam PRIVATE NOHCAM_HAS_DIRECTML=1)
endif()

if(NOHCAM_ENABLE_CUBISM_FRAMEWORK)
    set(NOHCAM_CUBISM_FRAMEWORK_DIR "${NOHCAM_CUBISM_SDK_ROOT}/Framework")
    set(NOHCAM_CUBISM_CORE_INCLUDE_DIR "${NOHCAM_CUBISM_SDK_ROOT}/Core/include")

    if(EXISTS "${NOHCAM_CUBISM_FRAMEWORK_DIR}/CMakeLists.txt" AND EXISTS "${NOHCAM_CUBISM_CORE_INCLUDE_DIR}/Live2DCubismCore.h")
        set(RENDER_INCLUDE_PATH "${NOHCAM_CUBISM_CORE_INCLUDE_DIR}")
        set(FRAMEWORK_DEFINITIONS "")
        set(FRAMEWORK_SOURCE D3D11)
        add_subdirectory("${NOHCAM_CUBISM_FRAMEWORK_DIR}" "${CMAKE_BINARY_DIR}/cubism-framework")

        target_include_directories(NohCam
            PRIVATE
                "${NOHCAM_CUBISM_CORE_INCLUDE_DIR}"
        )

        target_compile_definitions(NohCam PRIVATE NOHCAM_HAS_CUBISM_SDK=1)

        if(TARGET Framework)
            target_link_libraries(NohCam PRIVATE Framework)
        endif()
    else()
        message(STATUS "Cubism SDK not fully available at ${NOHCAM_CUBISM_SDK_ROOT}; building without Framework linkage.")
    endif()
endif()

set_target_properties(NohCam PROPERTIES
    OUTPUT_NAME "NohCam"
)
