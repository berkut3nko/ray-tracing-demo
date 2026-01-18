#!/bin/bash
export CC=clang-18
export CXX=clang++-18
mkdir -p build/release
cd build/release

echo "🔹 Build ray tracing shader..."
glslc src/shaders/raytrace.comp -o src/shaders/raytrace.comp.spv

echo "Configurating Release build with Ninja..."
cmake -G "Ninja" \
      -DCMAKE_BUILD_TYPE=release \
      -DCMAKE_INTERPROCEDURAL_OPTIMIZATION=ON \
      ../..
echo "Building..."
ninja 

echo "Build Complete. Run with ./build/release/RayTracingDemo"