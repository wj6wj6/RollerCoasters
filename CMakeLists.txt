cmake_minimum_required(VERSION 3.16)

project(RollerCoasters LANGUAGES C CXX)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src/)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include/)
set(LIB_DIR ${PROJECT_SOURCE_DIR}/lib/)

include_directories(${INCLUDE_DIR})
include_directories(${INCLUDE_DIR}glad4.6/include/)
include_directories(${INCLUDE_DIR}glm-0.9.8.5/glm/)

add_Definitions("-D_XKEYCHECK_H")
add_definitions(-DPROJECT_DIR="${PROJECT_SOURCE_DIR}")

add_executable(RollerCoasters
    ${SRC_DIR}CallBacks.h
    ${SRC_DIR}CallBacks.cpp
    ${SRC_DIR}ControlPoint.h
    ${SRC_DIR}ControlPoint.cpp
    ${SRC_DIR}main.cpp
    ${SRC_DIR}Object.h
    ${SRC_DIR}Track.h
    ${SRC_DIR}Track.cpp
    ${SRC_DIR}TrainView.h
    ${SRC_DIR}TrainView.cpp
    ${SRC_DIR}TrainWindow.h
    ${SRC_DIR}TrainWindow.cpp
    ${INCLUDE_DIR}glad4.6/src/glad.c)

add_library(Utilities 
    ${SRC_DIR}Utilities/3DUtils.h
    ${SRC_DIR}Utilities/3DUtils.cpp
    ${SRC_DIR}Utilities/ArcBallCam.h
    ${SRC_DIR}Utilities/ArcBallCam.cpp
    ${SRC_DIR}Utilities/Pnt3f.h
    ${SRC_DIR}Utilities/Pnt3f.cpp)

target_link_libraries(RollerCoasters 
    debug ${LIB_DIR}Debug/fltk_formsd.lib      optimized ${LIB_DIR}Release/fltk_forms.lib
    debug ${LIB_DIR}Debug/fltk_gld.lib         optimized ${LIB_DIR}Release/fltk_gl.lib
    debug ${LIB_DIR}Debug/fltk_imagesd.lib     optimized ${LIB_DIR}Release/fltk_images.lib
    debug ${LIB_DIR}Debug/fltk_jpegd.lib       optimized ${LIB_DIR}Release/fltk_jpeg.lib
    debug ${LIB_DIR}Debug/fltk_pngd.lib        optimized ${LIB_DIR}Release/fltk_png.lib
    debug ${LIB_DIR}Debug/fltk_zd.lib          optimized ${LIB_DIR}Release/fltk_z.lib
    debug ${LIB_DIR}Debug/fltkd.lib            optimized ${LIB_DIR}Release/fltk.lib)

target_link_libraries(RollerCoasters 
    ${LIB_DIR}OpenGL32.lib
    ${LIB_DIR}glu32.lib)

target_link_libraries(RollerCoasters Utilities)

option(ROLLERCOASTERS_BUILD_TESTS "Build camera gesture regression check" OFF)
if(ROLLERCOASTERS_BUILD_TESTS)
    enable_testing()
    add_executable(GestureMotionCheck tests/GestureMotionCheck.cpp)
    target_include_directories(GestureMotionCheck PRIVATE ${SRC_DIR}Utilities)
    target_link_libraries(GestureMotionCheck Utilities
        debug ${LIB_DIR}Debug/fltk_gld.lib optimized ${LIB_DIR}Release/fltk_gl.lib
        debug ${LIB_DIR}Debug/fltkd.lib optimized ${LIB_DIR}Release/fltk.lib
        ${LIB_DIR}OpenGL32.lib ${LIB_DIR}glu32.lib)
    add_test(NAME GestureMotionCheck COMMAND GestureMotionCheck)
endif()
    
