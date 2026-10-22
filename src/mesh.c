/* tuff3d - platform independent math, mesh, primitives, mesh ops and OBJ io */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"

#define PI_F 3.14159265358979f
#define DEG2RAD (PI_F / 180.0f)

static void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) abort();
    return q;
}

static void *xcalloc(size_t n, size_t s)
{
    void *q = calloc(n ? n : 1, s ? s : 1);
    if (!q) abort();
    return q;
}

/* ---------------------------------------------------------------- math */

M4 m4_id(void)
{
    M4 r;
    memset(&r, 0, sizeof r);
    r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1;
    return r;
}

M4 m4_mul(M4 a, M4 b)
{
    M4 r;
    for (int c = 0; c < 4; c++)
        for (int i = 0; i < 4; i++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += a.m[k * 4 + i] * b.m[c * 4 + k];
            r.m[c * 4 + i] = s;
        }
    return r;
}

M4 m4_inv(M4 a)
{
    const float *m = a.m;
    float inv[16];
    inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
    inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
    inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
    inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
    inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
    inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
    inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
    inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
    inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
    inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
    inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
    inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
    inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
    inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
    inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
    inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];
    float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
    M4 r;
    if (fabsf(det) < 1e-30f) return m4_id();
    det = 1.0f / det;
    for (int i = 0; i < 16; i++) r.m[i] = inv[i] * det;
    return r;
}

V3 m4_pt(M4 a, V3 p)
{
    const float *m = a.m;
    return v3(m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
              m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
              m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]);
}

V3 m4_dir(M4 a, V3 p)
{
    const float *m = a.m;
    return v3(m[0] * p.x + m[4] * p.y + m[8] * p.z,
              m[1] * p.x + m[5] * p.y + m[9] * p.z,
              m[2] * p.x + m[6] * p.y + m[10] * p.z);
}

V3 m4_col(M4 a, int c) { return v3(a.m[c * 4], a.m[c * 4 + 1], a.m[c * 4 + 2]); }

/* R = Rz * Ry * Rx (X applied first, Maya's default xyz rotate order) */
M4 m4_euler(V3 r)
{
    float cx = cosf(r.x * DEG2RAD), sx = sinf(r.x * DEG2RAD);
    float cy = cosf(r.y * DEG2RAD), sy = sinf(r.y * DEG2RAD);
    float cz = cosf(r.z * DEG2RAD), sz = sinf(r.z * DEG2RAD);
    M4 m = m4_id();
    m.m[0] = cz * cy;  m.m[4] = cz * sy * sx - sz * cx;  m.m[8] = cz * sy * cx + sz * sx;
    m.m[1] = sz * cy;  m.m[5] = sz * sy * sx + cz * cx;  m.m[9] = sz * sy * cx - cz * sx;
    m.m[2] = -sy;      m.m[6] = cy * sx;                 m.m[10] = cy * cx;
    return m;
}

V3 m4_to_euler(M4 r)
{
    V3 c0 = v3norm(m4_col(r, 0)), c1 = v3norm(m4_col(r, 1)), c2 = v3norm(m4_col(r, 2));
    float r00 = c0.x, r10 = c0.y, r20 = c0.z, r01 = c1.x, r11 = c1.y, r21 = c1.z, r22 = c2.z;
    float x, y, z;
    if (fabsf(r20) < 0.99999f) {
        y = asinf(-r20);
        x = atan2f(r21, r22);
        z = atan2f(r10, r00);
    } else {
        y = r20 < 0 ? PI_F / 2 : -PI_F / 2;
        x = 0;
        z = atan2f(-r01, r11);
    }
    return v3(x / DEG2RAD, y / DEG2RAD, z / DEG2RAD);
}

M4 m4_axis_angle(V3 a, float ang)
{
    a = v3norm(a);
    float c = cosf(ang), s = sinf(ang), t = 1 - c;
    M4 m = m4_id();
    m.m[0] = t * a.x * a.x + c;       m.m[4] = t * a.x * a.y - s * a.z; m.m[8] = t * a.x * a.z + s * a.y;
    m.m[1] = t * a.x * a.y + s * a.z; m.m[5] = t * a.y * a.y + c;       m.m[9] = t * a.y * a.z - s * a.x;
    m.m[2] = t * a.x * a.z - s * a.y; m.m[6] = t * a.y * a.z + s * a.x; m.m[10] = t * a.z * a.z + c;
    return m;
}

M4 m4_trs(V3 t, V3 r, V3 s)
{
    M4 m = m4_euler(r);
    for (int i = 0; i < 3; i++) {
        m.m[i] *= s.x;
        m.m[4 + i] *= s.y;
        m.m[8 + i] *= s.z;
    }
    m.m[12] = t.x; m.m[13] = t.y; m.m[14] = t.z;
    return m;
}

M4 m4_persp(float fovy, float aspect, float n, float f)
{
    M4 m;
    memset(&m, 0, sizeof m);
    float k = 1.0f / tanf(fovy * 0.5f);
    m.m[0] = k / aspect;
    m.m[5] = k;
    m.m[10] = (f + n) / (n - f);
    m.m[11] = -1;
    m.m[14] = 2 * f * n / (n - f);
    return m;
}

M4 m4_lookat(V3 eye, V3 at, V3 up)
{
    V3 f = v3norm(v3sub(at, eye));
    V3 s = v3norm(v3cross(f, up));
    V3 u = v3cross(s, f);
    M4 m = m4_id();
    m.m[0] = s.x; m.m[4] = s.y; m.m[8] = s.z;
    m.m[1] = u.x; m.m[5] = u.y; m.m[9] = u.z;
    m.m[2] = -f.x; m.m[6] = -f.y; m.m[10] = -f.z;
    m.m[12] = -v3dot(s, eye);
    m.m[13] = -v3dot(u, eye);
    m.m[14] = v3dot(f, eye);
    return m;
}

M4 m4_ortho(float l, float r, float b, float t, float n, float f)
{
    M4 m = m4_id();
    m.m[0] = 2 / (r - l);
    m.m[5] = 2 / (t - b);
    m.m[10] = -2 / (f - n);
    m.m[12] = -(r + l) / (r - l);
    m.m[13] = -(t + b) / (t - b);
    m.m[14] = -(f + n) / (f - n);
    return m;
}

/* ---------------------------------------------------------------- edge hash */

typedef struct { unsigned long long *k; int *v; unsigned mask; } HMap;

static void hm_init(HMap *h, int n)
{
    unsigned s = 16;
    while (s < (unsigned)n * 2u) s <<= 1;
    h->mask = s - 1;
    h->k = xrealloc(NULL, s * sizeof *h->k);
    h->v = xrealloc(NULL, s * sizeof *h->v);
    memset(h->k, 0xff, s * sizeof *h->k);
}

static void hm_free(HMap *h) { free(h->k); free(h->v); }

/* value slot for the ordered pair (a,b); new slots start at -1 */
static int *hm_getd(HMap *h, int a, int b)
{
    unsigned long long key = ((unsigned long long)(unsigned)a << 32) | (unsigned)b;
    unsigned i = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 32) & h->mask;
    while (h->k[i] != ~0ull && h->k[i] != key) i = (i + 1) & h->mask;
    if (h->k[i] == ~0ull) { h->k[i] = key; h->v[i] = -1; }
    return &h->v[i];
}

/* value slot for undirected edge (a,b) */
static int *hm_get(HMap *h, int a, int b)
{
    if (a > b) { int t = a; a = b; b = t; }
    return hm_getd(h, a, b);
}

/* edge index by vertex pair, for a mesh's current edges */
static void edge_map(HMap *h, const Mesh *m)
{
    hm_init(h, 2 * m->ne + 8);
    for (int e = 0; e < m->ne; e++) *hm_get(h, m->ev[2 * e], m->ev[2 * e + 1]) = e;
}

static int edge_at(HMap *h, int a, int b)
{
    return *hm_get(h, a, b);
}

/* ---------------------------------------------------------------- mesh basics */

/* mesh builder; hp holds (a, b, eh) triples: shading overrides for the edges a-b once built */
typedef struct { Mesh m; int cv, cf, ci, ni; int *hp, nhp, chp; } MB;

static void mb_hard(MB *b, int a, int c, int val)
{
    if (b->nhp + 3 > b->chp) { b->chp = b->chp ? b->chp * 2 : 48; b->hp = xrealloc(b->hp, b->chp * sizeof(int)); }
    b->hp[b->nhp++] = a;
    b->hp[b->nhp++] = c;
    b->hp[b->nhp++] = val;
}

static void mb_init(MB *b)
{
    memset(b, 0, sizeof *b);
    b->cf = 64;
    b->m.fo = xrealloc(NULL, b->cf * sizeof(int));
    b->m.fs = xrealloc(NULL, b->cf);
    b->m.fo[0] = 0;
}

static int mb_vert(MB *b, V3 p, V3 c)
{
    Mesh *m = &b->m;
    if (m->nv == b->cv) {
        b->cv = b->cv ? b->cv * 2 : 64;
        m->p = xrealloc(m->p, b->cv * sizeof(V3));
        m->c = xrealloc(m->c, b->cv * sizeof(V3));
        m->vs = xrealloc(m->vs, b->cv);
    }
    m->p[m->nv] = p;
    m->c[m->nv] = c;
    m->vs[m->nv] = 0;
    return m->nv++;
}

static void mb_face(MB *b, const int *idx, int n, int sel)
{
    Mesh *m = &b->m;
    if (m->nf + 2 > b->cf) {
        b->cf *= 2;
        m->fo = xrealloc(m->fo, b->cf * sizeof(int));
        m->fs = xrealloc(m->fs, b->cf);
    }
    if (b->ni + n > b->ci) {
        b->ci = b->ci ? b->ci * 2 : 256;
        if (b->ci < b->ni + n) b->ci = b->ni + n;
        m->fi = xrealloc(m->fi, b->ci * sizeof(int));
    }
    memcpy(m->fi + b->ni, idx, n * sizeof(int));
    b->ni += n;
    m->fs[m->nf] = (unsigned char)sel;
    m->nf++;
    m->fo[m->nf] = b->ni;
}

static V3 newell(const V3 *p, const int *idx, int n)
{
    V3 r = v3(0, 0, 0);
    for (int i = 0; i < n; i++) {
        V3 a = p[idx[i]], b = p[idx[(i + 1) % n]];
        r.x += (a.y - b.y) * (a.z + b.z);
        r.y += (a.z - b.z) * (a.x + b.x);
        r.z += (a.x - b.x) * (a.y + b.y);
    }
    return r;
}

/* add a face, reversing its winding if it does not face along 'want' */
static void mb_face_dir(MB *b, int *idx, int n, V3 want)
{
    if (v3dot(newell(b->m.p, idx, n), want) < 0)
        for (int i = 0; i < n / 2; i++) { int t = idx[i]; idx[i] = idx[n - 1 - i]; idx[n - 1 - i] = t; }
    mb_face(b, idx, n, 0);
}

/* drop vertices no face uses (selection flags move with them); *mapout (optional) gets old -> new */
static void mesh_compact(Mesh *m, int **mapout)
{
    int ni = m->fo[m->nf];
    int *map = xrealloc(NULL, (m->nv ? m->nv : 1) * sizeof(int));
    for (int i = 0; i < m->nv; i++) map[i] = -1;
    for (int i = 0; i < ni; i++) map[m->fi[i]] = 1;
    int n = 0;
    for (int i = 0; i < m->nv; i++) {
        if (map[i] < 0) continue;
        map[i] = n;
        m->p[n] = m->p[i];
        m->c[n] = m->c[i];
        m->vs[n] = m->vs[i];
        n++;
    }
    for (int i = 0; i < ni; i++) m->fi[i] = map[m->fi[i]];
    m->nv = n;
    if (mapout) *mapout = map;
    else free(map);
}

