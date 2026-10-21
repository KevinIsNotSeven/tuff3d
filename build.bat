@echo off
rem Build tuff3d.exe on Windows with MinGW-w64 (gcc on PATH, e.g. from MSYS2 or w64devkit).
if not exist bin mkdir bin
windres res\tuff3d.rc -O coff -o bin\tuff3d_res.o || exit /b 1
gcc -std=c99 -O2 -s -Wall -mwindows -static-libgcc -fno-asynchronous-unwind-tables -o bin\tuff3d.exe src\main.c src\mesh.c src\mlt.c bin\tuff3d_res.o -lopengl32 -lgdi32 -lcomdlg32 -luser32 -ladvapi32 -lshell32
del bin\tuff3d_res.o
