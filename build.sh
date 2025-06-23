#!/bin/bash

# 创建构建目录
mkdir -p build
cd build

# 生成Makefile
cmake ..

# 编译
make

echo "Build complete. Executable is ./build/test_exe"

