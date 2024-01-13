cmake_minimum_required(VERSION 3.0)

PROJECT( live555 )

# includes
SET(live555Includes
${live555_SOURCE_DIR}/BasicUsageEnvironment/include
${live555_SOURCE_DIR}/groupsock/include
${live555_SOURCE_DIR}/liveMedia/include
${live555_SOURCE_DIR}/UsageEnvironment/include
)

INCLUDE_DIRECTORIES(
${CMAKE_FIND_ROOT_PATH}/include
${live555Includes}
)


# DSOCKLEN_T=socklen_t -D_LARGEFILE_SOURCE=1 -D_FILE_OFFSET_BITS=64
# add_definitions("-DSOCKLEN_T socklen_t")
# add_definitions("-D_LARGEFILE_SOURCE 1")
# add_definitions("-D_FILE_OFFSET_BITS 64")


ADD_SUBDIRECTORY( BasicUsageEnvironment )
ADD_SUBDIRECTORY( groupsock ) 
ADD_SUBDIRECTORY( liveMedia ) 
ADD_SUBDIRECTORY( mediaServer )
#ADD_SUBDIRECTORY( proxyServer )
ADD_SUBDIRECTORY( UsageEnvironment ) 
ADD_SUBDIRECTORY( testProgs )
