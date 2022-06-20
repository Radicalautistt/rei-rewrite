#!/bin/bash

echo "Building CIMGUI..."

cd cimgui
make -j4 static
mv libcimgui.a ../build-outs/
make clean
make fclean
cd ..
