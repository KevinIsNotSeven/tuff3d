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

    /* multi mask: union of selected verts, edges and faces */
    prim_defaults(&p, PRIM_CUBE);
    m = mesh_prim(&p);
    {
        unsigned char mask[8];
        m.vs[0] = 1;
        m.es[0] = 1;
        CHECK(mesh_vmask(&m, COMP_MULTI, mask) == 1 + (m.ev[0] != 0) + (m.ev[1] != 0));   /* {0} + edge 0's ends */
        m.fs[0] = 1;
        CHECK(mesh_vmask(&m, COMP_MULTI, mask) >= 4);
        CHECK(mesh_vmask(&m, COMP_FACE, mask) == 4);
    }
    mesh_free(&m);

    /* edge loops: middle ring of a 2-high cylinder, a vertical column, a plane border */
    prim_defaults(&p, PRIM_CYL);
    p.csides = 12; p.chseg = 3;
    m = mesh_prim(&p);
    {
        int ring = -1, vert = -1;
        for (int e = 0; e < m.ne; e++) {
            V3 a = m.p[m.ev[2 * e]], b = m.p[m.ev[2 * e + 1]];
            if (fabsf(a.y - b.y) < 1e-4f && fabsf(a.y - (-1.0f / 3)) < 1e-3f && ring < 0) ring = e;
            if (fabsf(a.x - b.x) < 1e-4f && fabsf(a.z - b.z) < 1e-4f && vert < 0) vert = e;
        }
        CHECK(ring >= 0 && vert >= 0);
        CHECK(mesh_edge_loop(&m, ring) == 12);
        memset(m.es, 0, m.ne);
        CHECK(mesh_edge_loop(&m, vert) == 3);   /* stops at the 3-valent cap rings */
        int n = 0;
        for (int e = 0; e < m.ne; e++) n += m.es[e];
        CHECK(n == 3);
    }
    mesh_free(&m);
    prim_defaults(&p, PRIM_PLANE);
    p.psw = p.psd = 3;
    m = mesh_prim(&p);
    {
        int border = -1;
        for (int e = 0; e < m.ne && border < 0; e++)
            if (m.ef[2 * e + 1] < 0 && fabsf(m.p[m.ev[2 * e]].x - 0.5f) < 1e-4f && fabsf(m.p[m.ev[2 * e + 1]].x - 0.5f) < 1e-4f) border = e;
        CHECK(border >= 0 && mesh_edge_loop(&m, border) == 3);
    }
    mesh_free(&m);

    /* grow / shrink / convert on a cube */
    prim_defaults(&p, PRIM_CUBE);
    m = mesh_prim(&p);
    {
        int n = 0;
        m.vs[0] = 1;
        mesh_grow(&m, COMP_VERT, 0);
        for (int v = 0; v < 8; v++) n += m.vs[v];
        CHECK(n == 4);
        mesh_grow(&m, COMP_VERT, 1);
        n = 0;
        for (int v = 0; v < 8; v++) n += m.vs[v];
        CHECK(n == 1 && m.vs[0]);
        mesh_convert(&m, COMP_VERT, COMP_FACE);
        n = 0;
        for (int f = 0; f < 6; f++) n += m.fs[f];
        CHECK(n == 3 && !m.vs[0]);
        memset(m.fs, 0, 6);
        m.fs[0] = 1;
        mesh_convert(&m, COMP_FACE, COMP_EDGE);
        n = 0;
        for (int e = 0; e < 12; e++) n += m.es[e];
        CHECK(n == 4);
        mesh_convert(&m, COMP_EDGE, COMP_FACE);
        n = 0;
        for (int f = 0; f < 6; f++) n += m.fs[f];
        CHECK(n == 5);   /* the face and its 4 neighbors */
        mesh_flip(&m);
        CHECK(closed_oriented(&m) && volume(&m) < -0.99f);
    }
    mesh_free(&m);

    /* fill hole: delete a cube face, fill it back, closed again */
    prim_defaults(&p, PRIM_CUBE);
    m = mesh_prim(&p);
    m.fs[2] = 1;
    mesh_delete_faces(&m);
    CHECK(m.nf == 5 && !closed_oriented(&m));
    {
        unsigned char touch[8] = { 0 };
        CHECK(mesh_fill_holes(&m, touch) == 0 && m.nf == 5);   /* selection not on the hole */
        int bv = -1;
        for (int e = 0; e < m.ne; e++) if (m.ef[2 * e + 1] < 0) bv = m.ev[2 * e];
        touch[bv] = 1;
        CHECK(mesh_fill_holes(&m, touch) == 1);
    }
    CHECK(m.nf == 6 && closed_oriented(&m) && fabsf(volume(&m) - 1) < 1e-4f);
    {
        int sel = 0;
        for (int f = 0; f < m.nf; f++) sel += m.fs[f];
        CHECK(sel == 1);
    }
    mesh_free(&m);
    /* a cylinder without caps has two holes */
    prim_defaults(&p, PRIM_CYL);
    p.ctop = p.cbot = 0;
    m = mesh_prim(&p);
    CHECK(mesh_fill_holes(&m, NULL) == 2 && closed_oriented(&m) && volume(&m) > 0);
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
