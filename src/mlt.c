/* tuff3d - Maya LT scene (.mlt) reader.

   An .mlt file is Maya's 64-bit binary scene format (IFF style FOR8 groups) with light
   scrambling: chunk sizes are XOR-masked per chunk type, chunk tags are XOR-masked, node names
   are rotated and XOR-masked with a 10 byte cycle, attribute names are reversed and masked, and
   multi attribute paths are masked with one byte. The connection list is plain text.

   Shapes saved without history store their mesh, which is read as is. Otherwise Maya LT saves
   construction history rather than finished meshes, so the visible meshes are rebuilt by
   replaying the poly operations (primitives, smooth, chip off, separate, extrude, bridge, split,
   bevel, close border, delete, unite, merge, tweaks, vertex colors). Later steps refer to
   vertices, edges and faces by number, so the replay numbers them the way Maya does; the rules
   were worked out against the checks Maya leaves in the file (MLT_DEBUG=1 prints them). */
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
                if (q + 16 + s > end) s = end - q - 16;   /* a learned size key can be a few bytes off */
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

/* Maya's mesh layout: vertices, an ordered list of edges with a stored direction, and faces as
   loops of edges. Operations number new vertices, edges and faces in a fixed order, and later
   history steps (tweaks, component selections) refer to those numbers, so the replay keeps the
   same three lists in the same order. Faces are kept as vertex loops plus the edge leaving each
   corner; fc is a per face color (x < 0: none). */
typedef struct {
    int nv, ne, nf;
    V3 *p;
    int *ea, *eb;
    int *fo, *fi, *fe;
    V3 *fc;
    int *hk, hcap, hn;   /* edge lookup by vertex pair, covering edges [0, hn) */
} G;

static void g_hash_reset(G *g) { free(g->hk); g->hk = NULL; g->hcap = g->hn = 0; }

static void g_free(G *g)
{
    free(g->p); free(g->ea); free(g->eb); free(g->fo); free(g->fi); free(g->fe); free(g->fc); free(g->hk);
    memset(g, 0, sizeof *g);
}

static G g_new(int nv, const V3 *p)
{
    G g;
    memset(&g, 0, sizeof g);
    g.nv = nv;
    g.p = xalloc((nv + 1) * sizeof(V3));
    if (nv && p) memcpy(g.p, p, nv * sizeof(V3));
    g.ea = xalloc(sizeof(int)); g.eb = xalloc(sizeof(int));
    g.fo = xalloc(sizeof(int)); g.fi = xalloc(sizeof(int)); g.fe = xalloc(sizeof(int));
    g.fc = xalloc(sizeof(V3));
    return g;
}

static G g_copy(const G *s)
{
    G g = *s;
    int nc = s->fo[s->nf];
    g.p = xalloc((s->nv + 1) * sizeof(V3)); memcpy(g.p, s->p, s->nv * sizeof(V3));
    g.ea = xalloc((s->ne + 1) * sizeof(int)); memcpy(g.ea, s->ea, s->ne * sizeof(int));
    g.eb = xalloc((s->ne + 1) * sizeof(int)); memcpy(g.eb, s->eb, s->ne * sizeof(int));
    g.fo = xalloc((s->nf + 1) * sizeof(int)); memcpy(g.fo, s->fo, (s->nf + 1) * sizeof(int));
    g.fi = xalloc((nc + 1) * sizeof(int)); memcpy(g.fi, s->fi, nc * sizeof(int));
    g.fe = xalloc((nc + 1) * sizeof(int)); memcpy(g.fe, s->fe, nc * sizeof(int));
    g.fc = xalloc((s->nf + 1) * sizeof(V3)); memcpy(g.fc, s->fc, s->nf * sizeof(V3));
    g.hk = NULL; g.hcap = g.hn = 0;
    return g;
}

static int g_vert(G *g, V3 p)
{
    g->p = xgrow(g->p, (g->nv + 1) * sizeof(V3));
    g->p[g->nv] = p;
    return g->nv++;
}

static unsigned ekey(int a, int b)
{
    if (a > b) { int t = a; a = b; b = t; }
    return (unsigned)a * 2654435761u ^ (unsigned)b * 2246822519u;
}

static void g_hash_put(G *g, int e)
{
    unsigned m = (unsigned)g->hcap - 1, h = ekey(g->ea[e], g->eb[e]) & m;
    while (g->hk[h]) h = (h + 1) & m;
    g->hk[h] = e + 1;
}

/* the edge between a and b (the oldest one if there are several), or -1 */
static int g_find_edge(G *g, int a, int b)
{
    if ((g->ne + 1) * 2 > g->hcap) {
        int cap = 64;
        while (cap < (g->ne + 1) * 4) cap *= 2;
        free(g->hk);
        g->hk = xalloc(cap * sizeof(int));
        g->hcap = cap;
        g->hn = 0;
    }
    while (g->hn < g->ne) g_hash_put(g, g->hn++);
    unsigned m = (unsigned)g->hcap - 1, h = ekey(a, b) & m;
    int best = -1;
    for (; g->hk[h]; h = (h + 1) & m) {
        int e = g->hk[h] - 1;
        if (((g->ea[e] == a && g->eb[e] == b) || (g->ea[e] == b && g->eb[e] == a)) && (best < 0 || e < best)) best = e;
    }
    return best;
}

static int g_add_edge(G *g, int a, int b)
{
    g->ea = xgrow(g->ea, (g->ne + 1) * sizeof(int));
    g->eb = xgrow(g->eb, (g->ne + 1) * sizeof(int));
    g->ea[g->ne] = a; g->eb[g->ne] = b;
    return g->ne++;
}

static int g_edge(G *g, int a, int b)
{
    int e = g_find_edge(g, a, b);
    return e >= 0 ? e : g_add_edge(g, a, b);
}

/* append a face; edges are looked up, or created in corner order; e may give them instead */
static int g_face(G *g, const int *v, const int *e, int n, V3 col)
{
    int base = g->fo[g->nf];
    g->fo = xgrow(g->fo, (g->nf + 2) * sizeof(int));
    g->fi = xgrow(g->fi, (base + n + 1) * sizeof(int));
    g->fe = xgrow(g->fe, (base + n + 1) * sizeof(int));
    g->fc = xgrow(g->fc, (g->nf + 1) * sizeof(V3));
    for (int j = 0; j < n; j++) {
        g->fi[base + j] = v[j];
        g->fe[base + j] = e ? e[j] : g_edge(g, v[j], v[(j + 1) % n]);
    }
    g->fc[g->nf] = col;
    g->fo[g->nf + 1] = base + n;
    return g->nf++;
}

static int g_fn(const G *g, int f) { return g->fo[f + 1] - g->fo[f]; }

/* the two faces on each edge (-1: none), and for each the corner where the face leaves along it */
static int *g_edge_faces(const G *g)
{
    int *ef = xalloc((g->ne + 1) * 2 * sizeof(int));
    for (int e = 0; e < 2 * g->ne; e++) ef[e] = -1;
    for (int f = 0; f < g->nf; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int e = g->fe[j];
            if (e < 0 || e >= g->ne) continue;
            if (ef[2 * e] < 0) ef[2 * e] = f;
            else if (ef[2 * e + 1] < 0 && ef[2 * e] != f) ef[2 * e + 1] = f;
        }
    return ef;
}

/* keep the listed faces (keepf) and what they use; vertices and edges keep their relative
   order, which is how Maya removes components. keepv/keepe (optional) keep unused ones too. */
static G g_compact(const G *g, const unsigned char *keepf, const unsigned char *keepv, const unsigned char *keepe)
{
    int *vm = xalloc((g->nv + 1) * sizeof(int)), *em = xalloc((g->ne + 1) * sizeof(int));
    for (int v = 0; v < g->nv; v++) vm[v] = keepv && keepv[v] ? 0 : -1;
    for (int e = 0; e < g->ne; e++) em[e] = keepe && keepe[e] ? 0 : -1;
    for (int f = 0; f < g->nf; f++)
        if (keepf[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) { vm[g->fi[j]] = 0; if (g->fe[j] >= 0) em[g->fe[j]] = 0; }
    for (int e = 0; e < g->ne; e++) if (em[e] >= 0) { vm[g->ea[e]] = 0; vm[g->eb[e]] = 0; }
    G out = g_new(0, NULL);
    for (int v = 0; v < g->nv; v++) if (vm[v] >= 0) vm[v] = g_vert(&out, g->p[v]);
    for (int e = 0; e < g->ne; e++) if (em[e] >= 0) em[e] = g_add_edge(&out, vm[g->ea[e]], vm[g->eb[e]]);
    for (int f = 0; f < g->nf; f++) {
        if (!keepf[f]) continue;
        int n = g_fn(g, f), *v = xalloc(n * sizeof(int)), *e = xalloc(n * sizeof(int));
        for (int j = 0; j < n; j++) { v[j] = vm[g->fi[g->fo[f] + j]]; e[j] = g->fe[g->fo[f] + j] >= 0 ? em[g->fe[g->fo[f] + j]] : -1; }
        g_face(&out, v, e, n, g->fc[f]);
        free(v); free(e);
    }
    free(vm); free(em);
    return out;
}

/* ---- primitives, in Maya's numbering */

static G g_cube(float w, float h, float d, int sw, int sh, int sd)
{
    if (sw <= 1 && sh <= 1 && sd <= 1) {
        float x = w / 2, y = h / 2, z = d / 2;
        V3 p[8] = { { -x, -y, z }, { x, -y, z }, { -x, y, z }, { x, y, z }, { -x, y, -z }, { x, y, -z }, { -x, -y, -z }, { x, -y, -z } };
        static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 5 }, { 4, 6 }, { 5, 7 }, { 6, 0 }, { 7, 1 } };
        static const int F[6][4] = { { 0, 1, 3, 2 }, { 2, 3, 5, 4 }, { 4, 5, 7, 6 }, { 6, 7, 1, 0 }, { 1, 7, 5, 3 }, { 6, 0, 2, 4 } };
        G g = g_new(8, p);
        for (int i = 0; i < 12; i++) g_add_edge(&g, E[i][0], E[i][1]);
        for (int i = 0; i < 6; i++) g_face(&g, F[i], NULL, 4, v3(-1, -1, -1));
        return g;
    }
    Prim pr;
    prim_defaults(&pr, PRIM_CUBE);
    pr.w = w; pr.h = h; pr.d = d; pr.sw = sw; pr.sh = sh; pr.sd = sd;
    Mesh m = mesh_prim(&pr);
    G g = g_new(m.nv, m.p);
    for (int f = 0; f < m.nf; f++) g_face(&g, m.fi + m.fo[f], NULL, m.fo[f + 1] - m.fo[f], v3(-1, -1, -1));
    mesh_free(&m);
    return g;
}

