/* tuff3d - Maya LT scene (.mlt) reader.

   An .mlt file is Maya's 64-bit binary scene format (IFF style FOR8 groups) with light
   scrambling: chunk sizes are XOR-masked per chunk type, chunk tags are XOR-masked, node names
   are rotated and XOR-masked with a 10 byte cycle, attribute names are reversed and masked, and
   multi attribute paths are masked with one byte. The connection list is plain text.

   Maya LT saves construction history rather than finished meshes, so the visible meshes are
   rebuilt here by replaying the supported poly operations (cube/sphere/cylinder/plane, smooth,
   chip off, separate, extrude edge/face, tweaks, delete faces, unite, merge, vertex colors).
   Vertex numbers inside the history follow Maya's conventions where they are known; rings of
   vertices made by edge extrusion are matched to their tweaks by geometry, and a final vertex
   color node (which lists the faces around every vertex) maps Maya's vertex numbers onto ours. */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"

static void *xalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p) abort();
    return p;
}

static void *xgrow(void *p, size_t n)
{
    p = realloc(p, n ? n : 1);
    if (!p) abort();
    return p;
}

/* ---------------------------------------------------------------- file decoding */

#define GROUP_KEY 0x4b003cc3u
static const uint8_t TAG_KEY[4] = { 0xf9, 0x76, 0x56, 0x51 };
static const uint8_t NAME_CYCLE[10] = { 0xc4, 0x97, 0x12, 0xda, 0x13, 0x98, 0xe1, 0x9a, 0xb6, 0x0a };
static const uint8_t ATTR_KEY[4] = { 0x2a, 0xc5, 0x22, 0xdb };
static const uint8_t CREA_TAG[4] = { 0x35, 0xab, 0xcf, 0x79 };
static const uint8_t INT_TAG[4] = { '^', '3', '4', '5' };

typedef struct { char tag[5]; char name[96]; uint8_t flag; const uint8_t *v; int n; } Attr;
typedef struct {
    uint32_t code; char name[64], parent[160], path[256];
    int pidx; Attr *a; int na;
} Node;
typedef struct { char src[160], dst[160]; int sn, dn; const char *sat, *dat; } Conn;   /* resolved node + attribute */
typedef struct {
    const uint8_t *d; size_t n;
    Node *nd; int nn;
    Conn *cn; int nc;
    struct { uint8_t raw[4]; uint32_t key; } learned[64];
    int nlearned;
    char report[512];
} Doc;

static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static double bedbl(const uint8_t *p)
{
    uint64_t u = (uint64_t)be32(p) << 32 | be32(p + 4);
    double d;
    memcpy(&d, &u, 8);
    return d;
}
static float beflt(const uint8_t *p)
{
    uint32_t u = be32(p);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static void tag_name(const uint8_t *raw, char *out)
{
    if (!memcmp(raw, CREA_TAG, 4)) { strcpy(out, "CREA"); return; }
    if (!memcmp(raw, INT_TAG, 4)) { strcpy(out, "INT "); return; }
    for (int i = 0; i < 4; i++) out[i] = (char)(raw[i] ^ TAG_KEY[i]);
    out[4] = 0;
}

static int known_key(const char *t, uint32_t *k)
{
    static const struct { const char *t; uint32_t k; } K[] = {
        { "CREA", 0x01291967 }, { "DBLE", 0x442a9e43 }, { "DBL2", 0x442a9e43 }, { "DBL3", 0x442a9e43 },
        { "FLT2", 0x442a9e43 }, { "FLT3", 0x442a9e43 }, { "STR ", 0x342aa009 }, { "STR#", 0x342aa009 },
        { "CMPD", 0x29b104c2 }, { "CMP#", 0x29b104c2 }, { "MATR", 0x03be1007 }, { "INT ", 0x04cd2929 },
    };
    for (size_t i = 0; i < sizeof K / sizeof *K; i++)
        if (!strcmp(t, K[i].t)) { *k = K[i].k; return 1; }
    return 0;
}

static int is_group(const uint8_t *p) { return !memcmp(p, "FOR8", 4) || !memcmp(p, "LIS8", 4); }

static int header_ok(Doc *D, size_t q, size_t end)
{
    return q + 16 <= end && be32(D->d + q + 8) == 0 && be32(D->d + q) != 0;
}

static int chunk_key(Doc *D, const uint8_t *raw, uint32_t *k)
{
    char t[5];
    tag_name(raw, t);
    if (known_key(t, k)) return 1;
    for (int i = 0; i < D->nlearned; i++)
        if (!memcmp(D->learned[i].raw, raw, 4)) { *k = D->learned[i].key; return 1; }
    return 0;
}

/* size of the chunk at p; an unknown chunk type is sized by finding where a valid next header starts */
static size_t chunk_size(Doc *D, size_t p, size_t end)
{
    uint32_t Y = be32(D->d + p + 12), k;
    if (is_group(D->d + p)) return Y ^ GROUP_KEY;
    if (chunk_key(D, D->d + p, &k)) return Y ^ k;
    for (size_t s = 0; p + 16 + s <= end; s++) {
        size_t nxt = p + 16 + ((s + 7) & ~(size_t)7);
        uint32_t k2;
        if (nxt == end || (header_ok(D, nxt, end) && (is_group(D->d + nxt) || chunk_key(D, D->d + nxt, &k2)))) {
            if (D->nlearned < 64) {
                memcpy(D->learned[D->nlearned].raw, D->d + p, 4);
                D->learned[D->nlearned++].key = Y ^ (uint32_t)s;
            }
            return s;
        }
    }
    return end - p - 16;
}

static int is_ident(const char *s, int path)
{
    if (!*s) return 0;
    for (const char *c = s; *c; c++)
        if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_' ||
              (path && (*c == '|' || *c == ':'))))
            return 0;
    return !(s[0] >= '0' && s[0] <= '9');
}

/* node names: rotated by a length dependent amount and XORed with NAME_CYCLE from (n + 4) % 10 */
static const int NAME_ROT[29] = { -1, -1, -1, 1, 3, 2, 4, 5, 3, 4, 2, 6, 5, 8, 7, 9, 6, 7, 6, 10,
                                  13, -1, -1, -1, -1, 8, -1, -1, 12 };   /* seen so far */

static void decode_name(const uint8_t *e, int n, char *out, int outsz, Doc *D)
{
    char best[160] = "", s[160];
    if (n >= (int)sizeof s || n >= outsz) n = outsz - 1;
    int e0 = (n + 4) % 10, fixed = n < 29 ? NAME_ROT[n] : -1;
    /* the known rotation for this length first, then every other one */
    for (int t = -1; t < n; t++) {
        int r = t < 0 ? fixed : t;
        if (r < 0 || (t >= 0 && t == fixed)) continue;
        for (int j = 0; j < n; j++) {
            int i = ((j - r) % n + n) % n;
            s[j] = (char)(e[i] ^ NAME_CYCLE[(e0 + i) % 10]);
        }
        s[n] = 0;
        if (!is_ident(s, 0)) continue;
        /* a name that appears in the connection list is certainly right */
        for (int c = 0; c < D->nc; c++) {
            size_t l = strlen(s);
            if ((!strncmp(D->cn[c].src, s, l) && D->cn[c].src[l] == '.') || (!strncmp(D->cn[c].dst, s, l) && D->cn[c].dst[l] == '.')) {
                snprintf(out, outsz, "%s", s);
                return;
            }
        }
        if (!best[0]) strcpy(best, s);
        if (t < 0) break;   /* the known rotation gave a valid name */
    }
    snprintf(out, outsz, "%s", best[0] ? best : "?");
}

static int path_ok(const char *s)
{
    /* seg ('.' seg)*, seg = [a-z][A-Za-z0-9_]* ('[' digits (':' digits)? ']')? */
    const char *c = s;
    for (;;) {
        if (!(*c >= 'a' && *c <= 'z')) return 0;
        while ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '_') c++;
        if (*c == '[') {
            c++;
            if (!(*c >= '0' && *c <= '9')) return 0;
            while (*c >= '0' && *c <= '9') c++;
            if (*c == ':') { c++; if (!(*c >= '0' && *c <= '9')) return 0; while (*c >= '0' && *c <= '9') c++; }
            if (*c++ != ']') return 0;
        }
        if (!*c) return 1;
        if (*c++ != '.') return 0;
    }
}

/* attribute chunk: number\0 + (4 byte short name + flag | masked path\0 + flag) + value */
static void decode_attr(const uint8_t *data, int n, Attr *a)
{
    int i = 0;
    while (i < n && data[i]) i++;
    i++;
    a->name[0] = 0;
    if (i + 5 <= n) {
        uint8_t d[4];
        int z = -1, k = 0;
        for (int j = 0; j < 4; j++) { d[j] = data[i + j] ^ ATTR_KEY[j]; if (!d[j]) z = j; }
        for (int j = 3; j > z; j--) a->name[k++] = (char)d[j];
        a->name[k] = 0;
        int ok = k > 0 && a->name[0] >= 'a' && a->name[0] <= 'z';
        for (int j = 0; j < k && ok; j++) {
            char c = a->name[j];
            ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '[' || c == ']';
        }
        if (ok) { a->flag = data[i + 4]; a->v = data + i + 5; a->n = n - i - 5; return; }
    }
    int j = i;
    while (j < n && data[j]) j++;
    int len = j - i, bestsc = -1;
    char s[96];
    if (len > 0 && len < (int)sizeof s) {
        for (int key = 0; key < 256; key++) {
            for (int c = 0; c < len; c++) s[c] = (char)(data[i + c] ^ key);
            s[len] = 0;
            if (!path_ok(s)) continue;
            int sc = 0;
            for (int c = 0; c < len; c++) sc += (s[c] == '[' || s[c] == ']' || s[c] == '.') ? 4 : (s[c] >= 'a' && s[c] <= 'z');
            if (sc > bestsc) { bestsc = sc; strcpy(a->name, s); }
        }
    }
    if (bestsc < 0) strcpy(a->name, "?");
    a->flag = j + 1 < n ? data[j + 1] : 0;
    a->v = data + j + 2;
    a->n = n - j - 2 > 0 ? n - j - 2 : 0;
}

