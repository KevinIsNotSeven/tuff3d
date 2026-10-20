#!/bin/sh
# Cross-compile tuff3d.exe with mingw-w64 (Linux/macOS/MSYS2).
set -e
CC=${CC:-x86_64-w64-mingw32-gcc}
mkdir -p bin
$CC -std=c99 -Os -s -Wall -Wextra -mwindows -static-libgcc -fno-asynchronous-unwind-tables \
    -o bin/tuff3d.exe src/main.c src/mesh.c -lopengl32 -lgdi32 -lcomdlg32 -luser32
ls -l bin/tuff3d.exe