/* rotate a primitive built around +Y onto the axis ax */
static void g_axis(G *g, V3 ax)
{
    float l = v3len(ax);
    if (l < 1e-9f) return;
    ax = v3mul(ax, 1 / l);
    if (ax.y > 0.9999f) return;
    M4 r;
    if (ax.y < -0.9999f) r = m4_axis_angle(v3(1, 0, 0), 3.14159265f);
    else r = m4_axis_angle(v3cross(v3(0, 1, 0), ax), acosf(ax.y));
    for (int v = 0; v < g->nv; v++) g->p[v] = m4_pt(r, g->p[v]);
}

/* rings bottom to top, vertex i of a ring at angle -2pi(i+1)/sa; ring edges, then the edges up
   each layer; side faces, then the caps (fans around a center when sc > 0) */
static G g_cylinder(float r, float h, int sa, int sh, int sc)
{
    if (sa < 3) sa = 3;
    if (sh < 1) sh = 1;
    G g = g_new(0, NULL);
    for (int j = 0; j <= sh; j++)
        for (int i = 0; i < sa; i++) {
            float a = -6.2831853f * (i + 1) / sa;
            g_vert(&g, v3(r * cosf(a), -h / 2 + h * j / sh, r * sinf(a)));
        }
    for (int j = 0; j <= sh; j++)
        for (int i = 0; i < sa; i++) g_add_edge(&g, j * sa + i, j * sa + (i + 1) % sa);
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sa; i++) g_add_edge(&g, j * sa + i, (j + 1) * sa + i);
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sa; i++) {
            int b = j * sa, q[4] = { b + i, b + (i + 1) % sa, b + sa + (i + 1) % sa, b + sa + i };
            g_face(&g, q, NULL, 4, v3(-1, -1, -1));
        }
    int top = sh * sa;
    if (sc <= 0) {
        int *q = xalloc(sa * sizeof(int));
        for (int i = 0; i < sa; i++) q[i] = (sa - i) % sa;
        g_face(&g, q, NULL, sa, v3(-1, -1, -1));
        for (int i = 0; i < sa; i++) q[i] = top + i;
        g_face(&g, q, NULL, sa, v3(-1, -1, -1));
        free(q);
    } else {
        int cb = g_vert(&g, v3(0, -h / 2, 0)), ct = g_vert(&g, v3(0, h / 2, 0));
        for (int i = 0; i < sa; i++) { int q[3] = { cb, (i + 1) % sa, i }; g_face(&g, q, NULL, 3, v3(-1, -1, -1)); }
        for (int i = 0; i < sa; i++) { int q[3] = { ct, top + i, top + (i + 1) % sa }; g_face(&g, q, NULL, 3, v3(-1, -1, -1)); }
    }
    return g;
}

/* rows from +Z to -Z, columns from -X to +X; per vertex its edge to the right, then the one
   to the next row (a 1x1 plane gives Maya's 0-1, 0-2, 1-3, 2-3) */
static G g_plane(float w, float h, int sw, int sh)
{
    if (sw < 1) sw = 1;
    if (sh < 1) sh = 1;
    G g = g_new(0, NULL);
    for (int r = 0; r <= sh; r++)
        for (int c = 0; c <= sw; c++) g_vert(&g, v3(-w / 2 + w * c / sw, 0, h / 2 - h * r / sh));
    for (int r = 0; r <= sh; r++)
        for (int c = 0; c <= sw; c++) {
            int v = r * (sw + 1) + c;
            if (c < sw) g_add_edge(&g, v, v + 1);
            if (r < sh) g_add_edge(&g, v, v + sw + 1);
        }
    for (int r = 0; r < sh; r++)
        for (int c = 0; c < sw; c++) {
            int v = r * (sw + 1) + c, q[4] = { v, v + 1, v + sw + 2, v + sw + 1 };
            g_face(&g, q, NULL, 4, v3(-1, -1, -1));
        }
    return g;
}

/* rings of the tube (sh) around the axis (sa) */
static G g_torus(float R, float r, int sa, int sh, float twist)
{
    if (sa < 3) sa = 3;
    if (sh < 3) sh = 3;
    G g = g_new(0, NULL);
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sa; i++) {
            float a = -6.2831853f * (i + 1) / sa, t = 6.2831853f * j / sh + twist + 3.14159265f;
            float d = R + r * cosf(t);
            g_vert(&g, v3(d * cosf(a), r * sinf(t), d * sinf(a)));
        }
    for (int j = 0; j < sh; j++)
        for (int i = 0; i < sa; i++) {
            int b = j * sa, nb = ((j + 1) % sh) * sa, q[4] = { b + i, b + (i + 1) % sa, nb + (i + 1) % sa, nb + i };
            g_face(&g, q, NULL, 4, v3(-1, -1, -1));
        }
    return g;
}

/* rings bottom to top, then the two poles; quads, then the bottom and top triangle fans */
static G g_sphere(float r, int sa, int sh)
{
    if (sa < 3) sa = 3;
    if (sh < 2) sh = 2;
    G g = g_new(0, NULL);
    for (int j = 1; j < sh; j++) {
        float t = -1.5707963f + 3.14159265f * j / sh;
        for (int i = 0; i < sa; i++) {
            float a = -6.2831853f * (i + 1) / sa;
            g_vert(&g, v3(r * cosf(t) * cosf(a), r * sinf(t), r * cosf(t) * sinf(a)));
        }
    }
    int sp = g_vert(&g, v3(0, -r, 0)), np = g_vert(&g, v3(0, r, 0));
    for (int j = 0; j < sh - 2; j++)
        for (int i = 0; i < sa; i++) {
            int b = j * sa, q[4] = { b + i, b + (i + 1) % sa, b + sa + (i + 1) % sa, b + sa + i };
            g_face(&g, q, NULL, 4, v3(-1, -1, -1));
        }
    for (int i = 0; i < sa; i++) { int q[3] = { (i + 1) % sa, i, sp }; g_face(&g, q, NULL, 3, v3(-1, -1, -1)); }
    int t = (sh - 2) * sa;
    for (int i = 0; i < sa; i++) { int q[3] = { t + i, t + (i + 1) % sa, np }; g_face(&g, q, NULL, 3, v3(-1, -1, -1)); }
    return g;
}

/* ---- operations */

/* one Catmull-Clark level: corner points, then edge points, then face points */
static G g_smooth(G *g)
{
    int nv = g->nv, ne = g->ne, nf = g->nf;
    int *ef = g_edge_faces(g);
    V3 *fp = xalloc((nf + 1) * sizeof(V3)), *ep = xalloc((ne + 1) * sizeof(V3)), *vp = xalloc((nv + 1) * sizeof(V3));
    for (int f = 0; f < nf; f++) {
        V3 s = v3(0, 0, 0);
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) s = v3add(s, g->p[g->fi[j]]);
        fp[f] = v3mul(s, 1.0f / g_fn(g, f));
    }
    int *val = xalloc((nv + 1) * sizeof(int)), *bord = xalloc((nv + 1) * sizeof(int)), *nfv = xalloc((nv + 1) * sizeof(int));
    V3 *Rsum = xalloc((nv + 1) * sizeof(V3)), *Qsum = xalloc((nv + 1) * sizeof(V3)), *Bsum = xalloc((nv + 1) * sizeof(V3));
    for (int i = 0; i < ne; i++) {
        V3 mid = v3mul(v3add(g->p[g->ea[i]], g->p[g->eb[i]]), 0.5f);
        int f0 = ef[2 * i], f1 = ef[2 * i + 1];
        ep[i] = f0 >= 0 && f1 >= 0 ? v3mul(v3add(v3add(g->p[g->ea[i]], g->p[g->eb[i]]), v3add(fp[f0], fp[f1])), 0.25f) : mid;
        int ends[2] = { g->ea[i], g->eb[i] };
        for (int k = 0; k < 2; k++) {
            int v = ends[k];
            val[v]++;
            Rsum[v] = v3add(Rsum[v], mid);
            if (f1 < 0) { bord[v]++; Bsum[v] = v3add(Bsum[v], mid); }
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
    /* edge points are numbered in the order the edges are met going through the faces, starting
       nf edges in (checked against Maya on a twice smoothed cube) */
    int *walk = xalloc((ne + 1) * sizeof(int)), nw = 0, *epn = xalloc((ne + 1) * sizeof(int));
    for (int i = 0; i < ne; i++) epn[i] = -1;
    for (int f = 0; f < nf; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++)
            if (g->fe[j] >= 0 && epn[g->fe[j]] < 0) { epn[g->fe[j]] = 0; walk[nw++] = g->fe[j]; }
    for (int i = 0; i < ne; i++) if (epn[i] < 0) walk[nw++] = i;
    for (int i = 0; i < nw; i++) epn[walk[(i + nf) % nw]] = nv + i;
    G out = g_new(0, NULL);
    for (int v = 0; v < nv; v++) g_vert(&out, vp[v]);
    for (int i = 0; i < nw; i++) g_vert(&out, ep[walk[(i + nf) % nw]]);
    for (int f = 0; f < nf; f++) g_vert(&out, fp[f]);
    /* edges: the two halves of every edge (in edge order), then the spokes in the order the new
       faces meet them. The face at corner k of face f is face 4f+k, its loop rotated so the old
       corner sits at position k. */
    for (int i = 0; i < ne; i++) { g_add_edge(&out, g->ea[i], epn[i]); g_add_edge(&out, epn[i], g->eb[i]); }
    int nq = g->fo[nf], *Q = xalloc((nq * 4 + 1) * sizeof(int));
    for (int f = 0; f < nf; f++) {
        int s = g->fo[f], n = g_fn(g, f);
        for (int k = 0; k < n; k++) {
            int c = g->fi[s + k], en = g->fe[s + k], epv = g->fe[s + (k - 1 + n) % n];
            int q[4] = { c, epn[en], nv + ne + f, epn[epv] }, r = k % 4;
            for (int z = 0; z < 4; z++) Q[4 * (s + k) + (z + r) % 4] = q[z];
        }
    }
    for (int i = 0; i < nq; i++)
        for (int z = 0; z < 4; z++) {
            int a = Q[4 * i + z], b = Q[4 * i + (z + 1) % 4];
            if ((a >= nv + ne || b >= nv + ne) && g_find_edge(&out, a, b) < 0) g_add_edge(&out, a, b);
        }
    for (int f = 0; f < nf; f++)
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) g_face(&out, Q + 4 * j, NULL, 4, g->fc[f]);
    free(Q); free(walk); free(epn);
    free(fp); free(ep); free(vp); free(val); free(bord); free(nfv); free(Rsum); free(Qsum); free(Bsum); free(ef);
    return out;
}

