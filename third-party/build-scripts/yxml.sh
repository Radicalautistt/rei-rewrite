#!/bin/bash

echo -e '\e[1;32mBuilding YXML...\e[;0m'

cd yxml
gcc -Wall -Wextra -Wno-unused-parameter -O3 -pipe -I. -c yxml.c
ar rcs libyxml.a yxml.o
rm -rf yxml.o
mv libyxml.a ../build-outs/