static void parse_connections(Doc *D, size_t p)
{
    for (; p + 20 < D->n; p += 4) {
        if (memcmp(D->d + p, "LWFC", 4) || be32(D->d + p + 8)) continue;
        const char *s = (const char *)D->d + p + 17;
        size_t room = D->n - (p + 17);
        size_t l1 = strnlen(s, room);
        if (l1 >= room) break;
        size_t l2 = strnlen(s + l1 + 1, room - l1 - 1);
        if (!l1 || !l2 || l1 >= 160 || l2 >= 160) continue;
        D->cn = xgrow(D->cn, (D->nc + 1) * sizeof(Conn));
        memcpy(D->cn[D->nc].src, s, l1); D->cn[D->nc].src[l1] = 0;
        memcpy(D->cn[D->nc].dst, s + l1 + 1, l2); D->cn[D->nc].dst[l2] = 0;
        D->nc++;
    }
}

static int find_node(Doc *D, const char *name);

static int doc_parse(Doc *D)
{
    if (D->n < 32 || memcmp(D->d, "FOR8", 4) || memcmp(D->d + 16, "MBLT", 4)) return 0;
    /* pass 1: find where the nodes end and read the plain-text connection list */
    size_t p = 0x14, list = 0;
    while (p + 16 <= D->n && !memcmp(D->d + p, "FOR8", 4)) p += 16 + (((be32(D->d + p + 12) ^ GROUP_KEY) + 3) & ~3u);
    list = p;
    parse_connections(D, list);
    /* pass 2: nodes */
    for (p = 0x14; p < list;) {
        size_t sz = be32(D->d + p + 12) ^ GROUP_KEY, end = p + 16 + sz;
        if (end > list) break;
        size_t q = p + 20;
        if (q + 16 <= end && !memcmp(D->d + q, CREA_TAG, 4)) {
            Node nd;
            memset(&nd, 0, sizeof nd);
            nd.code = be32(D->d + p + 16);
            size_t csz = chunk_size(D, q, end);
            const uint8_t *c = D->d + q + 16;
            int l1 = (int)strnlen((const char *)c + 1, csz > 1 ? csz - 1 : 0);
            decode_name(c + 1, l1, nd.name, sizeof nd.name, D);
            if ((size_t)(2 + l1) < csz) {
                const uint8_t *pp = c + 2 + l1;
                int l2 = (int)strnlen((const char *)pp, csz - 2 - l1);
                for (int i = 0; i < l2 && i < (int)sizeof nd.parent - 1; i++) nd.parent[i] = (char)(pp[i] ^ 0xda);
            }
            q += 16 + ((csz + 7) & ~(size_t)7);
            while (q + 16 <= end) {
                if (is_group(D->d + q)) { q += 16 + (((be32(D->d + q + 12) ^ GROUP_KEY) + 3) & ~3u); continue; }
                size_t s = chunk_size(D, q, end);
                if (q + 16 + s > end) break;
                nd.a = xgrow(nd.a, (nd.na + 1) * sizeof(Attr));
                Attr *a = &nd.a[nd.na++];
                memset(a, 0, sizeof *a);
                tag_name(D->d + q, a->tag);
                decode_attr(D->d + q + 16, (int)s, a);
                q += 16 + ((s + 7) & ~(size_t)7);
            }
            D->nd = xgrow(D->nd, (D->nn + 1) * sizeof(Node));
            D->nd[D->nn++] = nd;
        }
        p = end;
    }
    /* full DAG paths: a parent written as a short name is the most recent node with that name */
    for (int i = 0; i < D->nn; i++) {
        Node *n = &D->nd[i];
        n->pidx = -1;
        if (n->parent[0]) {
            for (int j = i - 1; j >= 0 && n->pidx < 0; j--) {
                const char *pp = D->nd[j].path;
                size_t lp = strlen(pp), lq = strlen(n->parent);
                const char *want = n->parent[0] == '|' ? n->parent : NULL;
                if (want ? !strcmp(pp, want) : (lp > lq && !strcmp(pp + lp - lq, n->parent) && pp[lp - lq - 1] == '|'))
                    n->pidx = j;
            }
        }
        snprintf(n->path, sizeof n->path, "%s|%s", n->pidx >= 0 ? D->nd[n->pidx].path : "", n->name);
    }
    /* connection ends: node (a name or a |path) up to the first '.', then the attribute */
    for (int c = 0; c < D->nc; c++) {
        Conn *k = &D->cn[c];
        char tmp[160];
        char *dot = strchr(k->src, '.');
        k->sn = k->dn = -1;
        k->sat = dot ? dot + 1 : "";
        if (dot) { snprintf(tmp, sizeof tmp, "%.*s", (int)(dot - k->src), k->src); k->sn = find_node(D, tmp); }
        dot = strchr(k->dst, '.');
        k->dat = dot ? dot + 1 : "";
        if (dot) { snprintf(tmp, sizeof tmp, "%.*s", (int)(dot - k->dst), k->dst); k->dn = find_node(D, tmp); }
    }
    return D->nn > 0;
}

/* ---------------------------------------------------------------- node and attribute access */

enum { T_OTHER, T_TRANSFORM, T_MESH };

static int node_type(const Node *n)
{
    if (n->code == 0x60699291u) return T_TRANSFORM;
    if (n->code == 0x6568998du) return T_MESH;
    return T_OTHER;
}

/* the node's type name: its default name without the trailing number (polyExtrudeEdge4 -> polyExtrudeEdge) */
static void op_name(const Node *n, char *out)
{
    static const struct { uint32_t code; const char *name; } C[] = {
        { 0x6f6e9799, "polyCube" }, { 0x6b768799, "polySmoothFace" }, { 0x64739799, "polyChipOff" },
        { 0x7d7e8799, "polySeparate" }, { 0x6972848e, "groupId" }, { 0x7d6b868e, "groupParts" },
        { 0x68639199, "polyExtrudeEdge" }, { 0x666c8099, "polyTweak" }, { 0x64758199, "polyUnite" },
        { 0x686d9999, "polyMergeVert" }, { 0x68748799, "polySoftEdge" }, { 0x6b639199, "polyExtrudeFace" },
        { 0x7978918d, "deleteComponent" }, { 0x7b6b9799, "polyColorPerVertex" },
    };
    for (size_t i = 0; i < sizeof C / sizeof *C; i++)
        if (n->code == C[i].code) { strcpy(out, C[i].name); return; }
    size_t l = strlen(n->name);
    while (l && n->name[l - 1] >= '0' && n->name[l - 1] <= '9') l--;
    memcpy(out, n->name, l);
    out[l] = 0;
}

static const Attr *attr(const Node *n, const char *name)
{
    for (int i = 0; i < n->na; i++)
        if (!strcmp(n->a[i].name, name)) return &n->a[i];
    return NULL;
}

static double attr_d(const Node *n, const char *name, double def)
{
    const Attr *a = attr(n, name);
    if (!a || a->n < 8) {
        if (a && !strcmp(a->tag, "INT ") && a->n >= 4) return (int32_t)be32(a->v);
        return def;
    }
    return bedbl(a->v);
}

static V3 attr_v3(const Node *n, const char *name, V3 def)
{
    const Attr *a = attr(n, name);
    if (!a) return def;
    if (!strcmp(a->tag, "DBL3") && a->n >= 24) return v3((float)bedbl(a->v), (float)bedbl(a->v + 8), (float)bedbl(a->v + 16));
    if (!strcmp(a->tag, "FLT3") && a->n >= 12) return v3(beflt(a->v), beflt(a->v + 4), beflt(a->v + 8));
    return def;
}

static int find_node(Doc *D, const char *name)
{
    for (int i = D->nn - 1; i >= 0; i--) {
        const char *p = D->nd[i].path;
        size_t lp = strlen(p), ln = strlen(name);
        if (!strcmp(D->nd[i].name, name) || (lp >= ln && !strcmp(p + lp - ln, name) && (name[0] == '|' || p[lp - ln - 1] == '|')))
            return i;
    }
    return -1;
}

/* what feeds node.attr: returns the source node and copies the source attribute into sattr */
static int input(Doc *D, int node, const char *at, char *sattr)
{
    for (int c = 0; c < D->nc; c++) {
        if (D->cn[c].dn != node || strcmp(D->cn[c].dat, at) || D->cn[c].sn < 0) continue;
        if (sattr) snprintf(sattr, 160, "%s", D->cn[c].sat);
        return D->cn[c].sn;
    }
    return -1;
}