static Mesh mb_finish(MB *b)
{
    Mesh m = b->m;
    if (!m.p) {
        m.p = xrealloc(NULL, sizeof(V3));
        m.c = xrealloc(NULL, sizeof(V3));
        m.vs = xrealloc(NULL, 1);
    }
    if (!m.fi) m.fi = xrealloc(NULL, sizeof(int));
    int *map, nv0 = m.nv;
    mesh_compact(&m, &map);
    mesh_edges(&m);
    if (b->nhp) {
        HMap h;
        edge_map(&h, &m);
        for (int i = b->nhp - 3; i >= 0; i -= 3) {   /* backwards: the first pair given for an edge wins */
            int a = b->hp[i], c = b->hp[i + 1];
            if (a < 0 || c < 0 || a >= nv0 || c >= nv0 || map[a] < 0 || map[c] < 0) continue;
            int e = edge_at(&h, map[a], map[c]);
            if (e >= 0) m.eh[e] = (unsigned char)b->hp[i + 2];
        }
        hm_free(&h);
    }
    free(map);
    free(b->hp);
    b->hp = NULL;
    b->nhp = b->chp = 0;
    return m;
}

void mesh_free(Mesh *m)
{
    free(m->p); free(m->c); free(m->vs);
    free(m->fo); free(m->fi); free(m->fs);
    free(m->ev); free(m->ef); free(m->es); free(m->eh);
    memset(m, 0, sizeof *m);
}

static void *dup_mem(const void *src, size_t n)
{
    void *d = xrealloc(NULL, n);
    if (n) memcpy(d, src, n);
    return d;
}

Mesh mesh_copy(const Mesh *s)
{
    Mesh m = *s;
    int ni = s->fo[s->nf];
    m.p = dup_mem(s->p, s->nv * sizeof(V3));
    m.c = dup_mem(s->c, s->nv * sizeof(V3));
    m.vs = dup_mem(s->vs, s->nv);
    m.fo = dup_mem(s->fo, (s->nf + 1) * sizeof(int));
    m.fi = dup_mem(s->fi, ni * sizeof(int));
    m.fs = dup_mem(s->fs, s->nf);
    m.ev = dup_mem(s->ev, 2 * s->ne * sizeof(int));
    m.ef = dup_mem(s->ef, 2 * s->ne * sizeof(int));
    m.es = dup_mem(s->es, s->ne);
    m.eh = s->eh ? dup_mem(s->eh, s->ne) : xcalloc(s->ne, 1);
    return m;
}

void mesh_edges(Mesh *m)
{
    int ni = m->fo[m->nf];
    free(m->ev); free(m->ef); free(m->es); free(m->eh);
    m->ev = xrealloc(NULL, (2 * ni + 2) * sizeof(int));
    m->ef = xrealloc(NULL, (2 * ni + 2) * sizeof(int));
    m->ne = 0;
    HMap h;
    hm_init(&h, ni);
    for (int f = 0; f < m->nf; f++) {
        int o = m->fo[f], n = m->fo[f + 1] - o;
        for (int k = 0; k < n; k++) {
            int a = m->fi[o + k], b = m->fi[o + (k + 1) % n];
            if (a == b) continue;
            int *e = hm_get(&h, a, b);
            if (*e < 0) {
                *e = m->ne++;
                m->ev[2 * *e] = a < b ? a : b;
                m->ev[2 * *e + 1] = a < b ? b : a;
                m->ef[2 * *e] = f;
                m->ef[2 * *e + 1] = -1;
            } else if (m->ef[2 * *e + 1] < 0 && m->ef[2 * *e] != f) {
                m->ef[2 * *e + 1] = f;
            }
        }
    }
    hm_free(&h);
    m->es = xcalloc(m->ne, 1);
    m->eh = xcalloc(m->ne, 1);
}

V3 mesh_fnormal(const Mesh *m, int f)
{
    return v3norm(newell(m->p, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f]));
}

V3 mesh_fcenter(const Mesh *m, int f)
{
    V3 c = v3(0, 0, 0);
    int o = m->fo[f], n = m->fo[f + 1] - o;
    for (int k = 0; k < n; k++) c = v3add(c, m->p[m->fi[o + k]]);
    return n ? v3mul(c, 1.0f / n) : c;
}

/* Per face-corner normals (out[fo[f] + k]). The corners around a vertex share one normal (the area
   weighted average of their faces) across every soft edge: an edge is soft when its faces are within
   'angle' degrees of each other (0 = hard/flat, 180 = fully smooth, in between = auto smooth), unless
   its eh override says hard or soft. */
static int find_root(int *par, int x)
{
    while (par[x] != x) x = par[x] = par[par[x]];
    return x;
}

static int corner_at(const Mesh *m, int f, int v)
{
    for (int c = m->fo[f]; c < m->fo[f + 1]; c++) if (m->fi[c] == v) return c;
    return -1;
}

void mesh_cnormals(const Mesh *m, float angle, V3 *out)
{
    int nc = m->fo[m->nf];
    V3 *fn = xrealloc(NULL, (m->nf + 1) * sizeof(V3)), *sum = xcalloc(nc + 1, sizeof(V3));
    int *cf = xrealloc(NULL, (nc + 1) * sizeof(int)), *par = xrealloc(NULL, (nc + 1) * sizeof(int));
    for (int f = 0; f < m->nf; f++) {
        fn[f] = newell(m->p, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f]);
        for (int c = m->fo[f]; c < m->fo[f + 1]; c++) { cf[c] = f; par[c] = c; }
    }
    float ct = angle >= 180 ? -2.0f : angle <= 0 ? 2.0f : cosf(angle * DEG2RAD) - 1e-5f;
    for (int e = 0; e < m->ne; e++) {
        int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1], h = m->eh ? m->eh[e] : EH_AUTO;
        if (f0 < 0 || f1 < 0 || h == EH_HARD) continue;
        if (h != EH_SOFT && v3dot(v3norm(fn[f0]), v3norm(fn[f1])) < ct) continue;
        for (int k = 0; k < 2; k++) {
            int c0 = corner_at(m, f0, m->ev[2 * e + k]), c1 = corner_at(m, f1, m->ev[2 * e + k]);
            if (c0 >= 0 && c1 >= 0) par[find_root(par, c0)] = find_root(par, c1);
        }
    }
    for (int c = 0; c < nc; c++) { int r = find_root(par, c); sum[r] = v3add(sum[r], fn[cf[c]]); }
    for (int c = 0; c < nc; c++) {
        V3 n = v3norm(sum[find_root(par, c)]);
        out[c] = v3dot(n, n) > 0 ? n : v3norm(fn[cf[c]]);
    }
    free(fn); free(sum); free(cf); free(par);
}


int mesh_vmask(const Mesh *m, int comp, unsigned char *mask)
{
    memset(mask, 0, m->nv);
    if (comp == COMP_VERT || comp == COMP_MULTI) {
        for (int i = 0; i < m->nv; i++) mask[i] = m->vs[i] != 0;
    }
    if (comp == COMP_EDGE || comp == COMP_MULTI) {
        for (int e = 0; e < m->ne; e++)
            if (m->es[e]) mask[m->ev[2 * e]] = mask[m->ev[2 * e + 1]] = 1;
    }
    if (comp == COMP_FACE || comp == COMP_MULTI) {
        for (int f = 0; f < m->nf; f++)
            if (m->fs[f])
                for (int k = m->fo[f]; k < m->fo[f + 1]; k++) mask[m->fi[k]] = 1;
    }
    int n = 0;
    for (int i = 0; i < m->nv; i++) n += mask[i];
    return n;
}

/* b was started with mb_from_verts(m), so its vertex numbers match m's: edge overrides carry over */
static void mesh_replace(Mesh *m, MB *b)
{
    for (int e = 0; e < m->ne; e++)
        if (m->eh && m->eh[e]) mb_hard(b, m->ev[2 * e], m->ev[2 * e + 1], m->eh[e]);
    Mesh n = mb_finish(b);
    mesh_free(m);
    *m = n;
}

static MB mb_from_verts(const Mesh *m)
{
    MB b;
    mb_init(&b);
    for (int v = 0; v < m->nv; v++) mb_vert(&b, m->p[v], m->c[v]);
    return b;
}

/* ---------------------------------------------------------------- mesh ops */

/* Extrude selected faces, keeping adjacent faces together. The extruded faces
   stay selected and sit on top of the original ones until moved. */
int mesh_extrude_faces(Mesh *m)
{
    int nsel = 0;
    for (int f = 0; f < m->nf; f++) nsel += m->fs[f] != 0;
    if (!nsel) return 0;

    MB b = mb_from_verts(m);
    int *dup = xrealloc(NULL, m->nv * sizeof(int));
    int *tmp = xrealloc(NULL, (m->fo[m->nf] + 4) * sizeof(int));
    for (int v = 0; v < m->nv; v++) dup[v] = -1;
    HMap h;
    hm_init(&h, m->fo[m->nf]);

    for (int f = 0; f < m->nf; f++) {
        int o = m->fo[f], n = m->fo[f + 1] - o;
        if (!m->fs[f]) { mb_face(&b, m->fi + o, n, 0); continue; }
        for (int k = 0; k < n; k++) {
            int v = m->fi[o + k];
            if (dup[v] < 0) dup[v] = mb_vert(&b, m->p[v], m->c[v]);
            tmp[k] = dup[v];
            int a = v, c = m->fi[o + (k + 1) % n];
            if (a != c) { int *cnt = hm_get(&h, a, c); *cnt = *cnt < 0 ? 1 : *cnt + 1; }
        }
        mb_face(&b, tmp, n, 1);
    }
    /* side walls along the border of the selected region */
    for (int f = 0; f < m->nf; f++) {
        if (!m->fs[f]) continue;
        int o = m->fo[f], n = m->fo[f + 1] - o;
        for (int k = 0; k < n; k++) {
            int a = m->fi[o + k], c = m->fi[o + (k + 1) % n];
            if (a == c || *hm_get(&h, a, c) != 1) continue;
            int q[4] = { a, c, dup[c], dup[a] };
            mb_face(&b, q, 4, 0);
        }
    }
    hm_free(&h);
    free(dup);
    free(tmp);
    mesh_replace(m, &b);
    return 1;
}

/* Extrude selected edges into new quads; the new outer edges become selected. */
int mesh_extrude_edges(Mesh *m)
{
    int nsel = 0;
    for (int e = 0; e < m->ne; e++) nsel += m->es[e] != 0;
    if (!nsel) return 0;

    MB b = mb_from_verts(m);
    int *dup = xrealloc(NULL, m->nv * sizeof(int));
    int made = 0;
    for (int v = 0; v < m->nv; v++) dup[v] = -1;
    for (int f = 0; f < m->nf; f++) mb_face(&b, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], 0);

    for (int e = 0; e < m->ne; e++) {
        if (!m->es[e]) continue;
        int a = m->ev[2 * e], c = m->ev[2 * e + 1];
        int f = m->ef[2 * e];
        if (f < 0) continue;
        /* orient so that the existing face runs a->c */
        int o = m->fo[f], n = m->fo[f + 1] - o;
        for (int k = 0; k < n; k++)
            if (m->fi[o + k] == c && m->fi[o + (k + 1) % n] == a) { int t = a; a = c; c = t; break; }
        if (dup[a] < 0) { dup[a] = mb_vert(&b, m->p[a], m->c[a]); b.m.vs[dup[a]] = 2; }
        if (dup[c] < 0) { dup[c] = mb_vert(&b, m->p[c], m->c[c]); b.m.vs[dup[c]] = 2; }
        int q[4] = { c, a, dup[a], dup[c] };
        mb_face(&b, q, 4, 0);
        made++;
    }
    free(dup);
    mesh_replace(m, &b);
    /* the only edges joining two new vertices are the new outer edges */
    for (int e = 0; e < m->ne; e++) m->es[e] = m->vs[m->ev[2 * e]] == 2 && m->vs[m->ev[2 * e + 1]] == 2;
    memset(m->vs, 0, m->nv);
    return made > 0;
}

