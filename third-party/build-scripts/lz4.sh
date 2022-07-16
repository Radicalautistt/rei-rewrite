#!/bin/bash

echo "Building LZ4..."

cd lz4
make -j4
mv lib/liblz4.a ../build-outs/
make clean
cd ..