static int consumer(Doc *D, int node, const char *at)
{
    for (int c = 0; c < D->nc; c++)
        if (D->cn[c].sn == node && !strcmp(D->cn[c].sat, at)) return D->cn[c].dn;
    return -1;
}

/* component list (CMP#): groups of [type][range count][a b ...]; 0xffffffff means "all" */
typedef struct { char type; int n; int *idx; } Comps;

static Comps comps(const Node *nd, const char *name, int total)
{
    Comps c = { 0, 0, NULL };
    const Attr *a = attr(nd, name);
    if (!a || a->n < 4) return c;
    const uint8_t *v = a->v, *e = a->v + a->n;
    int groups = (int)be32(v);
    v += 4;
    for (int g = 0; g < groups && v + 8 <= e; g++) {
        c.type = (char)v[3];   /* "CMDF" faces, "CMDE" edges, "CMDV" vertices */
        int nr = (int)be32(v + 4);
        v += 8;
        for (int r = 0; r < nr && v + 8 <= e; r++, v += 8) {
            int lo = (int)be32(v), hi = (int)be32(v + 4);
            if (be32(v) == 0xffffffffu) { lo = 0; hi = total - 1; }
            for (int i = lo; i <= hi && i < 1 << 22; i++) {
                c.idx = xgrow(c.idx, (c.n + 1) * sizeof(int));
                c.idx[c.n++] = i;
            }
        }
    }
    return c;
}

/* indexed float3 entries of an array attribute: tk[3], pt[25:32] ... */
typedef struct { int n; int *i; V3 *v; } Tweaks;

static Tweaks tweaks(const Node *nd, const char *base)
{
    Tweaks t = { 0, NULL, NULL };
    size_t bl = strlen(base);
    for (int k = 0; k < nd->na; k++) {
        const Attr *a = &nd->a[k];
        if (strncmp(a->name, base, bl) || a->name[bl] != '[' || strcmp(a->tag, "FLT3")) continue;
        int lo = atoi(a->name + bl + 1), hi = lo;
        const char *colon = strchr(a->name, ':');
        if (colon) hi = atoi(colon + 1);
        for (int i = lo; i <= hi && (i - lo + 1) * 12 <= a->n; i++) {
            t.i = xgrow(t.i, (t.n + 1) * sizeof(int));
            t.v = xgrow(t.v, (t.n + 1) * sizeof(V3));
            const uint8_t *p = a->v + (i - lo) * 12;
            t.i[t.n] = i;
            t.v[t.n++] = v3(beflt(p), beflt(p + 4), beflt(p + 8));
        }
    }
    return t;
}

static void tweaks_free(Tweaks *t) { free(t->i); free(t->v); }

/* ---------------------------------------------------------------- working meshes */

/* A plain polygon list plus what the replay needs: per face colors (r < 0: unset), and the first
   vertex of the newest edge-extrusion ring with its size, so tweaks on it can be fitted. */
typedef struct {
    int nv, nf;
    V3 *p;
    int *fo, *fi;
    V3 *fc;
    int ring0, ringn;
    float ringsign;
} G;

static void g_free(G *g) { free(g->p); free(g->fo); free(g->fi); free(g->fc); memset(g, 0, sizeof *g); }

static G g_copy(const G *s)
{
    G g = *s;
    g.p = xalloc((s->nv + 1) * sizeof(V3)); memcpy(g.p, s->p, s->nv * sizeof(V3));
    g.fo = xalloc((s->nf + 1) * sizeof(int)); memcpy(g.fo, s->fo, (s->nf + 1) * sizeof(int));
    g.fi = xalloc((s->fo[s->nf] + 1) * sizeof(int)); memcpy(g.fi, s->fi, s->fo[s->nf] * sizeof(int));
    g.fc = xalloc((s->nf + 1) * sizeof(V3)); memcpy(g.fc, s->fc, s->nf * sizeof(V3));
    return g;
}

static G g_new(int nv, const V3 *p, int nf, const int *fo, const int *fi)
{
    G g;
    memset(&g, 0, sizeof g);
    g.nv = nv; g.nf = nf;
    g.p = xalloc((nv + 1) * sizeof(V3));
    g.fo = xalloc((nf + 1) * sizeof(int));
    g.fi = xalloc((fo[nf] + 1) * sizeof(int));
    if (nv) memcpy(g.p, p, nv * sizeof(V3));
    memcpy(g.fo, fo, (nf + 1) * sizeof(int));
    if (fo[nf]) memcpy(g.fi, fi, fo[nf] * sizeof(int));
    g.fc = xalloc((nf + 1) * sizeof(V3));
    for (int f = 0; f < nf; f++) g.fc[f] = v3(-1, -1, -1);
    g.ringn = 0;
    return g;
}

static int g_vert(G *g, V3 p)
{
    g->p = xgrow(g->p, (g->nv + 1) * sizeof(V3));
    g->p[g->nv] = p;
    return g->nv++;
}

static void g_face(G *g, const int *v, int n, V3 col)
{
    g->fo = xgrow(g->fo, (g->nf + 2) * sizeof(int));
    g->fi = xgrow(g->fi, (g->fo[g->nf] + n + 1) * sizeof(int));
    memcpy(g->fi + g->fo[g->nf], v, n * sizeof(int));
    g->fc = xgrow(g->fc, (g->nf + 1) * sizeof(V3));
    g->fc[g->nf] = col;
    g->fo[g->nf + 1] = g->fo[g->nf] + n;
    g->nf++;
}

/* Maya's polyCube vertex/face order for one subdivision (tweaks on it rely on it) */
static G g_cube(float w, float h, float d, int sw, int sh, int sd)
{
    if (sw <= 1 && sh <= 1 && sd <= 1) {
        float x = w / 2, y = h / 2, z = d / 2;
        V3 p[8] = { { -x, -y, z }, { x, -y, z }, { -x, y, z }, { x, y, z }, { -x, y, -z }, { x, y, -z }, { -x, -y, -z }, { x, -y, -z } };
        int fo[7] = { 0, 4, 8, 12, 16, 20, 24 };
        int fi[24] = { 0, 1, 3, 2, 2, 3, 5, 4, 4, 5, 7, 6, 6, 7, 1, 0, 1, 7, 5, 3, 6, 0, 2, 4 };
        return g_new(8, p, 6, fo, fi);
    }
    Prim pr;
    prim_defaults(&pr, PRIM_CUBE);
    pr.w = w; pr.h = h; pr.d = d; pr.sw = sw; pr.sh = sh; pr.sd = sd;
    Mesh m = mesh_prim(&pr);
    G g = g_new(m.nv, m.p, m.nf, m.fo, m.fi);
    mesh_free(&m);
    return g;
}

static G g_from_prim(Prim *pr)
{
    Mesh m = mesh_prim(pr);
    G g = g_new(m.nv, m.p, m.nf, m.fo, m.fi);
    mesh_free(&m);
    return g;
}

/* edge key -> index map built on demand (sorted pairs) */
typedef struct { int a, b, f0, f1, idx; } Edge;

static int ecmp(const void *x, const void *y)
{
    const Edge *p = x, *q = y;
    return p->a != q->a ? p->a - q->a : p->b - q->b;
}

static Edge *g_edges(const G *g, int *ne)
{
    int n = g->fo[g->nf];
    Edge *e = xalloc((n + 1) * sizeof(Edge)), *out = xalloc((n + 1) * sizeof(Edge));
    int k = 0;
    for (int f = 0; f < g->nf; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int a = g->fi[j], b = g->fi[j + 1 < g->fo[f + 1] ? j + 1 : g->fo[f]];
            e[k].a = a < b ? a : b; e[k].b = a < b ? b : a; e[k].f0 = f; e[k].f1 = -1; e[k].idx = k;
            k++;
        }
    qsort(e, k, sizeof(Edge), ecmp);
    int m = 0;
    for (int i = 0; i < k; i++) {
        if (m && out[m - 1].a == e[i].a && out[m - 1].b == e[i].b) { out[m - 1].f1 = e[i].f0; continue; }
        out[m++] = e[i];
    }
    free(e);
    *ne = m;
    return out;
}

static int edge_find(Edge *e, int ne, int a, int b)
{
    Edge key = { a < b ? a : b, a < b ? b : a, 0, 0, 0 };
    Edge *r = bsearch(&key, e, ne, sizeof(Edge), ecmp);
    return r ? (int)(r - e) : -1;
}

/* one Catmull-Clark level: corner points, then edge points, then face points; the subface at
   corner k of face f is face 4f+k for quads (this matches Maya's face numbering) */
