
file(GLOB MEDIA_SERVER_SRCS *.cpp)
file(GLOB MEDIA_SERVER_HDRS *.hh)


ADD_EXECUTABLE(mediaServer ${MEDIA_SERVER_HDRS} ${MEDIA_SERVER_SRCS})
TARGET_LINK_LIBRARIES(
mediaServer
BasicUsageEnvironment
groupsock
liveMedia
UsageEnvironment
) 