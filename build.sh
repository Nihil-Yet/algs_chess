#/bin/bash

BIN_NAME="${BIN_NAME:-chess}"

echo "Start build..."

if [ ! -d build ]; then
    echo "Create \"build\" dir"
    mkdir build
fi

cd build
g++ -o "${BIN_NAME}" ../main.cpp
