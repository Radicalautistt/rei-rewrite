#!/bin/bash

src_path=$1
src_path_no_ext="${src_path%.*}"
new_path="$src_path_no_ext-opt.gltf"

echo "Optimizing $src_path..."

cp $src_path $new_path
cp "$src_path_no_ext.bin" "$src_path_no_ext-opt.bin"

# Optimize gltf file parsing by removing spaces and newlines.
sed -i 's/ //g' $new_path
sed -z -i 's/\n//g' $new_path
sed -i 's///g' $new_path