static G g_smooth(const G *g)
{
    int ne;
    Edge *e = g_edges(g, &ne);
    int nv = g->nv, nf = g->nf;
    V3 *fp = xalloc((nf + 1) * sizeof(V3)), *ep = xalloc((ne + 1) * sizeof(V3)), *vp = xalloc((nv + 1) * sizeof(V3));
    for (int f = 0; f < nf; f++) {
        V3 s = v3(0, 0, 0);
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) s = v3add(s, g->p[g->fi[j]]);
        fp[f] = v3mul(s, 1.0f / (g->fo[f + 1] - g->fo[f]));
    }
    int *val = xalloc((nv + 1) * sizeof(int)), *bord = xalloc((nv + 1) * sizeof(int));
    V3 *Rsum = xalloc((nv + 1) * sizeof(V3)), *Qsum = xalloc((nv + 1) * sizeof(V3)), *Bsum = xalloc((nv + 1) * sizeof(V3));
    int *nfv = xalloc((nv + 1) * sizeof(int));
    for (int i = 0; i < ne; i++) {
        V3 mid = v3mul(v3add(g->p[e[i].a], g->p[e[i].b]), 0.5f);
        ep[i] = e[i].f1 >= 0 ? v3mul(v3add(v3add(g->p[e[i].a], g->p[e[i].b]), v3add(fp[e[i].f0], fp[e[i].f1])), 0.25f) : mid;
        int ends[2] = { e[i].a, e[i].b };
        for (int k = 0; k < 2; k++) {
            int v = ends[k];
            val[v]++;
            Rsum[v] = v3add(Rsum[v], mid);
            if (e[i].f1 < 0) { bord[v]++; Bsum[v] = v3add(Bsum[v], mid); }
        }
    }
    for (int f = 0; f < nf; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) { Qsum[g->fi[j]] = v3add(Qsum[g->fi[j]], fp[f]); nfv[g->fi[j]]++; }
    for (int v = 0; v < nv; v++) {
        if (bord[v] == 2) vp[v] = v3add(v3mul(g->p[v], 0.5f), v3mul(Bsum[v], 0.25f));
        else if (bord[v] || !nfv[v] || !val[v]) vp[v] = g->p[v];
        else {
            float n = (float)nfv[v];
            V3 Q = v3mul(Qsum[v], 1 / n), R = v3mul(Rsum[v], 1.0f / val[v]);
            vp[v] = v3mul(v3add(v3add(Q, v3mul(R, 2)), v3mul(g->p[v], n - 3)), 1 / n);
        }
    }
    V3 *P = xalloc((nv + ne + nf + 1) * sizeof(V3));
    memcpy(P, vp, nv * sizeof(V3));
    memcpy(P + nv, ep, ne * sizeof(V3));
    memcpy(P + nv + ne, fp, nf * sizeof(V3));
    G out = g_new(nv + ne + nf, P, 0, (int[]){ 0 }, NULL);
    for (int f = 0; f < nf; f++) {
        int s = g->fo[f], n = g->fo[f + 1] - s;
        for (int k = 0; k < n; k++) {
            int c = g->fi[s + k], nx = g->fi[s + (k + 1) % n], pv = g->fi[s + (k - 1 + n) % n];
            int q[4] = { c, nv + edge_find(e, ne, c, nx), nv + ne + f, nv + edge_find(e, ne, pv, c) };
            g_face(&out, q, 4, g->fc[f]);
        }
    }
    free(P); free(fp); free(ep); free(vp); free(val); free(bord); free(Rsum); free(Qsum); free(Bsum); free(nfv); free(e);
    return out;
}

/* chip off (dup == 0: detach the faces, splitting their border vertices; else copy them) */
static void g_chipoff(G *g, const Comps *c, int dup)
{
    unsigned char *sel = xalloc(g->nf + 1), *used = xalloc(g->nv + 1);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->nf) sel[c->idx[i]] = 1;
    int nf0 = g->nf;
    if (dup) {
        int *map = xalloc((g->nv + 1) * sizeof(int));
        for (int v = 0; v < g->nv; v++) map[v] = -1;
        for (int f = 0; f < nf0; f++) {
            if (!sel[f]) continue;
            int n = g->fo[f + 1] - g->fo[f], q[64];
            for (int j = 0; j < n && j < 64; j++) {
                int v = g->fi[g->fo[f] + j];
                if (map[v] < 0) map[v] = g_vert(g, g->p[v]);
                q[j] = map[v];
            }
            g_face(g, q, n < 64 ? n : 64, g->fc[f]);
        }
        free(map);
    } else {
        for (int f = 0; f < nf0; f++)
            if (!sel[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) used[g->fi[j]] = 1;
        int *map = xalloc((g->nv + 1) * sizeof(int));
        int nv0 = g->nv;
        for (int v = 0; v < nv0; v++) map[v] = -1;
        for (int f = 0; f < nf0; f++) {
            if (!sel[f]) continue;
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
                int v = g->fi[j];
                if (v < nv0 && used[v]) {
                    if (map[v] < 0) map[v] = g_vert(g, g->p[v]);
                    g->fi[j] = map[v];
                }
            }
        }
        free(map);
    }
    free(sel); free(used);
}