/* Merge all masked vertices into one vertex at their center. */
int mesh_merge(Mesh *m, const unsigned char *mask)
{
    int cnt = 0, target = -1;
    V3 cen = v3(0, 0, 0), col = v3(0, 0, 0);
    for (int v = 0; v < m->nv; v++)
        if (mask[v]) {
            if (target < 0) target = v;
            cen = v3add(cen, m->p[v]);
            col = v3add(col, m->c[v]);
            cnt++;
        }
    if (cnt < 2) return 0;

    MB b = mb_from_verts(m);
    b.m.p[target] = v3mul(cen, 1.0f / cnt);
    b.m.c[target] = v3mul(col, 1.0f / cnt);
    b.m.vs[target] = 1;
    int *tmp = xrealloc(NULL, (m->fo[m->nf] + 1) * sizeof(int));
    for (int f = 0; f < m->nf; f++) {
        int o = m->fo[f], n = m->fo[f + 1] - o, k2 = 0;
        for (int k = 0; k < n; k++) {
            int v = m->fi[o + k];
            if (mask[v]) v = target;
            if (k2 == 0 || tmp[k2 - 1] != v) tmp[k2++] = v;
        }
        while (k2 > 1 && tmp[k2 - 1] == tmp[0]) k2--;
        if (k2 >= 3) mb_face(&b, tmp, k2, 0);
    }
    free(tmp);
    mesh_replace(m, &b);
    return 1;
}

int mesh_delete_faces(Mesh *m)
{
    int nsel = 0;
    for (int f = 0; f < m->nf; f++) nsel += m->fs[f] != 0;
    if (!nsel) return 0;
    MB b = mb_from_verts(m);
    for (int f = 0; f < m->nf; f++)
        if (!m->fs[f]) mb_face(&b, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], 0);
    mesh_replace(m, &b);
    return 1;
}

int mesh_raycast(const Mesh *m, V3 o, V3 d, float *tout)
{
    int best = -1;
    float bt = 1e30f;
    for (int f = 0; f < m->nf; f++) {
        int s = m->fo[f], n = m->fo[f + 1] - s;
        V3 p0 = m->p[m->fi[s]];
        for (int k = 1; k + 1 < n; k++) {
            V3 e1 = v3sub(m->p[m->fi[s + k]], p0), e2 = v3sub(m->p[m->fi[s + k + 1]], p0);
            V3 pv = v3cross(d, e2);
            float det = v3dot(e1, pv);
            if (fabsf(det) < 1e-12f) continue;
            float inv = 1.0f / det;
            V3 tv = v3sub(o, p0);
            float u = v3dot(tv, pv) * inv;
            if (u < 0 || u > 1) continue;
            V3 qv = v3cross(tv, e1);
            float w = v3dot(d, qv) * inv;
            if (w < 0 || u + w > 1) continue;
            float t = v3dot(e2, qv) * inv;
            if (t > 0 && t < bt) { bt = t; best = f; }
        }
    }
    if (tout) *tout = bt;
    return best;
}

Mesh mesh_from_arrays(int nv, const V3 *p, const V3 *c, int nf, const int *fo, const int *fi)
{
    MB b;
    mb_init(&b);
    for (int v = 0; v < nv; v++) mb_vert(&b, p[v], c ? c[v] : v3(1, 1, 1));
    for (int f = 0; f < nf; f++) mb_face(&b, fi + fo[f], fo[f + 1] - fo[f], 0);
    return mb_finish(&b);
}

/* ---------------------------------------------------------------- selection helpers */

/* vertex -> incident edges, CSR layout: edges of v are ve[off[v] .. off[v+1]-1] */
static void vert_edges(const Mesh *m, int **off, int **ve)
{
    int *o = xrealloc(NULL, (m->nv + 1) * sizeof(int)), *e = xrealloc(NULL, (2 * m->ne + 1) * sizeof(int));
    memset(o, 0, (m->nv + 1) * sizeof(int));
    for (int i = 0; i < 2 * m->ne; i++) o[m->ev[i] + 1]++;
    for (int v = 0; v < m->nv; v++) o[v + 1] += o[v];
    int *fill = xrealloc(NULL, (m->nv + 1) * sizeof(int));
    memcpy(fill, o, (m->nv + 1) * sizeof(int));
    for (int i = 0; i < 2 * m->ne; i++) e[fill[m->ev[i]]++] = i / 2;
    free(fill);
    *off = o;
    *ve = e;
}

static int shares_face(const Mesh *m, int a, int b)
{
    for (int i = 0; i < 2; i++)
        for (int j = 0; j < 2; j++)
            if (m->ef[2 * a + i] >= 0 && m->ef[2 * a + i] == m->ef[2 * b + j]) return 1;
    return 0;
}

/* the edge continuing a loop from edge ce through vertex v, or -1: straight across a 4-valent
   vertex, or along an open border */
static int loop_next(const Mesh *m, const int *off, const int *ve, int ce, int v)
{
    int border = m->ef[2 * ce + 1] < 0, cand = -1, n = 0;
    if (!border && off[v + 1] - off[v] != 4) return -1;
    for (int k = off[v]; k < off[v + 1]; k++) {
        int x = ve[k];
        if (x == ce || shares_face(m, x, ce)) continue;
        if (border && m->ef[2 * x + 1] >= 0) continue;
        cand = x;
        n++;
    }
    return n == 1 ? cand : -1;
}

int mesh_edge_loop(Mesh *m, int e)
{
    if (e < 0 || e >= m->ne) return 0;
    int *off, *ve, n = 1;
    unsigned char *seen = xrealloc(NULL, m->ne + 1);
    vert_edges(m, &off, &ve);
    memset(seen, 0, m->ne + 1);
    seen[e] = 1;
    for (int side = 0; side < 2; side++) {
        int ce = e, v = m->ev[2 * e + side];
        for (;;) {
            int nx = loop_next(m, off, ve, ce, v);
            if (nx < 0 || seen[nx]) break;
            seen[nx] = 1;
            n++;
            v = m->ev[2 * nx] == v ? m->ev[2 * nx + 1] : m->ev[2 * nx];
            ce = nx;
        }
    }
    for (int i = 0; i < m->ne; i++) if (seen[i]) m->es[i] = 1;
    free(seen);
    free(off);
    free(ve);
    return n;
}

void mesh_grow(Mesh *m, int comp, int shrink)
{
    unsigned char *vm = xrealloc(NULL, m->nv + 1);
    memset(vm, 0, m->nv + 1);
    if (!shrink) {
        mesh_vmask(m, comp, vm);
        if (comp == COMP_VERT) {
            for (int e = 0; e < m->ne; e++) {
                int a = m->ev[2 * e], b = m->ev[2 * e + 1];
                if (vm[a] || vm[b]) m->vs[a] = m->vs[b] = 1;
            }
        } else if (comp == COMP_EDGE) {
            for (int e = 0; e < m->ne; e++) if (vm[m->ev[2 * e]] || vm[m->ev[2 * e + 1]]) m->es[e] = 1;
        } else {
            for (int f = 0; f < m->nf; f++)
                for (int k = m->fo[f]; k < m->fo[f + 1]; k++) if (vm[m->fi[k]]) m->fs[f] = 1;
        }
    } else {
        /* vm = vertices touched by something unselected; selected parts touching them go */
        if (comp == COMP_VERT) {
            for (int e = 0; e < m->ne; e++) {
                int a = m->ev[2 * e], b = m->ev[2 * e + 1];
                if (!m->vs[a]) vm[b] = 1;
                if (!m->vs[b]) vm[a] = 1;
            }
            for (int v = 0; v < m->nv; v++) if (vm[v]) m->vs[v] = 0;
        } else if (comp == COMP_EDGE) {
            for (int e = 0; e < m->ne; e++) if (!m->es[e]) vm[m->ev[2 * e]] = vm[m->ev[2 * e + 1]] = 1;
            for (int e = 0; e < m->ne; e++) if (vm[m->ev[2 * e]] || vm[m->ev[2 * e + 1]]) m->es[e] = 0;
        } else {
            for (int f = 0; f < m->nf; f++)
                if (!m->fs[f]) for (int k = m->fo[f]; k < m->fo[f + 1]; k++) vm[m->fi[k]] = 1;
            for (int f = 0; f < m->nf; f++)
                for (int k = m->fo[f]; k < m->fo[f + 1]; k++) if (vm[m->fi[k]]) m->fs[f] = 0;
        }
    }
    free(vm);
}

/* Like Maya: going down (faces to edges/verts, edges to verts) keeps the parts of the selection;
   going up selects everything touching it (edges to their faces, verts to touching edges/faces). */
void mesh_convert(Mesh *m, int from, int to)
{
    if (from == to) return;
    unsigned char *vm = xrealloc(NULL, m->nv + 1);
    mesh_vmask(m, from, vm);
    if (to == COMP_VERT) {
        memcpy(m->vs, vm, m->nv);
    } else if (to == COMP_EDGE) {
        for (int e = 0; e < m->ne; e++) {
            int a = vm[m->ev[2 * e]], b = vm[m->ev[2 * e + 1]];
            if (from == COMP_FACE) {
                int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1];
                m->es[e] = (f0 >= 0 && m->fs[f0]) || (f1 >= 0 && m->fs[f1]);
            } else {
                m->es[e] = a || b;
            }
        }
    } else {
        memset(m->fs, 0, m->nf);
        if (from == COMP_EDGE) {
            for (int e = 0; e < m->ne; e++)
                if (m->es[e])
                    for (int i = 0; i < 2; i++) if (m->ef[2 * e + i] >= 0) m->fs[m->ef[2 * e + i]] = 1;
        } else {
            for (int f = 0; f < m->nf; f++)
                for (int k = m->fo[f]; k < m->fo[f + 1]; k++) if (vm[m->fi[k]]) m->fs[f] = 1;
        }
    }
    if (from == COMP_VERT) memset(m->vs, 0, m->nv);
    if (from == COMP_EDGE) memset(m->es, 0, m->ne);
    if (from == COMP_FACE) memset(m->fs, 0, m->nf);
    free(vm);
}

int mesh_fill_holes(Mesh *m, const unsigned char *touch)
{
    /* each border edge a->b (as its one face winds it) becomes b->a in the cap */
    int *from = xrealloc(NULL, (m->ne + 1) * sizeof(int)), *to = xrealloc(NULL, (m->ne + 1) * sizeof(int));
    int nh = 0;
    for (int e = 0; e < m->ne; e++) {
        int f = m->ef[2 * e];
        if (f < 0 || m->ef[2 * e + 1] >= 0) continue;
        int a = m->ev[2 * e], b = m->ev[2 * e + 1], o = m->fo[f], n = m->fo[f + 1] - o;
        for (int k = 0; k < n; k++)
            if (m->fi[o + k] == b && m->fi[o + (k + 1) % n] == a) { int t = a; a = b; b = t; break; }
        from[nh] = b;
        to[nh] = a;
        nh++;
    }
    /* trace the loops first; cap[] holds them back to back, cstart[i] where loop i begins */
    unsigned char *used = xrealloc(NULL, nh + 1);
    int *cap = xrealloc(NULL, (nh + 1) * sizeof(int)), *cstart = xrealloc(NULL, (nh + 2) * sizeof(int));
    int made = 0;
    memset(used, 0, nh + 1);
    cstart[0] = 0;
    for (int s = 0; s < nh; s++) {
        if (used[s]) continue;
        int n = 0, h = s, hit = !touch, closed = 0, *loop = cap + cstart[made];
        while (h >= 0 && !used[h]) {
            used[h] = 1;
            loop[n++] = from[h];
            hit |= touch && touch[from[h]];
            if (to[h] == from[s]) { closed = 1; break; }
            int nx = -1;
            for (int k = 0; k < nh && nx < 0; k++)
                if (!used[k] && from[k] == to[h]) nx = k;
            h = nx;
        }
        if (closed && hit && n >= 3) { cstart[made + 1] = cstart[made] + n; made++; }   /* else reuse the space */
    }
    if (made) {
        MB b = mb_from_verts(m);
        for (int f = 0; f < m->nf; f++) mb_face(&b, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], 0);
        for (int i = 0; i < made; i++) mb_face(&b, cap + cstart[i], cstart[i + 1] - cstart[i], 1);
        mesh_replace(m, &b);
    }
    free(from); free(to); free(used); free(cap); free(cstart);
    return made;
}

void mesh_flip(Mesh *m)
{
    for (int f = 0; f < m->nf; f++)
        for (int a = m->fo[f], b = m->fo[f + 1] - 1; a < b; a++, b--) {
            int t = m->fi[a];
            m->fi[a] = m->fi[b];
            m->fi[b] = t;
        }
}

