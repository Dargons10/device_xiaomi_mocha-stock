#!/bin/sh

rootdirectory="$PWD"
dirs="bionic/libm frameworks/base frameworks/native"

for dir in $dirs ; do
	cd $rootdirectory
	cd $dir
	echo "Cleaning $dir patches..."
	git checkout -- . && git clean -df
done

echo "Done!"
cd $rootdirectory