/* keep only faces with keep[f]; vertices keep their relative order */
static G g_subset(const G *g, const unsigned char *keep)
{
    int *map = xalloc((g->nv + 1) * sizeof(int)), nv = 0;
    unsigned char *uv = xalloc(g->nv + 1);
    for (int f = 0; f < g->nf; f++)
        if (keep[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) uv[g->fi[j]] = 1;
    for (int v = 0; v < g->nv; v++) map[v] = uv[v] ? nv++ : -1;
    V3 *p = xalloc((nv + 1) * sizeof(V3));
    for (int v = 0; v < g->nv; v++) if (map[v] >= 0) p[map[v]] = g->p[v];
    G out = g_new(nv, p, 0, (int[]){ 0 }, NULL);
    for (int f = 0; f < g->nf; f++) {
        if (!keep[f]) continue;
        int n = g->fo[f + 1] - g->fo[f], q[256];
        if (n > 256) n = 256;
        for (int j = 0; j < n; j++) q[j] = map[g->fi[g->fo[f] + j]];
        g_face(&out, q, n, g->fc[f]);
    }
    free(map); free(uv); free(p);
    return out;
}

/* number of shells, and each face's shell (shells numbered in order of their first face) */
static int g_shell_ids(const G *g, int *sid)
{
    int *par = xalloc((g->nv + 1) * sizeof(int)), *num = xalloc((g->nv + 1) * sizeof(int)), shells = 0;
    for (int v = 0; v < g->nv; v++) { par[v] = v; num[v] = -1; }
    for (int f = 0; f < g->nf; f++)
        for (int j = g->fo[f] + 1; j < g->fo[f + 1]; j++) {
            int a = g->fi[g->fo[f]], b = g->fi[j];
            while (par[a] != a) a = par[a];
            while (par[b] != b) b = par[b];
            par[b] = a;
        }
    for (int f = 0; f < g->nf; f++) {
        int a = g->fi[g->fo[f]];
        while (par[a] != a) a = par[a];
        if (num[a] < 0) num[a] = shells++;
        sid[f] = num[a];
    }
    free(par); free(num);
    return shells;
}

/* shell k, or with rest shells k and up (a separate with fewer outputs than shells leaves the
   remaining shells together in its last output) */
static G g_shell(const G *g, int k, int rest)
{
    int *sid = xalloc((g->nf + 1) * sizeof(int));
    g_shell_ids(g, sid);
    unsigned char *keep = xalloc(g->nf + 1);
    for (int f = 0; f < g->nf; f++) keep[f] = rest ? sid[f] >= k : sid[f] == k;
    G out = g_subset(g, keep);
    free(sid); free(keep);
    return out;
}

/* extrude every open border edge (what an extrude of a whole border does); the new vertices
   become the current ring, ready for a tweak to shape them */
static void g_extrude_border(G *g, V3 col)
{
    int ne;
    Edge *e = g_edges(g, &ne);
    int *nv = xalloc((g->nv + 1) * sizeof(int));
    for (int v = 0; v < g->nv; v++) nv[v] = -1;
    int start = g->nv, nf0 = g->nf;
    for (int i = 0; i < ne; i++)
        if (e[i].f1 < 0) {
            if (nv[e[i].a] < 0) nv[e[i].a] = 0;
            if (nv[e[i].b] < 0) nv[e[i].b] = 0;
        }
    int nv0 = g->nv;
    for (int v = 0; v < nv0; v++) if (nv[v] == 0) nv[v] = g_vert(g, g->p[v]);
    for (int f = 0; f < nf0; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int a = g->fi[j], b = g->fi[j + 1 < g->fo[f + 1] ? j + 1 : g->fo[f]];
            int k = edge_find(e, ne, a, b);
            if (k < 0 || e[k].f1 >= 0) continue;
            int q[4] = { b, a, nv[a], nv[b] };
            g_face(g, q, 4, col);
        }
    g->ring0 = start;
    g->ringn = g->nv - start;
    free(nv); free(e);
}

/* extrude a region of faces: copies of their vertices (in face order) become the new cap */
static void g_extrude_faces(G *g, const Comps *c)
{
    unsigned char *sel = xalloc(g->nf + 1);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->nf) sel[c->idx[i]] = 1;
    int ne;
    Edge *e = g_edges(g, &ne);
    int *map = xalloc((g->nv + 1) * sizeof(int));
    for (int v = 0; v < g->nv; v++) map[v] = -1;
    int nf0 = g->nf, nv0 = g->nv;
    for (int f = 0; f < nf0; f++)
        if (sel[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (map[g->fi[j]] < 0) map[g->fi[j]] = g_vert(g, g->p[g->fi[j]]);
    for (int f = 0; f < nf0; f++) {
        if (!sel[f]) continue;
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int a = g->fi[j], b = g->fi[j + 1 < g->fo[f + 1] ? j + 1 : g->fo[f]];
            int k = edge_find(e, ne, a < nv0 ? a : a, b);
            int other = k < 0 ? -1 : (e[k].f0 == f ? e[k].f1 : e[k].f0);
            if (other >= 0 && sel[other]) continue;
            int q[4] = { a, b, map[b], map[a] };
            g_face(g, q, 4, g->fc[f]);
        }
    }
    for (int f = 0; f < nf0; f++)
        if (sel[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) g->fi[j] = map[g->fi[j]];
    free(sel); free(e); free(map);
}

static void g_delete_faces(G *g, const Comps *c)
{
    unsigned char *keep = xalloc(g->nf + 1);
    memset(keep, 1, g->nf);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->nf) keep[c->idx[i]] = 0;
    G out = g_subset(g, keep);
    out.ring0 = g->ring0; out.ringn = 0; out.ringsign = g->ringsign;
    g_free(g);
    *g = out;
    free(keep);
}

/* Fit tweak vectors onto the current ring: each goes to the ring vertex whose direction from the
   ring's center it lines up with best; sign picks inward (-1) or outward (+1) offsets. */
static void g_fit_ring(G *g, const Tweaks *t, float sign)
{
    int n = g->ringn, r0 = g->ring0;
    V3 c = v3(0, 0, 0);
    for (int i = 0; i < n; i++) c = v3add(c, g->p[r0 + i]);
    c = v3mul(c, 1.0f / n);
    unsigned char *taken = xalloc(n + 1);
    V3 *np = xalloc((n + 1) * sizeof(V3));
    for (int i = 0; i < n; i++) np[i] = g->p[r0 + i];
    for (int k = 0; k < t->n; k++) {
        V3 tv = t->v[k];
        float tl = sqrtf(tv.x * tv.x + tv.z * tv.z), best = -1e30f;
        int bi = -1;
        for (int i = 0; i < n; i++) {
            if (taken[i]) continue;
            V3 d = v3sub(g->p[r0 + i], c);
            float dl = sqrtf(d.x * d.x + d.z * d.z), s = tl > 1e-9f && dl > 1e-9f ? sign * (d.x * tv.x + d.z * tv.z) / (dl * tl) : 0;
            if (s > best) { best = s; bi = i; }
        }
        if (bi < 0) break;
        taken[bi] = 1;
        np[bi] = v3add(np[bi], tv);
    }
    for (int i = 0; i < n; i++) g->p[r0 + i] = np[i];
    free(taken); free(np);
}

static int tweaks_cover_ring(const G *g, const Tweaks *t)
{
    if (!g->ringn || t->n != g->ringn) return 0;
    for (int k = 0; k < t->n; k++) if (t->i[k] < g->ring0 || t->i[k] >= g->ring0 + g->ringn) return 0;
    return 1;
}

static void g_xform(G *g, const M4 *m)
{
    for (int v = 0; v < g->nv; v++) g->p[v] = m4_pt(*m, g->p[v]);
    if (m->m[0] * (m->m[5] * m->m[10] - m->m[6] * m->m[9]) - m->m[4] * (m->m[1] * m->m[10] - m->m[2] * m->m[9]) +
        m->m[8] * (m->m[1] * m->m[6] - m->m[2] * m->m[5]) < 0)
        for (int f = 0; f < g->nf; f++)   /* mirrored: keep faces facing out */
            for (int a = g->fo[f], b = g->fo[f + 1] - 1; a < b; a++, b--) { int x = g->fi[a]; g->fi[a] = g->fi[b]; g->fi[b] = x; }
}

static void g_append(G *g, const G *s)
{
    int base = g->nv;
    for (int v = 0; v < s->nv; v++) g_vert(g, s->p[v]);
    for (int f = 0; f < s->nf; f++) {
        int n = s->fo[f + 1] - s->fo[f], q[256];
        if (n > 256) n = 256;
        for (int j = 0; j < n; j++) q[j] = s->fi[s->fo[f] + j] + base;
        g_face(g, q, n, s->fc[f]);
    }
}

/* merge the listed vertices that are close together (to their average), dropping the extras */
static void g_merge(G *g, const Comps *c, float dist)
{
    if (c->n < 2) return;
    V3 lo = v3(1e30f, 1e30f, 1e30f), hi = v3(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < c->n; i++) {
        if (c->idx[i] >= g->nv) continue;
        V3 p = g->p[c->idx[i]];
        lo = v3(fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z));
        hi = v3(fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z));
    }
    float tol = fmaxf(dist, v3len(v3sub(hi, lo)) * 0.02f);
    int *to = xalloc((g->nv + 1) * sizeof(int));
    for (int v = 0; v < g->nv; v++) to[v] = v;
    for (int i = 0; i < c->n; i++) {
        int a = c->idx[i];
        if (a >= g->nv || to[a] != a) continue;
        V3 sum = g->p[a];
        int cnt = 1;
        for (int j = i + 1; j < c->n; j++) {
            int b = c->idx[j];
            if (b >= g->nv || to[b] != b || v3len(v3sub(g->p[a], g->p[b])) > tol) continue;
            to[b] = a;
            sum = v3add(sum, g->p[b]);
            cnt++;
        }
        g->p[a] = v3mul(sum, 1.0f / cnt);
    }
    int *map = xalloc((g->nv + 1) * sizeof(int)), nv = 0;
    for (int v = 0; v < g->nv; v++) map[v] = to[v] == v ? nv++ : -1;
    for (int v = 0; v < g->nv; v++) if (to[v] == v) g->p[map[v]] = g->p[v];
    for (int j = 0; j < g->fo[g->nf]; j++) g->fi[j] = map[to[g->fi[j]]];
    g->nv = nv;
    free(to); free(map);
}

/* A mesh stored as data (MESH chunk: a shape saved without history, or a cached copy):
   [n][n/3 points as float xyz] [n][edge vertex pairs, top bit = hard edge]
   [n][face loops as edge numbers: bit 31 = edge used backwards, bit 29 = last edge of the face,
   bit 30 = last edge of a loop (a face's later loops are holes)]
   [0][uv set count]{ [0] name\0 [n][floats] [n][indices] }
   [color set count]{ [0] name\0 [channels] [n][floats] [n][indices per face corner, -1 = none] } */
static int g_from_data(const Attr *a, G *out)
{
    const uint8_t *v = a->v, *e = a->v + a->n;
#define NEED(k) if (e - v < (ptrdiff_t)(k)) goto bad
    NEED(4);
    uint32_t np = be32(v) / 3;
    v += 4;
    NEED((size_t)np * 12);
    V3 *P = xalloc((np + 1) * sizeof(V3));
    for (uint32_t i = 0; i < np; i++, v += 12) P[i] = v3(beflt(v), beflt(v + 4), beflt(v + 8));
    NEED(4);
    uint32_t ne = be32(v) / 2;
    v += 4;
    NEED((size_t)ne * 8);
    int *E = xalloc((ne + 1) * 2 * sizeof(int));
    for (uint32_t i = 0; i < ne; i++, v += 8) { E[2 * i] = (int)(be32(v) & 0x7fffffff); E[2 * i + 1] = (int)(be32(v + 4) & 0x7fffffff); }
    NEED(4);
    uint32_t nl = be32(v);
    v += 4;
    NEED((size_t)nl * 4);
    *out = g_new((int)np, P, 0, (int[]){ 0 }, NULL);
    free(P);
    int *q = xalloc((nl + 1) * sizeof(int)), nq = 0, hole = 0, corners = 0;
    int *corner_face = xalloc((nl + 1) * sizeof(int));   /* face of each face corner, for colors */
    for (uint32_t i = 0; i < nl; i++, v += 4) {
        uint32_t x = be32(v), k = x & 0x1fffffff;
        int vert = k < ne ? E[2 * k + (x >> 31)] : 0;
        if (vert >= (int)np) vert = 0;
        corner_face[corners++] = out->nf;
        if (!hole) q[nq++] = vert;   /* holes are dropped: tuff3d faces are simple polygons */
        if (x & 0x40000000u) hole = 1;
        if (x & 0x20000000u) {
            if (nq >= 3) g_face(out, q, nq, v3(-1, -1, -1));
            else for (int c = corners - 1; c >= 0 && corner_face[c] == out->nf; c--) corner_face[c] = -1;
            nq = 0; hole = 0;
        }
    }
    free(q); free(E);
    /* uv sets are skipped; the first color set gives per face colors */
    if (e - v >= 8) {
        uint32_t nuv = be32(v + 4);
        v += 8;
        for (uint32_t s = 0; s < nuv && s < 64; s++) {
            NEED(4); v += 4;
            while (v < e && *v) v++;
            v++;
            NEED(4); v += 4 + (size_t)be32(v) * 4;
            NEED(4); v += 4 + (size_t)be32(v) * 4;
        }
        if (e - v >= 4 && be32(v) > 0) {
            v += 4;
            NEED(4); v += 4;
            while (v < e && *v) v++;
            v++;
            NEED(8);
            uint32_t ch = be32(v), nf = be32(v + 4);
            v += 8;
            NEED((size_t)nf * 4);
            const uint8_t *cols = v;
            v += (size_t)nf * 4;
            NEED(4);
            uint32_t ni = be32(v);
            v += 4;
            NEED((size_t)ni * 4);
            if (ch >= 3 && ch <= 4) {
                V3 *sum = xalloc((out->nf + 1) * sizeof(V3));
                int *cnt = xalloc((out->nf + 1) * sizeof(int));
                for (uint32_t i = 0; i < ni && (int)i < corners; i++) {
                    int32_t k = (int32_t)be32(v + 4 * i), f = corner_face[i];
                    if (k < 0 || f < 0 || f >= out->nf || (uint32_t)(k + 1) * ch > nf) continue;
                    const uint8_t *c = cols + (size_t)k * ch * 4;
                    sum[f] = v3add(sum[f], v3(beflt(c), beflt(c + 4), beflt(c + 8)));
                    cnt[f]++;
                }
                for (int f = 0; f < out->nf; f++) if (cnt[f]) out->fc[f] = v3mul(sum[f], 1.0f / cnt[f]);
                free(sum); free(cnt);
            }
        }
    }
    free(corner_face);
    return out->nf > 0;
bad:
#undef NEED
    return out->nf > 0;
}

