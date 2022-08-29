#!/bin/bash

echo -e '\e[1;32mBuilding LZ4...\e[;0m'

cd lz4
make -j4 lib
mv lib/liblz4.a ../build-outs/
make clean
cd ..
