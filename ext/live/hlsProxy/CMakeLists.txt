file(GLOB HLSPROXY_SRCS *.cpp)
file(GLOB HLSPROXY_HEADERS *.hh)

INCLUDE_DIRECTORIES(
${live555Includes}
)

ADD_LIBRARY( hlsProxy STATIC ${HLSPROXY_HEADERS} ${HLSPROXY_SRCS})

TARGET_LINK_LIBRARIES(
hlsProxy
BasicUsageEnvironment
groupsock
liveMedia
UsageEnvironment
) 