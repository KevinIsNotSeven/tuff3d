@echo off
rem Build tuff3d.exe on Windows with MinGW-w64 (gcc on PATH, e.g. from MSYS2 or w64devkit).
if not exist bin mkdir bin
gcc -std=c99 -Os -s -Wall -mwindows -static-libgcc -fno-asynchronous-unwind-tables -o bin\tuff3d.exe src\main.c src\mesh.c -lopengl32 -lgdi32 -lcomdlg32 -luser32
