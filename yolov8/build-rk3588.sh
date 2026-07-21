#!/bin/bash
rm -rf build

export CXXFLAGS="-pthread"
export LDFLAGS="-pthread"

cmake -S . -B build 
cmake --build build -j8

echo "✅ 部署完成！部署目录: build"