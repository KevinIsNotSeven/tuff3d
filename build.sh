#!/bin/sh
# Cross-compile tuff3d.exe with mingw-w64 (Linux/macOS/MSYS2).
set -e
CC=${CC:-x86_64-w64-mingw32-gcc}
WINDRES=${WINDRES:-x86_64-w64-mingw32-windres}
mkdir -p bin
$WINDRES res/tuff3d.rc -O coff -o bin/tuff3d_res.o
$CC -std=c99 -O2 -s -Wall -Wextra -mwindows -static-libgcc -fno-asynchronous-unwind-tables \
    -o bin/tuff3d.exe src/main.c src/mesh.c src/mlt.c bin/tuff3d_res.o -lopengl32 -lgdi32 -lcomdlg32 -luser32 -ladvapi32 -lshell32
rm -f bin/tuff3d_res.o
ls -l bin/tuff3d.exe