static unsigned char *face_set(const G *g, const Comps *c)
{
    unsigned char *sel = xalloc(g->nf + 1);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->nf) sel[c->idx[i]] = 1;
    return sel;
}

/* replace vertex a by b (and edge ea by eb) in face f */
static void face_swap(G *g, int f, const int *vmap, const int *emap)
{
    for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
        if (vmap && vmap[g->fi[j]] >= 0) g->fi[j] = vmap[g->fi[j]];
        if (emap && g->fe[j] >= 0 && emap[g->fe[j]] >= 0) g->fe[j] = emap[g->fe[j]];
    }
}

/* Extract (dup == 0): detach the faces, copying the vertices and edges they share with the rest;
   Duplicate (dup != 0): copy the faces as a separate piece */
static void g_chipoff(G *g, const Comps *c, int dup)
{
    unsigned char *sel = face_set(g, c);
    int nv0 = g->nv, ne0 = g->ne, nf0 = g->nf;
    int *ef = g_edge_faces(g);
    int *vm = xalloc((nv0 + 1) * sizeof(int)), *em = xalloc((ne0 + 1) * sizeof(int));
    unsigned char *shared = xalloc(nv0 + 1), *done = xalloc(ne0 + 1);
    for (int v = 0; v < nv0; v++) vm[v] = -1;
    for (int e = 0; e < ne0; e++) em[e] = -1;
    if (!dup)
        for (int f = 0; f < nf0; f++)
            if (!sel[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) shared[g->fi[j]] = 1;
    for (int f = 0; f < nf0; f++) {
        if (!sel[f]) continue;
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++)
            if ((dup || shared[g->fi[j]]) && vm[g->fi[j]] < 0) vm[g->fi[j]] = g_vert(g, g->p[g->fi[j]]);
    }
    for (int f = 0; f < nf0; f++) {
        if (!sel[f]) continue;
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int e = g->fe[j];
            if (e < 0 || e >= ne0 || done[e]) continue;
            done[e] = 1;
            int other = ef[2 * e] == f ? ef[2 * e + 1] : ef[2 * e];
            if (dup || (other >= 0 && !sel[other]) || vm[g->ea[e]] >= 0 || vm[g->eb[e]] >= 0) {
                int a = vm[g->ea[e]] >= 0 ? vm[g->ea[e]] : g->ea[e], b = vm[g->eb[e]] >= 0 ? vm[g->eb[e]] : g->eb[e];
                if (dup || (other >= 0 && !sel[other])) em[e] = g_add_edge(g, a, b);
                else { g->ea[e] = a; g->eb[e] = b; }   /* edge only inside the piece: it moves along */
            }
        }
    }
    if (dup) {
        for (int f = 0; f < nf0; f++) {
            if (!sel[f]) continue;
            int n = g_fn(g, f), *v = xalloc(n * sizeof(int)), *e = xalloc(n * sizeof(int));
            for (int j = 0; j < n; j++) { v[j] = vm[g->fi[g->fo[f] + j]]; e[j] = em[g->fe[g->fo[f] + j]]; }
            g_face(g, v, e, n, g->fc[f]);
            free(v); free(e);
        }
    } else {
        for (int f = 0; f < nf0; f++) if (sel[f]) face_swap(g, f, vm, em);
    }
    g_hash_reset(g);
    free(sel); free(ef); free(vm); free(em); free(shared); free(done);
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
    G out = g_compact(g, keep, NULL, NULL);
    free(sid); free(keep);
    return out;
}

/* Extrude faces (kept together): the region's boundary vertices are copied, in the order the
   boundary edges are met going through the selected faces (each edge's stored start first);
   then the edges up the sides, the cap edges, and one side face per boundary edge. */
static int g_extrude_faces(G *g, const Comps *c, int *nfirst)
{
    unsigned char *sel = face_set(g, c);
    int *ef = g_edge_faces(g);
    int nv0 = g->nv, ne0 = g->ne, nf0 = g->nf;
    unsigned char *bnd = xalloc(ne0 + 1);
    for (int e = 0; e < ne0; e++) {
        int a = ef[2 * e], b = ef[2 * e + 1];
        bnd[e] = (a >= 0 && sel[a]) != (b >= 0 && sel[b]) || (a >= 0 && sel[a] && b < 0);
    }
    int *vm = xalloc((nv0 + 1) * sizeof(int)), *em = xalloc((ne0 + 1) * sizeof(int));
    for (int v = 0; v < nv0; v++) vm[v] = -1;
    for (int e = 0; e < ne0; e++) em[e] = -1;
    /* boundary edges in the order met */
    int *order = xalloc((ne0 + 1) * sizeof(int)), no = 0, *side_f = xalloc((ne0 + 1) * sizeof(int)), *side_j = xalloc((ne0 + 1) * sizeof(int));
    unsigned char *met = xalloc(ne0 + 1);
    for (int i = 0; i < c->n; i++) {
        int f = c->idx[i];
        if (f < 0 || f >= nf0 || (i && c->idx[i - 1] == f)) continue;
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int e = g->fe[j];
            if (e < 0 || !bnd[e] || met[e]) continue;
            met[e] = 1;
            side_f[no] = f; side_j[no] = j;
            order[no++] = e;
        }
    }
    *nfirst = nv0;
    /* copies: every edge of the selected faces in order (interior ones too), each edge's stored
       start first, for the vertices on the region's boundary */
    unsigned char *onb = xalloc(nv0 + 1);
    for (int k = 0; k < no; k++) { onb[g->ea[order[k]]] = 1; onb[g->eb[order[k]]] = 1; }
    for (int i = 0; i < c->n; i++) {
        int f = c->idx[i];
        if (f < 0 || f >= nf0 || (i && c->idx[i - 1] == f)) continue;
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int e = g->fe[j];
            if (e < 0) continue;
            int ends[2] = { g->ea[e], g->eb[e] };
            for (int z = 0; z < 2; z++) if (onb[ends[z]] && vm[ends[z]] < 0) vm[ends[z]] = g_vert(g, g->p[ends[z]]);
        }
    }
    free(onb);
    int *up = xalloc((nv0 + 1) * sizeof(int));
    for (int v = 0; v < nv0; v++) up[v] = -1;
    for (int v = 0; v < nv0; v++) if (vm[v] >= 0) up[v] = -2;
    /* vertical edges in new-vertex order, then cap edges in boundary order */
    for (int nvx = nv0; nvx < g->nv; nvx++)
        for (int v = 0; v < nv0; v++) if (vm[v] == nvx) up[v] = g_add_edge(g, v, nvx);
    for (int k = 0; k < no; k++) { int e = order[k]; em[e] = g_add_edge(g, vm[g->ea[e]], vm[g->eb[e]]); }
    /* interior edges of the region that touch the boundary follow their copied vertices */
    for (int e = 0; e < ne0; e++) {
        if (bnd[e]) continue;
        int a = ef[2 * e], b = ef[2 * e + 1];
        if (!((a >= 0 && sel[a]) && (b >= 0 && sel[b]))) continue;
        if (vm[g->ea[e]] >= 0) g->ea[e] = vm[g->ea[e]];
        if (vm[g->eb[e]] >= 0) g->eb[e] = vm[g->eb[e]];
    }
    /* side faces: a b b' a' along the selected face's direction */
    int *so = xalloc((no + 1) * sizeof(int));
    for (int k = 0; k < no; k++) so[k] = k;
    for (int kk = 0; kk < no; kk++) {
        int k = so[kk];
        int e = order[k], f = side_f[k], j = side_j[k];
        int n = g_fn(g, f), a = g->fi[j], b = g->fi[g->fo[f] + (j - g->fo[f] + 1) % n];
        int q[4] = { a, b, vm[b], vm[a] }, qe[4] = { e, up[b], em[e], up[a] };
        g_face(g, q, qe, 4, g->fc[f]);
    }
    free(so);
    for (int f = 0; f < nf0; f++) if (sel[f]) face_swap(g, f, vm, em);
    g_hash_reset(g);
    free(sel); free(ef); free(bnd); free(vm); free(em); free(order); free(side_f); free(side_j); free(met); free(up);
    return no;
}

/* Extrude with thickness: the selection becomes a closed shell. Every vertex of the selected
   faces gets a copy (in vertex order) that the selected faces move to; copies of their edges,
   the edges joining each border vertex to its copy, then the back faces (the old vertices,
   reversed) and one side face per border edge. */
