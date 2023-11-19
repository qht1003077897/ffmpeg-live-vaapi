file(GLOB GROUPSOCK_SRCS *.cpp *.c)
file(GLOB GROUPSOCK_HEADERS *.hh)

INCLUDE_DIRECTORIES(
${live555Includes}
)

ADD_LIBRARY( groupsock STATIC ${GROUPSOCK_HEADERS} ${GROUPSOCK_SRCS})
target_compile_definitions(groupsock PRIVATE SOCKLEN_T=socklen_t)
target_link_libraries(groupsock BasicUsageEnvironment)