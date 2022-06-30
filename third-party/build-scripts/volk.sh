#!/bin/bash

echo "Building volk..."

echo '#ifdef __linux__' > volk_implementation.c
echo '  #define VK_USE_PLATFORM_XCB_KHR' >> volk_implementation.c
echo '#else' >> volk_implementation.c
echo '  #error "Volk builder: Unhandled platform..."' >> volk_implementation.c
echo '#endif' >> volk_implementation.c

echo '#define VOLK_IMPLEMENTATION' >> volk_implementation.c
echo '#include "volk/volk.h"' >> volk_implementation.c

gcc -std=c99 -O3 -pipe -march=native -c volk_implementation.c -o build-outs/volk.o

rm -rf volk_implementation.c
