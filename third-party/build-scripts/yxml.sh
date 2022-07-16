#!/bin/bash

echo 'Building YXML...'
cd yxml
gcc -Wall -Wextra -Wno-unused-parameter -O3 -pipe -I. -c yxml.c
ar rcs libyxml.a yxml.o
rm -rf yxml.o
mv libyxml.a ../build-outs/
