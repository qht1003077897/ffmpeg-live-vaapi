file(GLOB testProgs_SRCS *.cpp)
file(GLOB testProgs_HEADERS *.hh)


ADD_LIBRARY( testProgs STATIC ${testProgs_HEADERS} ${testProgs_SRCS})
