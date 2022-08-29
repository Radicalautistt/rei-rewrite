#!/bin/bash

# Make sure that GLSLC is available in the system.
[[ `command -v glslc &> /dev/null` ]] && \
  echo -e "\e[1;31mREI shader compiler error: no GLSLC available in the system, please, check your Vulkan installation.\e[;0m]" && \
  exit 1

shaders_path='shaders'

compile_shader () {
  # Compile a shader only if it is a source file and not a spv binary.
  ! [[ $1 == *spv* ]] && glslc -c $1 -o "$1.spv"
}

case "$1" in
  "-f") echo -e "\e[1;32mCompiling $1...\e[;0m"; compile_shader $1;;
  "-a") echo -e "\e[1;32mCompiling every shader in $shaders_path...\e[;0m"; for f in $shaders_path/*; do compile_shader $f; done;;
  *) usage_str=`cat <<:
REI shader compiler usage:
  -f file_name: compile specific shader file.
  -a: build all shader files in $shaders_path.
:`

  echo -e "\e[1;31m$usage_str\e[;0m"
  exit 1
esac