static void g_extrude_thick(G *g, const Comps *c, int *nfirst)
{
    unsigned char *sel = face_set(g, c);
    int *ef = g_edge_faces(g);
    int nv0 = g->nv, ne0 = g->ne, nf0 = g->nf;
    unsigned char *usev = xalloc(nv0 + 1), *usee = xalloc(ne0 + 1), *bnd = xalloc(ne0 + 1);
    for (int f = 0; f < nf0; f++)
        if (sel[f]) for (int j = g->fo[f]; j < g->fo[f + 1]; j++) { usev[g->fi[j]] = 1; if (g->fe[j] >= 0) usee[g->fe[j]] = 1; }
    for (int e = 0; e < ne0; e++) {
        int a = ef[2 * e], b = ef[2 * e + 1];
        bnd[e] = usee[e] && !((a >= 0 && sel[a]) && (b >= 0 && sel[b]));
    }
    int *vm = xalloc((nv0 + 1) * sizeof(int)), *em = xalloc((ne0 + 1) * sizeof(int)), *up = xalloc((nv0 + 1) * sizeof(int));
    *nfirst = nv0;
    /* copies in the order the edges are met going through the selected faces (stored start first) */
    for (int v = 0; v < nv0; v++) vm[v] = up[v] = -1;
    for (int e = 0; e < ne0; e++) em[e] = -1;
    for (int f = 0; f < nf0; f++)
        if (sel[f])
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
                int e = g->fe[j];
                if (e < 0) continue;
                if (vm[g->ea[e]] < 0) vm[g->ea[e]] = g_vert(g, g->p[g->ea[e]]);
                if (vm[g->eb[e]] < 0) vm[g->eb[e]] = g_vert(g, g->p[g->eb[e]]);
            }
    for (int f = 0; f < nf0; f++)
        if (sel[f])
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++)
                if (g->fe[j] >= 0 && em[g->fe[j]] < 0) em[g->fe[j]] = g_add_edge(g, vm[g->ea[g->fe[j]]], vm[g->eb[g->fe[j]]]);
    for (int e = 0; e < ne0; e++)
        if (bnd[e]) {
            int ends[2] = { g->ea[e], g->eb[e] };
            for (int k = 0; k < 2; k++) if (up[ends[k]] < 0) up[ends[k]] = g_add_edge(g, ends[k], vm[ends[k]]);
        }
    for (int f = 0; f < nf0; f++) {
        if (!sel[f]) continue;
        int n = g_fn(g, f), s = g->fo[f], *v = xalloc(n * sizeof(int)), *e = xalloc(n * sizeof(int));
        for (int k = 0; k < n; k++) { v[k] = g->fi[s + (n - k) % n]; e[k] = g->fe[s + (2 * n - k - 1) % n]; }
        g_face(g, v, e, n, g->fc[f]);
        free(v); free(e);
    }
    for (int f = 0; f < nf0; f++) {
        if (!sel[f]) continue;
        int n = g_fn(g, f);
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) {
            int e = g->fe[j];
            if (e < 0 || !bnd[e]) continue;
            int a = g->fi[j], b = g->fi[g->fo[f] + (j - g->fo[f] + 1) % n];
            int q[4] = { a, b, vm[b], vm[a] }, qe[4] = { e, up[b], em[e], up[a] };
            /* the side face runs against the cap, so along the back face's direction */
            int r[4] = { b, a, vm[a], vm[b] }, re[4] = { e, up[a], em[e], up[b] };
            (void)q; (void)qe;
            g_face(g, r, re, 4, g->fc[f]);
        }
    }
    for (int f = 0; f < nf0; f++) if (sel[f]) face_swap(g, f, vm, em);
    g_hash_reset(g);
    free(sel); free(ef); free(usev); free(usee); free(bnd); free(vm); free(em); free(up);
}

/* Extrude edges: new vertices at the ends of the selected (border) edges in edge order, the
   edges up from them, the outer edges, and a face per edge */
static void g_extrude_edges(G *g, const Comps *c, V3 col, int **vmap)
{
    int *ef = g_edge_faces(g);
    int nv0 = g->nv, ne0 = g->ne;
    unsigned char *sel = xalloc(ne0 + 1);
    int any = 0;
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < ne0) { sel[c->idx[i]] = 1; any = 1; }
    if (!any) for (int e = 0; e < ne0; e++) sel[e] = ef[2 * e + 1] < 0;   /* no list: the whole border */
    int *vm = xalloc((nv0 + 1) * sizeof(int)), *up = xalloc((nv0 + 1) * sizeof(int));
    for (int v = 0; v < nv0; v++) vm[v] = up[v] = -1;
    /* per edge: its new end vertices and the edges up to them (when not made already), then the
       outer edge; the faces follow once all edges are made */
    int *top = xalloc((ne0 + 1) * sizeof(int));
    for (int e = 0; e < ne0; e++) {
        if (!sel[e]) continue;
        int ends[2] = { g->ea[e], g->eb[e] };
        for (int k = 0; k < 2; k++)
            if (vm[ends[k]] < 0) { vm[ends[k]] = g_vert(g, g->p[ends[k]]); up[ends[k]] = g_add_edge(g, ends[k], vm[ends[k]]); }
        top[e] = g_add_edge(g, vm[g->ea[e]], vm[g->eb[e]]);
    }
    for (int e = 0; e < ne0; e++) {
        if (!sel[e]) continue;
        /* the new face runs against the existing face along the edge */
        int a = g->ea[e], b = g->eb[e], f = ef[2 * e];
        if (f >= 0)
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++)
                if (g->fe[j] == e && g->fi[j] == a) { int t = a; a = b; b = t; break; }
        int q[4] = { a, b, vm[b], vm[a] }, qe[4] = { e, up[b], top[e], up[a] };
        g_face(g, q, qe, 4, f >= 0 ? g->fc[f] : col);
    }
    free(top);
    if (vmap) { *vmap = vm; vm = NULL; }
    g_hash_reset(g);
    free(ef); free(sel); free(vm); free(up);
}

static void g_delete_faces(G *g, const Comps *c)
{
    if (getenv("MLT_DEBUG") && atoi(getenv("MLT_DEBUG")) > 2)
        for (int i = 0; i < c->n; i++) {
            int f = c->idx[i];
            if (f < 0 || f >= g->nf) continue;
            V3 ctr = v3(0, 0, 0), n = v3(0, 0, 0);
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++) { ctr = v3add(ctr, g->p[g->fi[j]]); n = v3add(n, v3cross(g->p[g->fi[j]], g->p[g->fi[g->fo[f] + (j - g->fo[f] + 1) % g_fn(g, f)]])); }
            ctr = v3mul(ctr, 1.0f / g_fn(g, f));
            float l = v3len(n) + 1e-12f;
            fprintf(stderr, "      delete f%d at %.2f %.2f %.2f normal %.2f %.2f %.2f:", f, ctr.x, ctr.y, ctr.z, n.x / l, n.y / l, n.z / l);
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++) fprintf(stderr, " %d", g->fi[j]);
            fprintf(stderr, "\n");
        }
    unsigned char *keep = xalloc(g->nf + 1);
    memset(keep, 1, g->nf);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->nf) keep[c->idx[i]] = 0;
    G out = g_compact(g, keep, NULL, NULL);
    g_free(g);
    *g = out;
    free(keep);
}

/* delete edges: the two faces on each edge become one, in the higher numbered face's place;
   clean also removes vertices left with only two edges */
static void g_delete_edges(G *g, const Comps *c, int clean)
{
    for (int i = c->n - 1; i >= 0; i--) {
        int e = c->idx[i];
        if (e < 0 || e >= g->ne) continue;
        int *ef = g_edge_faces(g);
        int f0 = ef[2 * e], f1 = ef[2 * e + 1];
        free(ef);
        if (f0 < 0 || f1 < 0 || f0 == f1) continue;
        if (f1 < f0) { int t = f0; f0 = f1; f1 = t; }
        /* walk f0 from just after e, then f1 from just after e */
        int n0 = g_fn(g, f0), n1 = g_fn(g, f1), j0 = -1, j1 = -1;
        for (int j = 0; j < n0; j++) if (g->fe[g->fo[f0] + j] == e) j0 = j;
        for (int j = 0; j < n1; j++) if (g->fe[g->fo[f1] + j] == e) j1 = j;
        int *v = xalloc((n0 + n1) * sizeof(int)), *ee = xalloc((n0 + n1) * sizeof(int)), n = 0;
        for (int k = 1; k < n0; k++) { int j = g->fo[f0] + (j0 + k) % n0; v[n] = g->fi[j]; ee[n++] = g->fe[j]; }
        for (int k = 1; k < n1; k++) { int j = g->fo[f1] + (j1 + k) % n1; v[n] = g->fi[j]; ee[n++] = g->fe[j]; }
        /* rebuild with f0 replaced and f1 dropped */
        G out = g_new(0, NULL);
        for (int k = 0; k < g->nv; k++) g_vert(&out, g->p[k]);
        for (int k = 0; k < g->ne; k++) g_add_edge(&out, g->ea[k], g->eb[k]);
        for (int f = 0; f < g->nf; f++) {
            if (f == f0) continue;
            if (f == f1) g_face(&out, v, ee, n, g->fc[f]);
            else g_face(&out, g->fi + g->fo[f], g->fe + g->fo[f], g_fn(g, f), g->fc[f]);
        }
        free(v); free(ee);
        unsigned char *keepf = xalloc(out.nf + 1), *keepv = xalloc(out.nv + 1), *keepe = xalloc(out.ne + 1);
        memset(keepf, 1, out.nf);
        G cmp = g_compact(&out, keepf, keepv, keepe);   /* drops edge e, which no face uses now */
        g_free(&out);
        g_free(g);
        *g = cmp;
        free(keepf); free(keepv); free(keepe);
    }
    if (clean) {
        /* vertices with exactly two edges, both shared by the same faces: remove them */
        for (int pass = 0; pass < 64; pass++) {
            int *deg = xalloc((g->nv + 1) * sizeof(int)), victim = -1;
            for (int e = 0; e < g->ne; e++) { deg[g->ea[e]]++; deg[g->eb[e]]++; }
            int *ef = g_edge_faces(g);
            for (int v = 0; v < g->nv && victim < 0; v++) if (deg[v] == 2) victim = v;
            free(deg);
            if (victim < 0) { free(ef); break; }
            int e1 = -1, e2 = -1;
            for (int e = 0; e < g->ne; e++) if (g->ea[e] == victim || g->eb[e] == victim) { if (e1 < 0) e1 = e; else e2 = e; }
            free(ef);
            if (e1 < 0 || e2 < 0) break;
            int other = g->ea[e2] == victim ? g->eb[e2] : g->ea[e2];
            if (g->ea[e1] == victim) g->ea[e1] = other; else g->eb[e1] = other;
            /* faces: drop the corner at victim; the edge leaving the previous corner is e1 */
            G out = g_new(0, NULL);
            for (int k = 0; k < g->nv; k++) g_vert(&out, g->p[k]);
            for (int k = 0; k < g->ne; k++) g_add_edge(&out, g->ea[k], g->eb[k]);
            for (int f = 0; f < g->nf; f++) {
                int n = g_fn(g, f), *v = xalloc((n + 1) * sizeof(int)), *ee = xalloc((n + 1) * sizeof(int)), m = 0;
                for (int j = 0; j < n; j++) {
                    int k = g->fo[f] + j;
                    if (g->fi[k] == victim) continue;
                    v[m] = g->fi[k];
                    ee[m++] = g->fe[k] == e2 ? e1 : g->fe[k];
                    if (g->fi[g->fo[f] + (j + 1) % n] == victim) ee[m - 1] = e1;
                }
                if (m >= 3) g_face(&out, v, ee, m, g->fc[f]);
                free(v); free(ee);
            }
            unsigned char *keepf = xalloc(out.nf + 1), *keepv = xalloc(out.nv + 1), *keepe = xalloc(out.ne + 1);
            memset(keepf, 1, out.nf);
            memset(keepv, 1, out.nv);
            keepv[victim] = 0;
            G cmp = g_compact(&out, keepf, keepv, keepe);
            g_free(&out);
            g_free(g);
            *g = cmp;
            free(keepf); free(keepv); free(keepe);
        }
    }
}

