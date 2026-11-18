cmake_minimum_required(VERSION 3.25)
project(RollerCoasters VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
        OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "The bundled FLTK libraries require Windows x64 with MSVC or clang-cl. Use the windows or clang preset.")
endif()

option(ROLLERCOASTERS_BUILD_TESTS "Build regression and integration checks" ON)
option(ROLLERCOASTERS_ANALYZE "Run MSVC static analysis on first-party C++" OFF)

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_17)
target_compile_definitions(project_options INTERFACE
    _XKEYCHECK_H
    ROLLERCOASTERS_VERSION="${PROJECT_VERSION}")
target_compile_options(project_options INTERFACE /W3 /WX /utf-8)
if(ROLLERCOASTERS_ANALYZE)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        message(FATAL_ERROR "MSVC static analysis requires the analyze preset. Build Clang separately with the clang preset.")
    endif()
    target_compile_options(project_options INTERFACE /analyze /analyze:external-)
endif()
# The supplied Debug FLTK archives have no PDBs. Keep other linker diagnostics enabled.
target_link_options(project_options INTERFACE "$<$<CONFIG:Debug>:/IGNORE:4099>")

add_library(fltk INTERFACE)
target_include_directories(fltk SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/include")
foreach(component forms gl images jpeg png z)
    target_link_libraries(fltk INTERFACE
        debug "${PROJECT_SOURCE_DIR}/lib/Debug/fltk_${component}d.lib"
        optimized "${PROJECT_SOURCE_DIR}/lib/Release/fltk_${component}.lib")
endforeach()
target_link_libraries(fltk INTERFACE
    debug "${PROJECT_SOURCE_DIR}/lib/Debug/fltkd.lib"
    optimized "${PROJECT_SOURCE_DIR}/lib/Release/fltk.lib"
    "${PROJECT_SOURCE_DIR}/lib/OpenGL32.Lib"
    "${PROJECT_SOURCE_DIR}/lib/GlU32.Lib")

# Generated third-party C is built separately from the first-party warning policy.
add_library(glad STATIC include/glad4.6/src/glad.c)
target_include_directories(glad SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/include/glad4.6/include")

add_library(Utilities
    src/Utilities/3DUtils.h src/Utilities/3DUtils.cpp
    src/Utilities/ArcBallCam.H src/Utilities/ArcBallCam.cpp
    src/Utilities/FlyCam.H src/Utilities/FlyCam.cpp
    src/Utilities/HeadLook.H src/Utilities/HeadLook.cpp
    src/Utilities/Pnt3f.H src/Utilities/Pnt3f.cpp
    src/Utilities/TwoFingerGesture.H src/Utilities/TwoFingerGesture.cpp)
target_link_libraries(Utilities PUBLIC project_options fltk)

add_executable(RollerCoasters
    src/AdvancedLighting.h src/AdvancedLighting.cpp
    src/Frustum.h src/Frustum.cpp
    src/LandTextures.h src/LandTextures.cpp
    src/LandWorld.h src/LandWorld.cpp
    src/MatrixMath.h
    src/OceanWorld.h src/OceanWorld.cpp
    src/PixelArt.h src/PixelArt.cpp
    src/PostEffects.h src/PostEffects.cpp
    src/TrainModel.h src/TrainModel.cpp
    src/TrainViewEffects.cpp
    src/TransformGizmo.h src/TransformGizmo.cpp
    src/UnderwaterOptics.h
    src/WaterFlow.h src/WaterFlow.cpp
    src/WorldShape.h src/WorldShape.cpp
    src/CallBacks.H src/CallBacks.cpp
    src/EditKeys.h src/EditKeys.cpp
    src/ControlPoint.H src/ControlPoint.cpp
    src/main.cpp src/Object.H
    src/Track.H src/Track.cpp
    src/TrackInsert.h src/TrackInsert.cpp
    src/TouchpadGestures.H src/TouchpadGestures.cpp
    src/TrackRenderer.H src/TrackRenderer.cpp
    src/TrainView.H src/TrainView.cpp
    src/TrainWindow.H src/TrainWindow.cpp
    src/UiTheme.h src/UiTheme.cpp)
target_include_directories(RollerCoasters SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/include/glm-0.9.8.5/glm")
# hid: HidP_* parse the raw precision touchpad reports.
target_link_libraries(RollerCoasters PRIVATE Utilities glad hid imm32)
# Load/Save read and write TrackFiles\ beside the exe (main() moves there), so
# the build makes the folder and Visual Studio debugs from the exe's folder.
add_custom_command(TARGET RollerCoasters POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:RollerCoasters>/TrackFiles"
    VERBATIM)
set_target_properties(RollerCoasters PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:RollerCoasters>")

if(ROLLERCOASTERS_BUILD_TESTS)
    enable_testing()
    add_executable(GestureMotionCheck tests/GestureMotionCheck.cpp)
    target_include_directories(GestureMotionCheck PRIVATE src/Utilities)
    target_link_libraries(GestureMotionCheck PRIVATE Utilities)
    add_test(NAME GestureMotionCheck COMMAND GestureMotionCheck)
    set_tests_properties(GestureMotionCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(FlyCamCheck tests/FlyCamCheck.cpp)
    target_include_directories(FlyCamCheck PRIVATE src/Utilities)
    target_link_libraries(FlyCamCheck PRIVATE Utilities)
    add_test(NAME FlyCamCheck COMMAND FlyCamCheck)
    set_tests_properties(FlyCamCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(WaterFlowCheck tests/WaterFlowCheck.cpp src/WaterFlow.cpp)
    target_include_directories(WaterFlowCheck PRIVATE src)
    target_link_libraries(WaterFlowCheck PRIVATE project_options)
    add_test(NAME WaterFlowCheck COMMAND WaterFlowCheck)
    set_tests_properties(WaterFlowCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(EditKeysCheck tests/EditKeysCheck.cpp src/EditKeys.cpp)
    target_include_directories(EditKeysCheck PRIVATE src src/Utilities)
    target_link_libraries(EditKeysCheck PRIVATE Utilities)
    add_test(NAME EditKeysCheck COMMAND EditKeysCheck)
    set_tests_properties(EditKeysCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(HeadLookCheck tests/HeadLookCheck.cpp)
    target_include_directories(HeadLookCheck PRIVATE src/Utilities)
    target_link_libraries(HeadLookCheck PRIVATE Utilities)
    add_test(NAME HeadLookCheck COMMAND HeadLookCheck)
    set_tests_properties(HeadLookCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(TwoFingerGestureCheck tests/TwoFingerGestureCheck.cpp)
    target_include_directories(TwoFingerGestureCheck PRIVATE src/Utilities)
    target_link_libraries(TwoFingerGestureCheck PRIVATE Utilities)
    add_test(NAME TwoFingerGestureCheck COMMAND TwoFingerGestureCheck)
    set_tests_properties(TwoFingerGestureCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(TrackRoundTripCheck tests/TrackRoundTripCheck.cpp
        src/Track.cpp src/ControlPoint.cpp)
    target_include_directories(TrackRoundTripCheck PRIVATE src)
    target_link_libraries(TrackRoundTripCheck PRIVATE Utilities)
    add_test(NAME TrackRoundTripCheck COMMAND TrackRoundTripCheck)
    set_tests_properties(TrackRoundTripCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(TrackSpacingCheck tests/TrackSpacingCheck.cpp
        src/Track.cpp src/ControlPoint.cpp src/TrackRenderer.cpp)
    target_include_directories(TrackSpacingCheck PRIVATE src)
    target_link_libraries(TrackSpacingCheck PRIVATE Utilities glad)
    add_test(NAME TrackSpacingCheck COMMAND TrackSpacingCheck)
    set_tests_properties(TrackSpacingCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(TrackInsertCheck tests/TrackInsertCheck.cpp
        src/TrackInsert.cpp src/Track.cpp src/ControlPoint.cpp src/TransformGizmo.cpp)
    target_include_directories(TrackInsertCheck PRIVATE src)
    target_link_libraries(TrackInsertCheck PRIVATE Utilities glad)
    add_test(NAME TrackInsertCheck COMMAND TrackInsertCheck)
    set_tests_properties(TrackInsertCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(GizmoDragCheck tests/GizmoDragCheck.cpp src/TransformGizmo.cpp)
    target_include_directories(GizmoDragCheck PRIVATE src)
    target_link_libraries(GizmoDragCheck PRIVATE glad)
    add_test(NAME GizmoDragCheck COMMAND GizmoDragCheck)
    set_tests_properties(GizmoDragCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(OceanRideClearanceCheck tests/OceanRideClearanceCheck.cpp
        src/OceanWorld.cpp src/Frustum.cpp src/LandTextures.cpp src/PixelArt.cpp src/WorldShape.cpp
        src/Track.cpp src/ControlPoint.cpp)
    target_include_directories(OceanRideClearanceCheck PRIVATE src)
    target_link_libraries(OceanRideClearanceCheck PRIVATE Utilities glad)
    add_test(NAME OceanRideClearanceCheck COMMAND OceanRideClearanceCheck)
    set_tests_properties(OceanRideClearanceCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(LandRideClearanceCheck tests/LandRideClearanceCheck.cpp
        src/LandWorld.cpp src/Frustum.cpp src/LandTextures.cpp src/OceanWorld.cpp src/PixelArt.cpp
        src/WaterFlow.cpp src/WorldShape.cpp src/Track.cpp src/ControlPoint.cpp)
    target_include_directories(LandRideClearanceCheck PRIVATE src)
    target_link_libraries(LandRideClearanceCheck PRIVATE Utilities glad)
    add_test(NAME LandRideClearanceCheck COMMAND LandRideClearanceCheck)
    set_tests_properties(LandRideClearanceCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(FrustumCheck tests/FrustumCheck.cpp src/Frustum.cpp)
    target_include_directories(FrustumCheck PRIVATE src)
    target_link_libraries(FrustumCheck PRIVATE project_options glad)
    add_test(NAME FrustumCheck COMMAND FrustumCheck)
    set_tests_properties(FrustumCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_test(NAME VersionCheck COMMAND RollerCoasters --version)
    set_tests_properties(VersionCheck PROPERTIES TIMEOUT 10
        PASS_REGULAR_EXPRESSION "RollerCoasters ${PROJECT_VERSION}" LABELS smoke)
endif()

install(TARGETS RollerCoasters RUNTIME DESTINATION .)
install(FILES README.md CONTRIBUTING.md AGENTS.md DESTINATION .)
install(DIRECTORY docs/ DESTINATION docs)
install(DIRECTORY DESTINATION TrackFiles)
