#!/bin/bash

if [ ! -f llvm-18.1.8.src.tar.xz ]; then
  wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/llvm-18.1.8.src.tar.xz
fi
if [ ! -f clang-18.1.8.src.tar.xz ]; then
  wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/clang-18.1.8.src.tar.xz
fi
if [ ! -f cmake-18.1.8.src.tar.xz ]; then
  wget https://github.com/llvm/llvm-project/releases/download/llvmorg-18.1.8/cmake-18.1.8.src.tar.xz
fi
rm -rf llvm clang cmake build install

tar -xJvf llvm-18.1.8.src.tar.xz
tar -xJvf clang-18.1.8.src.tar.xz
tar -xJvf cmake-18.1.8.src.tar.xz
mv llvm-18.1.8.src llvm
mv clang-18.1.8.src clang
mv cmake-18.1.8.src cmake

mkdir build install
cmake llvm -B build -G Ninja\
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=$(realpath install) \
  -DLLVM_ENABLE_PROJECTS="clang" \
  -DLLVM_TARGETS_TO_BUILD="X86" \
  -DLLVM_USE_LINKER=lld \
  -DLLVM_INCLUDE_BENCHMARKS=OFF \
  -DLLVM_INCLUDE_EXAMPLES=OFF \
  -DLLVM_INCLUDE_TESTS=OFF \
  -DLLVM_PARALLEL_COMPILE_JOBS=2
cmake --build build --target install