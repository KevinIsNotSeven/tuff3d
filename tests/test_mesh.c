/* Core geometry tests: cc -O1 -Isrc tests/test_mesh.c src/mesh.c -lm && ./a.out */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

/* every edge used exactly once in each direction => closed + consistently wound */
static int closed_oriented(const Mesh *m)
{
    for (int e = 0; e < m->ne; e++) {
        int a = m->ev[2 * e], b = m->ev[2 * e + 1], ab = 0, ba = 0;
        for (int f = 0; f < m->nf; f++) {
            int o = m->fo[f], n = m->fo[f + 1] - o;
            for (int k = 0; k < n; k++) {
                int x = m->fi[o + k], y = m->fi[o + (k + 1) % n];
                ab += x == a && y == b;
                ba += x == b && y == a;
            }
        }
        if (ab != 1 || ba != 1) return 0;
    }
    return 1;
}

static float volume(const Mesh *m)
{
    float v = 0;
    for (int f = 0; f < m->nf; f++) {
        int o = m->fo[f], n = m->fo[f + 1] - o;
        V3 p0 = m->p[m->fi[o]];
        for (int k = 1; k + 1 < n; k++)
            v += v3dot(p0, v3cross(m->p[m->fi[o + k]], m->p[m->fi[o + k + 1]])) / 6.0f;
    }
    return v;
}

int main(void)
{
    Prim p;
    prim_defaults(&p, PRIM_CUBE);
    Mesh m = mesh_prim(&p);
    CHECK(m.nv == 8 && m.nf == 6 && m.ne == 12);
    CHECK(closed_oriented(&m));
    CHECK(fabsf(volume(&m) - 1) < 1e-4f);

    p.sw = 3; p.sh = 2; p.sd = 4;
    Mesh m2 = mesh_prim(&p);
    CHECK(m2.nf == 2 * (3 * 2 + 2 * 4 + 3 * 4));
    CHECK(closed_oriented(&m2));
    CHECK(fabsf(volume(&m2) - 1) < 1e-4f);
    mesh_free(&m2);

    /* extrude top face of the cube */
    int top = -1;
    for (int f = 0; f < m.nf; f++) if (mesh_fnormal(&m, f).y > 0.9f) top = f;
    CHECK(top >= 0);
    m.fs[top] = 1;
    CHECK(mesh_extrude_faces(&m));
    CHECK(m.nv == 12 && m.nf == 10);
    CHECK(closed_oriented(&m));
    int nsel = 0;
    unsigned char mask[64];
    for (int f = 0; f < m.nf; f++) nsel += m.fs[f];
    CHECK(nsel == 1);
    CHECK(mesh_vmask(&m, COMP_FACE, mask) == 4);
    for (int v = 0; v < m.nv; v++) if (mask[v]) m.p[v].y += 1;
    CHECK(fabsf(volume(&m) - 2) < 1e-4f);

    /* merge the top 4 verts into a pyramid apex */
    CHECK(mesh_merge(&m, mask));
    CHECK(m.nv == 9);
    CHECK(closed_oriented(&m));
    CHECK(m.nf == 9); /* the top quad collapsed */
    mesh_free(&m);

    /* cylinders */
    prim_defaults(&p, PRIM_CYL);
    m = mesh_prim(&p);
    CHECK(m.nv == 40 && m.nf == 22 && closed_oriented(&m) && volume(&m) > 0);
    mesh_free(&m);
    p.ccap = 2; p.chseg = 3;
    m = mesh_prim(&p);
    CHECK(closed_oriented(&m) && volume(&m) > 0);
    mesh_free(&m);
    p.ctop = 0; p.ccap = 0;
    m = mesh_prim(&p);
    CHECK(m.nf == 20 * 3 + 1);
    /* extrude the open border */
    int nb = 0;
    for (int e = 0; e < m.ne; e++) if (m.ef[2 * e + 1] < 0) { m.es[e] = 1; nb++; }
    CHECK(nb == 20);
    CHECK(mesh_extrude_edges(&m));
    CHECK(m.nv == 20 * 4 + 20 && m.nf == 20 * 4 + 1);
    nsel = 0;
    for (int e = 0; e < m.ne; e++) nsel += m.es[e];
    CHECK(nsel == 20);
    for (int e = 0; e < m.ne; e++) if (m.es[e]) CHECK(m.ef[2 * e + 1] < 0);
    mesh_free(&m);

    /* sphere */
    prim_defaults(&p, PRIM_SPHERE);
    m = mesh_prim(&p);
    CHECK(m.nv == 20 * 19 + 2 && m.nf == 400 && closed_oriented(&m));
    CHECK(volume(&m) > 3.9f && volume(&m) < 4.2f);
    /* raycast from +Z hits the front */
    float t;
    int f = mesh_raycast(&m, v3(0.01f, 0.02f, 5), v3(0, 0, -1), &t);
    CHECK(f >= 0 && fabsf(t - 4) < 0.05f);
    CHECK(mesh_fnormal(&m, f).z > 0.9f);
    /* delete faces */
    m.fs[0] = m.fs[1] = 1;
    CHECK(mesh_delete_faces(&m) && m.nf == 398);
    mesh_free(&m);

    /* euler round trip */
    V3 r = v3(30, -50, 120);
    V3 r2 = m4_to_euler(m4_euler(r));
    CHECK(fabsf(r.x - r2.x) < 1e-3f && fabsf(r.y - r2.y) < 1e-3f && fabsf(r.z - r2.z) < 1e-3f);
    M4 a = m4_trs(v3(1, 2, 3), r, v3(2, 3, 4)), ai = m4_inv(a);
    V3 q = m4_pt(ai, m4_pt(a, v3(0.3f, -0.7f, 2)));
    CHECK(fabsf(q.x - 0.3f) < 1e-4f && fabsf(q.y + 0.7f) < 1e-4f && fabsf(q.z - 2) < 1e-4f);

    /* OBJ round trip with colors */
    prim_defaults(&p, PRIM_CUBE);
    m = mesh_prim(&p);
    m.c[3] = v3(1, 0, 0);
    const Mesh *ms[1] = { &m };
    M4 mats[1] = { m4_trs(v3(5, 0, 0), v3(0, 0, 0), v3(1, 1, -1)) };
    const char *names[1] = { "pCube1" };
    CHECK(obj_save("test_out.obj", 1, ms, mats, names));
    Mesh *lm;
    char (*ln)[32];
    int n = obj_load("test_out.obj", &lm, &ln);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(lm[0].nv == 8 && lm[0].nf == 6 && !strcmp(ln[0], "pCube1"));
        CHECK(closed_oriented(&lm[0]) && volume(&lm[0]) > 0.99f); /* winding flipped for mirror */
        int red = 0;
        for (int v = 0; v < 8; v++) red += lm[0].c[v].y == 0;
        CHECK(red == 1);
        mesh_free(&lm[0]);
        free(lm); free(ln);
    }
    remove("test_out.obj");
    mesh_free(&m);

    /* plane: grid of up-facing quads, open border, extrudable */
    prim_defaults(&p, PRIM_PLANE);
    p.psw = 3; p.psd = 2;
    m = mesh_prim(&p);
    CHECK(m.nv == 4 * 3 && m.nf == 6 && m.ne == 3 * 3 + 4 * 2);
    for (int f = 0; f < m.nf; f++) CHECK(mesh_fnormal(&m, f).y > 0.99f);
    m.fs[0] = 1;
    CHECK(mesh_extrude_faces(&m) && m.nf == 6 + 4);
    mesh_free(&m);

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
