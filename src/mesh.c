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

/* value slot for undirected edge (a,b); new slots start at -1 */
static int *hm_get(HMap *h, int a, int b)
{
    if (a > b) { int t = a; a = b; b = t; }
    unsigned long long key = ((unsigned long long)(unsigned)a << 32) | (unsigned)b;
    unsigned i = (unsigned)((key * 0x9E3779B97F4A7C15ull) >> 32) & h->mask;
    while (h->k[i] != ~0ull && h->k[i] != key) i = (i + 1) & h->mask;
    if (h->k[i] == ~0ull) { h->k[i] = key; h->v[i] = -1; }
    return &h->v[i];
}

/* ---------------------------------------------------------------- mesh basics */

typedef struct { Mesh m; int cv, cf, ci, ni; } MB;

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

/* drop vertices no face uses (selection flags move with them) */
static void mesh_compact(Mesh *m)
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
    free(map);
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
    mesh_compact(&m);
    mesh_edges(&m);
    return m;
}

void mesh_free(Mesh *m)
{
    free(m->p); free(m->c); free(m->vs);
    free(m->fo); free(m->fi); free(m->fs);
    free(m->ev); free(m->ef); free(m->es);
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
    return m;
}

void mesh_edges(Mesh *m)
{
    int ni = m->fo[m->nf];
    free(m->ev); free(m->ef); free(m->es);
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
}

V3 mesh_fnormal(const Mesh *m, int f)
{
    return v3norm(newell(m->p, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f]));
}

/* Per face-corner normals (out[fo[f] + k]). A corner averages the area weighted
   normals of the faces around its vertex that are within 'angle' degrees of its
   own face: 0 = hard (flat), 180 = fully smooth, in between = auto smooth. */
void mesh_cnormals(const Mesh *m, float angle, V3 *out)
{
    int nc = m->fo[m->nf];
    V3 *fn = xrealloc(NULL, (m->nf + 1) * sizeof(V3));
    int *vo = xrealloc(NULL, (m->nv + 1) * sizeof(int)), *vc = xrealloc(NULL, (nc + 1) * sizeof(int));
    int *cf = xrealloc(NULL, (nc + 1) * sizeof(int));
    for (int f = 0; f < m->nf; f++) {
        fn[f] = newell(m->p, m->fi + m->fo[f], m->fo[f + 1] - m->fo[f]);
        for (int c = m->fo[f]; c < m->fo[f + 1]; c++) cf[c] = f;
    }
    /* vertex -> corners */
    memset(vo, 0, (m->nv + 1) * sizeof(int));
    for (int c = 0; c < nc; c++) vo[m->fi[c] + 1]++;
    for (int v = 0; v < m->nv; v++) vo[v + 1] += vo[v];
    int *fill = xrealloc(NULL, (m->nv + 1) * sizeof(int));
    memcpy(fill, vo, m->nv * sizeof(int));
    for (int c = 0; c < nc; c++) vc[fill[m->fi[c]]++] = c;
    free(fill);
    float ct = angle >= 180 ? -2.0f : angle <= 0 ? 2.0f : cosf(angle * 3.14159265f / 180.0f) - 1e-5f;
    for (int c = 0; c < nc; c++) {
        V3 own = v3norm(fn[cf[c]]), sum = fn[cf[c]];
        int v = m->fi[c];
        for (int j = vo[v]; j < vo[v + 1]; j++) {
            int g = cf[vc[j]];
            if (g == cf[c]) continue;
            if (v3dot(own, v3norm(fn[g])) >= ct) sum = v3add(sum, fn[g]);
        }
        sum = v3norm(sum);
        out[c] = v3dot(sum, sum) > 0 ? sum : own;
    }
    free(fn); free(vo); free(vc); free(cf);
}

V3 mesh_fcenter(const Mesh *m, int f)
{
    V3 c = v3(0, 0, 0);
    int o = m->fo[f], n = m->fo[f + 1] - o;
    for (int k = 0; k < n; k++) c = v3add(c, m->p[m->fi[o + k]]);
    return n ? v3mul(c, 1.0f / n) : c;
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

static void mesh_replace(Mesh *m, MB *b)
{
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
            fprintf(f, "s %s\n", angles[i] > 0 ? "1" : "off");
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
    Mesh *ms; char (*names)[32]; int nm, cm;
    char name[32];
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
    }
    r->ms[r->nm] = mb_finish(&b);
    memcpy(r->names[r->nm], r->name, 32);
    r->nm++;
    r->nf = r->ni = 0;
}

int obj_load(const char *path, Mesh **ms, char (**names)[32])
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    ObjRead r;
    memset(&r, 0, sizeof r);
    r.cf = 64;
    r.fo = xrealloc(NULL, r.cf * sizeof(int));
    r.fo[0] = 0;
    strcpy(r.name, "imported");
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
        }
    }
    objr_flush(&r);
    fclose(f);
    free(buf); free(r.p); free(r.c); free(r.fo); free(r.fi); free(r.map);
    *ms = r.ms;
    *names = r.names;
    return r.nm;
}
