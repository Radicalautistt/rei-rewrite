#!/bin/bash

echo -e "\e[1;32mBuilding volk...\e[;0m"

cat << EOF > volk_implementation.c
#ifdef __linux__
  #define VK_USE_PLATFORM_XCB_KHR
#else
  #error "Volk builder: Unhandled platform..."
#endif

#define VOLK_IMPLEMENTATION
#include "volk/volk.h"
EOF

gcc -std=c99 -O3 -pipe -march=native -c volk_implementation.c -o build-outs/volk.o

rm -rf volk_implementation.c
