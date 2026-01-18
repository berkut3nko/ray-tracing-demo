#!/bin/bash

# Експорт змінних компілятора
export CC=clang-18
export CXX=clang++-18

# Очистка
rm -rf build/debug
mkdir -p build/debug
cd build/debug

echo "🔹 Build ray tracing shader..."
glslc src/shaders/raytrace.comp -o src/shaders/raytrace.comp.spv

echo "🔹 Configuring Debug build with Ninja..."
# Прибрали ручну передачу шляху до сканера — CMake знайде сам
cmake -G "Ninja" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
      ../..

echo "🔹 Building..."
ninja

# Копіювання для IDE
cp compile_commands.json ../../
echo "✅ Build Complete."