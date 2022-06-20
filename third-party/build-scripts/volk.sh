#!/bin/bash

echo "Building volk..."

echo '#define VOLK_IMPLEMENTATION' > volk_implementation.c
echo '#include "volk/volk.h"' >> volk_implementation.c

gcc -std=c99 -O3 -pipe -march=native -c volk_implementation.c -o build-outs/volk.o

rm -rf volk_implementation.c
