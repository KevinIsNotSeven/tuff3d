# tuff3d

A small, minimalist polygon modeler for Windows with Autodesk Maya's navigation and hotkeys.
It is one ~150 KB exe with no installer, and it only uses DLLs that ship with Windows
(OpenGL 1.1, GDI, common dialogs). It redraws only when something changes, so it sits at
0% CPU when idle.

**Download:** [`bin/tuff3d.exe`](bin/tuff3d.exe)

## Features

- Select objects, vertices, edges, faces (click, marquee, Shift/Ctrl modifiers; marquee only picks what's visible in shaded mode)
- Move / rotate / scale manipulators: axis handle = 1 axis, square handle = 2 axes, center = all 3
- Extrude faces (kept together, then pull along the normal) and extrude edges
- Merge to Center
- Vertex paint brush (color, opacity, radius) and "Apply Color to Selection" (tint by opacity)
- Primitives with editable parameters (in the side panel until you edit the mesh's components)
  - Cube: width/height/depth, subdivisions per axis
  - Cylinder: radius, height, sides, height divisions, cap divisions, top/bottom cap on/off
  - Sphere: radius, columns, rows
- Export OBJ (world space, vertex colors written as `v x y z r g b`, shading written as `vn` normals), plus OBJ import so you can reopen work
- Hard shade, Smooth shade and Auto Smooth per object (Display menu). Auto Smooth keeps edges
  sharper than the Normal Angle (default 30°, editable in the side panel) hard and smooths the rest
- Undo / redo, duplicate, delete objects or faces

## Controls (Maya defaults)

| Input | Action |
| --- | --- |
| Alt + LMB / MMB / RMB drag | Tumble / track / dolly |
| Mouse wheel | Dolly |
| F / A | Frame selected / frame all |
| Q W E R | Select / move / rotate / scale tool |
| F8 / F9 / F10 / F11 | Object / vertex / edge / face mode |
| RMB on object | Component mode menu |
| Shift / Ctrl / Ctrl+Shift + click | Toggle / deselect / add |
| Ctrl + drag axis handle | Move / scale on the other two axes |
| MMB drag | Drag with the last used handle |
| hold X / hold J | Snap move to grid / snap rotation to 15° |
| + / - | Manipulator size |
| Ctrl+E | Extrude |
| Ctrl+D | Duplicate |
| Delete | Delete objects / faces |
| Z or Ctrl+Z / Shift+Z or Ctrl+Y | Undo / redo |
| G | Repeat last command |
| 4 / 5 | Wireframe / shaded |
| B + LMB drag | Brush radius (paint tool) |
| Ctrl+N / Ctrl+O / Ctrl+S | New / import OBJ / export OBJ |
| Ctrl+A | Toggle side panel |
| F1 | Hotkey list |

Merge to Center and the Paint Vertex Color Tool are in the Mesh and Tools menus, as in Maya,
where they have no default hotkey.

## Building

Plain C99 with MinGW-w64:

- Linux/macOS: `./build.sh` (needs `x86_64-w64-mingw32-gcc`)
- Windows: `build.bat` (needs `gcc` from MSYS2 or w64devkit on PATH)

Geometry tests (any C compiler): `cc -Isrc tests/test_mesh.c src/mesh.c -lm && ./a.out`

`src/mesh.c` is the platform-independent geometry core (primitives, extrude, merge, OBJ).
`src/main.c` is the Win32/OpenGL UI.
