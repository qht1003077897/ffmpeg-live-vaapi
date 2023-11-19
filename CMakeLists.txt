cmake_minimum_required(VERSION 3.5)

project(ffvademo)
set(CMAKE_CXX_STANDARD 17)
# set(CMAKE_VERBOSE_MAKEFILE ON)

# set(ENV{PKG_CONFIG_PATH} /usr/lib/pkgconfig)
#find_package(PkgConfig REQUIRED)


add_definitions(-DUSE_EGL)
add_definitions(-DUSE_X11)
add_definitions(-DUSE_DRM)
add_definitions(-DUSE_VA_X11)
add_definitions(-DUSE_VA_VPP)
# add_definitions(-DUSE_GLES_VERSION=1)

add_subdirectory(ext/live)

set(live555Includes
${live555_SOURCE_DIR}/BasicUsageEnvironment/include
${live555_SOURCE_DIR}/groupsock/include
${live555_SOURCE_DIR}/liveMedia/include
${live555_SOURCE_DIR}/UsageEnvironment/include
)


include_directories($ENV{INCLUDE_PATH})

# include_directories(${CMAKE_CURRENT_SOURCE_DIR}/ffmpegInclude)
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${live555Includes}
)


file(GLOB Souce_files
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvademo.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffmpeg_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvadecoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvadisplay.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvafilter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvarenderer_drm.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvarenderer_egl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvarenderer_x11.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvarenderer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvasurface.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvadecoder.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/vaapi_utils.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ffvartsp.cpp
    )

message(STATUS "os is ${CMAKE_SYSTEM_NAME}")

set(target_name ffvademo)
add_executable(${target_name} ${Souce_files})
# add_library(${target_name}  SHARED 
#          ${Souce_files})


target_link_libraries(${target_name}
   va
   avformat
   avfilter
   avcodec
   swscale
   swresample
   avutil
   va-x11
   X11
   EGL
   GLESv2
   liveMedia
   BasicUsageEnvironment
   groupsock
   UsageEnvironment
   ssl
   crypto
)


target_compile_definitions(
	${target_name}
	PRIVATE
)