static void g_reverse_face(G *g, int f)
{
    int s = g->fo[f], n = g_fn(g, f);
    int *v = xalloc(n * sizeof(int)), *e = xalloc(n * sizeof(int));
    for (int k = 0; k < n; k++) { v[k] = g->fi[s + (n - k) % n]; e[k] = g->fe[s + (2 * n - k - 1) % n]; }
    memcpy(g->fi + s, v, n * sizeof(int));
    memcpy(g->fe + s, e, n * sizeof(int));
    free(v); free(e);
}

static void g_xform(G *g, const M4 *m)
{
    for (int v = 0; v < g->nv; v++) g->p[v] = m4_pt(*m, g->p[v]);
    if (m->m[0] * (m->m[5] * m->m[10] - m->m[6] * m->m[9]) - m->m[4] * (m->m[1] * m->m[10] - m->m[2] * m->m[9]) +
        m->m[8] * (m->m[1] * m->m[6] - m->m[2] * m->m[5]) < 0)
        for (int f = 0; f < g->nf; f++) g_reverse_face(g, f);   /* mirrored: keep faces facing out */
}

static void g_append(G *g, const G *s)
{
    int vb = g->nv, eb = g->ne;
    for (int v = 0; v < s->nv; v++) g_vert(g, s->p[v]);
    for (int e = 0; e < s->ne; e++) g_add_edge(g, s->ea[e] + vb, s->eb[e] + vb);
    for (int f = 0; f < s->nf; f++) {
        int n = g_fn(s, f), *v = xalloc(n * sizeof(int)), *e = xalloc(n * sizeof(int));
        for (int j = 0; j < n; j++) { v[j] = s->fi[s->fo[f] + j] + vb; e[j] = s->fe[s->fo[f] + j] + eb; }
        g_face(g, v, e, n, s->fc[f]);
        free(v); free(e);
    }
}

/* merge the listed vertices that are close together into the lowest numbered one (moved to their
   average); edges that collapse go, doubled edges keep the older one, faces lose repeated corners */
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
    /* edges */
    int *eto = xalloc((g->ne + 1) * sizeof(int));
    G tmp = g_new(0, NULL);
    for (int v = 0; v < g->nv; v++) g_vert(&tmp, g->p[v]);
    for (int e = 0; e < g->ne; e++) {
        int a = to[g->ea[e]], b = to[g->eb[e]];
        if (a == b) { eto[e] = -1; continue; }
        int k = g_find_edge(&tmp, a, b);
        eto[e] = k >= 0 ? k : g_add_edge(&tmp, a, b);
    }
    for (int f = 0; f < g->nf; f++) {
        int n = g_fn(g, f), *v = xalloc((n + 1) * sizeof(int)), *ee = xalloc((n + 1) * sizeof(int)), m = 0;
        for (int j = 0; j < n; j++) {
            int k = g->fo[f] + j;
            if (eto[g->fe[k]] < 0) continue;   /* collapsed edge: skip this corner */
            v[m] = to[g->fi[k]];
            ee[m++] = eto[g->fe[k]];
        }
        if (m >= 3) g_face(&tmp, v, ee, m, g->fc[f]);
        free(v); free(ee);
    }
    unsigned char *keepf = xalloc(tmp.nf + 1), *keepv = xalloc(tmp.nv + 1);
    memset(keepf, 1, tmp.nf);
    for (int v = 0; v < tmp.nv; v++) keepv[v] = to[v] == v;
    G out = g_compact(&tmp, keepf, keepv, NULL);
    g_free(&tmp);
    g_free(g);
    *g = out;
    free(to); free(eto); free(keepf); free(keepv);
}

/* ordered chain(s) of the listed edges: returns vertex runs; chain k is verts run[k][..] */
typedef struct { int n, *v, *e; int closed; } Chain;

static int g_chains(const G *g, const unsigned char *sel, Chain *out, int maxc)
{
    int nc = 0;
    unsigned char *used = xalloc(g->ne + 1);
    for (int s = 0; s < g->ne && nc < maxc; s++) {
        if (!sel[s] || used[s]) continue;
        /* grow from s both ways */
        int cap = 16, n = 0, *v = xalloc(cap * sizeof(int)), *e = xalloc(cap * sizeof(int));
        int *bv = xalloc(cap * sizeof(int)), *be = xalloc(cap * sizeof(int)), nb = 0;
        used[s] = 1;
        v[n] = g->ea[s]; e[n++] = s; v[n] = g->eb[s];
        for (int dir = 0; dir < 2; dir++) {
            int tip = dir ? g->ea[s] : g->eb[s];
            for (;;) {
                int nx = -1;
                for (int k = 0; k < g->ne; k++)
                    if (sel[k] && !used[k] && (g->ea[k] == tip || g->eb[k] == tip)) { nx = k; break; }
                if (nx < 0) break;
                used[nx] = 1;
                int other = g->ea[nx] == tip ? g->eb[nx] : g->ea[nx];
                if (dir == 0) {
                    if (n + 2 >= cap) { cap *= 2; v = xgrow(v, cap * sizeof(int)); e = xgrow(e, cap * sizeof(int)); }
                    e[n++] = nx; v[n] = other;
                } else {
                    if (nb + 2 >= cap) { cap *= 2; bv = xgrow(bv, cap * sizeof(int)); be = xgrow(be, cap * sizeof(int)); }
                    be[nb] = nx; bv[nb++] = other;
                }
                tip = other;
            }
        }
        /* prepend the backward part */
        int tot = n + 1 + nb;
        int *vv = xalloc((tot + 1) * sizeof(int)), *ee = xalloc((tot + 1) * sizeof(int)), m = 0;
        for (int k = nb - 1; k >= 0; k--) { vv[m] = bv[k]; ee[m++] = be[k]; }
        for (int k = 0; k <= n; k++) { vv[m] = v[k]; ee[m] = k < n ? e[k] : -1; m++; }
        out[nc].closed = m > 2 && vv[0] == vv[m - 1];
        out[nc].n = out[nc].closed ? m - 1 : m;
        out[nc].v = vv; out[nc].e = ee;
        nc++;
        free(v); free(e); free(bv); free(be);
    }
    free(used);
    return nc;
}

/* Bridge two edge chains (dv rows in between), starting from sv1/sv2 when given */
static int g_bridge(G *g, const Comps *c, int dv, int sv1, int sv2)
{
    unsigned char *sel = xalloc(g->ne + 1);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->ne) sel[c->idx[i]] = 1;
    Chain ch[2];
    int nc = g_chains(g, sel, ch, 2);
    if (getenv("MLT_DEBUG") && atoi(getenv("MLT_DEBUG")) > 1) { for (int i = 0; i < c->n; i++) fprintf(stderr, "    sel e%d: %d-%d\n", c->idx[i], c->idx[i] < g->ne ? g->ea[c->idx[i]] : -1, c->idx[i] < g->ne ? g->eb[c->idx[i]] : -1); for (int e = 0; e < g->ne; e++) fprintf(stderr, "      e%d: %d-%d\n", e, g->ea[e], g->eb[e]); }
    if (getenv("MLT_DEBUG")) for (int k = 0; k < nc; k++) { fprintf(stderr, "    chain %d closed %d:", k, ch[k].closed); for (int i = 0; i < ch[k].n; i++) fprintf(stderr, " %d", ch[k].v[i]); fprintf(stderr, "\n"); }
    free(sel);
    if (nc != 2 || ch[0].n != ch[1].n) { for (int k = 0; k < nc; k++) { free(ch[k].v); free(ch[k].e); } return 0; }
    int n = ch[0].n, closed = ch[0].closed && ch[1].closed;
    /* line chain 1 up with chain 0: start vertices, then the direction that keeps them closest */
    int *a = ch[0].v, *b = xalloc((n + 1) * sizeof(int));
    int s0 = 0, s1 = 0;
    if (closed) {
        for (int k = 0; k < n; k++) { if (a[k] == sv1) s0 = k; if (ch[1].v[k] == sv2) s1 = k; }
        if (sv1 < 0 || sv2 < 0) {
            float best = 1e30f;
            for (int k = 0; k < n; k++) { float d = v3len(v3sub(g->p[a[0]], g->p[ch[1].v[k]])); if (d < best) { best = d; s1 = k; } }
        }
    }
    float dfw = 0, dbw = 0;
    for (int k = 0; k < n; k++) {
        int fw = ch[1].v[(s1 + k) % n], bw = ch[1].v[((s1 - k) % n + n) % n];
        if (!closed) { fw = ch[1].v[k]; bw = ch[1].v[n - 1 - k]; }
        dfw += v3len(v3sub(g->p[a[(s0 + k) % n]], g->p[fw]));
        dbw += v3len(v3sub(g->p[a[(s0 + k) % n]], g->p[bw]));
    }
    int *av = xalloc((n + 1) * sizeof(int));
    for (int k = 0; k < n; k++) {
        av[k] = closed ? a[(s0 + k) % n] : a[k];
        b[k] = closed ? (dfw <= dbw ? ch[1].v[(s1 + k) % n] : ch[1].v[((s1 - k) % n + n) % n]) : (dfw <= dbw ? ch[1].v[k] : ch[1].v[n - 1 - k]);
    }
    /* rows of new vertices in between */
    int rows = dv + 2, **R = xalloc(rows * sizeof(int *));
    R[0] = av; R[rows - 1] = b;
    for (int r = 1; r < rows - 1; r++) {
        R[r] = xalloc((n + 1) * sizeof(int));
        float t = (float)r / (rows - 1);
        for (int k = 0; k < n; k++) R[r][k] = g_vert(g, v3add(v3mul(g->p[av[k]], 1 - t), v3mul(g->p[b[k]], t)));
    }
    /* orient the faces against the face already on chain 0's first edge */
    int *ef = g_edge_faces(g), flip = 0;
    int e0 = g_find_edge(g, av[0], av[1 % n]);
    if (e0 >= 0 && ef[2 * e0] >= 0) {
        int f = ef[2 * e0];
        for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (g->fe[j] == e0) flip = g->fi[j] == av[0];
    }
    free(ef);
    int segs = closed ? n : n - 1;
    for (int r = 0; r < rows - 1; r++)
        for (int k = 0; k < segs; k++) {
            int k2 = (k + 1) % n, q[4] = { R[r][k], R[r][k2], R[r + 1][k2], R[r + 1][k] };
            if (flip) { int t = q[1]; q[1] = q[3]; q[3] = t; }
            g_face(g, q, NULL, 4, v3(-1, -1, -1));
        }
    for (int r = 1; r < rows - 1; r++) free(R[r]);
    free(R); free(av); free(b);
    for (int k = 0; k < 2; k++) { free(ch[k].v); free(ch[k].e); }
    return 1;
}

