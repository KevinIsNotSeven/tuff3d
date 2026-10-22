/* tuff3d - platform independent math, mesh, primitives, mesh ops and OBJ io */
#ifndef TUFF_MESH_H
#define TUFF_MESH_H

#include <math.h>

typedef struct { float x, y, z; } V3;
typedef struct { float m[16]; } M4; /* column-major, OpenGL layout */

static inline V3 v3(float x, float y, float z) { V3 r; r.x = x; r.y = y; r.z = z; return r; }
static inline V3 v3add(V3 a, V3 b) { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline V3 v3sub(V3 a, V3 b) { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline V3 v3mul(V3 a, float s) { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 v3cross(V3 a, V3 b) { return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
static inline float v3len(V3 a) { return sqrtf(v3dot(a, a)); }
static inline V3 v3norm(V3 a) { float l = v3len(a); return l > 1e-20f ? v3mul(a, 1.0f / l) : v3(0, 0, 0); }
static inline V3 v3lerp(V3 a, V3 b, float t) { return v3(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }

M4 m4_id(void);
M4 m4_mul(M4 a, M4 b);
M4 m4_inv(M4 a);
V3 m4_pt(M4 m, V3 p);
V3 m4_dir(M4 m, V3 d);
V3 m4_col(M4 m, int c);
M4 m4_euler(V3 rdeg);               /* Maya XYZ rotate order */
V3 m4_to_euler(M4 r);               /* inverse of m4_euler (degrees) */
M4 m4_axis_angle(V3 axis, float rad);
M4 m4_trs(V3 t, V3 rdeg, V3 s);
M4 m4_persp(float fovy, float aspect, float n, float f);
M4 m4_lookat(V3 eye, V3 at, V3 up);
M4 m4_ortho(float l, float r, float b, float t, float n, float f);

/* Polygon mesh. Faces are index lists: face f uses fi[fo[f] .. fo[f+1]-1].
   Edges are derived (mesh_edges) and rebuilt after topology changes.
   vs/es/fs hold the per component selection flags. eh is a per edge shading override:
   EH_AUTO follows the object's normal angle, EH_HARD / EH_SOFT force it (Harden / Soften Edge). */
typedef struct Mesh {
    int nv; V3 *p; V3 *c; unsigned char *vs;
    int nf; int *fo; int *fi; unsigned char *fs;
    int ne; int *ev; int *ef; unsigned char *es;   /* ev: 2 verts (lo,hi), ef: up to 2 faces (-1) */
    unsigned char *eh;
} Mesh;

enum { EH_AUTO, EH_HARD, EH_SOFT };

enum { COMP_VERT = 1, COMP_EDGE = 2, COMP_FACE = 3, COMP_MULTI = 4 };   /* MULTI: union of all three */
enum { PRIM_NONE, PRIM_CUBE, PRIM_CYL, PRIM_SPHERE, PRIM_PLANE, PRIM_COUNT };

typedef struct Prim {
    int type;
    float w, h, d; int sw, sh, sd;                          /* cube */
    float cr, ch; int csides, chseg, ccap, ctop, cbot;      /* cylinder (ccap: cap rings, 0 = n-gon) */
    float sr; int scols, srows;                             /* sphere */
    float pw, pd; int psw, psd;                             /* plane (XZ, facing +Y) */
} Prim;

void prim_defaults(Prim *p, int type);
void prim_clamp(Prim *p);
Mesh mesh_prim(const Prim *p);

void mesh_free(Mesh *m);
Mesh mesh_copy(const Mesh *m);
void mesh_edges(Mesh *m);
V3 mesh_fnormal(const Mesh *m, int f);
V3 mesh_fcenter(const Mesh *m, int f);
void mesh_cnormals(const Mesh *m, float angle, V3 *out);   /* out: one per face corner, fo[nf] */
int mesh_vmask(const Mesh *m, int comp, unsigned char *mask);
int mesh_extrude_faces(Mesh *m);
int mesh_extrude_edges(Mesh *m);
int mesh_merge(Mesh *m, const unsigned char *mask);
int mesh_delete_faces(Mesh *m);
int mesh_raycast(const Mesh *m, V3 o, V3 d, float *t);

/* selection helpers; comp is COMP_VERT/EDGE/FACE */
int mesh_edge_loop(Mesh *m, int e);                 /* adds the loop through edge e, returns its length */
void mesh_grow(Mesh *m, int comp, int shrink);      /* grow/shrink by one ring of vertex neighbors */
void mesh_convert(Mesh *m, int from, int to);       /* selection of kind 'from' becomes kind 'to' */
void mesh_flip(Mesh *m);                            /* reverse every face's winding */
int mesh_edge_find(const Mesh *m, int a, int b);    /* the edge joining a and b, or -1 */
void mesh_set_eh(Mesh *m, const int *trip, int n);  /* n (a, b, eh) triples: edge overrides */
/* the edge ring through edge e (across quads), oriented so t measured from each ring edge's
   first vertex lines up; ra/rb get the ring's vertex pairs (room for m->ne). Returns its length,
   *closed says whether it wraps around. */
int mesh_edge_ring(const Mesh *m, int e, int *ra, int *rb, int *closed);
/* cap open border loops with one face each: every loop touching a vertex in touch[] (all loops when
   touch is NULL). The new faces are the only selected faces afterwards. Returns how many were made. */
int mesh_fill_holes(Mesh *m, const unsigned char *touch);

/* ---- modeling ops. fmask/vmask/emask pick faces/vertices/edges (NULL: all where allowed).
   Each returns how many things it changed (0: nothing, the mesh is untouched). */
int mesh_reverse(Mesh *m, const unsigned char *fmask);               /* flip the winding (normals) */
int mesh_triangulate(Mesh *m, const unsigned char *fmask);
int mesh_quadrangulate(Mesh *m, const unsigned char *fmask, float angle);   /* join triangle pairs */
int mesh_shells(const Mesh *m, int *fshell);                         /* face -> shell id, returns count */
Mesh mesh_subset(const Mesh *m, const unsigned char *fmask);         /* a new mesh of just these faces */
void mesh_append(Mesh *dst, const Mesh *src, const M4 *xf);          /* add src's faces, transformed */
int mesh_merge_dist(Mesh *m, const unsigned char *vmask, float dist);   /* merge vertices this close */
int mesh_weld(Mesh *m, int src, int dst);                            /* Target Weld: src onto dst */
int mesh_delete_edges(Mesh *m, const unsigned char *emask, int clean);  /* clean: drop leftover verts */
int mesh_delete_verts(Mesh *m, const unsigned char *vmask);
/* Multi-Cut: points on edges (vertex pair a-b, fraction t from a) or vertices (b < 0); each pair of
   consecutive points that share a face cuts it. The new edges are left selected. */
typedef struct { int a, b; float t; } CutPt;
int mesh_cut(Mesh *m, const CutPt *pts, int n);
int mesh_insert_loop(Mesh *m, int e, float t);                       /* t along edge e (ev order) */
int mesh_bevel(Mesh *m, const unsigned char *emask, float width);    /* new faces left selected */
/* Bridge two open borders (selected border edges in two runs of the same length); divs rows of
   faces in between. Returns faces made, -1 if the selection is not two runs, -2 if they differ. */
int mesh_bridge(Mesh *m, const unsigned char *emask, int divs);

/* angles: per mesh normal angle (writes vn + s), or NULL for positions only */
int obj_save(const char *path, int n, const Mesh *const *ms, const M4 *mats, const char *const *names, const float *angles);
/* angles (may be NULL): per mesh normal angle from s lines or a tuff3d comment, -1 = not given */
int obj_load(const char *path, Mesh **ms, char (**names)[32], float **angles);

/* build a mesh from plain arrays (face f uses fi[fo[f] .. fo[f+1]-1]); c may be NULL for white */
Mesh mesh_from_arrays(int nv, const V3 *p, const V3 *c, int nf, const int *fo, const int *fi);

/* Maya LT scene (.mlt): rebuilds each visible mesh from its construction history, in world space.
   report (optional) receives a short summary of what was rebuilt or skipped. Returns -1 on error. */
int mlt_load(const char *path, Mesh **ms, char (**names)[32], char *report, int report_size);

#endif
