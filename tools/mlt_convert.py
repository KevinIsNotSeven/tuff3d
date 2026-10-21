"""Batch-convert Maya LT scenes (.mlt) into files other programs can open.

Maya LT is discontinued and its .mlt scenes can't be read by anything else, but full Maya
(2023 or newer, including the free 30-day trial) can load them. This script opens every
.mlt in a folder (and its subfolders) and writes, into a "converted" folder next to them:

  name.obj  every mesh in world space, with vertex colors as "v x y z r g b" (tuff3d reads
            these; Maya's own OBJ exporter drops vertex colors)
  name.fbx  meshes with UVs, materials and colors, for other 3D apps / game engines
  name.ma   a Maya ASCII copy of the whole scene, construction history included

The original .mlt files are only read, never modified.

How to run it, either way:
  1. In Maya: Windows > General Editors > Script Editor, Python tab, File > Load Script...,
     pick this file, then press Execute (Ctrl+Enter). It asks for the folder to convert.
  2. From a command prompt, without opening Maya's window:
       "C:\\Program Files\\Autodesk\\Maya2025\\bin\\mayapy.exe" mlt_convert.py "D:\\My MLT files"
     Options: --no-fbx, --no-ma, --overwrite (redo files that were already converted).
"""

import os
import sys
import time
import traceback

import maya.cmds as cmds
import maya.mel as mel
import maya.api.OpenMaya as om

OUT_DIR = "converted"


def _safe_name(s):
    s = s.split("|")[-1].split(":")[-1]
    return "".join(c if c.isalnum() or c in "_-." else "_" for c in s) or "mesh"


def write_obj_with_colors(path):
    """Write all visible, non-intermediate meshes in world space. Returns the mesh count."""
    meshes = cmds.ls(type="mesh", noIntermediate=True, long=True) or []
    count = 0
    vbase = 1
    with open(path, "w") as f:
        f.write("# converted from Maya LT by tuff3d tools/mlt_convert.py\n")
        for m in meshes:
            # skip meshes that are hidden or only templates in the scene
            if not cmds.ls(m, visible=True):
                continue
            sel = om.MSelectionList()
            sel.add(m)
            dag = sel.getDagPath(0)
            fn = om.MFnMesh(dag)
            pts = fn.getPoints(om.MSpace.kWorld)
            colors = None
            if fn.numColorSets:
                try:
                    colors = fn.getVertexColors()
                except RuntimeError:
                    colors = None
            counts, verts = fn.getVertices()
            mirrored = dag.inclusiveMatrix().det3x3() < 0
            parent = cmds.listRelatives(m, parent=True, fullPath=True)
            f.write("o %s\n" % _safe_name(parent[0] if parent else m))
            for i, p in enumerate(pts):
                c = colors[i] if colors is not None and i < len(colors) else None
                if c is not None and c.r >= 0:
                    f.write("v %.6f %.6f %.6f %.4f %.4f %.4f\n" % (p.x, p.y, p.z, c.r, c.g, c.b))
                else:
                    f.write("v %.6f %.6f %.6f\n" % (p.x, p.y, p.z))
            k = 0
            for n in counts:
                face = [verts[k + j] + vbase for j in range(n)]
                k += n
                if mirrored:
                    face.reverse()   # a mirrored transform turns the faces inside out
                f.write("f %s\n" % " ".join(str(v) for v in face))
            vbase += len(pts)
            count += 1
    return count


def export_fbx(path):
    if not cmds.pluginInfo("fbxmaya", query=True, loaded=True):
        cmds.loadPlugin("fbxmaya", quiet=True)
    mel.eval("FBXResetExport")
    mel.eval("FBXExportInAscii -v false")
    mel.eval('FBXExport -f "%s"' % path.replace("\\", "/"))


def convert_one(src, out_base, do_fbx=True, do_ma=True):
    cmds.file(new=True, force=True)
    cmds.file(src, open=True, force=True, prompt=False, ignoreVersion=True)
    n = write_obj_with_colors(out_base + ".obj")
    if do_fbx:
        export_fbx(out_base + ".fbx")
    if do_ma:
        cmds.file(rename=out_base + ".ma")
        cmds.file(save=True, type="mayaAscii", force=True)
    return n


def convert_folder(folder, do_fbx=True, do_ma=True, overwrite=False):
    folder = os.path.abspath(folder)
    out_root = os.path.join(folder, OUT_DIR)
    jobs = []
    for root, dirs, files in os.walk(folder):
        dirs[:] = [d for d in dirs if os.path.join(root, d) != out_root]
        for fn in files:
            if fn.lower().endswith(".mlt"):
                jobs.append(os.path.join(root, fn))
    jobs.sort()
    if not jobs:
        print("No .mlt files found in %s" % folder)
        return
    log = []
    ok = 0
    t0 = time.time()
    for i, src in enumerate(jobs):
        rel = os.path.relpath(src, folder)
        out_base = os.path.join(out_root, os.path.splitext(rel)[0])
        if not os.path.isdir(os.path.dirname(out_base)):
            os.makedirs(os.path.dirname(out_base))
        if not overwrite and os.path.exists(out_base + ".obj"):
            log.append("skipped (already converted)  %s" % rel)
            continue
        print("[%d/%d] %s" % (i + 1, len(jobs), rel))
        try:
            n = convert_one(src, out_base, do_fbx, do_ma)
            ok += 1
            log.append("ok, %d mesh(es)  %s" % (n, rel))
        except Exception:
            log.append("FAILED  %s\n%s" % (rel, traceback.format_exc()))
    cmds.file(new=True, force=True)
    summary = "Converted %d of %d .mlt file(s) in %.0f s. Output: %s" % (ok, len(jobs), time.time() - t0, out_root)
    with open(os.path.join(out_root, "convert_log.txt"), "w") as f:
        f.write(summary + "\n\n" + "\n".join(log) + "\n")
    print("\n".join(log))
    print(summary)
    return summary


def main_gui():
    picked = cmds.fileDialog2(fileMode=3, caption="Folder with .mlt files to convert")
    if not picked:
        return
    summary = convert_folder(picked[0])
    if summary:
        cmds.confirmDialog(title="MLT conversion", message=summary + "\n\nDetails are in convert_log.txt.", button=["OK"])


def main_cli(argv):
    import maya.standalone
    maya.standalone.initialize(name="python")
    try:
        args = [a for a in argv if not a.startswith("--")]
        if not args:
            print(__doc__)
            return 1
        convert_folder(args[0], do_fbx="--no-fbx" not in argv, do_ma="--no-ma" not in argv,
                       overwrite="--overwrite" in argv)
    finally:
        maya.standalone.uninitialize()
    return 0


if __name__ == "__main__":
    if os.path.basename(sys.executable).lower().startswith("mayapy"):
        sys.exit(main_cli(sys.argv[1:]))
    main_gui()   # Maya's Script Editor runs loaded scripts as __main__
