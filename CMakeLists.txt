cmake_minimum_required(VERSION 3.20)
project(DubStemMixPortable VERSION 0.1.0 LANGUAGES C CXX)
# Portable DSP and optional experimental Windows application.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
include(CheckCSourceCompiles)
check_c_source_compiles("#include <stdatomic.h>
int main(void) { _Atomic float f = 0; _Atomic long long t = 0;
atomic_store(&f, 1); return atomic_load(&t) != 0; }" DUB_HAS_C11_ATOMICS)
if(NOT DUB_HAS_C11_ATOMICS)
  message(FATAL_ERROR "C11 atomics required. On Windows use Visual Studio 2022 with ClangCL (-T ClangCL).")
endif()
add_library(DubDSP STATIC
  Sources/DubDSP/dub_brake.c
  Sources/DubDSP/dub_delay.c
  Sources/DubDSP/dub_effect.c
  Sources/DubDSP/dub_flanger.c
  Sources/DubDSP/dub_limiter.c
  Sources/DubDSP/dub_loudness.c
  Sources/DubDSP/dub_master.c
  Sources/DubDSP/dub_phaser.c
  Sources/DubDSP/dub_plate.c
  Sources/DubDSP/dub_portal.c
  Sources/DubDSP/dub_siren.c
  Sources/DubDSP/dub_spring.c
  Sources/DubDSP/dub_sub.c
  Sources/DubDSP/dub_switch.c
  Sources/DubDSP/dub_syndrum.c
  Sources/DubDSP/dub_wah.c
)
target_include_directories(DubDSP PUBLIC Sources/DubDSP/include)
if(NOT WIN32)
  target_link_libraries(DubDSP PUBLIC m)
endif()
include(CTest)
if(BUILD_TESTING)
  add_executable(dub_dsp_portability Tests/PortableDSP/portability.c)
  target_link_libraries(dub_dsp_portability PRIVATE DubDSP)
  add_test(NAME dsp_portability COMMAND dub_dsp_portability)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(BUILD_TESTING)
  add_executable(dub_windows_core_test Tests/WindowsCore/mixer_test.cpp)
  target_include_directories(dub_windows_core_test PRIVATE Sources/WindowsApp)
  target_link_libraries(dub_windows_core_test PRIVATE DubDSP)
  add_test(NAME windows_mixer_core COMMAND dub_windows_core_test)
endif()
option(DUB_BUILD_WINDOWS_APP "Build experimental Windows desktop application" OFF)
if(DUB_BUILD_WINDOWS_APP)
  if(NOT WIN32)
    message(FATAL_ERROR "DUB_BUILD_WINDOWS_APP requires Windows")
  endif()
  include(FetchContent)
  FetchContent_Declare(miniaudio
    GIT_REPOSITORY https://github.com/mackron/miniaudio.git
    GIT_TAG f40cf03f80cdb7e741d43e53b7e706e8c1394bcf
    SOURCE_SUBDIR no-subproject
  )
  FetchContent_MakeAvailable(miniaudio)
  FetchContent_Declare(onnxruntime
    URL https://github.com/microsoft/onnxruntime/releases/download/v1.19.2/onnxruntime-win-x64-1.19.2.zip
    SOURCE_SUBDIR no-subproject
  )
  FetchContent_MakeAvailable(onnxruntime)
  add_library(onnxruntime_cpu SHARED IMPORTED)
  set_target_properties(onnxruntime_cpu PROPERTIES
    IMPORTED_IMPLIB "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.lib"
    IMPORTED_LOCATION "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll"
    INTERFACE_INCLUDE_DIRECTORIES "${onnxruntime_SOURCE_DIR}/include"
  )
  add_executable(DubStemMix WIN32 Sources/WindowsApp/Main.cpp Sources/WindowsApp/Miniaudio.c Sources/WindowsApp/Separator.cpp)
  target_include_directories(DubStemMix PRIVATE Sources/WindowsApp "${miniaudio_SOURCE_DIR}")
  target_compile_definitions(DubStemMix PRIVATE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
  target_link_libraries(DubStemMix PRIVATE DubDSP winmm comctl32 comdlg32 ole32 user32 gdi32 bcrypt winhttp shell32 uuid onnxruntime_cpu)
  if(MSVC)
    target_compile_options(DubStemMix PRIVATE /utf-8)
  endif()
  if(MINGW)
    # Main.cpp uses wWinMain; MinGW needs -municode to pick the Unicode entry
    # point. -static keeps the portable package independent of GCC runtime DLLs.
    target_link_options(DubStemMix PRIVATE -municode -static)
  endif()
  add_custom_command(TARGET DubStemMix POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll" "$<TARGET_FILE_DIR:DubStemMix>"
  )
  install(TARGETS DubStemMix RUNTIME DESTINATION .)
  install(FILES "${onnxruntime_SOURCE_DIR}/lib/onnxruntime.dll" DESTINATION .)
  install(FILES "${onnxruntime_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME ONNXRUNTIME-LICENSE.txt)
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ".")
  include(InstallRequiredSystemLibraries)
  install(FILES LICENSE NOTICE DESTINATION licenses)
  install(FILES "${miniaudio_SOURCE_DIR}/LICENSE" DESTINATION licenses RENAME MINIAUDIO-LICENSE.txt)
  install(FILES docs/WINDOWS_PRIMEROS_PASOS.txt DESTINATION . RENAME LEEME.txt)
endif()
