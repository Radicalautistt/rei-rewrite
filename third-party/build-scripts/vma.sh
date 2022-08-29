#!/bin/bash

echo -e '\e[1;32mBuilding Vulkan memory allocator...\e[;0m'

cat << EOF > vma_implementation.cpp
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#define VMA_IMPLEMENTATION
#include "VulkanMemoryAllocator/include/vk_mem_alloc.h"
EOF

g++ -O3 -pipe -march=native -c vma_implementation.cpp -o build-outs/vma.o

rm -rf vma_implementation.cpp