/* ---------------------------------------------------------------- transforms */

static M4 local_matrix(const Node *n)
{
    V3 t = attr_v3(n, "t", v3(0, 0, 0)), r = attr_v3(n, "r", v3(0, 0, 0)), s = attr_v3(n, "s", v3(1, 1, 1));
    V3 rp = attr_v3(n, "rp", v3(0, 0, 0)), sp = attr_v3(n, "sp", v3(0, 0, 0));
    V3 rpt = attr_v3(n, "rpt", v3(0, 0, 0)), spt = attr_v3(n, "spt", v3(0, 0, 0));
    /* Maya: p' = ((p - sp) S + sp + spt - rp) R + rp + rpt + t ; rotation is stored in radians */
    const float D = 57.29577951f;
    M4 S = m4_trs(v3(0, 0, 0), v3(0, 0, 0), s), R = m4_euler(v3(r.x * D, r.y * D, r.z * D));
    M4 a = m4_trs(v3mul(sp, -1), v3(0, 0, 0), v3(1, 1, 1));
    M4 b = m4_trs(v3sub(v3add(sp, spt), rp), v3(0, 0, 0), v3(1, 1, 1));
    M4 c = m4_trs(v3add(v3add(rp, rpt), t), v3(0, 0, 0), v3(1, 1, 1));
    return m4_mul(c, m4_mul(R, m4_mul(b, m4_mul(S, a))));
}

static M4 world_matrix(Doc *D, int node)
{
    M4 m = m4_id();
    for (int i = node; i >= 0; i = D->nd[i].pidx)
        if (node_type(&D->nd[i]) == T_TRANSFORM) m = m4_mul(local_matrix(&D->nd[i]), m);
    return m;
}

/* ---------------------------------------------------------------- history replay */

typedef struct {
    Doc *D;
    int depth, steps, debug;
    char skipped[256];
    struct { int node; char plug[32]; G g; } *memo;   /* results by node and output plug */
    int nmemo;
} Ctx;

static void skip_note(Ctx *x, const char *what)
{
    if (strstr(x->skipped, what)) return;
    size_t l = strlen(x->skipped);
    snprintf(x->skipped + l, sizeof x->skipped - l, "%s%s", l ? ", " : "", what);
}

static int eval(Ctx *x, int node, const char *plug, G *out);

static int eval_in(Ctx *x, int node, const char *at, G *out)
{
    char sp[160];
    int src = input(x->D, node, at, sp);
    return src >= 0 && eval(x, src, sp, out);
}

/* the sign (inward/outward) for fitting tweak t on ring g: compare the ring's size with the
   bounding box the next extrude recorded (cbn/cbx), when there is one */
static float ring_sign(Ctx *x, int node, G *g, const Tweaks *t)
{
    int ext = consumer(x->D, node, "out");
    for (int hops = 0; ext >= 0 && hops < 4; hops++) {
        char op[64];
        op_name(&x->D->nd[ext], op);
        if (!strncmp(op, "polyExtrude", 11)) break;
        ext = consumer(x->D, ext, "og");
    }
    if (ext >= 0 && attr(&x->D->nd[ext], "cbx") && attr(&x->D->nd[ext], "cbn")) {
        const Node *e = &x->D->nd[ext];
        V3 bmin = attr_v3(e, "cbn", v3(0, 0, 0)), bmax = attr_v3(e, "cbx", v3(0, 0, 0));
        float want = bmax.x - bmin.x, best = 1e30f, sign = -1;
        for (int s = -1; s <= 1; s += 2) {
            G tmp = g_copy(g);
            g_fit_ring(&tmp, t, (float)s);
            M4 ix = m4_id();
            const Attr *a = attr(e, "ix");
            if (a && a->n >= 128) for (int i = 0; i < 16; i++) ix.m[i] = (float)bedbl(a->v + 8 * i);
            float lo = 1e30f, hi = -1e30f;
            for (int i = 0; i < tmp.ringn; i++) {
                V3 p = m4_pt(ix, tmp.p[tmp.ring0 + i]);
                lo = fminf(lo, p.x); hi = fmaxf(hi, p.x);
            }
            float err = fabsf((hi - lo) - want);
            if (err < best) { best = err; sign = (float)s; }
            g_free(&tmp);
        }
        return sign;
    }
    return g->ringsign ? g->ringsign : -1;
}

static void apply_tweaks(Ctx *x, int node, G *g, const Tweaks *t)
{
    if (tweaks_cover_ring(g, t)) {
        float s = ring_sign(x, node, g, t);
        g_fit_ring(g, t, s);
        g->ringsign = s;
        return;
    }
    for (int k = 0; k < t->n; k++)
        if (t->i[k] >= 0 && t->i[k] < g->nv) g->p[t->i[k]] = v3add(g->p[t->i[k]], t->v[k]);
}

/* vertex colors: per face colors from the per face-vertex entries vclr[v].vfcl[f].vfc? */
static void color_faces(const Node *nd, G *g)
{
    for (int k = 0; k < nd->na; k++) {
        const Attr *a = &nd->a[k];
        int v, f;
        char ch;
        if (sscanf(a->name, "vclr[%d].vfcl[%d].vfc%c", &v, &f, &ch) != 3 || f < 0 || f >= g->nf || a->n < 8) continue;
        if (g->fc[f].x < 0) g->fc[f] = v3(1, 1, 1);
        float val = (float)bedbl(a->v);
        if (ch == 'r') g->fc[f].x = val;
        if (ch == 'g') g->fc[f].y = val;
        if (ch == 'b') g->fc[f].z = val;
    }
}

/* Maya vertex number -> ours, from a vertex color node that lists the faces around each vertex */
static int *vertex_map(const Node *cpv, const G *g, int *count)
{
    int nm = 0;
    for (int k = 0; k < cpv->na; k++) {
        int v, f;
        if (sscanf(cpv->a[k].name, "vclr[%d].vfcl[%d]", &v, &f) == 2 && v + 1 > nm) nm = v + 1;
    }
    if (!nm) return NULL;
    /* face sets, as bitsets over faces */
    int words = (g->nf + 63) / 64;
    uint64_t *ms = xalloc((size_t)nm * words * 8), *ours = xalloc((size_t)g->nv * words * 8);
    for (int k = 0; k < cpv->na; k++) {
        int v, f;
        if (sscanf(cpv->a[k].name, "vclr[%d].vfcl[%d]", &v, &f) == 2 && f < g->nf) ms[(size_t)v * words + f / 64] |= 1ull << (f % 64);
    }
    for (int f = 0; f < g->nf; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) ours[(size_t)g->fi[j] * words + f / 64] |= 1ull << (f % 64);
    int *map = xalloc((nm + 1) * sizeof(int));
    unsigned char *used = xalloc(g->nv + 1);
    for (int pass = 0; pass < 2; pass++)
        for (int v = 0; v < nm; v++) {
            if (pass && map[v] >= 0) continue;
            if (!pass) map[v] = -1;
            int best = -1, bs = pass ? 0 : -1;
            for (int u = 0; u < g->nv; u++) {
                if (used[u]) continue;
                int same = 1, inter = 0;
                for (int w = 0; w < words; w++) {
                    uint64_t a = ms[(size_t)v * words + w], b = ours[(size_t)u * words + w];
                    if (a != b) same = 0;
                    inter += __builtin_popcountll(a & b);
                }
                if (!pass && same) { best = u; break; }
                if (pass && inter > bs) { bs = inter; best = u; }
            }
            if (best >= 0) { map[v] = best; used[best] = 1; }
        }
    free(ms); free(ours); free(used);
    *count = nm;
    return map;
}

/* the polyColorPerVertex feeding a mesh shape, if its history ends with one */
static int color_node_before(Ctx *x, int shape)
{
    int n = input(x->D, shape, "i", NULL);
    for (int hops = 0; n >= 0 && hops < 8; hops++) {
        char op[64];
        op_name(&x->D->nd[n], op);
        if (!strcmp(op, "polyColorPerVertex")) return n;
        if (strcmp(op, "groupParts") && strcmp(op, "polySoftEdge") && strcmp(op, "polyNormal")) return -1;
        int nx = input(x->D, n, "ig", NULL);
        if (nx < 0) nx = input(x->D, n, "ip", NULL);
        n = nx;
    }
    return -1;
}

