# tuff3d

A small, minimalist polygon modeler for Windows with Autodesk Maya's navigation and hotkeys.
It is one ~150 KB exe with no installer, and it only uses DLLs that ship with Windows
(OpenGL 1.1, GDI, common dialogs). It redraws only when something changes, so it sits at
0% CPU when idle.

**Download:** [`bin/tuff3d.exe`](bin/tuff3d.exe)

## Features

- Toolbar with the tools, selection modes, primitives and common commands, each labeled with its hotkey

- Select objects, vertices, edges, faces (click, marquee, Shift/Ctrl modifiers; marquee only picks what's visible in shaded mode)
- Move / rotate / scale manipulators: axis handle = 1 axis, square handle = 2 axes, center = all 3.
  Scaling components stops at 0, so dragging a handle in past the pivot flattens them onto one plane
  (e.g. line up 2 vertices on X). The status bar shows the move/rotate/scale amount while dragging.
- Select All / Invert Selection (objects or components)
- Extrude faces (kept together, then pull along the normal) and extrude edges
- Merge to Center
- Vertex paint brush (color, opacity, radius) and "Apply Color to Selection" (tint by opacity)
- Primitives with editable parameters (in the side panel until you edit the mesh's components)
  - Cube: width/height/depth, subdivisions per axis
  - Cylinder: radius, height, sides, height divisions, cap divisions, top/bottom cap on/off
  - Sphere: radius, columns, rows
  - Plane: width/height, subdivisions per axis
- Export OBJ (world space, vertex colors written as `v x y z r g b`), plus OBJ import so you can reopen work
- Undo / redo, duplicate, delete objects or faces
- The title bar shows the file name with `*` when there are unexported changes, and New / Exit ask before discarding them
- Themes under Display > Theme: Classic, Light, Dark, Midnight, Tuff, Forest, Sunset. They color the
  viewport, side panel, status bar, menu bar and (Windows 10/11) the title bar; the choice is remembered.

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
| Ctrl+Shift+A / Ctrl+Shift+I | Select all / invert selection |
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

- Linux/macOS: `./build.sh` (needs `x86_64-w64-mingw32-gcc` and `x86_64-w64-mingw32-windres`)
- Windows: `build.bat` (needs `gcc` and `windres` from MSYS2 or w64devkit on PATH)

The icon is `res/tuff3d.ico` (16 to 256 px), built from `res/tuff3d.png` with `python3 res/make_icon.py res/tuff3d.png res/tuff3d.ico` (needs Pillow).

Geometry tests (any C compiler): `cc -Isrc tests/test_mesh.c src/mesh.c -lm && ./a.out`

`src/mesh.c` is the platform-independent geometry core (primitives, extrude, merge, OBJ).
`src/main.c` is the Win32/OpenGL UI.
