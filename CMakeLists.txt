cmake_minimum_required(VERSION 3.25)
project(RollerCoasters VERSION 0.1.0 LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC"
        OR NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "RollerCoasters needs Windows x64 with MSVC or clang-cl (Win32 input, the bundled OpenGL import libraries). Use the windows or clang preset.")
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

# OpenGL and GLU: the course's headers (include/GL) and import libraries (lib/).
add_library(opengl INTERFACE)
target_include_directories(opengl SYSTEM INTERFACE "${PROJECT_SOURCE_DIR}/include")
target_link_libraries(opengl INTERFACE
    "${PROJECT_SOURCE_DIR}/lib/OpenGL32.Lib"
    "${PROJECT_SOURCE_DIR}/lib/GlU32.Lib")

# Generated third-party C is built separately from the first-party warning policy.
add_library(glad STATIC include/glad4.6/src/glad.c)
target_include_directories(glad SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/include/glad4.6/include")

# GLFW 3.4 (include/glfw-3.4, zlib licence): the window, its OpenGL context and
# the input. Built from source for Win32 only, outside the first-party warning
# policy like glad.
set(GLFW_DIR "${PROJECT_SOURCE_DIR}/include/glfw-3.4")
add_library(glfw STATIC
    ${GLFW_DIR}/src/context.c ${GLFW_DIR}/src/init.c ${GLFW_DIR}/src/input.c
    ${GLFW_DIR}/src/monitor.c ${GLFW_DIR}/src/platform.c ${GLFW_DIR}/src/vulkan.c
    ${GLFW_DIR}/src/window.c ${GLFW_DIR}/src/egl_context.c ${GLFW_DIR}/src/osmesa_context.c
    ${GLFW_DIR}/src/null_init.c ${GLFW_DIR}/src/null_monitor.c ${GLFW_DIR}/src/null_window.c
    ${GLFW_DIR}/src/null_joystick.c
    ${GLFW_DIR}/src/win32_init.c ${GLFW_DIR}/src/win32_joystick.c ${GLFW_DIR}/src/win32_module.c
    ${GLFW_DIR}/src/win32_monitor.c ${GLFW_DIR}/src/win32_time.c ${GLFW_DIR}/src/win32_thread.c
    ${GLFW_DIR}/src/win32_window.c ${GLFW_DIR}/src/wgl_context.c)
target_include_directories(glfw SYSTEM PUBLIC "${GLFW_DIR}/include")
target_compile_definitions(glfw PRIVATE _GLFW_WIN32 UNICODE _UNICODE _CRT_SECURE_NO_WARNINGS)
target_link_libraries(glfw PRIVATE gdi32 user32 shell32)

# stb_image 2.30 (include/stb-2.30, public domain): decodes the PNG pictures of
# texture packs. Its implementation is one generated C file, outside the
# first-party warning policy like glad.
file(WRITE "${CMAKE_BINARY_DIR}/stb_image.c"
    "#define STB_IMAGE_IMPLEMENTATION\n#define STBI_ONLY_PNG\n#define STBI_NO_STDIO\n"
    "#include <stb_image.h>\n")
add_library(stb_image STATIC "${CMAKE_BINARY_DIR}/stb_image.c")
target_include_directories(stb_image SYSTEM PUBLIC "${PROJECT_SOURCE_DIR}/include/stb-2.30")

add_library(Utilities
    src/Utilities/3DUtils.h src/Utilities/3DUtils.cpp
    src/Utilities/ArcBallCam.H src/Utilities/ArcBallCam.cpp
    src/Utilities/FlyCam.H src/Utilities/FlyCam.cpp
    src/Utilities/HeadLook.H src/Utilities/HeadLook.cpp
    src/Utilities/Input.H
    src/Utilities/GlfwInput.H src/Utilities/GlfwInput.cpp
    src/Utilities/Platform.H src/Utilities/PlatformLoop.cpp
    src/Utilities/Pnt3f.H src/Utilities/Pnt3f.cpp
    src/Utilities/TwoFingerGesture.H src/Utilities/TwoFingerGesture.cpp
    src/Utilities/Utf8File.H)
# stb_image goes along with Utilities: every target that builds TexturePack.cpp links it.
target_link_libraries(Utilities PUBLIC project_options opengl stb_image)
# GlfwInput.cpp reads GLFW's key and button numbers from its header.
target_include_directories(Utilities SYSTEM PUBLIC "${GLFW_DIR}/include")

add_executable(RollerCoasters
    src/AdvancedLighting.h src/AdvancedLighting.cpp
    src/AppControl.h src/AppControl.cpp
    src/BlockMesh.h src/BlockMesh.cpp
    src/ChaseRig.h src/ChaseRig.cpp
    src/Frustum.h src/Frustum.cpp
    src/LandTextures.h src/LandTextures.cpp
    src/LandWaterMotion.h
    src/LandWorld.h src/LandWorld.cpp
    src/MatrixMath.h
    src/Mobs.h src/Mobs.cpp
    src/OceanWorld.h src/OceanWorld.cpp
    src/PerfGpu.h src/PerfGpu.cpp
    src/PerfOverlay.h src/PerfOverlay.cpp
    src/PerfStats.h src/PerfStats.cpp
    src/PixelArt.h src/PixelArt.cpp
    src/PostEffects.h src/PostEffects.cpp
    src/TexturePack.h src/TexturePack.cpp
    src/TrainModel.h src/TrainModel.cpp
    src/TrainViewControl.cpp
    src/TrainViewEffects.cpp
    src/TransformGizmo.h src/TransformGizmo.cpp
    src/UnderwaterOptics.h
    src/WaterFlow.h src/WaterFlow.cpp
    src/WorldShape.h src/WorldShape.cpp
    src/ZipArchive.h src/ZipArchive.cpp
    src/CallBacks.H src/CallBacks.cpp
    src/Folders.h src/Folders.cpp
    src/GameMenu.h src/GameMenu.cpp
    src/EditKeys.h src/EditKeys.cpp
    src/KeyBinds.h src/KeyBinds.cpp
    src/ControlPoint.H src/ControlPoint.cpp
    src/main.cpp
    src/Track.H src/Track.cpp
    src/TrackHistory.h src/TrackHistory.cpp
    src/TrackInsert.h src/TrackInsert.cpp
    src/TouchpadGestures.H src/TouchpadGestures.cpp
    src/TrackRenderer.H src/TrackRenderer.cpp
    src/TrainView.H src/TrainView.cpp
    src/TrainWindow.H src/TrainWindow.cpp
    src/WindowsKey.h src/WindowsKey.cpp
    src/SceneSettings.h
    src/Ui/Gui.h src/Ui/Gui.cpp
    src/Ui/GuiGl.h src/Ui/GuiGl.cpp
    src/Ui/PixelFont.h src/Ui/PixelFont.cpp
    src/Ui/UiAtlas.h src/Ui/UiAtlas.cpp)
target_include_directories(RollerCoasters SYSTEM PRIVATE
    "${PROJECT_SOURCE_DIR}/include/glm-0.9.8.5/glm")
# hid: HidP_* parse the raw precision touchpad reports.
target_link_libraries(RollerCoasters PRIVATE Utilities glad glfw hid imm32)
# Load/Save read and write TrackFiles\ beside the exe (main() moves there), so
# the build makes the folder and Visual Studio debugs from the exe's folder.
add_custom_command(TARGET RollerCoasters POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:RollerCoasters>/TrackFiles"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:RollerCoasters>/texturepacks"
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

    add_executable(EditKeysCheck tests/EditKeysCheck.cpp src/EditKeys.cpp src/KeyBinds.cpp)
    target_include_directories(EditKeysCheck PRIVATE src src/Utilities)
    target_link_libraries(EditKeysCheck PRIVATE Utilities)
    add_test(NAME EditKeysCheck COMMAND EditKeysCheck)
    set_tests_properties(EditKeysCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(KeyBindsCheck tests/KeyBindsCheck.cpp src/KeyBinds.cpp)
    target_include_directories(KeyBindsCheck PRIVATE src src/Utilities)
    target_link_libraries(KeyBindsCheck PRIVATE Utilities)
    add_test(NAME KeyBindsCheck COMMAND KeyBindsCheck)
    set_tests_properties(KeyBindsCheck PROPERTIES TIMEOUT 10 LABELS integration)

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

    add_executable(AppControlCheck tests/AppControlCheck.cpp src/AppControl.cpp
        src/Track.cpp src/ControlPoint.cpp
        src/TexturePack.cpp src/ZipArchive.cpp src/LandTextures.cpp src/PixelArt.cpp)
    target_include_directories(AppControlCheck PRIVATE src)
    target_link_libraries(AppControlCheck PRIVATE Utilities glad)
    add_test(NAME AppControlCheck COMMAND AppControlCheck)
    set_tests_properties(AppControlCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(TrackHistoryCheck tests/TrackHistoryCheck.cpp
        src/TrackHistory.cpp src/Track.cpp src/ControlPoint.cpp)
    target_include_directories(TrackHistoryCheck PRIVATE src)
    target_link_libraries(TrackHistoryCheck PRIVATE Utilities)
    add_test(NAME TrackHistoryCheck COMMAND TrackHistoryCheck)
    set_tests_properties(TrackHistoryCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(TrackInsertCheck tests/TrackInsertCheck.cpp
        src/TrackInsert.cpp src/Track.cpp src/ControlPoint.cpp src/TransformGizmo.cpp)
    target_include_directories(TrackInsertCheck PRIVATE src)
    target_link_libraries(TrackInsertCheck PRIVATE Utilities glad)
    add_test(NAME TrackInsertCheck COMMAND TrackInsertCheck)
    set_tests_properties(TrackInsertCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(GuiCheck tests/GuiCheck.cpp
        src/Ui/Gui.cpp src/Ui/PixelFont.cpp src/Ui/UiAtlas.cpp)
    target_include_directories(GuiCheck PRIVATE src)
    target_link_libraries(GuiCheck PRIVATE project_options)
    add_test(NAME GuiCheck COMMAND GuiCheck)
    set_tests_properties(GuiCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(PerfStatsCheck tests/PerfStatsCheck.cpp src/PerfStats.cpp
        src/PerfOverlay.cpp src/Ui/Gui.cpp src/Ui/PixelFont.cpp src/Ui/UiAtlas.cpp)
    target_include_directories(PerfStatsCheck PRIVATE src)
    target_link_libraries(PerfStatsCheck PRIVATE project_options)
    add_test(NAME PerfStatsCheck COMMAND PerfStatsCheck)
    set_tests_properties(PerfStatsCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(FoldersCheck tests/FoldersCheck.cpp src/Folders.cpp)
    target_include_directories(FoldersCheck PRIVATE src)
    target_link_libraries(FoldersCheck PRIVATE project_options shell32)
    add_test(NAME FoldersCheck COMMAND FoldersCheck)
    set_tests_properties(FoldersCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(WindowsKeyCheck tests/WindowsKeyCheck.cpp)
    target_include_directories(WindowsKeyCheck PRIVATE src)
    target_link_libraries(WindowsKeyCheck PRIVATE project_options)
    add_test(NAME WindowsKeyCheck COMMAND WindowsKeyCheck)
    set_tests_properties(WindowsKeyCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(GlfwInputCheck tests/GlfwInputCheck.cpp)
    target_include_directories(GlfwInputCheck PRIVATE src)
    target_link_libraries(GlfwInputCheck PRIVATE Utilities)
    add_test(NAME GlfwInputCheck COMMAND GlfwInputCheck)
    set_tests_properties(GlfwInputCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(TexturePackCheck tests/TexturePackCheck.cpp
        src/TexturePack.cpp src/ZipArchive.cpp src/LandTextures.cpp src/PixelArt.cpp)
    target_include_directories(TexturePackCheck PRIVATE src)
    target_link_libraries(TexturePackCheck PRIVATE Utilities glad)
    add_test(NAME TexturePackCheck COMMAND TexturePackCheck)
    set_tests_properties(TexturePackCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(MobsCheck tests/MobsCheck.cpp src/Mobs.cpp src/PixelArt.cpp)
    target_include_directories(MobsCheck PRIVATE src)
    target_link_libraries(MobsCheck PRIVATE glad)
    add_test(NAME MobsCheck COMMAND MobsCheck)
    set_tests_properties(MobsCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(GizmoDragCheck tests/GizmoDragCheck.cpp src/TransformGizmo.cpp)
    target_include_directories(GizmoDragCheck PRIVATE src)
    target_link_libraries(GizmoDragCheck PRIVATE glad)
    add_test(NAME GizmoDragCheck COMMAND GizmoDragCheck)
    set_tests_properties(GizmoDragCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(BlockMeshCheck tests/BlockMeshCheck.cpp src/BlockMesh.cpp)
    target_include_directories(BlockMeshCheck PRIVATE src)
    target_link_libraries(BlockMeshCheck PRIVATE glad)
    add_test(NAME BlockMeshCheck COMMAND BlockMeshCheck)
    set_tests_properties(BlockMeshCheck PROPERTIES TIMEOUT 10 LABELS integration)

    add_executable(OceanRideClearanceCheck tests/OceanRideClearanceCheck.cpp
        src/OceanWorld.cpp src/BlockMesh.cpp src/Frustum.cpp src/LandTextures.cpp src/PixelArt.cpp src/WorldShape.cpp
        src/TexturePack.cpp src/ZipArchive.cpp src/Track.cpp src/ControlPoint.cpp)
    target_include_directories(OceanRideClearanceCheck PRIVATE src)
    target_link_libraries(OceanRideClearanceCheck PRIVATE Utilities glad)
    add_test(NAME OceanRideClearanceCheck COMMAND OceanRideClearanceCheck)
    set_tests_properties(OceanRideClearanceCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(LandRideClearanceCheck tests/LandRideClearanceCheck.cpp
        src/LandWorld.cpp src/BlockMesh.cpp src/Frustum.cpp src/LandTextures.cpp src/Mobs.cpp src/OceanWorld.cpp
        src/PixelArt.cpp src/TexturePack.cpp src/WaterFlow.cpp src/WorldShape.cpp
        src/ZipArchive.cpp src/Track.cpp src/ControlPoint.cpp)
    target_include_directories(LandRideClearanceCheck PRIVATE src)
    target_link_libraries(LandRideClearanceCheck PRIVATE Utilities glad)
    add_test(NAME LandRideClearanceCheck COMMAND LandRideClearanceCheck)
    set_tests_properties(LandRideClearanceCheck PROPERTIES TIMEOUT 30 LABELS integration)

    add_executable(ChaseRigCheck tests/ChaseRigCheck.cpp src/ChaseRig.cpp)
    target_include_directories(ChaseRigCheck PRIVATE src)
    target_link_libraries(ChaseRigCheck PRIVATE project_options)
    add_test(NAME ChaseRigCheck COMMAND ChaseRigCheck)
    set_tests_properties(ChaseRigCheck PROPERTIES TIMEOUT 10 LABELS integration)

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
# where the user puts texture packs (TexturePack.h); none is shipped
install(DIRECTORY DESTINATION texturepacks)