static int eval_op(Ctx *x, int node, const char *plug, G *out);

static M4 attr_m4(const Node *n, const char *name)
{
    M4 m = m4_id();
    const Attr *a = attr(n, name);
    if (a && a->n >= 128) for (int i = 0; i < 16; i++) m.m[i] = (float)bedbl(a->v + 8 * i);
    return m;
}

/* place the new cap of an extrusion: local translate along the face normals (lt, plus thickness
   tk), world translate (t) and world scale about the pivot (s), local scale (ls) about each cap's
   center. Distances are in world space; ix is the object's world matrix at the time. */
static void move_extruded(const Node *nd, G *g, const Comps *c, int first)
{
    V3 lt = attr_v3(nd, "lt", v3(0, 0, 0)), t = attr_v3(nd, "t", v3(0, 0, 0)), sc = attr_v3(nd, "s", v3(1, 1, 1));
    V3 ls = attr_v3(nd, "ls", v3(1, 1, 1)), pvt = attr_v3(nd, "pvt", v3(0, 0, 0));
    lt.z += (float)attr_d(nd, "tk", 0);
    if (g->nv <= first) return;
    int n = g->nv - first;
    M4 ix = attr_m4(nd, "ix"), inv = m4_inv(ix);
    V3 *nrm = xalloc((n + 1) * sizeof(V3)), *w = xalloc((n + 1) * sizeof(V3));
    for (int i = 0; i < n; i++) w[i] = m4_pt(ix, g->p[first + i]);
    for (int i = 0; i < c->n; i++) {
        int f = c->idx[i];
        if (f < 0 || f >= g->nf) continue;
        V3 fn = v3(0, 0, 0);
        int a = g->fo[f], b = g->fo[f + 1];
        for (int j = a; j < b; j++) {
            int u = g->fi[j], v = g->fi[j + 1 < b ? j + 1 : a];
            if (u < first || v < first) continue;
            fn = v3add(fn, v3cross(w[u - first], w[v - first]));
        }
        for (int j = a; j < b; j++) if (g->fi[j] >= first) nrm[g->fi[j] - first] = v3add(nrm[g->fi[j] - first], fn);
    }
    V3 ctr = v3(0, 0, 0);
    for (int i = 0; i < n; i++) ctr = v3add(ctr, w[i]);
    ctr = v3mul(ctr, 1.0f / n);
    for (int i = 0; i < n; i++) {
        V3 p = w[i];
        if (ls.x != 1 || ls.y != 1) p = v3add(ctr, v3mul(v3sub(p, ctr), (ls.x + ls.y) * 0.5f));
        V3 nn = nrm[i];
        float l = v3len(nn);
        if (l > 1e-12f) p = v3add(p, v3mul(nn, lt.z / l));
        p = v3add(pvt, v3(sc.x * (p.x - pvt.x), sc.y * (p.y - pvt.y), sc.z * (p.z - pvt.z)));
        p = v3add(p, t);
        g->p[first + i] = m4_pt(inv, p);
    }
    free(nrm); free(w);
}

/* debugging aid: operations record the bounding box (cbn/cbx, world space) of the components
   they work on; compare it with the replayed mesh to see where numbering goes wrong */
static void check_box(Ctx *x, const Node *nd, const G *g, const Comps *c)
{
    if (!x->debug || !attr(nd, "cbn")) return;
    M4 ix = attr_m4(nd, "ix");
    V3 lo = v3(1e30f, 1e30f, 1e30f), hi = v3(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < c->n; i++) {
        int k = c->idx[i];
        int a = 0, b = 0;
        if (c->type == 'F' && k < g->nf) { a = g->fo[k]; b = g->fo[k + 1]; }
        for (int j = a; j < b; j++) {
            V3 p = m4_pt(ix, g->p[g->fi[j]]);
            lo = v3(fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z));
            hi = v3(fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z));
        }
        if (c->type == 'V' && k < g->nv) {
            V3 p = m4_pt(ix, g->p[k]);
            lo = v3(fminf(lo.x, p.x), fminf(lo.y, p.y), fminf(lo.z, p.z));
            hi = v3(fmaxf(hi.x, p.x), fmaxf(hi.y, p.y), fmaxf(hi.z, p.z));
        }
    }
    V3 wl = attr_v3(nd, "cbn", lo), wh = attr_v3(nd, "cbx", hi);
    float err = fmaxf(v3len(v3sub(lo, wl)), v3len(v3sub(hi, wh))), size = v3len(v3sub(wh, wl)) + 1e-6f;
    fprintf(stderr, "  [box %s: %s err %.4f (%.1f%%)  ours %.3f %.3f %.3f / %.3f %.3f %.3f  maya %.3f %.3f %.3f / %.3f %.3f %.3f]\n",
            nd->name, err < size * 0.01f ? "OK" : "MISMATCH", err, 100 * err / size, lo.x, lo.y, lo.z, hi.x, hi.y, hi.z, wl.x, wl.y, wl.z, wh.x, wh.y, wh.z);
}

/* debugging aid: a vertex color node lists the faces around each vertex; count the vertices whose
   face list differs from the replayed mesh */
static void check_topology(Ctx *x, const Node *cpv, const G *g)
{
    if (!x->debug) return;
    int nm = 0, bad = 0, first = -1;
    for (int k = 0; k < cpv->na; k++) {
        int v, f;
        if (sscanf(cpv->a[k].name, "vclr[%d].vfcl[%d]", &v, &f) == 2 && v + 1 > nm) nm = v + 1;
    }
    if (!nm) return;
    for (int v = 0; v < nm; v++) {
        int have = 0, want = 0, match = 0;
        for (int k = 0; k < cpv->na; k++) {
            int vv, f;
            if (sscanf(cpv->a[k].name, "vclr[%d].vfcl[%d]", &vv, &f) != 2 || vv != v) continue;
            /* each face lists three channels; count each face once via its r entry */
            if (!strstr(cpv->a[k].name, "vfcr")) continue;
            want++;
            if (v < g->nv && f < g->nf)
                for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (g->fi[j] == v) { match++; break; }
        }
        if (v < g->nv)
            for (int f = 0; f < g->nf; f++)
                for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (g->fi[j] == v) { have++; break; }
        if (want && (match != want || have != want)) { bad++; if (first < 0) first = v; }
    }
    fprintf(stderr, "  [topology %s: maya %d verts, ours %d verts %d faces, %d verts differ (first %d)]\n", cpv->name, nm, g->nv, g->nf, bad, first);
}

/* evaluate a node's output once; branches that share history reuse the result */
static int eval(Ctx *x, int node, const char *plug, G *out)
{
    for (int i = 0; i < x->nmemo; i++)
        if (x->memo[i].node == node && !strcmp(x->memo[i].plug, plug)) { *out = g_copy(&x->memo[i].g); return 1; }
    if (++x->depth > 400) { x->depth--; return 0; }
    int ok = eval_op(x, node, plug, out);
    x->depth--;
    if (x->debug)
        fprintf(stderr, "%*s%s.%s -> %s %d verts %d faces\n", x->depth, "", x->D->nd[node].name, plug, ok ? "ok" : "FAILED",
                ok ? out->nv : 0, ok ? out->nf : 0);
    if (ok) {
        x->memo = xgrow(x->memo, (x->nmemo + 1) * sizeof *x->memo);
        x->memo[x->nmemo].node = node;
        snprintf(x->memo[x->nmemo].plug, sizeof x->memo[x->nmemo].plug, "%s", plug);
        x->memo[x->nmemo++].g = g_copy(out);
    }
    return ok;
}

