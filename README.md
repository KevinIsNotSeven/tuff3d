# tuff3d

A small, minimalist polygon modeler for Windows with Autodesk Maya's navigation and hotkeys.
It is one ~230 KB exe with no installer, and it only uses DLLs that ship with Windows
(OpenGL 1.1, GDI, common dialogs, shell drag and drop). It redraws only when something changes, so
it sits at 0% CPU when idle. Meshes are drawn from cached vertex arrays that are only rebuilt after
an edit, so tumbling and hovering stay fast on dense meshes.

**Download:** [`bin/tuff3d.exe`](bin/tuff3d.exe)

## Features

Modeling
- Primitives with editable parameters (in the side panel until you edit the mesh's components)
  - Cube: width/height/depth, subdivisions per axis
  - Cylinder: radius, height, sides, height divisions, cap divisions, top/bottom cap on/off
  - Sphere: radius, columns, rows
  - Plane: width/height, subdivisions per axis
- Move / rotate / scale manipulators: arrow = 1 axis, square = 2 axes, center = all 3. A faint line shows
  the axis a drag is locked to. Scaling components stops at 0, so dragging a handle past the pivot
  flattens them onto one plane (e.g. line up 2 vertices on X)
- Snapping: hold X (grid), V (onto vertices, with a marker on the target) or J (15 degree rotation);
  Grid Snap and Point Snap are also toggle buttons
- Extrude faces (kept together, then pull along the normal) and extrude edges; Merge to Center
- Fill Hole: caps the open border your selected vertices, edges or faces touch (or every hole of the
  selected objects in object mode); the new face is left selected, ready to extrude
- Modify menu: Freeze Transformations, Center Pivot, Reset Transformations
- Vertex paint brush (color, opacity, radius) and "Apply Color to Selection" (tint by opacity)
- Duplicate, delete objects or faces, hide / show objects

Selection
- Objects, vertices, edges, faces, or Multi mode (F7): all three at once, like Maya's multi-component
  selection (a click picks a nearby vertex, then an edge, then the face under the cursor)
- Click, marquee (only what's visible in shaded mode), Shift/Ctrl modifiers
- Double-click an edge for its edge loop; grow / shrink (> / <); convert between vertices, edges and
  faces (Ctrl+F9/F10/F11); select all / invert

Feedback
- Preselection highlight: whatever a click would pick (object, vertex, edge or face) lights up red
- Heads-up display with vertex/edge/face/tri/object counts (selected counts in gold)
- Short on-screen notes for every command ("Extruded", "Undo Move", "Edge loop: 20 edges")
- The move/rotate/scale amount next to the cursor while dragging; Esc cancels a drag
- Named undo steps (Edit > Undo Extrude) and a status line that explains the button or component
  under the mouse, or how to use the current tool
- Filled marquee with +, - or +/- for add, remove and toggle; cursors for orbit/pan/zoom and painting
- Tips on an empty scene and in an empty side panel

Comfort
- Toolbar with the tools, modes, primitives and common commands, each labeled with its hotkey
- Side panel: rename objects, type values, or drag a label left/right to scrub its value
  (Shift fast, Ctrl fine)
- Smooth camera moves for frame (F/A) and views; Home view, front/side/top views, and clicking
  X/Y/Z on the corner gizmo to look down that axis (click again for the opposite side)
- File > Open Recent, drag and drop .obj files onto the window (Shift adds them to the scene), and
  open a file passed on the command line (e.g. Open With)
- Autosave every minute while there are unexported changes; after a crash, tuff3d offers to recover
- The title bar shows the file name with `*` for unexported changes, and New / Open / Exit ask before
  discarding them
- Remembers the window position, side panel, wireframe, heads-up display, manipulator size and theme
- 48 themes (Classic, 33 dark, 14 light), picked in the theme picker (Ctrl+T or Display > Theme):
  a tile per theme showing tuff3d in its colors; hover a tile to preview it on the whole window,
  click to keep it, arrow keys to step through, R for a random one. Ctrl+Shift+T steps to the next
  theme. Themes color the viewport, side panel, toolbar, status bar, menu bar and (Windows 10/11)
  the title bar
- Hard Shade, Smooth Shade and Auto Smooth per object (Display menu). Auto Smooth keeps edges
  sharper than the Normal Angle (default 30°, editable in the side panel) hard and smooths the rest
- Export OBJ (world space, vertex colors written as `v x y z r g b`, shading written as `vn` normals)
  and open/import OBJ

## Controls (Maya defaults)

| Input | Action |
| --- | --- |
| Alt + LMB / MMB / RMB drag | Tumble / track / dolly |
| Mouse wheel | Dolly |
| F / A / Home | Frame selected / frame all / home view |
| Click X / Y / Z on the corner gizmo | Look down that axis |
| Q W E R | Select / move / rotate / scale tool |
| F8 / F9 / F10 / F11 / F7 | Object / vertex / edge / face / multi mode |
| Ctrl+F9 / F10 / F11 | Convert selection to vertices / edges / faces |
| RMB on object | Component mode menu |
| Shift / Ctrl / Ctrl+Shift + click | Toggle / deselect / add |
| Double-click an edge | Select its edge loop |
| > / < | Grow / shrink selection |
| Ctrl+Shift+A / Ctrl+Shift+I | Select all / invert selection |
| Ctrl + drag axis handle | Move / scale on the other two axes |
| MMB drag | Drag with the last used handle |
| hold X / V / J | Snap to grid / to vertices / rotation to 15° |
| Esc while dragging | Cancel the drag |
| + / - | Manipulator size |
| Ctrl+E | Extrude |
| Ctrl+D | Duplicate |
| Delete | Delete objects / faces |
| Ctrl+H / Ctrl+Shift+H | Hide selected / show all |
| Z or Ctrl+Z / Shift+Z or Ctrl+Y | Undo / redo |
| G | Repeat last command |
| 4 / 5 | Wireframe / shaded |
| B + LMB drag | Brush radius (paint tool) |
| Ctrl+N / Ctrl+O / Ctrl+S | New / open / export OBJ |
| Ctrl+Shift+O / Ctrl+Shift+S | Import OBJ into the scene / export as |
| Ctrl+A | Toggle side panel |
| Ctrl+T / Ctrl+Shift+T | Theme picker / next theme |
| F1 | Hotkey list |

Merge to Center and the Paint Vertex Color Tool are in the Mesh and Tools menus, as in Maya,
where they have no default hotkey.

## Maya LT scenes (.mlt)

Maya LT is discontinued and nothing else reads its `.mlt` scenes, so tuff3d opens them itself:
File > Open, drag and drop, or `tuff3d.exe "apple c.mlt"`. They are Maya's binary scene format
with scrambled chunk sizes and names. Objects saved without history store their mesh, which tuff3d
reads as is. Everything else is stored as construction history (the steps that build a model), so
tuff3d replays that history to rebuild every visible mesh in world space, with its vertex colors.
Save (Ctrl+S) then writes an `.obj` next to it; the `.mlt` itself is never written to.

Supported history steps: cube, cylinder, plane, sphere and torus primitives, Smooth, Extract (chip
off), Separate, Extrude faces (including thickness) and edges, Bridge, Split (Insert Edge Loop /
Multi-Cut cuts), Bevel, Fill Hole (close border), Delete faces and edges, Delete Edge, Combine
(unite), Merge Vertices, tweaks (moved vertices) and Paint Vertex Color, plus object transforms.
UV, normal and soft-edge steps are skipped since tuff3d doesn't use them.

Later steps pick vertices, edges and faces by number, so tuff3d numbers them the way Maya does.
Those rules were worked out against the checks Maya leaves in the file (each extrusion records the
bounding box of what it extruded, and vertex color steps list the faces around every vertex);
`tests/mlt_dump.c` with `MLT_DEBUG=1` prints them. Limits:

- Steps tuff3d doesn't know yet are skipped, and a warning names them, so the shape may be missing
  that edit.
- A few numbering cases are still guessed. When an extrusion's selection falls clearly outside the
  box Maya recorded, tuff3d swaps in the matching faces or edges, but a later step can still land
  on a slightly different face.
- Bevel follows Maya exactly when every edge is bevelled; partial bevels are approximated.
- Vertex colors are per vertex in tuff3d, so where Maya had a hard color edge the colors blend.
- Holes inside faces (rare) are dropped from stored meshes.

If a file doesn't open or looks wrong, send it in so support can be added.

### Converting with full Maya

With full Maya 2023 or newer (the free 30-day trial works), [`tools/mlt_convert.py`](tools/mlt_convert.py)
gives an exact conversion of a whole folder in one go. For every `.mlt` (subfolders included) it
writes into a `converted` folder:

- `name.obj`: every visible mesh in world space, with vertex colors as `v x y z r g b` (tuff3d reads
  these; Maya's own OBJ exporter drops vertex colors)
- `name.fbx`: UVs, materials and colors, for other 3D apps and game engines
- `name.ma`: a Maya ASCII copy of the whole scene with its history, so the edit steps survive too

The `.mlt` files are only read. A `convert_log.txt` lists what was converted. To run it:

1. In Maya: Windows > General Editors > Script Editor, Python tab, File > Load Script... and pick
   `mlt_convert.py`, then Execute (Ctrl+Enter). It asks for the folder.
2. Or from a command prompt, without opening Maya's window:
   `"C:\Program Files\Autodesk\Maya2025\bin\mayapy.exe" mlt_convert.py "D:\My MLT files"`
   (options: `--no-fbx`, `--no-ma`, `--overwrite`)

Files that were already converted are skipped, so it can be re-run after adding more.

## Building

Plain C99 with MinGW-w64:

- Linux/macOS: `./build.sh` (needs `x86_64-w64-mingw32-gcc` and `x86_64-w64-mingw32-windres`)
- Windows: `build.bat` (needs `gcc` and `windres` from MSYS2 or w64devkit on PATH)

The icon is `res/tuff3d.ico` (16 to 256 px), built from `res/tuff3d.png` with `python3 res/make_icon.py res/tuff3d.png res/tuff3d.ico` (needs Pillow).

Geometry tests (any C compiler): `cc -Isrc tests/test_mesh.c src/mesh.c -lm && ./a.out`

`src/mesh.c` is the platform-independent geometry core (primitives, extrude, merge, edge loops,
selection grow/shrink/convert, OBJ).
`src/main.c` is the Win32/OpenGL UI.
