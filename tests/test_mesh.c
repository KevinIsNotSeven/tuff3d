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

    /* shading normals: hard keeps face normals, smooth averages, auto smooth splits at the angle */
    prim_defaults(&p, PRIM_CUBE);
    m = mesh_prim(&p);
    {
        V3 *cn = malloc(m.fo[m.nf] * sizeof(V3));
        int hard = 1, soft = 1;
        float ang[2] = { 30, 0 };
        for (int a = 0; a < 2; a++) {
            mesh_cnormals(&m, ang[a], cn);
            for (int f = 0; f < m.nf; f++)
                for (int c = m.fo[f]; c < m.fo[f + 1]; c++) hard &= v3dot(cn[c], mesh_fnormal(&m, f)) > 0.9999f;
        }
        mesh_cnormals(&m, 180, cn);
        for (int c = 0; c < m.fo[m.nf]; c++)
            soft &= fabsf(fabsf(cn[c].x) - 0.57735f) < 1e-3f && fabsf(fabsf(cn[c].y) - 0.57735f) < 1e-3f;
        CHECK(hard && soft);
        free(cn);
    }
    mesh_free(&m);
    prim_defaults(&p, PRIM_SPHERE);
    m = mesh_prim(&p);
    {
        V3 *cn = malloc(m.fo[m.nf] * sizeof(V3)), *first = calloc(m.nv, sizeof(V3));
        mesh_cnormals(&m, 30, cn);
        int smooth = 1;
        for (int c = 0; c < m.fo[m.nf]; c++) {
            V3 *q = &first[m.fi[c]];
            if (v3dot(*q, *q) == 0) *q = cn[c];
            else smooth &= v3dot(*q, cn[c]) > 0.9999f;
            smooth &= v3dot(cn[c], v3norm(m.p[m.fi[c]])) > 0.99f;
        }
        CHECK(smooth);
        free(cn); free(first);
    }
    mesh_free(&m);

    /* OBJ round trip with colors and normals */
    prim_defaults(&p, PRIM_CUBE);
    m = mesh_prim(&p);
    m.c[3] = v3(1, 0, 0);
    const Mesh *ms[1] = { &m };
    M4 mats[1] = { m4_trs(v3(5, 0, 0), v3(0, 0, 0), v3(1, 1, -1)) };
    const char *names[1] = { "pCube1" };
    float angles[1] = { 30 };
    CHECK(obj_save("test_out.obj", 1, ms, mats, names, angles));
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
    {
        FILE *f = fopen("test_out.obj", "r");
        char line[256];
        int vn = 0, fslash = 0;
        while (f && fgets(line, sizeof line, f)) { vn += !strncmp(line, "vn ", 3); fslash += line[0] == 'f' && strstr(line, "//") != NULL; }
        if (f) fclose(f);
        CHECK(vn == 6 && fslash == 6);   /* hard cube: one shared normal per face */
    }
    CHECK(obj_save("test_out.obj", 1, ms, mats, names, NULL));
    n = obj_load("test_out.obj", &lm, &ln);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(lm[0].nv == 8 && lm[0].nf == 6);
        mesh_free(&lm[0]);
        free(lm); free(ln);
    }
    remove("test_out.obj");
    mesh_free(&m);

    printf(fails ? "%d FAILED\n" : "all tests passed\n", fails);
    return fails != 0;
}