/* Fill each border loop that runs through the listed edges (all border loops if none) */
static void g_close_border(G *g, const Comps *c)
{
    int *ef = g_edge_faces(g);
    unsigned char *sel = xalloc(g->ne + 1), *pick = xalloc(g->ne + 1);
    int any = 0;
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->ne) { pick[c->idx[i]] = 1; any = 1; }
    for (int e = 0; e < g->ne; e++) sel[e] = ef[2 * e] >= 0 && ef[2 * e + 1] < 0;
    Chain ch[64];
    int nc = g_chains(g, sel, ch, 64);
    for (int k = 0; k < nc; k++) {
        int want = !any;
        for (int i = 0; i + 1 <= ch[k].n && !want; i++) if (ch[k].e[i] >= 0 && pick[ch[k].e[i]]) want = 1;
        if (want && ch[k].closed && ch[k].n >= 3) {
            /* run opposite to the face on the first edge */
            int e0 = ch[k].e[0], f = ef[2 * e0], rev = 0;
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (g->fe[j] == e0) rev = g->fi[j] == ch[k].v[0];
            int n = ch[k].n, *v = xalloc(n * sizeof(int)), *e = xalloc(n * sizeof(int));
            for (int i = 0; i < n; i++) {
                v[i] = rev ? ch[k].v[(n - i) % n] : ch[k].v[i];
                e[i] = rev ? ch[k].e[(2 * n - i - 1) % n] : ch[k].e[i];
            }
            g_face(g, v, e, n, g->fc[f]);
            free(v); free(e);
        }
        free(ch[k].v); free(ch[k].e);
    }
    free(ef); free(sel); free(pick);
}

/* polySplit: points on edges (e[k], fraction d[k] from the edge's stored start); consecutive
   points that share a face cut it in two */
static void g_split(G *g, const int *edges, const float *fr, int n)
{
    int *pv = xalloc((n + 1) * sizeof(int));
    for (int k = 0; k < n; k++) {
        int e = edges[k];
        pv[k] = -1;
        if (e < 0 || e >= g->ne) continue;
        float t = fr[k];
        int again = -1;   /* a closed cut ends on its first point */
        for (int q = 0; q < k && again < 0; q++) if (edges[q] == e && fabsf(fr[q] - t) < 1e-6f) again = pv[q];
        if (again >= 0) { pv[k] = again; continue; }
        if (t <= 1e-6f) { pv[k] = g->ea[e]; continue; }
        if (t >= 1 - 1e-6f) { pv[k] = g->eb[e]; continue; }
        /* new vertex splitting edge e: e keeps a..m, a new edge m..b */
        int a = g->ea[e], b = g->eb[e];
        int m = g_vert(g, v3add(v3mul(g->p[a], 1 - t), v3mul(g->p[b], t)));
        g->eb[e] = m;
        int e2 = g_add_edge(g, m, b);
        g_hash_reset(g);
        /* insert m into every face using e */
        for (int f = 0; f < g->nf; f++) {
            int s = g->fo[f], fn = g_fn(g, f), at = -1;
            for (int j = 0; j < fn; j++) if (g->fe[s + j] == e) at = j;
            if (at < 0) continue;
            int fwd = g->fi[s + at] == a;   /* corner at a going to b */
            int *v = xalloc((fn + 2) * sizeof(int)), *ee = xalloc((fn + 2) * sizeof(int)), q = 0;
            for (int j = 0; j < fn; j++) {
                v[q] = g->fi[s + j]; ee[q++] = g->fe[s + j];
                if (j == at) { ee[q - 1] = fwd ? e : e2; v[q] = m; ee[q++] = fwd ? e2 : e; }
            }
            /* rebuild the corner arrays with one more corner */
            int tot = g->fo[g->nf];
            g->fi = xgrow(g->fi, (tot + 2) * sizeof(int));
            g->fe = xgrow(g->fe, (tot + 2) * sizeof(int));
            memmove(g->fi + s + fn + 1, g->fi + s + fn, (tot - s - fn) * sizeof(int));
            memmove(g->fe + s + fn + 1, g->fe + s + fn, (tot - s - fn) * sizeof(int));
            memcpy(g->fi + s, v, (fn + 1) * sizeof(int));
            memcpy(g->fe + s, ee, (fn + 1) * sizeof(int));
            for (int h = f + 1; h <= g->nf; h++) g->fo[h]++;
            free(v); free(ee);
        }
        pv[k] = m;
    }
    /* cut faces between consecutive points */
    for (int k = 0; k + 1 < n; k++) {
        int u = pv[k], w = pv[k + 1];
        if (u < 0 || w < 0 || u == w) continue;
        for (int f = 0; f < g->nf; f++) {
            int s = g->fo[f], fn = g_fn(g, f), iu = -1, iw = -1;
            for (int j = 0; j < fn; j++) { if (g->fi[s + j] == u) iu = j; if (g->fi[s + j] == w) iw = j; }
            if (iu < 0 || iw < 0 || (iu + 1) % fn == iw || (iw + 1) % fn == iu) continue;
            int ne = g_add_edge(g, u, w);
            int *v1 = xalloc((fn + 1) * sizeof(int)), *e1 = xalloc((fn + 1) * sizeof(int)), n1 = 0;
            int *v2 = xalloc((fn + 1) * sizeof(int)), *e2 = xalloc((fn + 1) * sizeof(int)), n2 = 0;
            for (int j = iu; j != iw; j = (j + 1) % fn) { v1[n1] = g->fi[s + j]; e1[n1++] = g->fe[s + j]; }
            v1[n1] = w; e1[n1++] = ne;
            for (int j = iw; j != iu; j = (j + 1) % fn) { v2[n2] = g->fi[s + j]; e2[n2++] = g->fe[s + j]; }
            v2[n2] = u; e2[n2++] = ne;
            /* face f keeps the part from the second point round to the first (rewritten in place),
               the part from the first point round to the second is a new face */
            {
                int *tv = v1, *te = e1, tn = n1;
                v1 = v2; e1 = e2; n1 = n2;
                v2 = tv; e2 = te; n2 = tn;
            }
            int tot = g->fo[g->nf], d = n1 - fn;
            if (d > 0) {
                g->fi = xgrow(g->fi, (tot + d + 1) * sizeof(int));
                g->fe = xgrow(g->fe, (tot + d + 1) * sizeof(int));
            }
            memmove(g->fi + s + n1, g->fi + s + fn, (tot - s - fn) * sizeof(int));
            memmove(g->fe + s + n1, g->fe + s + fn, (tot - s - fn) * sizeof(int));
            memcpy(g->fi + s, v1, n1 * sizeof(int));
            memcpy(g->fe + s, e1, n1 * sizeof(int));
            for (int h = f + 1; h <= g->nf; h++) g->fo[h] += d;
            g_face(g, v2, e2, n2, g->fc[f]);
            free(v1); free(e1); free(v2); free(e2);
            break;
        }
    }
    g_hash_reset(g);
    free(pv);
}

/* Bevel every edge (one segment), numbered as Maya does: going through the vertices in order,
   each vertex gets a new vertex per face around it, starting with the face before its lowest
   numbered face and turning the way the faces' incoming edges lead; the edges met on the way
   become strips (faces first), then the old faces (shrunk), then a face per old vertex. */
static int g_bevel_all(G *g, float frac)
{
    int nv = g->nv, nf = g->nf, ne = g->ne, *ef = g_edge_faces(g);
    for (int e = 0; e < ne; e++) if (ef[2 * e] < 0 || ef[2 * e + 1] < 0) { free(ef); return 0; }   /* closed meshes only */
    int nc = g->fo[nf];
    int *cv = xalloc((nc + 1) * sizeof(int)), *ring = xalloc((nf + 1) * sizeof(int)), *done = xalloc((ne + 1) * sizeof(int));
    int *strip_e = xalloc((ne + 1) * sizeof(int)), ns = 0;
    float *minl = xalloc((nv + 1) * sizeof(float));
    for (int v = 0; v < nv; v++) minl[v] = 1e30f;
    for (int e = 0; e < ne; e++) {
        float l = v3len(v3sub(g->p[g->ea[e]], g->p[g->eb[e]]));
        if (l < minl[g->ea[e]]) minl[g->ea[e]] = l;
        if (l < minl[g->eb[e]]) minl[g->eb[e]] = l;
    }
    G out = g_new(0, NULL);
    int *rings = xalloc((nc + 1) * sizeof(int)), *rs = xalloc((nv + 2) * sizeof(int)), nr = 0;
    for (int v = 0; v < nv; v++) {
        rs[v] = nr;
        int f0 = -1;
        for (int f = 0; f < nf && f0 < 0; f++)
            for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (g->fi[j] == v) { f0 = f; break; }
        if (f0 < 0) continue;
        /* faces around v: next = the other face on f's edge coming into v */
        int n = 0, f = f0;
        do {
            ring[n++] = f;
            int s = g->fo[f], k = g_fn(g, f), in = -1;
            for (int j = 0; j < k; j++) if (g->fi[s + j] == v) in = g->fe[s + (j - 1 + k) % k];
            if (in < 0) break;
            f = ef[2 * in] == f ? ef[2 * in + 1] : ef[2 * in];
        } while (f != f0 && f >= 0 && n < nf);
        /* start one face before f0 */
        for (int i = 0; i < n; i++) {
            int fc = ring[(i + n - 1) % n], s = g->fo[fc], k = g_fn(g, fc);
            for (int j = 0; j < k; j++)
                if (g->fi[s + j] == v) {
                    V3 p = g->p[v], a = g->p[g->fi[s + (j + 1) % k]], b = g->p[g->fi[s + (j - 1 + k) % k]];
                    float d = frac * 0.5f * minl[v];
                    V3 ua = v3sub(a, p), ub = v3sub(b, p);
                    float la = v3len(ua), lb = v3len(ub);
                    if (la > 1e-12f) p = v3add(p, v3mul(ua, d / la));
                    if (lb > 1e-12f) p = v3add(p, v3mul(ub, d / lb));
                    cv[s + j] = g_vert(&out, p);
                    rings[nr++] = s + j;
                }
        }
        /* strips for the edges between consecutive corners: last to first, then in order */
        for (int i = 0; i < n; i++) {
            int fa = ring[(i + n - 2) % n], fb = ring[(i + n - 1) % n];
            for (int e = 0; e < ne; e++)
                if (!done[e] && (g->ea[e] == v || g->eb[e] == v) &&
                    ((ef[2 * e] == fa && ef[2 * e + 1] == fb) || (ef[2 * e] == fb && ef[2 * e + 1] == fa))) {
                    done[e] = 1;
                    strip_e[ns++] = e;
                }
        }
    }
    rs[nv] = nr;
    /* strips: the corners of both faces along the edge */
    for (int i = 0; i < ns; i++) {
        int e = strip_e[i], fs[2] = { ef[2 * e], ef[2 * e + 1] }, q[4], m = 0;
        for (int z = 0; z < 2; z++) {
            int f = fs[z], s = g->fo[f], k = g_fn(g, f);
            for (int j = 0; j < k; j++)
                if (g->fe[s + j] == e) { q[m++] = cv[s + (j + 1) % k]; q[m++] = cv[s + j]; }
        }
        if (m == 4) g_face(&out, q, NULL, 4, g->fc[fs[0]]);
    }
    for (int f = 0; f < nf; f++) g_face(&out, cv + g->fo[f], NULL, g_fn(g, f), g->fc[f]);
    for (int v = 0; v < nv; v++) {
        int n = rs[v + 1] - rs[v];
        if (n < 3) continue;
        int *q = xalloc(n * sizeof(int));
        for (int i = 0; i < n; i++) q[i] = cv[rings[rs[v] + n - 1 - i]];
        g_face(&out, q, NULL, n, g->fc[g->nf ? 0 : 0]);
        free(q);
    }
    g_free(g);
    *g = out;
    free(ef); free(cv); free(ring); free(done); free(strip_e); free(minl); free(rings); free(rs);
    return 1;
}

