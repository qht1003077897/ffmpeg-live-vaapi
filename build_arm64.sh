#!/bin/bash

# Define the source directory
SOURCE_DIR="$(dirname $0)"

KYLIN_LIB="${HOME}/libs/cross"

# Define the build directory
BUILD_DIR="${SOURCE_DIR}/arm_build"

echo "KYLIN_LIB: $KYLIN_LIB"
echo "SOURCE_DIR: $SOURCE_DIR"
echo "BUILD_DIR: $BUILD_DIR"

# Create the build directory
mkdir -p "$BUILD_DIR/"
mkdir -p "$BUILD_DIR/third_party/mpp"
# rm -rf "$BUILD_DIR/*"

# docker run -v "$SOURCE_DIR/third_party/mpp:/project" \
#            -v "$KYLIN_LIB:/cross" \
#            -v "$BUILD_DIR/third_party/mpp:/build_dir" \
#            -w "/build_dir" \
#            -it wqhot/ftbuild:v9.3.0 \
#            /bin/bash -c "\
#                 cmake -DCMAKE_TOOLCHAIN_FILE=/project/build/linux/aarch64/arm.linux.cross.cmake -D CMAKE_BUILD_TYPE=Release /project && \
#                 make -j1
#               "

# docker run -v "$SOURCE_DIR/third_party/mpp:/project" \
#            -v "$KYLIN_LIB:/cross" \
#            -v "$BUILD_DIR/third_party/mpp:/build_dir" \
#            -w "/build_dir" \
#            -it wqhot/ftbuild:v9.3.0 \
#            /bin/bash

# Run the docker container and mount the source and build directories
# docker run -v "$SOURCE_DIR:/project" \
#            -v "$KYLIN_LIB:/cross" \
#            -v "$BUILD_DIR:/project/arm_build" \
#            -w "/project/arm_build" \
#            -it wqhot/ftbuild:v9.3.2 \
#            /bin/bash -c "\
#                 git config --global --add safe.directory /project && \
#                 cmake -D BUILD_TYPE=RELEASE -D PLATFORM=ARM .. && \
#                 make -j1
#               "

docker run -v "$SOURCE_DIR:/project" \
           -v "$KYLIN_LIB:/cross" \
           -v "$BUILD_DIR:/project/arm_build" \
           -w "/project/arm_build" \
           -it wqhot/ftbuild:v9.3.2 \
           /bin/bash
# docker run -v ".:/project" \
#            -v "${HOME}/libs/cross:/cross" \
#            -v "$BUILD_DIR:/project/arm_build" \
#            -w "/project/arm_build" \
#            -it wqhot/ftbuild:v9.3.0 \
#            /bin/bash