cmake_minimum_required(VERSION 3.28)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

project(gputester VERSION "2.0.0.0" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

add_executable(${PROJECT_NAME} main.cpp app.manifest app.rc)

set(_WIN32_WINNT_WIN10 0x0A00)
set(NTDDI_WIN10_NI 0x0A00000C)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    _CRT_NON_CONFORMING_SWPRINTFS
    _CRT_SECURE_NO_WARNINGS _CRT_SECURE_NO_DEPRECATE
    _CRT_NONSTDC_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE
    _SCL_SECURE_NO_WARNINGS _SCL_SECURE_NO_DEPRECATE
    _ENABLE_EXTENDED_ALIGNED_STORAGE # STL fixed a bug which breaks binary compatibility, thus need to be enabled manually by defining this.
    _USE_MATH_DEFINES # Enable the PI constant define for the math headers and also fix the redefinition error caused by Windows SDK's unguarded math macros.
    NOMINMAX # Avoid the Win32 macros min/max conflict with std::min()/std::max().
    UNICODE _UNICODE # Use the -W APIs by default (the -A APIs are just wrappers of the -W APIs internally, so calling the -W APIs directly is more efficient).
    STRICT # https://learn.microsoft.com/en-us/windows/win32/winprog/enabling-strict
    WIN32_LEAN_AND_MEAN WINRT_LEAN_AND_MEAN # Filter out some rarely used headers, to increase compilation speed.
    # According to MS docs, both "WINVER" and "_WIN32_WINNT" should be defined
    # at the same time and they should use exactly the same value.
    WINVER=${_WIN32_WINNT_WIN10} _WIN32_WINNT=${_WIN32_WINNT_WIN10}
    _WIN32_IE=${_WIN32_WINNT_WIN10} NTDDI_VERSION=${NTDDI_WIN10_NI}
    GDIPVER=0x0110 # Enable GDI+ v1.1, which is available since Windows Vista.
)

target_compile_options(${PROJECT_NAME} PRIVATE
    /options:strict /bigobj /utf-8 /MP /EHsc /GR /Zc:__cplusplus /permissive- /w
    $<$<CONFIG:Release>:/QIntel-jcc-erratum /GA /Gw /Gy /Zc:inline /guard:cf /guard:ehcont>
)

target_link_options(${PROJECT_NAME} PRIVATE
    /WX /TSAWARE /DYNAMICBASE /FIXED:NO /NXCOMPAT /HIGHENTROPYVA /LARGEADDRESSAWARE
    $<$<CONFIG:Release>:/OPT:REF /OPT:ICF /OPT:LBR /CETCOMPAT /GUARD:CF /guard:ehcont>
)