/* Bevel (one segment): every listed edge becomes a strip; each end vertex of a bevelled edge
   splits along the other edges at that vertex, by fraction f of their length */
static void g_bevel(G *g, const Comps *c, float frac)
{
    /* approximation: chamfer by insetting the faces on both sides of each edge */
    unsigned char *sel = xalloc(g->ne + 1);
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < g->ne) sel[c->idx[i]] = 1;
    int *ef = g_edge_faces(g);
    /* per face corner: a new vertex where the corner's vertex sits on a bevelled edge */
    int nc = g->fo[g->nf];
    int *cv = xalloc((nc + 1) * sizeof(int));
    for (int f = 0; f < g->nf; f++) {
        int s = g->fo[f], n = g_fn(g, f);
        for (int j = 0; j < n; j++) {
            int k = s + j, v = g->fi[k], en = g->fe[k], ep = g->fe[s + (j - 1 + n) % n];
            cv[k] = v;
            int sn = en >= 0 && sel[en], spv = ep >= 0 && sel[ep];
            if (!sn && !spv) continue;
            /* slide along the corner's other edge(s) */
            V3 p = g->p[v], dir = v3(0, 0, 0);
            int nxt = g->fi[s + (j + 1) % n], prv = g->fi[s + (j - 1 + n) % n];
            if (sn && !spv) dir = v3sub(g->p[prv], p);
            else if (spv && !sn) dir = v3sub(g->p[nxt], p);
            else dir = v3add(v3sub(g->p[prv], p), v3sub(g->p[nxt], p));
            cv[k] = g_vert(g, v3add(p, v3mul(dir, frac)));
        }
    }
    G out = g_new(0, NULL);
    for (int v = 0; v < g->nv; v++) g_vert(&out, g->p[v]);
    for (int f = 0; f < g->nf; f++) {
        int s = g->fo[f], n = g_fn(g, f);
        g_face(&out, cv + s, NULL, n, g->fc[f]);
    }
    /* strips on bevelled edges, and caps at the ends where three or more corners met */
    for (int e = 0; e < g->ne; e++) {
        if (!sel[e] || ef[2 * e] < 0 || ef[2 * e + 1] < 0) continue;
        int f0 = ef[2 * e], f1 = ef[2 * e + 1], a0 = -1, b0 = -1, a1 = -1, b1 = -1;
        for (int j = g->fo[f0]; j < g->fo[f0 + 1]; j++) if (g->fe[j] == e) { a0 = cv[j]; b0 = cv[g->fo[f0] + (j - g->fo[f0] + 1) % g_fn(g, f0)]; }
        for (int j = g->fo[f1]; j < g->fo[f1 + 1]; j++) if (g->fe[j] == e) { a1 = cv[j]; b1 = cv[g->fo[f1] + (j - g->fo[f1] + 1) % g_fn(g, f1)]; }
        if (a0 < 0 || a1 < 0) continue;
        int q[4] = { b0, a0, b1, a1 };
        g_face(&out, q, NULL, 4, g->fc[f0]);
    }
    unsigned char *keepf = xalloc(out.nf + 1);
    memset(keepf, 1, out.nf);
    G cmp = g_compact(&out, keepf, NULL, NULL);
    g_free(&out);
    g_free(g);
    *g = cmp;
    free(keepf); free(sel); free(ef); free(cv);
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
    *out = g_new(0, NULL);
#define NEED(k) if (e - v < (ptrdiff_t)(k)) goto bad
    NEED(4);
    uint32_t np = be32(v) / 3;
    v += 4;
    NEED((size_t)np * 12);
    for (uint32_t i = 0; i < np; i++, v += 12) g_vert(out, v3(beflt(v), beflt(v + 4), beflt(v + 8)));
    NEED(4);
    uint32_t ne = be32(v) / 2;
    v += 4;
    NEED((size_t)ne * 8);
    for (uint32_t i = 0; i < ne; i++, v += 8) {
        int ea = (int)(be32(v) & 0x7fffffff), eb = (int)(be32(v + 4) & 0x7fffffff);
        g_add_edge(out, ea < (int)np ? ea : 0, eb < (int)np ? eb : 0);
    }
    NEED(4);
    uint32_t nl = be32(v);
    v += 4;
    NEED((size_t)nl * 4);
    int *q = xalloc((nl + 1) * sizeof(int)), *qe = xalloc((nl + 1) * sizeof(int)), nq = 0, hole = 0, corners = 0;
    int *corner_face = xalloc((nl + 1) * sizeof(int));   /* face of each face corner, for colors */
    for (uint32_t i = 0; i < nl; i++, v += 4) {
        uint32_t x = be32(v), k = x & 0x1fffffff;
        int vert = k < ne ? (x >> 31 ? out->eb[k] : out->ea[k]) : 0;
        corner_face[corners++] = out->nf;
        if (!hole) { q[nq] = vert; qe[nq++] = k < ne ? (int)k : -1; }   /* holes are dropped: tuff3d faces are simple polygons */
        if (x & 0x40000000u) hole = 1;
        if (x & 0x20000000u) {
            if (nq >= 3) g_face(out, q, qe, nq, v3(-1, -1, -1));
            else for (int c = corners - 1; c >= 0 && corner_face[c] == out->nf; c--) corner_face[c] = -1;
            nq = 0; hole = 0;
        }
    }
    free(q); free(qe);
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