/* ---------------------------------------------------------------- editable face lists

   Ops that cut faces apart or join them work on plain per face vertex lists, then rebuild. */

typedef struct { int n, cap; int *v; } IV;

static void iv_push(IV *a, int x)
{
    if (a->n == a->cap) { a->cap = a->cap ? a->cap * 2 : 8; a->v = xrealloc(a->v, a->cap * sizeof(int)); }
    a->v[a->n++] = x;
}

static void iv_insert(IV *a, int at, int x)
{
    iv_push(a, 0);
    memmove(a->v + at + 1, a->v + at, (a->n - 1 - at) * sizeof(int));
    a->v[at] = x;
}

typedef struct { int nf, cf; IV *f; unsigned char *sel; } PL;

static void pl_add(PL *p, const int *v, int n, int sel)
{
    if (p->nf == p->cf) {
        p->cf = p->cf ? p->cf * 2 : 64;
        p->f = xrealloc(p->f, p->cf * sizeof(IV));
        p->sel = xrealloc(p->sel, p->cf);
    }
    IV *f = &p->f[p->nf];
    memset(f, 0, sizeof *f);
    for (int k = 0; k < n; k++) iv_push(f, v[k]);
    p->sel[p->nf++] = (unsigned char)sel;
}

static void pl_from(PL *p, const Mesh *m)
{
    memset(p, 0, sizeof *p);
    for (int f = 0; f < m->nf; f++) pl_add(p, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], m->fs[f]);
}

static void pl_free(PL *p)
{
    for (int f = 0; f < p->nf; f++) free(p->f[f].v);
    free(p->f);
    free(p->sel);
    memset(p, 0, sizeof *p);
}

static void pl_to_mb(const PL *p, MB *b)
{
    for (int f = 0; f < p->nf; f++)
        if (p->f[f].n >= 3) mb_face(b, p->f[f].v, p->f[f].n, p->sel[f]);
}

/* put vertex x between a and b in every face where they are neighbors */
static void pl_split_edge(PL *p, int a, int b, int x)
{
    for (int f = 0; f < p->nf; f++) {
        IV *v = &p->f[f];
        for (int k = 0; k < v->n; k++) {
            int u = v->v[k], w = v->v[(k + 1) % v->n];
            if ((u == a && w == b) || (u == b && w == a)) { iv_insert(v, k + 1, x); break; }
        }
    }
}

/* split a face holding u and w (not neighbors) along u-w; returns 0 if no face has both */
static int pl_cut(PL *p, int u, int w)
{
    for (int f = 0; f < p->nf; f++) {
        IV *v = &p->f[f];
        int n = v->n, iu = -1, iw = -1;
        for (int k = 0; k < n; k++) { if (v->v[k] == u) iu = k; if (v->v[k] == w) iw = k; }
        if (iu < 0 || iw < 0 || (iu + 1) % n == iw || (iw + 1) % n == iu) continue;
        IV a = { 0, 0, NULL }, b = { 0, 0, NULL };
        for (int k = iu;; k = (k + 1) % n) { iv_push(&a, v->v[k]); if (k == iw) break; }
        for (int k = iw;; k = (k + 1) % n) { iv_push(&b, v->v[k]); if (k == iu) break; }
        int sel = p->sel[f];
        free(v->v);
        p->f[f] = a;
        pl_add(p, b.v, b.n, sel);
        free(b.v);
        return 1;
    }
    return 0;
}

/* ---------------------------------------------------------------- modeling ops */

int mesh_edge_find(const Mesh *m, int a, int b)
{
    if (a > b) { int t = a; a = b; b = t; }
    for (int e = 0; e < m->ne; e++)
        if (m->ev[2 * e] == a && m->ev[2 * e + 1] == b) return e;
    return -1;
}

int mesh_reverse(Mesh *m, const unsigned char *fmask)
{
    int n = 0;
    for (int f = 0; f < m->nf; f++) {
        if (fmask && !fmask[f]) continue;
        for (int a = m->fo[f], b = m->fo[f + 1] - 1; a < b; a++, b--) {
            int t = m->fi[a];
            m->fi[a] = m->fi[b];
            m->fi[b] = t;
        }
        n++;
    }
    return n;
}

/* is q inside triangle a b c (all on the plane with normal nn)? */
static int in_tri(V3 q, V3 a, V3 b, V3 c, V3 nn)
{
    return v3dot(v3cross(v3sub(b, a), v3sub(q, a)), nn) >= 0 && v3dot(v3cross(v3sub(c, b), v3sub(q, b)), nn) >= 0 &&
           v3dot(v3cross(v3sub(a, c), v3sub(q, c)), nn) >= 0;
}

/* ear clipping, so concave polygons come out right; writes n-2 triangles */
static void ear_clip(const V3 *p, const int *idx, int n, int *out)
{
    int *r = xrealloc(NULL, n * sizeof(int)), rn = n, o = 0;
    memcpy(r, idx, n * sizeof(int));
    V3 nn = newell(p, idx, n);
    while (rn > 3) {
        int best = -1;
        for (int i = 0; i < rn && best < 0; i++) {
            int a = r[(i + rn - 1) % rn], b = r[i], c = r[(i + 1) % rn];
            if (v3dot(v3cross(v3sub(p[b], p[a]), v3sub(p[c], p[b])), nn) <= 0) continue;   /* reflex */
            int ok = 1;
            for (int j = 0; j < rn && ok; j++) {
                int q = r[j];
                if (q == a || q == b || q == c) continue;
                if (in_tri(p[q], p[a], p[b], p[c], nn)) ok = 0;
            }
            if (ok) best = i;
        }
        if (best < 0) best = 0;   /* degenerate: fall back to a fan step */
        out[o++] = r[(best + rn - 1) % rn];
        out[o++] = r[best];
        out[o++] = r[(best + 1) % rn];
        memmove(r + best, r + best + 1, (rn - best - 1) * sizeof(int));
        rn--;
    }
    out[o++] = r[0]; out[o++] = r[1]; out[o++] = r[2];
    free(r);
}

int mesh_triangulate(Mesh *m, const unsigned char *fmask)
{
    int n = 0;
    for (int f = 0; f < m->nf; f++) n += (!fmask || fmask[f]) && m->fo[f + 1] - m->fo[f] > 3;
    if (!n) return 0;
    MB b = mb_from_verts(m);
    int *tri = xrealloc(NULL, 3 * (m->fo[m->nf] + 1) * sizeof(int));
    for (int f = 0; f < m->nf; f++) {
        int s = m->fo[f], k = m->fo[f + 1] - s;
        if ((fmask && !fmask[f]) || k <= 3) { mb_face(&b, m->fi + s, k, m->fs[f]); continue; }
        ear_clip(m->p, m->fi + s, k, tri);
        for (int t = 0; t < k - 2; t++) mb_face(&b, tri + 3 * t, 3, m->fs[f]);
    }
    free(tri);
    mesh_replace(m, &b);
    return n;
}

typedef struct { float score; int e; } QCand;

static int qcand_cmp(const void *a, const void *b)
{
    float d = ((const QCand *)b)->score - ((const QCand *)a)->score;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

/* the quad left when the edge a-b between triangles f0 (a->b) and f1 is removed */
static int tri_pair_quad(const Mesh *m, int e, int q[4])
{
    int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1], a = m->ev[2 * e], b = m->ev[2 * e + 1];
    if (f0 < 0 || f1 < 0) return 0;
    const int *t0 = m->fi + m->fo[f0], *t1 = m->fi + m->fo[f1];
    int i = 0;
    while (i < 3 && t0[i] != a) i++;
    if (i == 3) return 0;
    if (t0[(i + 1) % 3] != b) { int t = a; a = b; b = t; }   /* now f0 runs a->b */
    int x0 = -1, x1 = -1;
    for (int k = 0; k < 3; k++) { if (t0[k] != a && t0[k] != b) x0 = t0[k]; if (t1[k] != a && t1[k] != b) x1 = t1[k]; }
    if (x0 < 0 || x1 < 0 || x0 == x1) return 0;
    q[0] = a; q[1] = x1; q[2] = b; q[3] = x0;
    return 1;
}

int mesh_quadrangulate(Mesh *m, const unsigned char *fmask, float angle)
{
    QCand *c = xrealloc(NULL, (m->ne + 1) * sizeof(QCand));
    int nc = 0;
    float ct = cosf(angle * DEG2RAD);
    for (int e = 0; e < m->ne; e++) {
        int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1], q[4];
        if (f0 < 0 || f1 < 0 || m->fo[f0 + 1] - m->fo[f0] != 3 || m->fo[f1 + 1] - m->fo[f1] != 3) continue;
        if (fmask && (!fmask[f0] || !fmask[f1])) continue;
        float d = v3dot(mesh_fnormal(m, f0), mesh_fnormal(m, f1));
        if (d < ct || !tri_pair_quad(m, e, q)) continue;
        V3 nn = newell(m->p, q, 4);
        int convex = 1;
        for (int k = 0; k < 4 && convex; k++) {
            V3 p0 = m->p[q[(k + 3) % 4]], p1 = m->p[q[k]], p2 = m->p[q[(k + 1) % 4]];
            convex = v3dot(v3cross(v3sub(p1, p0), v3sub(p2, p1)), nn) > 0;
        }
        if (!convex) continue;
        /* prefer flat pairs whose quad is closest to a square (diagonals of equal length) */
        float d1 = v3len(v3sub(m->p[q[0]], m->p[q[2]])), d2 = v3len(v3sub(m->p[q[1]], m->p[q[3]]));
        c[nc].score = d + 0.5f * fminf(d1, d2) / fmaxf(fmaxf(d1, d2), 1e-12f);
        c[nc++].e = e;
    }
    qsort(c, nc, sizeof *c, qcand_cmp);
    unsigned char *used = xcalloc(m->nf + 1, 1);
    int *quad = xrealloc(NULL, (m->nf + 1) * 4 * sizeof(int)), *qof = xrealloc(NULL, (m->nf + 1) * sizeof(int)), made = 0;
    for (int f = 0; f < m->nf; f++) qof[f] = -1;
    for (int i = 0; i < nc; i++) {
        int e = c[i].e, f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1];
        if (used[f0] || used[f1]) continue;
        used[f0] = used[f1] = 1;
        tri_pair_quad(m, e, quad + 4 * made);
        qof[f0 < f1 ? f0 : f1] = made;
        qof[f0 < f1 ? f1 : f0] = -2;   /* emitted with its partner */
        made++;
    }
    if (made) {
        MB b = mb_from_verts(m);
        for (int f = 0; f < m->nf; f++) {
            if (qof[f] == -2) continue;
            if (qof[f] >= 0) mb_face(&b, quad + 4 * qof[f], 4, 1);
            else mb_face(&b, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], m->fs[f]);
        }
        mesh_replace(m, &b);
    }
    free(c); free(used); free(quad); free(qof);
    return made;
}

int mesh_shells(const Mesh *m, int *fshell)
{
    int *par = xrealloc(NULL, (m->nv + 1) * sizeof(int)), *id = xrealloc(NULL, (m->nv + 1) * sizeof(int)), n = 0;
    for (int v = 0; v < m->nv; v++) { par[v] = v; id[v] = -1; }
    for (int f = 0; f < m->nf; f++)
        for (int c = m->fo[f] + 1; c < m->fo[f + 1]; c++)
            par[find_root(par, m->fi[c])] = find_root(par, m->fi[m->fo[f]]);
    for (int f = 0; f < m->nf; f++) {
        int r = find_root(par, m->fi[m->fo[f]]);
        if (id[r] < 0) id[r] = n++;
        fshell[f] = id[r];
    }
    free(par); free(id);
    return n;
}

Mesh mesh_subset(const Mesh *m, const unsigned char *fmask)
{
    MB b = mb_from_verts(m);
    for (int f = 0; f < m->nf; f++)
        if (fmask[f]) mb_face(&b, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], m->fs[f]);
    for (int e = 0; e < m->ne; e++)
        if (m->eh[e]) mb_hard(&b, m->ev[2 * e], m->ev[2 * e + 1], m->eh[e]);
    return mb_finish(&b);
}

