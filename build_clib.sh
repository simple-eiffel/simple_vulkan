#!/bin/sh
# Build Clib/simple_vulkan.lib from Git Bash when cl.exe, INCLUDE and LIB are
# already set (the EiffelStudio C environment). build_clib.bat does the same
# from an "x64 Native Tools" prompt.
cd "$(dirname "$0")/Clib" || exit 1
cl //nologo //c //O2 //W3 "//I$VULKAN_SDK\Include" simple_vulkan.c //Fosimple_vulkan.obj || exit 1
lib //nologo //OUT:simple_vulkan.lib simple_vulkan.obj || exit 1
echo "Built Clib/simple_vulkan.lib"