static void apply_tweaks(G *g, const Tweaks *t)
{
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
        float fl = v3len(fn);
        if (fl > 1e-12f) fn = v3mul(fn, 1 / fl);   /* each face counts the same */
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

/* place the new outer vertices of an edge extrusion: local translate y moves them away from the
   face beside each edge (in its plane), z along that face's normal; t and s as for faces */
static void move_extruded_edges(const Node *nd, const G *before, const int *ef, const Comps *c, G *g, const int *vm)
{
    V3 lt = attr_v3(nd, "lt", v3(0, 0, 0)), t = attr_v3(nd, "t", v3(0, 0, 0)), sc = attr_v3(nd, "s", v3(1, 1, 1));
    V3 pvt = attr_v3(nd, "pvt", v3(0, 0, 0));
    if (!vm) return;
    M4 ix = attr_m4(nd, "ix"), inv = m4_inv(ix);
    int nv0 = before->nv;
    V3 *dy = xalloc((nv0 + 1) * sizeof(V3)), *dz = xalloc((nv0 + 1) * sizeof(V3));
    for (int i = 0; i < c->n; i++) {
        int e = c->idx[i];
        if (e < 0 || e >= before->ne) continue;
        int f = ef[2 * e];
        V3 a = m4_pt(ix, before->p[before->ea[e]]), b = m4_pt(ix, before->p[before->eb[e]]);
        V3 n = v3(0, 0, 0), ctr = v3(0, 0, 0);
        if (f >= 0) {
            for (int j = before->fo[f]; j < before->fo[f + 1]; j++) {
                int u = before->fi[j], w = before->fi[before->fo[f] + (j - before->fo[f] + 1) % g_fn(before, f)];
                n = v3add(n, v3cross(m4_pt(ix, before->p[u]), m4_pt(ix, before->p[w])));
                ctr = v3add(ctr, m4_pt(ix, before->p[u]));
            }
            ctr = v3mul(ctr, 1.0f / g_fn(before, f));
        }
        float nl = v3len(n);
        if (nl > 1e-12f) n = v3mul(n, 1 / nl);
        V3 d = v3cross(v3sub(b, a), n);
        float dl = v3len(d);
        if (dl > 1e-12f) d = v3mul(d, 1 / dl);
        if (v3dot(d, v3sub(v3mul(v3add(a, b), 0.5f), ctr)) < 0) d = v3mul(d, -1);
        int ends[2] = { before->ea[e], before->eb[e] };
        for (int k = 0; k < 2; k++) { dy[ends[k]] = v3add(dy[ends[k]], d); dz[ends[k]] = v3add(dz[ends[k]], n); }
    }
    for (int v = 0; v < nv0; v++) {
        if (vm[v] < 0) continue;
        V3 p = m4_pt(ix, g->p[vm[v]]);
        float l = v3len(dy[v]);
        if (l > 1e-12f) p = v3add(p, v3mul(dy[v], lt.y / l));
        l = v3len(dz[v]);
        if (l > 1e-12f) p = v3add(p, v3mul(dz[v], lt.z / l));
        p = v3add(pvt, v3(sc.x * (p.x - pvt.x), sc.y * (p.y - pvt.y), sc.z * (p.z - pvt.z)));
        p = v3add(p, t);
        g->p[vm[v]] = m4_pt(inv, p);
    }
    free(dy); free(dz);
}

/* Operations record the bounding box (cbn/cbx, world space) of the components they work on.
   Where the replay numbers components differently from Maya, a selected face or edge can fall
   outside that box: swap each such one for the nearest numbered unselected one that lies inside
   (for edge extrusions, a border edge). */
static void repair_selection(Ctx *x, const Node *nd, const G *g, Comps *c, int border_only)
{
    if (!attr(nd, "cbn") || !attr(nd, "cbx") || !c->n) return;
    M4 ix = attr_m4(nd, "ix");
    V3 lo = attr_v3(nd, "cbn", v3(0, 0, 0)), hi = attr_v3(nd, "cbx", v3(0, 0, 0));
    float tol = v3len(v3sub(hi, lo)) * 0.02f + 1e-5f;   /* only clear misses: positions can drift a little */
    int n = c->type == 'F' ? g->nf : c->type == 'E' ? g->ne : 0;
    if (!n) return;
    int *ef = c->type == 'E' ? g_edge_faces(g) : NULL;
    unsigned char *in = xalloc(n + 1), *sel = xalloc(n + 1);
    for (int k = 0; k < n; k++) {
        int ok = 1;
        if (c->type == 'F') {
            for (int j = g->fo[k]; j < g->fo[k + 1] && ok; j++) {
                V3 p = m4_pt(ix, g->p[g->fi[j]]);
                ok = p.x >= lo.x - tol && p.y >= lo.y - tol && p.z >= lo.z - tol && p.x <= hi.x + tol && p.y <= hi.y + tol && p.z <= hi.z + tol;
            }
        } else {
            for (int z = 0; z < 2 && ok; z++) {
                V3 p = m4_pt(ix, g->p[z ? g->eb[k] : g->ea[k]]);
                ok = p.x >= lo.x - tol && p.y >= lo.y - tol && p.z >= lo.z - tol && p.x <= hi.x + tol && p.y <= hi.y + tol && p.z <= hi.z + tol;
            }
            if (border_only && ef[2 * k + 1] >= 0) ok = 0;
        }
        in[k] = (unsigned char)ok;
    }
    int bad = 0;
    for (int i = 0; i < c->n; i++) if (c->idx[i] >= 0 && c->idx[i] < n) { sel[c->idx[i]] = 1; if (!in[c->idx[i]]) bad++; }
    if (bad) {
        int fixed = 0;
        for (int i = 0; i < c->n; i++) {
            int k = c->idx[i];
            if (k < 0 || k >= n || in[k]) continue;
            int best = -1;
            for (int d = 1; d < n && best < 0; d++) {
                if (k - d >= 0 && in[k - d] && !sel[k - d]) best = k - d;
                else if (k + d < n && in[k + d] && !sel[k + d]) best = k + d;
            }
            if (best < 0) continue;
            sel[k] = 0; sel[best] = 1;
            c->idx[i] = best;
            fixed++;
        }
        if (x->debug) fprintf(stderr, "  [repair %s: %d of %d outside the recorded box, %d replaced]\n", nd->name, bad, c->n, fixed);
    }
    free(ef); free(in); free(sel);
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
        if (c->type == 'E' && k < g->ne)
            for (int z = 0; z < 2; z++) {
                V3 p = m4_pt(ix, g->p[z ? g->eb[k] : g->ea[k]]);
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
    if (x->debug > 1 && bad) {
        for (int v = 0; v < nm; v++) {
            fprintf(stderr, "    maya v%d:", v);
            for (int k = 0; k < cpv->na; k++) {
                int vv, f;
                if (sscanf(cpv->a[k].name, "vclr[%d].vfcl[%d]", &vv, &f) == 2 && vv == v && strstr(cpv->a[k].name, "vfcr")) fprintf(stderr, " %d", f);
            }
            fprintf(stderr, "   ours:");
            for (int f = 0; f < g->nf; f++)
                for (int j = g->fo[f]; j < g->fo[f + 1]; j++) if (g->fi[j] == v) fprintf(stderr, " %d", f);
            fprintf(stderr, "\n");
        }
        for (int f = 0; f < g->nf; f++) { fprintf(stderr, "    our f%d:", f); for (int j = g->fo[f]; j < g->fo[f + 1]; j++) fprintf(stderr, " %d", g->fi[j]); fprintf(stderr, "\n"); }
    }
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
                apply_tweaks(out, &t);
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
    } else if (!strcmp(op, "polyCylinder")) {
        *out = g_cylinder((float)attr_d(nd, "r", 1), (float)attr_d(nd, "h", 2), (int)attr_d(nd, "sa", 20), (int)attr_d(nd, "sh", 1),
                          (int)attr_d(nd, "sc", 0));
        g_axis(out, attr_v3(nd, "ax", v3(0, 1, 0)));
        ok = 1;
    } else if (!strcmp(op, "polyPlane")) {
        *out = g_plane((float)attr_d(nd, "w", 1), (float)attr_d(nd, "h", 1), (int)attr_d(nd, "sw", 10), (int)attr_d(nd, "sh", 10));
        g_axis(out, attr_v3(nd, "ax", v3(0, 1, 0)));
        ok = 1;
    } else if (!strcmp(op, "polySphere")) {
        *out = g_sphere((float)attr_d(nd, "r", 1), (int)attr_d(nd, "sa", 20), (int)attr_d(nd, "sh", 20));
        g_axis(out, attr_v3(nd, "ax", v3(0, 1, 0)));
        ok = 1;
    } else if (!strcmp(op, "polyTorus")) {
        *out = g_torus((float)attr_d(nd, "r", 1), (float)attr_d(nd, "sr", 0.5), (int)attr_d(nd, "sa", 20), (int)attr_d(nd, "sh", 20),
                       (float)attr_d(nd, "tw", 0));
        g_axis(out, attr_v3(nd, "ax", v3(0, 1, 0)));
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
            Comps c = comps(nd, "ics", out->ne);
            check_box(x, nd, out, &c);
            repair_selection(x, nd, out, &c, 1);
            if (x->debug > 1) {
                int *bef = g_edge_faces(out);
                fprintf(stderr, "    border edges:");
                for (int e = 0; e < out->ne; e++) if (bef[2 * e + 1] < 0) fprintf(stderr, " %d", e);
                fprintf(stderr, "\n    selected:");
                for (int i = 0; i < c.n; i++) fprintf(stderr, " %d", c.idx[i]);
                fprintf(stderr, "\n");
                free(bef);
            }
            int ne0 = out->ne, *vm = NULL;
            int *ef = g_edge_faces(out);
            G before = g_copy(out);
            g_extrude_edges(out, &c, v3(-1, -1, -1), &vm);
            move_extruded_edges(nd, &before, ef, &c, out, vm);
            g_free(&before);
            free(ef); free(vm);
            (void)ne0;
            free(c.idx);
        }
    } else if (!strcmp(op, "polyExtrudeFace")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->nf);
            check_box(x, nd, out, &c);
            repair_selection(x, nd, out, &c, 0);
            int first;
            if (x->debug > 1) {
                for (int i = 0; i < c.n; i++) {
                    int f = c.idx[i];
                    fprintf(stderr, "    f%d:", f);
                    for (int j = out->fo[f]; j < out->fo[f + 1]; j++) fprintf(stderr, " %d", out->fi[j]);
                    fprintf(stderr, "\n");
                }
                for (int f = 0; f < out->nf; f++) { fprintf(stderr, "      all f%d:", f); for (int j = out->fo[f]; j < out->fo[f + 1]; j++) fprintf(stderr, " %d", out->fi[j]); fprintf(stderr, "\n"); }
            }
            if (attr_d(nd, "tk", 0) != 0) g_extrude_thick(out, &c, &first);
            else g_extrude_faces(out, &c, &first);
            move_extruded(nd, out, &c, first);
            free(c.idx);
        }
    } else if (!strcmp(op, "polyTweak")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Tweaks t = tweaks(nd, "tk");
            apply_tweaks(out, &t);
            tweaks_free(&t);
        }
    } else if (!strcmp(op, "deleteComponent")) {
        if ((ok = eval_in(x, node, "ig", out))) {
            Comps c = comps(nd, "dc", out->nf);
            if (c.type == 'E') { free(c.idx); c = comps(nd, "dc", out->ne); }
            if (c.type == 'F') g_delete_faces(out, &c);
            else if (c.type == 'E') g_delete_edges(out, &c, 1);
            else if (c.n) skip_note(x, "deleting vertices");
            free(c.idx);
        }
    } else if (!strcmp(op, "polyMergeVert")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->nv);
            g_merge(out, &c, (float)attr_d(nd, "d", 0));
            free(c.idx);
        }
    } else if (!strcmp(op, "polyUnite")) {
        *out = g_new(0, NULL);
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
    } else if (!strcmp(op, "polyBridgeEdge")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->ne);
            if (!g_bridge(out, &c, (int)attr_d(nd, "dv", 0), (int)attr_d(nd, "sv1", -1), (int)attr_d(nd, "sv2", -1)))
                skip_note(x, "a bridge between edge runs of different lengths");
            free(c.idx);
        }
    } else if (!strcmp(op, "polyCloseBorder")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->ne);
            g_close_border(out, &c);
            free(c.idx);
        }
    } else if (!strcmp(op, "polyDelEdge")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->ne);
            g_delete_edges(out, &c, attr_d(nd, "cv", 0) != 0);
            free(c.idx);
        }
    } else if (!strcmp(op, "polyBevel")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            Comps c = comps(nd, "ics", out->ne);
            int all = c.n >= out->ne;
            if (!all || !g_bevel_all(out, (float)attr_d(nd, "f", 0.5))) g_bevel(out, &c, (float)attr_d(nd, "f", 0.5) * 0.5f);
            free(c.idx);
        }
    } else if (!strcmp(op, "polySplit")) {
        if ((ok = eval_in(x, node, "ip", out))) {
            /* the edge list holds edge numbers (top bit set) and the fraction list the positions */
            int n = 0, *edges = NULL;
            float *fr = NULL;
            for (int k = 0; k < nd->na; k++) {
                const Attr *a = &nd->a[k];
                int lo, hi;
                if (strcmp(a->tag, "DBLE")) continue;
                int two = sscanf(a->name + 1, "[%d:%d]", &lo, &hi);
                if (two < 1) continue;
                if (two == 1) hi = lo;
                for (int i = lo; i <= hi && (i - lo + 1) * 8 <= a->n && i < 4096; i++) {
                    double v = bedbl(a->v + 8 * (i - lo));
                    if (i >= n) { edges = xgrow(edges, (i + 1) * sizeof(int)); fr = xgrow(fr, (i + 1) * sizeof(float)); for (int z = n; z <= i; z++) { edges[z] = -1; fr[z] = 0; } n = i + 1; }
                    if (v < 0) edges[i] = (int)((int64_t)v & 0x7fffffff);
                    else fr[i] = (float)v;
                }
            }
            g_split(out, edges, fr, n);
            free(edges); free(fr);
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
