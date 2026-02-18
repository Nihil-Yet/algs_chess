#/bin/bash

BIN_NAME="${BIN_NAME:-chess}"

echo "Start build..."

if [ ! -d build ]; then
    echo "Create \"build\" dir"
    mkdir build
fi

cd build
g++ -o "${BIN_NAME}" ../main.cpp && \
    printf "\x1b[32mDone!\x1b[0m\n" || \
    printf "\x1b[31mBuild failed!\x1b[0m\n"
