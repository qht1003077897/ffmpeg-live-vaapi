file(GLOB PROXY_SERVER_SRCS *.cpp)

ADD_EXECUTABLE(proxyServer ${PROXY_SERVER_SRCS})
TARGET_LINK_LIBRARIES(
proxyServer
BasicUsageEnvironment
groupsock
liveMedia
UsageEnvironment
) 