void mesh_append(Mesh *dst, const Mesh *src, const M4 *xf)
{
    const float *x = xf->m;
    int flip = x[0] * (x[5] * x[10] - x[6] * x[9]) - x[4] * (x[1] * x[10] - x[2] * x[9]) + x[8] * (x[1] * x[6] - x[2] * x[5]) < 0;
    MB b = mb_from_verts(dst);
    for (int f = 0; f < dst->nf; f++) mb_face(&b, dst->fi + dst->fo[f], dst->fo[f + 1] - dst->fo[f], dst->fs[f]);
    int base = b.m.nv, *tmp = xrealloc(NULL, (src->fo[src->nf] + 1) * sizeof(int));
    for (int v = 0; v < src->nv; v++) mb_vert(&b, m4_pt(*xf, src->p[v]), src->c[v]);
    for (int f = 0; f < src->nf; f++) {
        int s = src->fo[f], n = src->fo[f + 1] - s;
        for (int k = 0; k < n; k++) tmp[k] = base + src->fi[s + (flip ? n - 1 - k : k)];
        mb_face(&b, tmp, n, src->fs[f]);
    }
    for (int e = 0; e < src->ne; e++)
        if (src->eh[e]) mb_hard(&b, base + src->ev[2 * e], base + src->ev[2 * e + 1], src->eh[e]);
    free(tmp);
    mesh_replace(dst, &b);
}

/* faces renumbered through to[] (merged vertices point at the one they join); faces lose repeated
   corners, and faces left with fewer than 3 go */
static void mesh_remap(Mesh *m, const int *to)
{
    MB b = mb_from_verts(m);
    int *tmp = xrealloc(NULL, (m->fo[m->nf] + 1) * sizeof(int));
    for (int f = 0; f < m->nf; f++) {
        int s = m->fo[f], n = m->fo[f + 1] - s, k2 = 0;
        for (int k = 0; k < n; k++) {
            int v = to[m->fi[s + k]];
            if (!k2 || tmp[k2 - 1] != v) tmp[k2++] = v;
        }
        while (k2 > 1 && tmp[k2 - 1] == tmp[0]) k2--;
        if (k2 >= 3) mb_face(&b, tmp, k2, m->fs[f]);
    }
    for (int e = 0; e < m->ne; e++)
        if (m->eh[e]) mb_hard(&b, to[m->ev[2 * e]], to[m->ev[2 * e + 1]], m->eh[e]);
    free(tmp);
    mesh_replace(m, &b);
}

static int cmp_x(const void *a, const void *b, const V3 *p)
{
    float d = p[*(const int *)a].x - p[*(const int *)b].x;
    return d > 0 ? 1 : d < 0 ? -1 : 0;
}

static const V3 *sortP;
static int cmp_xp(const void *a, const void *b) { return cmp_x(a, b, sortP); }

int mesh_merge_dist(Mesh *m, const unsigned char *vmask, float dist)
{
    int *idx = xrealloc(NULL, (m->nv + 1) * sizeof(int)), n = 0;
    int *par = xrealloc(NULL, (m->nv + 1) * sizeof(int));
    for (int v = 0; v < m->nv; v++) { par[v] = v; if (!vmask || vmask[v]) idx[n++] = v; }
    sortP = m->p;
    qsort(idx, n, sizeof(int), cmp_xp);
    for (int i = 0; i < n; i++)
        for (int j = i + 1; j < n && m->p[idx[j]].x - m->p[idx[i]].x <= dist; j++)
            if (v3len(v3sub(m->p[idx[i]], m->p[idx[j]])) <= dist) {
                int a = find_root(par, idx[i]), b = find_root(par, idx[j]);
                if (a != b) par[a > b ? a : b] = a < b ? a : b;   /* the lowest number survives */
            }
    int removed = 0;
    V3 *ps = xcalloc(m->nv + 1, sizeof(V3)), *cs = xcalloc(m->nv + 1, sizeof(V3));
    int *cnt = xcalloc(m->nv + 1, sizeof(int));
    for (int v = 0; v < m->nv; v++) {
        int r = find_root(par, v);
        par[v] = r;
        removed += r != v;
        ps[r] = v3add(ps[r], m->p[v]);
        cs[r] = v3add(cs[r], m->c[v]);
        cnt[r]++;
    }
    if (removed) {
        for (int v = 0; v < m->nv; v++)
            if (cnt[v] > 1) { m->p[v] = v3mul(ps[v], 1.0f / cnt[v]); m->c[v] = v3mul(cs[v], 1.0f / cnt[v]); }
        mesh_remap(m, par);
    }
    free(idx); free(par); free(ps); free(cs); free(cnt);
    return removed;
}

int mesh_weld(Mesh *m, int src, int dst)
{
    if (src == dst || src < 0 || dst < 0 || src >= m->nv || dst >= m->nv) return 0;
    int *to = xrealloc(NULL, (m->nv + 1) * sizeof(int));
    for (int v = 0; v < m->nv; v++) to[v] = v;
    to[src] = dst;
    mesh_remap(m, to);
    free(to);
    return 1;
}

/* Join the faces on each side of the masked edges into one face (where they make a single loop),
   then drop candidate vertices (rm) left between just two edges: interior ones always, border
   corners too when rmBorder. Returns how many faces were joined away. */
static int dissolve(Mesh *m, const unsigned char *emask, const unsigned char *rm, int rmBorder)
{
    int *par = xrealloc(NULL, (m->nf + 1) * sizeof(int)), joined = 0;
    for (int f = 0; f < m->nf; f++) par[f] = f;
    for (int e = 0; e < m->ne; e++) {
        int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1];
        if (emask[e] && f0 >= 0 && f1 >= 0 && f0 != f1) par[find_root(par, f0)] = find_root(par, f1);
    }
    /* faces of each group as a list: first[root] -> nextg[] */
    int *first = xrealloc(NULL, (m->nf + 1) * sizeof(int)), *nextg = xrealloc(NULL, (m->nf + 1) * sizeof(int));
    int *gsize = xcalloc(m->nf + 1, sizeof(int));
    for (int f = 0; f < m->nf; f++) first[f] = -1;
    for (int f = m->nf - 1; f >= 0; f--) {
        par[f] = find_root(par, f);
        nextg[f] = first[par[f]];
        first[par[f]] = f;
        gsize[par[f]]++;
    }
    HMap eh;
    edge_map(&eh, m);
    PL out;
    memset(&out, 0, sizeof out);
    unsigned char *done = xcalloc(m->nf + 1, 1);
    int *hu = xrealloc(NULL, (m->fo[m->nf] + 1) * sizeof(int)), *hw = xrealloc(NULL, (m->fo[m->nf] + 1) * sizeof(int));
    int *nxt = xrealloc(NULL, (m->nv + 1) * sizeof(int)), *outdeg = xcalloc(m->nv + 1, sizeof(int));
    for (int v = 0; v < m->nv; v++) nxt[v] = -1;
    for (int f = 0; f < m->nf; f++) {
        if (done[f]) continue;
        int r = par[f], size = gsize[r], nh = 0, sel = 0;
        if (size == 1) { done[f] = 1; pl_add(&out, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], m->fs[f]); continue; }
        /* the group's outline: half edges whose other face is outside the group */
        for (int g = first[r]; g >= 0; g = nextg[g]) {
            done[g] = 1;
            sel |= m->fs[g];
            int s = m->fo[g], n = m->fo[g + 1] - s;
            for (int k = 0; k < n; k++) {
                int u = m->fi[s + k], w = m->fi[s + (k + 1) % n], e = edge_at(&eh, u, w);
                int o = e < 0 ? -1 : m->ef[2 * e] == g ? m->ef[2 * e + 1] : m->ef[2 * e];
                if (o >= 0 && par[o] == r) continue;
                hu[nh] = u; hw[nh++] = w;
            }
        }
        int ok = nh >= 3;
        for (int i = 0; i < nh && ok; i++) { if (outdeg[hu[i]]++) ok = 0; nxt[hu[i]] = hw[i]; }
        IV loop = { 0, 0, NULL };
        if (ok) {
            int v = hu[0];
            do { iv_push(&loop, v); v = nxt[v]; } while (v >= 0 && v != hu[0] && loop.n <= nh);
            ok = v == hu[0] && loop.n == nh;
        }
        for (int i = 0; i < nh; i++) { outdeg[hu[i]] = 0; nxt[hu[i]] = -1; }
        if (ok) {
            pl_add(&out, loop.v, loop.n, sel);
            joined += size - 1;
        } else {
            /* pinched or holed: leave these faces as they were */
            for (int g = first[r]; g >= 0; g = nextg[g])
                pl_add(&out, m->fi + m->fo[g], m->fo[g + 1] - m->fo[g], m->fs[g]);
        }
        free(loop.v);
    }
    hm_free(&eh);
    /* leftover vertices between two edges */
    int removed = 0;
    if (rm) {
        HMap ec;
        int nc = 0;
        for (int f = 0; f < out.nf; f++) nc += out.f[f].n;
        hm_init(&ec, nc + 8);
        int *deg = xcalloc(m->nv + 1, sizeof(int)), *border = xcalloc(m->nv + 1, sizeof(int));
        for (int f = 0; f < out.nf; f++)
            for (int k = 0; k < out.f[f].n; k++) {
                int u = out.f[f].v[k], w = out.f[f].v[(k + 1) % out.f[f].n], *c = hm_get(&ec, u, w);
                if (*c < 0) { *c = 1; deg[u]++; deg[w]++; } else (*c)++;
            }
        for (int f = 0; f < out.nf; f++)
            for (int k = 0; k < out.f[f].n; k++) {
                int u = out.f[f].v[k], w = out.f[f].v[(k + 1) % out.f[f].n];
                if (*hm_get(&ec, u, w) == 1) border[u] = border[w] = 1;
            }
        hm_free(&ec);
        for (int f = 0; f < out.nf; f++) {
            IV *v = &out.f[f];
            for (int k = 0; k < v->n && v->n > 3; k++) {
                int x = v->v[k];
                if (!rm[x] || deg[x] != 2 || (border[x] && !rmBorder)) continue;
                memmove(v->v + k, v->v + k + 1, (v->n - k - 1) * sizeof(int));
                v->n--;
                k--;
                removed++;
            }
        }
        free(deg); free(border);
    }
    if (joined || removed) {
        MB b = mb_from_verts(m);
        pl_to_mb(&out, &b);
        mesh_replace(m, &b);
    }
    pl_free(&out);
    free(par); free(done); free(hu); free(hw); free(nxt); free(outdeg); free(first); free(nextg); free(gsize);
    return joined + removed;
}

int mesh_delete_edges(Mesh *m, const unsigned char *emask, int clean)
{
    unsigned char *rm = NULL;
    if (clean) {
        rm = xcalloc(m->nv + 1, 1);
        for (int e = 0; e < m->ne; e++) if (emask[e]) rm[m->ev[2 * e]] = rm[m->ev[2 * e + 1]] = 1;
    }
    int n = dissolve(m, emask, rm, 0);
    free(rm);
    return n;
}

int mesh_delete_verts(Mesh *m, const unsigned char *vmask)
{
    unsigned char *em = xcalloc(m->ne + 1, 1);
    for (int e = 0; e < m->ne; e++)
        em[e] = (vmask[m->ev[2 * e]] || vmask[m->ev[2 * e + 1]]) && m->ef[2 * e + 1] >= 0;
    int n = dissolve(m, em, vmask, 1);
    free(em);
    return n;
}

