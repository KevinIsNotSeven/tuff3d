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

/* Polygon mesh. Faces are index lists: face f uses fi[fo[f] .. fo[f+1]-1].
   Edges are derived (mesh_edges) and rebuilt after topology changes.
   vs/es/fs hold the per component selection flags. */
typedef struct Mesh {
    int nv; V3 *p; V3 *c; unsigned char *vs;
    int nf; int *fo; int *fi; unsigned char *fs;
    int ne; int *ev; int *ef; unsigned char *es;   /* ev: 2 verts (lo,hi), ef: up to 2 faces (-1) */
} Mesh;

enum { COMP_VERT = 1, COMP_EDGE = 2, COMP_FACE = 3 };
enum { PRIM_NONE, PRIM_CUBE, PRIM_CYL, PRIM_SPHERE };

typedef struct Prim {
    int type;
    float w, h, d; int sw, sh, sd;                          /* cube */
    float cr, ch; int csides, chseg, ccap, ctop, cbot;      /* cylinder (ccap: cap rings, 0 = n-gon) */
    float sr; int scols, srows;                             /* sphere */
} Prim;

void prim_defaults(Prim *p, int type);
void prim_clamp(Prim *p);
Mesh mesh_prim(const Prim *p);

void mesh_free(Mesh *m);
Mesh mesh_copy(const Mesh *m);
void mesh_edges(Mesh *m);
V3 mesh_fnormal(const Mesh *m, int f);
V3 mesh_fcenter(const Mesh *m, int f);
int mesh_vmask(const Mesh *m, int comp, unsigned char *mask);
int mesh_extrude_faces(Mesh *m);
int mesh_extrude_edges(Mesh *m);
int mesh_merge(Mesh *m, const unsigned char *mask);
int mesh_delete_faces(Mesh *m);
int mesh_raycast(const Mesh *m, V3 o, V3 d, float *t);

int obj_save(const char *path, int n, const Mesh *const *ms, const M4 *mats, const char *const *names);
int obj_load(const char *path, Mesh **ms, char (**names)[32]);

#endif
