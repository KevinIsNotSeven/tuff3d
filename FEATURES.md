# tuff3d feature checklist

Ticked = in tuff3d now. Unticked = ideas for later, roughly in the order they'd pay off.
Edit this file (or tick boxes on GitHub) to keep track.

## Done

Modeling
- [x] Cube, cylinder, sphere and plane primitives with editable parameters
- [x] Move / rotate / scale manipulators (1, 2 or 3 axes), Ctrl for the other two axes
- [x] Grid, point and 15° rotation snapping (hold X / V / J, or toggle buttons)
- [x] Extrude faces and edges, Merge to Center, Fill Hole
- [x] Freeze Transformations, Center Pivot, Reset Transformations
- [x] Duplicate, delete objects or faces, hide / show
- [x] Vertex paint brush and Apply Color to Selection

Selection
- [x] Object, vertex, edge, face and Multi modes
- [x] Click, marquee (visible-only in shaded mode), Shift / Ctrl modifiers
- [x] Edge loops (double-click), grow / shrink, convert, select all / invert
- [x] Preselection highlight

Display
- [x] Wireframe / shaded
- [x] Hard Shade, Smooth Shade, Auto Smooth with a per-object Normal Angle
- [x] Heads-up poly counts, corner axis gizmo, front / side / top / home views
- [x] 48 themes with a live-preview picker

Files and comfort
- [x] Open / import / export OBJ with vertex colors and shading normals
- [x] Shading survives an OBJ round trip; other apps' smoothing groups are read
- [x] Opens Maya LT `.mlt` scenes by replaying their history
- [x] Open Recent, drag and drop, command-line open
- [x] Autosave and crash recovery, unsaved-changes prompts
- [x] Toolbar, side panel with value scrubbing, named undo steps, on-screen notes
- [x] Remembers window, panel, display settings, theme and last shading mode

## Ideas

Modeling tools (the big gaps compared to Maya)
- [x] Insert Edge Loop (split a ring of faces through an edge)
- [x] Multi-Cut (click to cut new edges across faces)
- [x] Bevel selected edges
- [x] Bridge two edge borders
- [x] Combine / Separate objects
- [x] Delete Edge / Vertex that also cleans up the faces (Ctrl+Delete in Maya)
- [x] Merge vertices by distance and Target Weld
- [ ] Mirror geometry, and live symmetry while moving components
- [ ] Soft selection (B), falloff shown in color
- [x] Reverse (flip) normals on selected faces
- [ ] Smooth mesh (subdivide) and a smooth preview toggle (3 / 1 like Maya)
- [x] Harden / Soften selected edges, on top of the Normal Angle
- [x] Triangulate / Quadrangulate

Quality of life
- [x] Hard / Smooth / Auto Smooth buttons on the toolbar
- [x] Orthographic front / side / top cameras
- [ ] X-ray and backface culling toggles
- [ ] Isolate Select (show only the selection)
- [ ] Outliner: a list of objects to select, rename, hide and reorder
- [ ] Edit the pivot (D / Insert) and snap it to components
- [ ] Type exact move / rotate / scale amounts for components
- [ ] Selection size readout (bounding box in units)
- [ ] Color swatches and an eyedropper for vertex paint
- [ ] Grid size and spacing settings, grid on / off
- [x] Reference image planes for front / side / top
- [ ] Viewport screenshot to PNG
- [x] Hotkey editor

Files
- [ ] glTF (.glb) export with vertex colors, for game engines and the web
- [ ] Export each object to its own OBJ
- [x] A native scene file (.tuff) that keeps everything: hidden objects, primitive settings, camera, image planes