static int eval_op(Ctx *x, int node, const char *plug, G *out)
{
    Doc *D = x->D;
    const Node *nd = &D->nd[node];
    char op[64];
    op_name(nd, op);
    int ok = 0;
    memset(out, 0, sizeof *out);
    x->steps++;
    if (node_type(nd) == T_MESH) {
        /* a shape: its history output plus its own vertex tweaks (pt) */
        if (eval_in(x, node, "i", out)) {
            Tweaks t = tweaks(nd, "pt");
            int cpv = t.n ? color_node_before(x, node) : -1;
            int nmap = 0, *map = cpv >= 0 ? vertex_map(&D->nd[cpv], out, &nmap) : NULL;
            if (map) {
                for (int k = 0; k < t.n; k++) {
                    int v = t.i[k] >= 0 && t.i[k] < nmap ? map[t.i[k]] : -1;
                    if (v >= 0 && v < out->nv) out->p[v] = v3add(out->p[v], t.v[k]);
                }
                free(map);
            } else {
                apply_tweaks(x, node, out, &t);
            }
            tweaks_free(&t);
            ok = 1;
        } else {
            /* saved without history: the mesh itself is stored, as the output (o), or as the
               cached input (ci) that the shape's own tweaks (pt) still apply to */
            const Attr *m = NULL;
            for (int k = 0; k < nd->na && !m; k++) if (!strcmp(nd->a[k].tag, "MESH") && !strcmp(nd->a[k].name, "o")) m = &nd->a[k];
            for (int k = 0; k < nd->na && !m; k++) if (!strcmp(nd->a[k].tag, "MESH")) m = &nd->a[k];
            if (m && g_from_data(m, out)) {
                if (strcmp(m->name, "o")) {
                    Tweaks t = tweaks(nd, "pt");
                    for (int k = 0; k < t.n; k++)
                        if (t.i[k] >= 0 && t.i[k] < out->nv) out->p[t.i[k]] = v3add(out->p[t.i[k]], t.v[k]);
                    tweaks_free(&t);
                }
                ok = 1;
            } else {
                g_free(out);
                skip_note(x, "an object with no stored shape");
            }
        }
    } else if (!strcmp(op, "polyCube")) {
        *out = g_cube((float)attr_d(nd, "w", 1), (float)attr_d(nd, "h", 1), (float)attr_d(nd, "d", 1),
                      (int)attr_d(nd, "sw", 1), (int)attr_d(nd, "sh", 1), (int)attr_d(nd, "sd", 1));
        ok = 1;
    } else if (!strcmp(op, "polySphere") || !strcmp(op, "polyCylinder") || !strcmp(op, "polyPlane")) {
        Prim pr;
        if (op[4] == 'S') {
            prim_defaults(&pr, PRIM_SPHERE);
            pr.sr = (float)attr_d(nd, "r", 1); pr.scols = (int)attr_d(nd, "sa", 20); pr.srows = (int)attr_d(nd, "sh", 20);
        } else if (op[4] == 'C') {
            prim_defaults(&pr, PRIM_CYL);
            pr.cr = (float)attr_d(nd, "r", 1); pr.ch = (float)attr_d(nd, "h", 2);
            pr.csides = (int)attr_d(nd, "sa", 20); pr.chseg = (int)attr_d(nd, "sh", 1); pr.ccap = (int)attr_d(nd, "sc", 1);
        } else {
            prim_defaults(&pr, PRIM_PLANE);
            pr.pw = (float)attr_d(nd, "w", 1); pr.pd = (float)attr_d(nd, "h", 1);
            pr.psw = (int)attr_d(nd, "sw", 10); pr.psd = (int)attr_d(nd, "sh", 10);
        }
        *out = g_from_prim(&pr);
        ok = 1;
    } else if (!strcmp(op, "groupParts")) {
        ok = eval_in(x, node, "ig", out);
    } else if (!strcmp(op, "polySmoothFace")) {
        G in;
        if ((ok = eval_in(x, node, "ip", &in))) {
            int lv = (int)attr_d(nd, "dv", 1);
            for (int i = 0; i < lv && i < 4; i++) {
                G s = g_smooth(&in);
                g_free(&in);
                in = s;
            }
            *out = in;
        }
    } else if (!strcmp(op, "polyChipOff")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->nf);
            g_chipoff(out, &c, (int)attr_d(nd, "dup", 1));
            free(c.idx);
        }
    } else if (!strcmp(op, "polySeparate")) {
        G in;
        int k = 0;
        sscanf(plug, "out[%d]", &k);
        const Attr *no = attr(nd, "out");
        int nout = no && no->n >= 4 ? (int)be32(no->v) : 0;
        if ((ok = eval_in(x, node, "ip", &in))) { *out = g_shell(&in, k, nout > 0 && k == nout - 1); g_free(&in); }
    } else if (!strcmp(op, "polyExtrudeEdge")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            V3 col = v3(-1, -1, -1);
            g_extrude_border(out, col);
        }
    } else if (!strcmp(op, "polyExtrudeFace")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->nf);
            check_box(x, nd, out, &c);
            int first = out->nv;
            if (x->debug > 1) {
                for (int i = 0; i < c.n; i++) {
                    int f = c.idx[i];
                    fprintf(stderr, "    f%d:", f);
                    for (int j = out->fo[f]; j < out->fo[f + 1]; j++) fprintf(stderr, " %d", out->fi[j]);
                    fprintf(stderr, "\n");
                }
                for (int f = 0; f < out->nf; f++) { fprintf(stderr, "      all f%d:", f); for (int j = out->fo[f]; j < out->fo[f + 1]; j++) fprintf(stderr, " %d", out->fi[j]); fprintf(stderr, "\n"); }
            }
            g_extrude_faces(out, &c);
            move_extruded(nd, out, &c, first);
            out->ringn = 0;
            free(c.idx);
        }
    } else if (!strcmp(op, "polyTweak")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Tweaks t = tweaks(nd, "tk");
            apply_tweaks(x, node, out, &t);
            tweaks_free(&t);
        }
    } else if (!strcmp(op, "deleteComponent")) {
        if ((ok = eval_in(x, node, "ig", out))) {
            Comps c = comps(nd, "dc", out->nf);
            if (c.type == 'F') g_delete_faces(out, &c);
            else if (c.n) skip_note(x, "deleting edges/vertices");
            free(c.idx);
        }
    } else if (!strcmp(op, "polyMergeVert")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->nv);
            g_merge(out, &c, (float)attr_d(nd, "d", 0));
            out->ringn = 0;
            free(c.idx);
        }
    } else if (!strcmp(op, "polyUnite")) {
        *out = g_new(0, NULL, 0, (int[]){ 0 }, NULL);
        for (int k = 0; k < 64; k++) {
            char ip[16], im[16], sp[160];
            snprintf(ip, sizeof ip, "ip[%d]", k);
            snprintf(im, sizeof im, "im[%d]", k);
            G part;
            if (!eval_in(x, node, ip, &part)) break;
            int src = input(D, node, im, sp);
            if (src >= 0) {
                M4 w = world_matrix(D, src);
                g_xform(&part, &w);
            }
            g_append(out, &part);
            g_free(&part);
            ok = 1;
        }
    } else if (!strcmp(op, "polyColorPerVertex")) {
        if ((ok = eval_in(x, node, "ip", out))) { check_topology(x, nd, out); color_faces(nd, out); }
    } else {
        /* normals, UV and other non-shape operations pass their input through */
        static const char *const pass[] = { "polySoftEdge", "polyNormal", "polyAutoProj", "polyPlanarProj", "polyMapCut",
                                            "polyMapSew", "polyTweakUV", "polyMapDel", "polyNormalPerVertex", "polyFlipUV" };
        for (size_t i = 0; i < sizeof pass / sizeof *pass && !ok; i++)
            if (!strcmp(op, pass[i])) ok = eval_in(x, node, "ip", out);
        if (!ok) {
            skip_note(x, op);
            ok = eval_in(x, node, "ip", out) || eval_in(x, node, "ig", out);
        }
    }
    return ok;
}

/* ---------------------------------------------------------------- entry point */

int mlt_load(const char *path, Mesh **ms, char (**names)[32], char *report, int report_size)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (n <= 0 || n > (1L << 30)) { fclose(fp); return -1; }
    uint8_t *buf = xalloc((size_t)n + 64);
    if (fread(buf, 1, (size_t)n, fp) != (size_t)n) { fclose(fp); free(buf); return -1; }
    fclose(fp);
    Doc D;
    memset(&D, 0, sizeof D);
    D.d = buf;
    D.n = (size_t)n;
    *ms = NULL;
    *names = NULL;
    int count = -1;
    if (doc_parse(&D)) {
        Ctx x;
        memset(&x, 0, sizeof x);
        x.D = &D;
        x.debug = getenv("MLT_DEBUG") ? atoi(getenv("MLT_DEBUG")) : 0;
        count = 0;
        for (int i = 0; i < D.nn; i++) {
            const Node *nd = &D.nd[i];
            if (node_type(nd) != T_MESH || attr_d(nd, "io", 0) != 0) continue;   /* skip intermediate shapes */
            G g;
            if (!eval(&x, i, "o", &g) || !g.nf) continue;
            M4 w = world_matrix(&D, i);
            g_xform(&g, &w);
            /* per vertex color = average of the colored faces around it */
            V3 *col = xalloc((g.nv + 1) * sizeof(V3));
            int *cnt = xalloc((g.nv + 1) * sizeof(int)), any = 0;
            for (int f = 0; f < g.nf; f++) {
                if (g.fc[f].x < 0) continue;
                any = 1;
                for (int j = g.fo[f]; j < g.fo[f + 1]; j++) { col[g.fi[j]] = v3add(col[g.fi[j]], g.fc[f]); cnt[g.fi[j]]++; }
            }
            for (int v = 0; v < g.nv; v++) col[v] = cnt[v] ? v3mul(col[v], 1.0f / cnt[v]) : v3(1, 1, 1);
            *ms = xgrow(*ms, (count + 1) * sizeof(Mesh));
            *names = xgrow(*names, (count + 1) * sizeof **names);
            (*ms)[count] = mesh_from_arrays(g.nv, g.p, any ? col : NULL, g.nf, g.fo, g.fi);
            const Node *t = nd->pidx >= 0 ? &D.nd[nd->pidx] : nd;
            snprintf((*names)[count], 32, "%s", t->name);
            count++;
            free(col); free(cnt);
            g_free(&g);
        }
        for (int i = 0; i < x.nmemo; i++) g_free(&x.memo[i].g);
        free(x.memo);
        if (report)
            snprintf(report, report_size, "%d object(s) rebuilt from %d history step(s)%s%s", count, x.steps,
                     x.skipped[0] ? "; not supported yet: " : "", x.skipped);
    } else if (report) {
        snprintf(report, report_size, "not a Maya LT scene");
    }
    for (int i = 0; i < D.nn; i++) free(D.nd[i].a);
    free(D.nd); free(D.cn); free(buf);
    return count;
}
