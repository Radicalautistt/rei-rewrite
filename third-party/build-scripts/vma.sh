#!/bin/bash

echo 'Building Vulkan memory allocator...'

echo '#define VMA_IMPLEMENTATION' > vma_implementation.cpp
echo '#define VMA_STATIC_VULKAN_FUNCTIONS 0' >> vma_implementation.cpp
echo '#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0' >> vma_implementation.cpp
echo '#include "VulkanMemoryAllocator/include/vk_mem_alloc.h"' >> vma_implementation.cpp

g++ -O3 -pipe -march=native -c vma_implementation.cpp -o build-outs/vma_impl.o

rm -rf vma_implementation.cpp
