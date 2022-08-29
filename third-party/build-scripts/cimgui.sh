#!/bin/bash

echo -e '\e[1;32mBuilding CIMGUI...\e[;0m'

cd cimgui
make -j4 static
mv libcimgui.a ../build-outs/
make clean
make fclean
cd ..