int mesh_edge_ring(const Mesh *m, int e, int *ra, int *rb, int *closed)
{
    int n = 1;
    unsigned char *seen = xcalloc(m->nf + 1, 1);
    HMap h;
    edge_map(&h, m);
    ra[0] = m->ev[2 * e];
    rb[0] = m->ev[2 * e + 1];
    *closed = 0;
    for (int side = 0; side < 2 && !*closed; side++) {
        int a = ra[0], b = rb[0], f = m->ef[2 * e + side], ce = e;
        if (side == 1) { a = ra[0]; b = rb[0]; }
        while (f >= 0 && !seen[f] && m->fo[f + 1] - m->fo[f] == 4) {
            seen[f] = 1;
            const int *q = m->fi + m->fo[f];
            int i = 0;
            while (i < 4 && q[i] != a) i++;
            if (i == 4) break;
            int d, c;
            if (q[(i + 1) % 4] == b) { d = q[(i + 3) % 4]; c = q[(i + 2) % 4]; }
            else { d = q[(i + 1) % 4]; c = q[(i + 2) % 4]; }
            int ne_ = edge_at(&h, d, c);
            if (ne_ < 0) break;
            if (ne_ == e) { *closed = 1; break; }
            if (side == 0) { ra[n] = d; rb[n] = c; }
            else {
                memmove(ra + 1, ra, n * sizeof(int));
                memmove(rb + 1, rb, n * sizeof(int));
                ra[0] = d; rb[0] = c;
            }
            n++;
            a = d; b = c;
            f = m->ef[2 * ne_] == f ? m->ef[2 * ne_ + 1] : m->ef[2 * ne_];
            ce = ne_;
        }
        (void)ce;
    }
    hm_free(&h);
    free(seen);
    return n;
}

int mesh_cut(Mesh *m, const CutPt *pts, int n)
{
    if (n < 2) return 0;
    PL pl;
    pl_from(&pl, m);
    MB b = mb_from_verts(m);
    HMap eh;
    edge_map(&eh, m);
    /* points already put on each edge: (edge, t from its lower vertex, vertex) */
    int *se = xrealloc(NULL, n * sizeof(int)), *sv = xrealloc(NULL, n * sizeof(int)), ns = 0;
    float *st = xrealloc(NULL, n * sizeof(float));
    int *id = xrealloc(NULL, n * sizeof(int)), cuts = 0;
    for (int k = 0; k < n; k++) {
        int a = pts[k].a, c = pts[k].b;
        float t = pts[k].t;
        id[k] = -1;
        if (a < 0 || a >= m->nv) continue;
        if (c < 0) { id[k] = a; continue; }
        if (c >= m->nv) continue;
        if (a > c) { int x = a; a = c; c = x; t = 1 - t; }
        int e = edge_at(&eh, a, c);
        if (e < 0) continue;
        if (t <= 1e-4f) { id[k] = a; continue; }
        if (t >= 1 - 1e-4f) { id[k] = c; continue; }
        int lo = a, hi = c;
        float tlo = 0, thi = 1;
        for (int i = 0; i < ns && id[k] < 0; i++) {
            if (se[i] != e) continue;
            if (fabsf(st[i] - t) < 1e-4f) id[k] = sv[i];
            else if (st[i] < t && st[i] > tlo) { tlo = st[i]; lo = sv[i]; }
            else if (st[i] > t && st[i] < thi) { thi = st[i]; hi = sv[i]; }
        }
        if (id[k] >= 0) continue;
        int x = mb_vert(&b, v3lerp(m->p[a], m->p[c], t), v3lerp(m->c[a], m->c[c], t));
        pl_split_edge(&pl, lo, hi, x);
        if (m->eh[e]) { mb_hard(&b, lo, x, m->eh[e]); mb_hard(&b, x, hi, m->eh[e]); }
        se[ns] = e; st[ns] = t; sv[ns++] = x;
        id[k] = x;
    }
    int *cu = xrealloc(NULL, n * sizeof(int)), *cw = xrealloc(NULL, n * sizeof(int));
    for (int k = 0; k + 1 < n; k++) {
        int u = id[k], w = id[k + 1];
        if (u < 0 || w < 0 || u == w) continue;
        if (pl_cut(&pl, u, w)) { cu[cuts] = u; cw[cuts++] = w; }
    }
    if (cuts || ns) {
        pl_to_mb(&pl, &b);
        mesh_replace(m, &b);   /* every vertex is still used, so numbers stay put */
        HMap h;
        edge_map(&h, m);
        for (int i = 0; i < cuts; i++) {
            int e = edge_at(&h, cu[i], cw[i]);
            if (e >= 0) m->es[e] = 1;
        }
        hm_free(&h);
    } else {
        free(b.m.p); free(b.m.c); free(b.m.vs); free(b.m.fo); free(b.m.fs); free(b.m.fi); free(b.hp);
    }
    hm_free(&eh);
    pl_free(&pl);
    free(se); free(sv); free(st); free(id); free(cu); free(cw);
    return cuts;
}

int mesh_insert_loop(Mesh *m, int e, float t)
{
    if (e < 0 || e >= m->ne) return 0;
    int *ra = xrealloc(NULL, (m->ne + 1) * sizeof(int)), *rb = xrealloc(NULL, (m->ne + 1) * sizeof(int)), closed;
    int n = mesh_edge_ring(m, e, ra, rb, &closed);
    CutPt *p = xrealloc(NULL, (n + 1) * sizeof(CutPt));
    for (int i = 0; i < n; i++) { p[i].a = ra[i]; p[i].b = rb[i]; p[i].t = t; }
    if (closed) p[n++] = p[0];
    int made = mesh_cut(m, p, n);
    free(ra); free(rb); free(p);
    return made;
}

/* One segment bevel (chamfer). Each selected edge becomes a strip of width about 'width': at its
   ends the corners slide along the unselected edges (or inset between two selected ones), and a
   cap face closes the gap where three or more strips meet. */
int mesh_bevel(Mesh *m, const unsigned char *emask, float width)
{
    unsigned char *sel = xcalloc(m->ne + 1, 1);
    int *k = xcalloc(m->nv + 1, sizeof(int)), any = 0;
    for (int e = 0; e < m->ne; e++)
        if (emask[e] && m->ef[2 * e] >= 0 && m->ef[2 * e + 1] >= 0 && m->ef[2 * e] != m->ef[2 * e + 1]) {
            sel[e] = 1;
            k[m->ev[2 * e]]++;
            k[m->ev[2 * e + 1]]++;
            any = 1;
        }
    if (!any || width <= 0) { free(sel); free(k); return 0; }
    HMap eh, sl;
    edge_map(&eh, m);
    hm_init(&sl, 2 * m->ne + 8);
    MB b = mb_from_verts(m);
    IV owner = { 0, 0, NULL };   /* original vertex of each new vertex (index - m->nv) */
    int nc = m->fo[m->nf], *cv = xrealloc(NULL, (nc + 1) * sizeof(int));
    PL pl;
    memset(&pl, 0, sizeof pl);
#define NEWV(pos, col, own) (iv_push(&owner, own), mb_vert(&b, pos, col))
    for (int f = 0; f < m->nf; f++) {
        int s = m->fo[f], n = m->fo[f + 1] - s;
        IV nf = { 0, 0, NULL };
        for (int j = 0; j < n; j++) {
            int v = m->fi[s + j], pv = m->fi[s + (j + n - 1) % n], nx = m->fi[s + (j + 1) % n];
            cv[s + j] = -1;
            if (!k[v]) { iv_push(&nf, v); continue; }
            int ein = edge_at(&eh, pv, v), eout = edge_at(&eh, v, nx), ends[2] = { ein, eout }, slid[2];
            for (int z = 0; z < 2; z++) {
                int ee = ends[z];
                slid[z] = -1;
                if (ee < 0 || sel[ee]) continue;
                int *slot = hm_getd(&sl, v, ee);
                if (*slot < 0) {
                    int o = m->ev[2 * ee] == v ? m->ev[2 * ee + 1] : m->ev[2 * ee];
                    float L = v3len(v3sub(m->p[o], m->p[v])), d = fminf(width, 0.45f * L), u = L > 0 ? d / L : 0;
                    *slot = NEWV(v3lerp(m->p[v], m->p[o], u), v3lerp(m->c[v], m->c[o], u), v);
                    if (m->eh[ee]) mb_hard(&b, *slot, o, m->eh[ee]);
                }
                slid[z] = *slot;
            }
            int si = ein >= 0 && sel[ein], so = eout >= 0 && sel[eout];
            if (si && so) {
                V3 a = v3sub(m->p[pv], m->p[v]), c = v3sub(m->p[nx], m->p[v]);
                float la = v3len(a), lc = v3len(c);
                V3 p = m->p[v];
                if (la > 0) p = v3add(p, v3mul(a, fminf(width, 0.45f * la) / la));
                if (lc > 0) p = v3add(p, v3mul(c, fminf(width, 0.45f * lc) / lc));
                cv[s + j] = NEWV(p, m->c[v], v);
                iv_push(&nf, cv[s + j]);
            } else if (si) {
                cv[s + j] = slid[1];
                iv_push(&nf, slid[1]);
            } else if (so) {
                cv[s + j] = slid[0];
                iv_push(&nf, slid[0]);
            } else {
                iv_push(&nf, slid[0]);
                iv_push(&nf, slid[1]);
            }
        }
        pl_add(&pl, nf.v, nf.n, 0);
        free(nf.v);
    }
#undef NEWV
    /* strips: f0 runs u->w along the edge, f1 runs w->u */
    for (int e = 0; e < m->ne; e++) {
        if (!sel[e]) continue;
        int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1], c0u = -1, c0w = -1, c1u = -1, c1w = -1, u = -1, w = -1;
        int s = m->fo[f0], n = m->fo[f0 + 1] - s;
        for (int j = 0; j < n; j++) {
            int a = m->fi[s + j], c = m->fi[s + (j + 1) % n];
            if ((a == m->ev[2 * e] && c == m->ev[2 * e + 1]) || (c == m->ev[2 * e] && a == m->ev[2 * e + 1])) {
                u = a; w = c; c0u = cv[s + j]; c0w = cv[s + (j + 1) % n];
            }
        }
        s = m->fo[f1]; n = m->fo[f1 + 1] - s;
        for (int j = 0; j < n; j++) {
            if (m->fi[s + j] == u) c1u = cv[s + j];
            if (m->fi[s + j] == w) c1w = cv[s + j];
        }
        int q[4] = { c0w, c0u, c1u, c1w }, qn = 0, out[4];
        for (int i = 0; i < 4; i++) if (q[i] >= 0 && (!qn || out[qn - 1] != q[i])) out[qn++] = q[i];
        if (qn > 1 && out[qn - 1] == out[0]) qn--;
        if (qn >= 3) pl_add(&pl, out, qn, 1);
        if (m->eh[e]) for (int i = 0; i < qn; i++) mb_hard(&b, out[i], out[(i + 1) % qn], m->eh[e]);
    }
    /* caps: the open outline among each old vertex's new vertices */
    {
        HMap dh;
        int tot = 0;
        for (int f = 0; f < pl.nf; f++) tot += pl.f[f].n;
        hm_init(&dh, tot + 8);
        for (int f = 0; f < pl.nf; f++)
            for (int j = 0; j < pl.f[f].n; j++) *hm_getd(&dh, pl.f[f].v[j], pl.f[f].v[(j + 1) % pl.f[f].n]) = 1;
        int *nx = xrealloc(NULL, (b.m.nv + 1) * sizeof(int));
        for (int v = 0; v < b.m.nv; v++) nx[v] = -1;
        int pf = pl.nf;
        for (int f = 0; f < pf; f++)
            for (int j = 0; j < pl.f[f].n; j++) {
                int a = pl.f[f].v[j], c = pl.f[f].v[(j + 1) % pl.f[f].n];
                if (a < m->nv || c < m->nv || owner.v[a - m->nv] != owner.v[c - m->nv]) continue;
                if (*hm_getd(&dh, c, a) == 1) continue;
                nx[c] = a;   /* the cap runs against the open edge */
            }
        for (int v = m->nv; v < b.m.nv; v++) {
            if (nx[v] < 0) continue;
            IV loop = { 0, 0, NULL };
            int x = v, ok = 0;
            while (x >= 0 && loop.n <= 64) {
                iv_push(&loop, x);
                int y = nx[x];
                nx[x] = -1;
                if (y == v) { ok = 1; break; }
                x = y;
            }
            if (ok && loop.n >= 3) pl_add(&pl, loop.v, loop.n, 1);
            free(loop.v);
        }
        free(nx);
        hm_free(&dh);
    }
    pl_to_mb(&pl, &b);
    pl_free(&pl);
    /* overrides on edges that kept both old ends carry over; mesh_replace adds them */
    int made = 0;
    for (int e = 0; e < m->ne; e++) made += sel[e];
    mesh_replace(m, &b);
    hm_free(&eh); hm_free(&sl);
    free(sel); free(k); free(cv); free(owner.v);
    return made;
}

