#!/bin/bash

! [[ -e build-outs ]] && mkdir build-outs

echo -e "\e[1;32mBuilding third-party libraries...\e[;0m"

for s in build-scripts/*; do bash $s; done
