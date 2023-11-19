file(GLOB LIVE_MEDIA_SRCS *.cpp *.c)
file(GLOB LIVE_MEDIA_HEADERS *.hh)

INCLUDE_DIRECTORIES(
${live555Includes}
)

ADD_LIBRARY( liveMedia STATIC ${LIVE_MEDIA_HEADERS} ${LIVE_MEDIA_SRCS})
target_compile_definitions(liveMedia PRIVATE SOCKLEN_T=socklen_t)
target_link_libraries(liveMedia groupsock ssl crypto)