/* selected border edges as runs of vertices; returns the number of runs (at most 2 kept) */
static int border_runs(const Mesh *m, const unsigned char *emask, IV run[2], int closed[2])
{
    int *deg = xcalloc(m->nv + 1, sizeof(int)), nr = 0;
    unsigned char *use = xcalloc(m->ne + 1, 1);
    for (int e = 0; e < m->ne; e++)
        if (emask[e] && m->ef[2 * e] >= 0 && m->ef[2 * e + 1] < 0) { use[e] = 1; deg[m->ev[2 * e]]++; deg[m->ev[2 * e + 1]]++; }
    for (int v = 0; v < m->nv; v++) if (deg[v] > 2) { free(deg); free(use); return -1; }
    for (int pass = 0; pass < 2; pass++)   /* open runs start at an end; then closed loops */
        for (int s = 0; s < m->ne; s++) {
            if (!use[s]) continue;
            int a = m->ev[2 * s], c = m->ev[2 * s + 1];
            if (pass == 0 && deg[a] != 1 && deg[c] != 1) continue;
            if (pass == 0 && deg[a] != 1) { int t = a; a = c; c = t; }
            if (nr == 2) { nr = 3; goto out; }   /* more than two runs */
            IV *r = &run[nr];
            memset(r, 0, sizeof *r);
            closed[nr] = pass == 1;
            iv_push(r, a);
            int cur = s, v = c;
            use[s] = 0;
            for (;;) {
                if (v == r->v[0]) break;
                iv_push(r, v);
                int nx = -1;
                for (int e = 0; e < m->ne && nx < 0; e++)
                    if (use[e] && (m->ev[2 * e] == v || m->ev[2 * e + 1] == v)) nx = e;
                if (nx < 0) break;
                use[nx] = 0;
                v = m->ev[2 * nx] == v ? m->ev[2 * nx + 1] : m->ev[2 * nx];
                cur = nx;
            }
            (void)cur;
            nr++;
        }
out:
    free(deg); free(use);
    return nr;
}

int mesh_bridge(Mesh *m, const unsigned char *emask, int divs)
{
    IV r[3] = { { 0, 0, NULL }, { 0, 0, NULL }, { 0, 0, NULL } };
    int cl[3] = { 0, 0, 0 }, nr = border_runs(m, emask, r, cl), res = 0;
    if (nr != 2 || cl[0] != cl[1]) { res = -1; goto done; }
    int n = r[0].n;
    if (r[1].n != n || n < 2) { res = -2; goto done; }
    int closed = cl[0], *A = r[0].v, *B = xrealloc(NULL, n * sizeof(int));
    /* run A along its face's winding, so new faces hold A's edges backwards */
    {
        int e = mesh_edge_find(m, A[0], A[1]), f = m->ef[2 * e], s = m->fo[f], fn = m->fo[f + 1] - s, fwd = 0;
        for (int j = 0; j < fn; j++) if (m->fi[s + j] == A[0] && m->fi[s + (j + 1) % fn] == A[1]) fwd = 1;
        if (!fwd) {
            for (int i = 0; i < n / 2; i++) { int t = A[i]; A[i] = A[n - 1 - i]; A[n - 1 - i] = t; }
            if (closed) {   /* keep A[0] first */
                int t = A[n - 1];
                memmove(A + 1, A, (n - 1) * sizeof(int));
                A[0] = t;
            }
        }
    }
    /* line B up with A: the start and direction that keep matching vertices closest */
    float best = 1e30f;
    for (int s = 0; s < (closed ? n : 1); s++)
        for (int dir = 0; dir < 2; dir++) {
            float d = 0;
            for (int i = 0; i < n; i++) {
                int j = closed ? (dir ? (s - i + n) % n : (s + i) % n) : (dir ? n - 1 - i : i);
                d += v3len(v3sub(m->p[A[i]], m->p[r[1].v[j]]));
            }
            if (d < best) {
                best = d;
                for (int i = 0; i < n; i++) B[i] = r[1].v[closed ? (dir ? (s - i + n) % n : (s + i) % n) : (dir ? n - 1 - i : i)];
            }
        }
    MB b = mb_from_verts(m);
    for (int f = 0; f < m->nf; f++) mb_face(&b, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f], 0);
    int rows = divs + 2, *R = xrealloc(NULL, rows * n * sizeof(int));
    for (int i = 0; i < n; i++) { R[i] = A[i]; R[(rows - 1) * n + i] = B[i]; }
    for (int rr = 1; rr < rows - 1; rr++) {
        float t = (float)rr / (rows - 1);
        for (int i = 0; i < n; i++)
            R[rr * n + i] = mb_vert(&b, v3lerp(m->p[A[i]], m->p[B[i]], t), v3lerp(m->c[A[i]], m->c[B[i]], t));
    }
    int segs = closed ? n : n - 1;
    for (int rr = 0; rr < rows - 1; rr++)
        for (int i = 0; i < segs; i++) {
            int i2 = (i + 1) % n, q[4] = { R[rr * n + i2], R[rr * n + i], R[(rr + 1) * n + i], R[(rr + 1) * n + i2] };
            mb_face(&b, q, 4, 1);
            res++;
        }
    mesh_replace(m, &b);
    free(R);
    free(B);
done:
    for (int i = 0; i < 3; i++) free(r[i].v);
    return res;
}

/* ---------------------------------------------------------------- primitives */

void prim_defaults(Prim *p, int type)
{
    memset(p, 0, sizeof *p);
    p->type = type;
    p->w = p->h = p->d = 1; p->sw = p->sh = p->sd = 1;
    p->cr = 1; p->ch = 2; p->csides = 20; p->chseg = 1; p->ccap = 0; p->ctop = p->cbot = 1;
    p->sr = 1; p->scols = 20; p->srows = 20;
    p->pw = p->pd = 1; p->psw = p->psd = 10;
}

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

void prim_clamp(Prim *p)
{
    p->w = clampf(p->w, 1e-4f, 1e6f); p->h = clampf(p->h, 1e-4f, 1e6f); p->d = clampf(p->d, 1e-4f, 1e6f);
    p->sw = clampi(p->sw, 1, 200); p->sh = clampi(p->sh, 1, 200); p->sd = clampi(p->sd, 1, 200);
    p->cr = clampf(p->cr, 1e-4f, 1e6f); p->ch = clampf(p->ch, 1e-4f, 1e6f);
    p->csides = clampi(p->csides, 3, 512); p->chseg = clampi(p->chseg, 1, 200); p->ccap = clampi(p->ccap, 0, 100);
    p->ctop = p->ctop != 0; p->cbot = p->cbot != 0;
    p->sr = clampf(p->sr, 1e-4f, 1e6f);
    p->scols = clampi(p->scols, 3, 512); p->srows = clampi(p->srows, 2, 512);
    p->pw = clampf(p->pw, 1e-4f, 1e6f); p->pd = clampf(p->pd, 1e-4f, 1e6f);
    p->psw = clampi(p->psw, 1, 500); p->psd = clampi(p->psd, 1, 500);
}

static const V3 WHITE = { 1, 1, 1 };

static Mesh prim_cube(const Prim *p)
{
    MB b;
    mb_init(&b);
    int n[3] = { p->sw, p->sh, p->sd };
    float sz[3] = { p->w, p->h, p->d };
    int nl = (n[0] + 1) * (n[1] + 1) * (n[2] + 1);
    int *lat = xrealloc(NULL, nl * sizeof(int));
    for (int i = 0; i < nl; i++) lat[i] = -1;
    for (int ax = 0; ax < 3; ax++) {
        int u = (ax + 1) % 3, v = (ax + 2) % 3;
        for (int side = 0; side < 2; side++) {
            V3 want = v3(0, 0, 0);
            (&want.x)[ax] = side ? 1.0f : -1.0f;
            for (int iu = 0; iu < n[u]; iu++)
                for (int iv = 0; iv < n[v]; iv++) {
                    static const int du[4] = { 0, 1, 1, 0 }, dv[4] = { 0, 0, 1, 1 };
                    int q[4];
                    for (int k = 0; k < 4; k++) {
                        int c[3];
                        c[ax] = side ? n[ax] : 0;
                        c[u] = iu + du[k];
                        c[v] = iv + dv[k];
                        int li = (c[2] * (n[1] + 1) + c[1]) * (n[0] + 1) + c[0];
                        if (lat[li] < 0) {
                            V3 pos;
                            for (int a = 0; a < 3; a++) (&pos.x)[a] = sz[a] * ((float)c[a] / n[a] - 0.5f);
                            lat[li] = mb_vert(&b, pos, WHITE);
                        }
                        q[k] = lat[li];
                    }
                    mb_face_dir(&b, q, 4, want);
                }
        }
    }
    free(lat);
    return mb_finish(&b);
}

/* cap for a cylinder ring: n-gon when rings == 0, else concentric quads + fan */
static void cyl_cap(MB *b, const int *ring, int sides, int rings, float r, float y, V3 want)
{
    int *tmp = xrealloc(NULL, (sides + 4) * sizeof(int));
    if (rings == 0) {
        memcpy(tmp, ring, sides * sizeof(int));
        mb_face_dir(b, tmp, sides, want);
        free(tmp);
        return;
    }
    int *outer = xrealloc(NULL, sides * sizeof(int));
    int *inner = xrealloc(NULL, sides * sizeof(int));
    memcpy(outer, ring, sides * sizeof(int));
    for (int k = 1; k < rings; k++) {
        float rr = r * (1.0f - (float)k / rings);
        for (int i = 0; i < sides; i++) {
            float a = 2 * PI_F * i / sides;
            inner[i] = mb_vert(b, v3(rr * cosf(a), y, -rr * sinf(a)), WHITE);
        }
        for (int i = 0; i < sides; i++) {
            int j = (i + 1) % sides;
            int q[4] = { outer[i], outer[j], inner[j], inner[i] };
            mb_face_dir(b, q, 4, want);
        }
        memcpy(outer, inner, sides * sizeof(int));
    }
    int c = mb_vert(b, v3(0, y, 0), WHITE);
    for (int i = 0; i < sides; i++) {
        int t[3] = { outer[i], outer[(i + 1) % sides], c };
        mb_face_dir(b, t, 3, want);
    }
    free(outer);
    free(inner);
    free(tmp);
}

static Mesh prim_cylinder(const Prim *p)
{
    MB b;
    mb_init(&b);
    int n = p->csides, hs = p->chseg;
    int *ring = xrealloc(NULL, (hs + 1) * n * sizeof(int));
    for (int j = 0; j <= hs; j++) {
        float y = p->ch * ((float)j / hs - 0.5f);
        for (int i = 0; i < n; i++) {
            float a = 2 * PI_F * i / n;
            ring[j * n + i] = mb_vert(&b, v3(p->cr * cosf(a), y, -p->cr * sinf(a)), WHITE);
        }
    }
    for (int j = 0; j < hs; j++)
        for (int i = 0; i < n; i++) {
            int i2 = (i + 1) % n;
            float a = 2 * PI_F * (i + 0.5f) / n;
            int q[4] = { ring[j * n + i], ring[j * n + i2], ring[(j + 1) * n + i2], ring[(j + 1) * n + i] };
            mb_face_dir(&b, q, 4, v3(cosf(a), 0, -sinf(a)));
        }
    if (p->cbot) cyl_cap(&b, ring, n, p->ccap, p->cr, -p->ch * 0.5f, v3(0, -1, 0));
    if (p->ctop) cyl_cap(&b, ring + hs * n, n, p->ccap, p->cr, p->ch * 0.5f, v3(0, 1, 0));
    free(ring);
    return mb_finish(&b);
}

