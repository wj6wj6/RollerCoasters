# Linux build of the platform-neutral code and its CTest checks, for the cheap
# Linux CI job. The real app needs Windows (bundled MSVC FLTK, Win32 input),
# so the Windows workflow and scripts/check.ps1 stay the full gate; this only
# catches compile errors, warnings and failing checks early.
#
#   cmake -S tests/linux -B out/build/linux -G Ninja
#   cmake --build out/build/linux
#   xvfb-run -a ctest --test-dir out/build/linux --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(RollerCoastersLinuxChecks LANGUAGES C CXX)

if(WIN32)
    message(FATAL_ERROR "tests/linux is the Linux CI build. On Windows use scripts/check.ps1.")
endif()

get_filename_component(ROOT "${PROJECT_SOURCE_DIR}/../.." ABSOLUTE)
find_package(FLTK REQUIRED)
find_package(OpenGL REQUIRED COMPONENTS OpenGL)

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_17)
# stubs/ stands in for <windows.h> and the Windows-cased GL headers.
target_include_directories(project_options INTERFACE "${PROJECT_SOURCE_DIR}/stubs")
target_compile_definitions(project_options INTERFACE ROLLERCOASTERS_VERSION="linux-check")
# Warnings are errors, as with /W3 /WX on Windows. MSVC-only pragmas are ignored.
target_compile_options(project_options INTERFACE -Wall -Werror -Wno-unknown-pragmas
    "$<$<COMPILE_LANGUAGE:CXX>:SHELL:-include ${PROJECT_SOURCE_DIR}/stubs/msvc_crt.h>")

add_library(fltk_deps INTERFACE)
target_include_directories(fltk_deps SYSTEM INTERFACE ${FLTK_INCLUDE_DIR})
target_link_libraries(fltk_deps INTERFACE ${FLTK_LIBRARIES} OpenGL::GL ${OPENGL_glu_LIBRARY})

add_library(glad STATIC "${ROOT}/include/glad4.6/src/glad.c")
target_include_directories(glad SYSTEM PUBLIC "${ROOT}/include/glad4.6/include")
target_link_libraries(glad PUBLIC ${CMAKE_DL_LIBS})

file(GLOB utility_sources CONFIGURE_DEPENDS "${ROOT}/src/Utilities/*.cpp")
add_library(Utilities STATIC ${utility_sources})
target_include_directories(Utilities PUBLIC "${ROOT}/src/Utilities")
target_link_libraries(Utilities PUBLIC project_options fltk_deps)

enable_testing()
# check(<name> [TIMEOUT s] [GUI] SOURCES <files relative to the repo> LIBS <targets>)
function(check name)
    cmake_parse_arguments(CHECK "GUI" "TIMEOUT" "SOURCES;LIBS" ${ARGN})
    list(TRANSFORM CHECK_SOURCES PREPEND "${ROOT}/")
    add_executable(${name} "${ROOT}/tests/${name}.cpp" ${CHECK_SOURCES})
    target_include_directories(${name} PRIVATE "${ROOT}/src")
    target_link_libraries(${name} PRIVATE project_options ${CHECK_LIBS})
    add_test(NAME ${name} COMMAND ${name})
    if(NOT CHECK_TIMEOUT)
        set(CHECK_TIMEOUT 30)
    endif()
    set_tests_properties(${name} PROPERTIES TIMEOUT ${CHECK_TIMEOUT})
endfunction()

check(GestureMotionCheck LIBS Utilities)
check(FlyCamCheck LIBS Utilities)
check(WaterFlowCheck SOURCES src/WaterFlow.cpp)
check(EditKeysCheck SOURCES src/EditKeys.cpp LIBS Utilities)
check(HeadLookCheck LIBS Utilities)
check(TwoFingerGestureCheck LIBS Utilities)
check(TrackRoundTripCheck SOURCES src/Track.cpp src/ControlPoint.cpp LIBS Utilities)
check(TrackSpacingCheck SOURCES src/Track.cpp src/ControlPoint.cpp src/TrackRenderer.cpp
    LIBS Utilities glad)
check(GizmoDragCheck SOURCES src/TransformGizmo.cpp LIBS glad)
check(TrackHistoryCheck SOURCES src/TrackHistory.cpp src/Track.cpp src/ControlPoint.cpp
    LIBS Utilities)
check(TrackInsertCheck
    SOURCES src/TrackInsert.cpp src/Track.cpp src/ControlPoint.cpp src/TransformGizmo.cpp
    LIBS Utilities glad)
check(FrustumCheck SOURCES src/Frustum.cpp LIBS glad)
check(TexturePackCheck
    SOURCES src/TexturePack.cpp src/ZipArchive.cpp src/LandTextures.cpp src/PixelArt.cpp
    LIBS Utilities glad)
check(OceanRideClearanceCheck TIMEOUT 120
    SOURCES src/OceanWorld.cpp src/Frustum.cpp src/LandTextures.cpp src/PixelArt.cpp
        src/WorldShape.cpp src/TexturePack.cpp src/ZipArchive.cpp src/Track.cpp
        src/ControlPoint.cpp
    LIBS Utilities glad)
check(LandRideClearanceCheck TIMEOUT 120
    SOURCES src/LandWorld.cpp src/Frustum.cpp src/LandTextures.cpp src/OceanWorld.cpp
        src/PixelArt.cpp src/TexturePack.cpp src/WaterFlow.cpp src/WorldShape.cpp
        src/ZipArchive.cpp src/Track.cpp src/ControlPoint.cpp
    LIBS Utilities glad)

# Compile the rest of the app as well (not linked), so an error in code that no
# check uses still shows up here. These three need the real Win32 API; the
# MinGW pass in win32-syntax.sh covers them.
file(GLOB app_sources CONFIGURE_DEPENDS "${ROOT}/src/*.cpp")
list(FILTER app_sources EXCLUDE REGEX "/(main|TrainView|TouchpadGestures)\\.cpp$")
add_library(AppObjects OBJECT ${app_sources})
target_include_directories(AppObjects PRIVATE "${ROOT}/src")
target_include_directories(AppObjects SYSTEM PRIVATE "${ROOT}/include/glm-0.9.8.5/glm")
target_link_libraries(AppObjects PRIVATE project_options Utilities glad)