static Mesh prim_sphere(const Prim *p)
{
    MB b;
    mb_init(&b);
    int R = p->srows, C = p->scols;
    float r = p->sr;
    int top = mb_vert(&b, v3(0, r, 0), WHITE);
    int *ring = xrealloc(NULL, (R + 1) * C * sizeof(int));
    for (int i = 1; i < R; i++) {
        float phi = PI_F * i / R, y = r * cosf(phi), rr = r * sinf(phi);
        for (int j = 0; j < C; j++) {
            float th = 2 * PI_F * j / C;
            ring[i * C + j] = mb_vert(&b, v3(rr * cosf(th), y, -rr * sinf(th)), WHITE);
        }
    }
    int bot = mb_vert(&b, v3(0, -r, 0), WHITE);
    for (int j = 0; j < C; j++) {
        int j2 = (j + 1) % C;
        int t[3] = { top, ring[C + j], ring[C + j2] };
        mb_face_dir(&b, t, 3, v3(0, 1, 0));
        for (int i = 1; i + 1 < R; i++) {
            int q[4] = { ring[i * C + j], ring[(i + 1) * C + j], ring[(i + 1) * C + j2], ring[i * C + j2] };
            V3 c = v3add(v3add(b.m.p[q[0]], b.m.p[q[1]]), v3add(b.m.p[q[2]], b.m.p[q[3]]));
            mb_face_dir(&b, q, 4, c);
        }
        int t2[3] = { ring[(R - 1) * C + j], bot, ring[(R - 1) * C + j2] };
        mb_face_dir(&b, t2, 3, v3(0, -1, 0));
    }
    free(ring);
    return mb_finish(&b);
}

static Mesh prim_plane(const Prim *p)
{
    MB b;
    mb_init(&b);
    int W = p->psw, D = p->psd;
    int *g = xrealloc(NULL, (W + 1) * (D + 1) * sizeof(int));
    for (int j = 0; j <= D; j++)
        for (int i = 0; i <= W; i++)
            g[j * (W + 1) + i] = mb_vert(&b, v3(p->pw * ((float)i / W - 0.5f), 0, p->pd * ((float)j / D - 0.5f)), WHITE);
    for (int j = 0; j < D; j++)
        for (int i = 0; i < W; i++) {
            int q[4] = { g[j * (W + 1) + i], g[j * (W + 1) + i + 1], g[(j + 1) * (W + 1) + i + 1], g[(j + 1) * (W + 1) + i] };
            mb_face_dir(&b, q, 4, v3(0, 1, 0));
        }
    free(g);
    return mb_finish(&b);
}

Mesh mesh_prim(const Prim *pp)
{
    Prim p = *pp;
    prim_clamp(&p);
    switch (p.type) {
    case PRIM_CYL: return prim_cylinder(&p);
    case PRIM_SPHERE: return prim_sphere(&p);
    case PRIM_PLANE: return prim_plane(&p);
    default: return prim_cube(&p);
    }
}

/* ---------------------------------------------------------------- OBJ io */

int obj_save(const char *path, int n, const Mesh *const *ms, const M4 *mats, const char *const *names, const float *angles)
{
    FILE *f = fopen(path, "w");
    if (!f) return 0;
    int colors = 0;
    for (int i = 0; i < n && !colors; i++)
        for (int v = 0; v < ms[i]->nv; v++) {
            V3 c = ms[i]->c[v];
            if (c.x != 1 || c.y != 1 || c.z != 1) { colors = 1; break; }
        }
    fprintf(f, "# tuff3d\n");
    int base = 1, nbase = 1;
    for (int i = 0; i < n; i++) {
        const Mesh *m = ms[i];
        M4 M = mats[i];
        V3 c0 = m4_col(M, 0), c1 = m4_col(M, 1), c2 = m4_col(M, 2);
        int flip = v3dot(v3cross(c0, c1), c2) < 0;
        fprintf(f, "o %s\n", names[i]);
        for (int v = 0; v < m->nv; v++) {
            V3 p = m4_pt(M, m->p[v]);
            if (colors)
                fprintf(f, "v %.6g %.6g %.6g %.4g %.4g %.4g\n", p.x, p.y, p.z, m->c[v].x, m->c[v].y, m->c[v].z);
            else
                fprintf(f, "v %.6g %.6g %.6g\n", p.x, p.y, p.z);
        }
        /* corner normals in world space (inverse transpose), shared where equal */
        int nc = m->fo[m->nf], *ni = NULL;
        if (angles) {
            V3 *cn = xrealloc(NULL, (nc + 1) * sizeof(V3));
            int *vfirst = xrealloc(NULL, (m->nv + 1) * sizeof(int));
            ni = xrealloc(NULL, (nc + 1) * sizeof(int));
            mesh_cnormals(m, angles[i], cn);
            M4 N = m4_inv(M);
            for (int v = 0; v < m->nv; v++) vfirst[v] = -1;
            for (int fc = 0; fc < m->nf; fc++)
                for (int c = m->fo[fc]; c < m->fo[fc + 1]; c++) {
                    int v = m->fi[c];
                    cn[c] = v3norm(v3(N.m[0] * cn[c].x + N.m[1] * cn[c].y + N.m[2] * cn[c].z,
                                      N.m[4] * cn[c].x + N.m[5] * cn[c].y + N.m[6] * cn[c].z,
                                      N.m[8] * cn[c].x + N.m[9] * cn[c].y + N.m[10] * cn[c].z));
                    if (vfirst[v] >= 0 && !memcmp(&cn[vfirst[v]], &cn[c], sizeof(V3))) ni[c] = ni[vfirst[v]];
                    else if (c > m->fo[fc] && !memcmp(&cn[c - 1], &cn[c], sizeof(V3))) ni[c] = ni[c - 1];
                    else {
                        ni[c] = nbase++;
                        fprintf(f, "vn %.5g %.5g %.5g\n", cn[c].x, cn[c].y, cn[c].z);
                    }
                    if (vfirst[v] < 0) vfirst[v] = c;
                }
            free(cn); free(vfirst);
            fprintf(f, "# tuff3d normal angle %g\ns %s\n", angles[i], angles[i] > 0 ? "1" : "off");
        }
        for (int fc = 0; fc < m->nf; fc++) {
            int o = m->fo[fc], cnt = m->fo[fc + 1] - o;
            fputc('f', f);
            for (int k = 0; k < cnt; k++) {
                int c = o + (flip ? cnt - 1 - k : k);
                if (ni) fprintf(f, " %d//%d", base + m->fi[c], ni[c]);
                else fprintf(f, " %d", base + m->fi[c]);
            }
            fputc('\n', f);
        }
        free(ni);
        base += m->nv;
    }
    int ok = !ferror(f);
    if (fclose(f)) ok = 0;
    return ok;
}

static char *read_line(FILE *f, char **buf, size_t *cap)
{
    size_t len = 0;
    int ch;
    while ((ch = fgetc(f)) != EOF) {
        if (len + 2 > *cap) { *cap = *cap ? *cap * 2 : 256; *buf = xrealloc(*buf, *cap); }
        if (ch == '\n') break;
        (*buf)[len++] = (char)ch;
    }
    if (ch == EOF && len == 0) return NULL;
    if (!*buf) { *cap = 16; *buf = xrealloc(NULL, *cap); }
    (*buf)[len] = 0;
    return *buf;
}

typedef struct {
    V3 *p, *c; int np, cp;
    int *fo, *fi; int nf, cf, ni, ci;
    Mesh *ms; char (*names)[32]; float *angles; int nm, cm;
    char name[32];
    float angle; int exact;   /* shading of the current object: -1 unknown; exact = from our own comment */
    int *map;
} ObjRead;

static void objr_flush(ObjRead *r)
{
    if (!r->nf) return;
    MB b;
    mb_init(&b);
    r->map = xrealloc(r->map, (r->np + 1) * sizeof(int));
    for (int i = 0; i < r->np; i++) r->map[i] = -1;
    int *tmp = xrealloc(NULL, (r->ni + 1) * sizeof(int));
    for (int f = 0; f < r->nf; f++) {
        int o = r->fo[f], n = r->fo[f + 1] - o, ok = 1;
        for (int k = 0; k < n; k++) {
            int g = r->fi[o + k];
            if (g < 0 || g >= r->np) { ok = 0; break; }
            if (r->map[g] < 0) r->map[g] = mb_vert(&b, r->p[g], r->c[g]);
            tmp[k] = r->map[g];
        }
        if (ok) mb_face(&b, tmp, n, 0);
    }
    free(tmp);
    if (r->nm == r->cm) {
        r->cm = r->cm ? r->cm * 2 : 8;
        r->ms = xrealloc(r->ms, r->cm * sizeof(Mesh));
        r->names = xrealloc(r->names, r->cm * sizeof(*r->names));
        r->angles = xrealloc(r->angles, r->cm * sizeof(float));
    }
    r->angles[r->nm] = r->angle;
    r->ms[r->nm] = mb_finish(&b);
    memcpy(r->names[r->nm], r->name, 32);
    r->nm++;
    r->nf = r->ni = 0;
}

int obj_load(const char *path, Mesh **ms, char (**names)[32], float **angles)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    ObjRead r;
    memset(&r, 0, sizeof r);
    r.cf = 64;
    r.fo = xrealloc(NULL, r.cf * sizeof(int));
    r.fo[0] = 0;
    strcpy(r.name, "imported");
    r.angle = -1;
    char *buf = NULL;
    size_t cap = 0;
    char *line;
    while ((line = read_line(f, &buf, &cap))) {
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        if (s[0] == 'v' && (s[1] == ' ' || s[1] == '\t')) {
            float a[6] = { 0, 0, 0, 1, 1, 1 };
            int k = sscanf(s + 2, "%f %f %f %f %f %f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
            if (k < 3) continue;
            if (k < 6) a[3] = a[4] = a[5] = 1;
            if (a[3] > 1 || a[4] > 1 || a[5] > 1) { a[3] /= 255; a[4] /= 255; a[5] /= 255; }
            if (r.np == r.cp) {
                r.cp = r.cp ? r.cp * 2 : 1024;
                r.p = xrealloc(r.p, r.cp * sizeof(V3));
                r.c = xrealloc(r.c, r.cp * sizeof(V3));
            }
            r.p[r.np] = v3(a[0], a[1], a[2]);
            r.c[r.np] = v3(a[3], a[4], a[5]);
            r.np++;
        } else if (s[0] == 'f' && (s[1] == ' ' || s[1] == '\t')) {
            char *q = s + 2;
            int n = 0;
            for (;;) {
                while (*q == ' ' || *q == '\t' || *q == '\r') q++;
                if (!*q) break;
                char *end;
                long idx = strtol(q, &end, 10);
                if (end == q) break;
                while (*end && *end != ' ' && *end != '\t' && *end != '\r') end++;
                q = end;
                int g = idx < 0 ? r.np + (int)idx : (int)idx - 1;
                if (r.ni + 1 > r.ci) { r.ci = r.ci ? r.ci * 2 : 1024; r.fi = xrealloc(r.fi, r.ci * sizeof(int)); }
                r.fi[r.ni++] = g;
                n++;
            }
            if (n < 3) { r.ni -= n; continue; }
            if (r.nf + 2 > r.cf) { r.cf *= 2; r.fo = xrealloc(r.fo, r.cf * sizeof(int)); }
            r.nf++;
            r.fo[r.nf] = r.ni;
        } else if ((s[0] == 'o' || s[0] == 'g') && (s[1] == ' ' || s[1] == '\t')) {
            objr_flush(&r);
            char nm[32] = { 0 };
            if (sscanf(s + 2, "%31s", nm) == 1) strcpy(r.name, nm);
            if (s[0] == 'o') r.angle = -1, r.exact = 0;
        } else if (s[0] == 's' && (s[1] == ' ' || s[1] == '\t') && !r.exact) {
            /* smoothing groups from other apps: off/0 = hard, any group = smooth */
            char *q = s + 2;
            while (*q == ' ' || *q == '\t') q++;
            r.angle = !strncmp(q, "off", 3) || atoi(q) == 0 ? 0 : 180;
        } else if (!strncmp(s, "# tuff3d normal angle ", 22)) {
            r.angle = (float)atof(s + 22);
            r.exact = 1;
        }
    }
    objr_flush(&r);
    fclose(f);
    free(buf); free(r.p); free(r.c); free(r.fo); free(r.fi); free(r.map);
    *ms = r.ms;
    *names = r.names;
    if (angles) *angles = r.angles;
    else free(r.angles);
    return r.nm;
}
