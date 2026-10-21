/* tuff3d - minimalist polygon modeler with Maya style navigation and hotkeys.
   Win32 + OpenGL 1.1 only, so the result is a single small exe. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <commdlg.h>
#include <GL/gl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mesh.h"

#define PI_F 3.14159265358979f
#define APP_NAME "tuff3d"

/* ================================================================ scene */

typedef struct {
    char name[32];
    Mesh m;
    V3 t, r, s;          /* translate, rotate (degrees, xyz order), scale */
    int sel, hil;        /* selected (object mode) / hilited for component editing */
    unsigned order;      /* selection stamp, highest is the lead object */
    Prim prim;           /* construction history, PRIM_NONE once components are edited */
} Obj;

enum { M_OBJ, M_VERT, M_EDGE, M_FACE, M_MULTI };   /* component modes match COMP_* */
enum { T_SELECT, T_MOVE, T_ROTATE, T_SCALE, T_PAINT };
enum { H_NONE, H_X, H_Y, H_Z, H_YZ, H_XZ, H_XY, H_C, H_VIEW };

static Obj *ob;
static int nob, cob;
static int mode = M_OBJ, lastComp = M_VERT, tool = T_SELECT, wire;
static unsigned stamp;
static Prim defPrim[PRIM_COUNT];
static int normalFrame;
static M4 nframe;
static float manipSize = 1;
static V3 paintCol = { 1, 0, 0 };
static float paintOpa = 1, paintRad = 40;
static char g_path[MAX_PATH];
static char g_msg[200];
static int lastCmd, lastHandle, hover, modified;
static int hvObj = -1, hvKind, hvIdx, tracking;   /* preselection highlight (hvKind 0 = whole object) */

static HWND hMain, hView, hPanel, hStatus, hBar;
static HDC hdc;
static HMENU hMenu, hThemeMenu;
static HFONT hFont, hBold, hSmall;
static HBRUSH swatchBrush;
static int vw = 1, vh = 1, panelOn = 1;
static float ds = 1;

static void refresh_all(void);
static void redraw(void) { InvalidateRect(hView, NULL, FALSE); }
static int key(int vk) { return GetKeyState(vk) < 0; }
static void msg(const char *s) { snprintf(g_msg, sizeof g_msg, "%s", s); }

static M4 obj_mat(const Obj *o) { return m4_trs(o->t, o->r, o->s); }

/* ================================================================ themes */

typedef struct {
    const char *name;
    int sys, dark;          /* sys: Windows colors and menu bar; dark: dark title bar and popup menus */
    COLORREF ui, text, head, field, fieldText, line, hot;
    float bgTop[3], bgBot[3], grid[3], gridMajor[3], gridAxis[3], wire[3], sel2[3];
} Theme;

static Theme themes[] = {
    { "&Classic", 1, 0, 0, 0, 0, 0, 0, 0, 0,
      { .36f, .36f, .36f }, { .36f, .36f, .36f }, { .44f, .44f, .44f }, { .5f, .5f, .5f }, { .12f, .12f, .12f }, { 0, .02f, .4f }, { 1, 1, 1 } },
    { "&Light", 0, 0, RGB(243, 243, 243), RGB(30, 30, 30), RGB(0, 90, 180), RGB(255, 255, 255), RGB(20, 20, 20), RGB(200, 200, 200), RGB(214, 224, 238),
      { .80f, .82f, .85f }, { .62f, .64f, .68f }, { .53f, .55f, .59f }, { .45f, .47f, .51f }, { .2f, .2f, .22f }, { 0, .05f, .45f }, { .1f, .1f, .1f } },
    { "&Dark", 0, 1, RGB(37, 37, 38), RGB(220, 220, 220), RGB(255, 255, 255), RGB(28, 28, 28), RGB(230, 230, 230), RGB(70, 70, 72), RGB(62, 62, 64),
      { .27f, .27f, .28f }, { .13f, .13f, .14f }, { .31f, .31f, .32f }, { .38f, .38f, .39f }, { .05f, .05f, .05f }, { .6f, .7f, .9f }, { 1, 1, 1 } },
    { "&Midnight", 0, 1, RGB(20, 26, 42), RGB(200, 210, 230), RGB(110, 200, 255), RGB(12, 16, 28), RGB(220, 230, 250), RGB(45, 58, 88), RGB(40, 56, 92),
      { .13f, .19f, .31f }, { .04f, .06f, .12f }, { .18f, .25f, .38f }, { .24f, .32f, .48f }, { .02f, .03f, .06f }, { .45f, .75f, 1 }, { 1, 1, 1 } },
    { "&Tuff", 0, 1, RGB(58, 10, 34), RGB(240, 225, 235), RGB(165, 255, 20), RGB(36, 4, 20), RGB(255, 240, 250), RGB(110, 30, 70), RGB(150, 16, 205),
      { .08f, .36f, .80f }, { .03f, .12f, .32f }, { .15f, .40f, .75f }, { .25f, .52f, .90f }, { .35f, 0, .16f }, { .65f, 1, .08f }, { 1, 1, 1 } },
    { "&Forest", 0, 1, RGB(28, 38, 31), RGB(214, 228, 210), RGB(140, 210, 120), RGB(18, 26, 21), RGB(225, 238, 220), RGB(54, 74, 60), RGB(50, 78, 58),
      { .24f, .31f, .26f }, { .10f, .14f, .11f }, { .28f, .36f, .30f }, { .34f, .44f, .36f }, { .04f, .06f, .04f }, { .75f, .95f, .6f }, { 1, 1, 1 } },
    { "&Sunset", 0, 1, RGB(43, 29, 34), RGB(240, 222, 210), RGB(255, 154, 60), RGB(30, 20, 24), RGB(250, 235, 225), RGB(85, 55, 60), RGB(110, 58, 52),
      { .42f, .24f, .30f }, { .12f, .07f, .12f }, { .46f, .30f, .34f }, { .54f, .36f, .38f }, { .08f, .03f, .05f }, { 1, .75f, .45f }, { 1, 1, 1 } },
};
#define NTHEME ((int)(sizeof themes / sizeof *themes))
static Theme th;            /* active theme, with the Classic colors filled in from Windows */
static int themeIdx;
static HBRUSH brUi, brField, brLine, brHot, brAcc;

static COLORREF mix(COLORREF a, COLORREF b)
{
    return RGB((GetRValue(a) + GetRValue(b)) / 2, (GetGValue(a) + GetGValue(b)) / 2, (GetBValue(a) + GetBValue(b)) / 2);
}

static void theme_load(int i)
{
    themeIdx = i < 0 || i >= NTHEME ? 0 : i;
    th = themes[themeIdx];
    if (th.sys) {
        th.ui = GetSysColor(COLOR_BTNFACE);
        th.text = th.head = GetSysColor(COLOR_BTNTEXT);
        th.field = GetSysColor(COLOR_WINDOW);
        th.fieldText = GetSysColor(COLOR_WINDOWTEXT);
        th.line = GetSysColor(COLOR_BTNSHADOW);
        th.hot = GetSysColor(COLOR_BTNHIGHLIGHT);
    }
    HBRUSH *b[5] = { &brUi, &brField, &brLine, &brHot, &brAcc };
    COLORREF c[5] = { th.ui, th.field, th.line, th.hot, th.sys ? GetSysColor(COLOR_HIGHLIGHT) : th.head };
    for (int k = 0; k < 5; k++) {
        if (*b[k]) DeleteObject(*b[k]);
        *b[k] = CreateSolidBrush(c[k]);
    }
}

/* Title bar colors (DWM) and dark popup menus (uxtheme, Windows 10 1903+). Both are looked up at
   run time so older Windows just keeps its default look. */
static void theme_window(HWND h)
{
    typedef HRESULT (WINAPI *DwmSetAttr)(HWND, DWORD, LPCVOID, DWORD);
    typedef int (WINAPI *SetAppMode)(int);
    typedef BOOL (WINAPI *AllowDark)(HWND, BOOL);
    typedef void (WINAPI *Flush)(void);
    static HMODULE dwm, ux;
    if (!dwm) dwm = LoadLibraryA("dwmapi.dll");
    if (!ux) ux = LoadLibraryA("uxtheme.dll");
    BOOL dark = th.dark;
    if (ux) {
        SetAppMode setMode = (SetAppMode)(void (*)(void))GetProcAddress(ux, MAKEINTRESOURCEA(135));
        AllowDark allow = (AllowDark)(void (*)(void))GetProcAddress(ux, MAKEINTRESOURCEA(133));
        Flush flush = (Flush)(void (*)(void))GetProcAddress(ux, MAKEINTRESOURCEA(136));
        if (setMode) setMode(dark ? 2 : 0);    /* ForceDark / Default */
        if (allow) allow(h, dark);
        if (flush) flush();
    }
    if (dwm) {
        DwmSetAttr set = (DwmSetAttr)(void (*)(void))GetProcAddress(dwm, "DwmSetWindowAttribute");
        if (set) {
            COLORREF cap = th.sys ? 0xFFFFFFFF : th.ui, txt = th.sys ? 0xFFFFFFFF : th.text;
            set(h, 20, &dark, sizeof dark);   /* DWMWA_USE_IMMERSIVE_DARK_MODE */
            set(h, 19, &dark, sizeof dark);   /* same, Windows 10 before 20H1 */
            set(h, 35, &cap, sizeof cap);     /* DWMWA_CAPTION_COLOR (Windows 11) */
            set(h, 36, &txt, sizeof txt);     /* DWMWA_TEXT_COLOR */
        }
    }
}

static int theme_saved(void)
{
    DWORD v = 0, sz = sizeof v;
    if (RegGetValueA(HKEY_CURRENT_USER, "Software\\" APP_NAME, "Theme", RRF_RT_REG_DWORD, NULL, &v, &sz)) return 0;
    return (int)v;
}

static void theme_save(void)
{
    HKEY k;
    DWORD v = (DWORD)themeIdx;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, "Software\\" APP_NAME, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)) return;
    RegSetValueExA(k, "Theme", 0, REG_DWORD, (const BYTE *)&v, sizeof v);
    RegCloseKey(k);
}

/* Undocumented messages uxtheme sends to let an app paint its own menu bar (as Notepad++ does). */
#define WM_UAHDRAWMENU 0x0091
#define WM_UAHDRAWMENUITEM 0x0092
typedef struct { HMENU hmenu; HDC hdc; DWORD dwFlags; } UAHMENU;
typedef struct {
    int iPosition;
    union { struct { DWORD cx, cy; } rgsizeBar[2], rgsizePopup[4]; } umim;
    struct { DWORD rgcx[4]; DWORD fUpdateMaxWidths : 2; } umpm;
} UAHMENUITEM;
typedef struct { DRAWITEMSTRUCT dis; UAHMENU um; UAHMENUITEM umi; } UAHDRAWMENUITEM;

static void menubar_paint(HWND h, LPARAM lp)
{
    UAHMENU *u = (UAHMENU *)lp;
    MENUBARINFO mbi;
    RECT wr;
    memset(&mbi, 0, sizeof mbi);
    mbi.cbSize = sizeof mbi;
    if (!GetMenuBarInfo(h, OBJID_MENU, 0, &mbi)) return;
    GetWindowRect(h, &wr);
    OffsetRect(&mbi.rcBar, -wr.left, -wr.top);
    FillRect(u->hdc, &mbi.rcBar, brUi);
}

static void menubar_item(LPARAM lp)
{
    UAHDRAWMENUITEM *d = (UAHDRAWMENUITEM *)lp;
    char txt[64] = "";
    MENUITEMINFOA mi;
    memset(&mi, 0, sizeof mi);
    mi.cbSize = sizeof mi;
    mi.fMask = MIIM_STRING;
    mi.dwTypeData = txt;
    mi.cch = sizeof txt - 1;
    GetMenuItemInfoA(d->um.hmenu, d->umi.iPosition, TRUE, &mi);
    UINT st = d->dis.itemState;
    FillRect(d->um.hdc, &d->dis.rcItem, st & (ODS_HOTLIGHT | ODS_SELECTED) ? brHot : brUi);
    SetBkMode(d->um.hdc, TRANSPARENT);
    SetTextColor(d->um.hdc, st & (ODS_INACTIVE | ODS_GRAYED | ODS_DISABLED) ? mix(th.text, th.ui) : th.text);
    DrawTextA(d->um.hdc, txt, -1, &d->dis.rcItem, DT_CENTER | DT_SINGLELINE | DT_VCENTER | (st & ODS_NOACCEL ? DT_HIDEPREFIX : 0));
}

/* the 1px light line Windows leaves between the menu bar and the client area */
static void menubar_line(HWND h)
{
    RECT rc, wr;
    GetClientRect(h, &rc);
    MapWindowPoints(h, NULL, (POINT *)&rc, 2);
    GetWindowRect(h, &wr);
    OffsetRect(&rc, -wr.left, -wr.top);
    rc.bottom = rc.top;
    rc.top--;
    HDC dc = GetWindowDC(h);
    FillRect(dc, &rc, brUi);
    ReleaseDC(h, dc);
}

static int name_used(const char *n)
{
    for (int i = 0; i < nob; i++)
        if (!strcmp(ob[i].name, n)) return 1;
    return 0;
}

static void unique_name(char *out, const char *base)
{
    char stem[24];
    snprintf(stem, sizeof stem, "%s", base);
    size_t l = strlen(stem);
    while (l && stem[l - 1] >= '0' && stem[l - 1] <= '9') stem[--l] = 0;
    if (!l) strcpy(stem, "mesh");
    for (int n = 1;; n++) {
        char c[32];
        snprintf(c, sizeof c, "%s%d", stem, n);
        if (!name_used(c)) { strcpy(out, c); return; }
    }
}

/* may move 'ob': never hold Obj pointers across this call */
static Obj *obj_new(const char *base)
{
    if (nob == cob) {
        cob = cob ? cob * 2 : 16;
        ob = realloc(ob, cob * sizeof(Obj));
        if (!ob) abort();
    }
    Obj *o = &ob[nob];
    memset(o, 0, sizeof *o);
    o->s = v3(1, 1, 1);
    unique_name(o->name, base);
    nob++;
    return o;
}

static void obj_remove(int i)
{
    mesh_free(&ob[i].m);
    memmove(ob + i, ob + i + 1, (nob - i - 1) * sizeof(Obj));
    nob--;
}

static int active(const Obj *o) { return mode == M_OBJ ? o->sel : o->hil; }

static Obj *lead(void)
{
    Obj *b = NULL;
    for (int i = 0; i < nob; i++)
        if (active(&ob[i]) && (!b || ob[i].order > b->order)) b = &ob[i];
    return b;
}

/* does the current mode work on this component kind (M_VERT/M_EDGE/M_FACE)? Multi works on all three. */
static int shows(int kind) { return mode == kind || mode == M_MULTI; }

static unsigned char *comp_flags(Obj *o, int kind, int *n)
{
    Mesh *m = &o->m;
    if (kind == M_VERT) { *n = m->nv; return m->vs; }
    if (kind == M_EDGE) { *n = m->ne; return m->es; }
    *n = m->nf;
    return m->fs;
}

static int comp_count(Obj *o)
{
    int c = 0;
    for (int k = M_VERT; k <= M_FACE; k++) {
        if (!shows(k)) continue;
        int n;
        unsigned char *f = comp_flags(o, k, &n);
        for (int i = 0; i < n; i++) c += f[i] != 0;
    }
    return c;
}

static int has_sel(void)
{
    for (int i = 0; i < nob; i++) {
        if (mode == M_OBJ ? ob[i].sel : (ob[i].hil && comp_count(&ob[i]))) return 1;
    }
    return 0;
}

static void clear_sel(void)
{
    for (int i = 0; i < nob; i++) {
        if (mode == M_OBJ) ob[i].sel = 0;
        else if (ob[i].hil) {
            for (int k = M_VERT; k <= M_FACE; k++) {
                int n;
                unsigned char *f = comp_flags(&ob[i], k, &n);
                if (shows(k)) memset(f, 0, n);
            }
        }
    }
}

/* ================================================================ undo */

typedef struct { Obj *o; int n, mode; } Snap;
#define UNDO_MAX 60
static Snap undo_s[UNDO_MAX], redo_s[UNDO_MAX];
static int nundo, nredo;

static Snap snap_take(void)
{
    Snap s;
    s.n = nob;
    s.mode = mode;
    s.o = malloc((nob ? nob : 1) * sizeof(Obj));
    for (int i = 0; i < nob; i++) {
        s.o[i] = ob[i];
        s.o[i].m = mesh_copy(&ob[i].m);
    }
    return s;
}

static void snap_free(Snap *s)
{
    for (int i = 0; i < s->n; i++) mesh_free(&s->o[i].m);
    free(s->o);
    s->o = NULL;
}

static void snap_restore(Snap *s)
{
    for (int i = 0; i < nob; i++) mesh_free(&ob[i].m);
    free(ob);
    ob = s->o;
    nob = cob = s->n;
    if (!cob) { free(ob); ob = NULL; }
    mode = s->mode;
    s->o = NULL;
    normalFrame = 0;
}

static void stack_push(Snap *st, int *n, Snap s)
{
    if (*n == UNDO_MAX) {
        snap_free(&st[0]);
        memmove(st, st + 1, (UNDO_MAX - 1) * sizeof(Snap));
        (*n)--;
    }
    st[(*n)++] = s;
}

static void undo_push(void)
{
    modified = 1;
    stack_push(undo_s, &nundo, snap_take());
    while (nredo) snap_free(&redo_s[--nredo]);
}

static void do_undo(void)
{
    if (!nundo) { msg("Nothing to undo"); return; }
    stack_push(redo_s, &nredo, snap_take());
    snap_restore(&undo_s[--nundo]);
    modified = 1;
    msg("Undo");
}

static void do_redo(void)
{
    if (!nredo) { msg("Nothing to redo"); return; }
    stack_push(undo_s, &nundo, snap_take());
    snap_restore(&redo_s[--nredo]);
    modified = 1;
    msg("Redo");
}

/* ================================================================ camera */

static V3 coi;
static float yaw = 0.7854f, pitch = 0.46f, dist = 10.0f;
static const float FOVY = 0.6981f;
static M4 Pm, Vm, VPm, iVPm;
static V3 eye, camF, camR, camU;

static void cam_update(void)
{
    V3 dir = v3(cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw));
    eye = v3add(coi, v3mul(dir, dist));
    Vm = m4_lookat(eye, coi, v3(0, 1, 0));
    Pm = m4_persp(FOVY, (float)vw / vh, dist * 0.01f, dist * 1000.0f);
    VPm = m4_mul(Pm, Vm);
    iVPm = m4_inv(VPm);
    camF = v3mul(dir, -1);
    camR = v3(Vm.m[0], Vm.m[4], Vm.m[8]);
    camU = v3(Vm.m[1], Vm.m[5], Vm.m[9]);
}

static int proj(V3 p, float *x, float *y, float *z)
{
    const float *m = VPm.m;
    float cx = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
    float cy = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
    float cz = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
    float cw = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
    if (cw < 1e-6f) return 0;
    *x = (cx / cw * 0.5f + 0.5f) * vw;
    *y = (0.5f - cy / cw * 0.5f) * vh;
    if (z) *z = cz / cw * 0.5f + 0.5f;
    return 1;
}

static V3 unproj(float nx, float ny, float nz)
{
    const float *m = iVPm.m;
    float x = m[0] * nx + m[4] * ny + m[8] * nz + m[12];
    float y = m[1] * nx + m[5] * ny + m[9] * nz + m[13];
    float z = m[2] * nx + m[6] * ny + m[10] * nz + m[14];
    float w = m[3] * nx + m[7] * ny + m[11] * nz + m[15];
    return v3(x / w, y / w, z / w);
}

static void mouse_ray(float sx, float sy, V3 *o, V3 *d)
{
    float nx = 2 * sx / vw - 1, ny = 1 - 2 * sy / vh;
    V3 a = unproj(nx, ny, -1), b = unproj(nx, ny, 1);
    *o = a;
    *d = v3norm(v3sub(b, a));
}

/* world units per pixel at point p */
static float wpp(V3 p)
{
    float depth = v3dot(v3sub(p, eye), camF);
    if (depth < 1e-4f) depth = 1e-4f;
    return 2 * depth * tanf(FOVY * 0.5f) / vh;
}

static int ray_plane(V3 o, V3 d, V3 p, V3 n, V3 *hit)
{
    float den = v3dot(d, n);
    if (fabsf(den) < 1e-8f) return 0;
    *hit = v3add(o, v3mul(d, v3dot(v3sub(p, o), n) / den));
    return 1;
}

/* closest point on line p + a*t to the ray o + d*s, returns t */
static float line_param(V3 p, V3 a, V3 o, V3 d)
{
    V3 w = v3sub(p, o);
    float b = v3dot(a, d), den = 1 - b * b;
    if (den < 1e-6f) return 0;
    return (b * v3dot(d, w) - v3dot(a, w)) / den;
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by, float *u)
{
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy;
    float t = l2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / l2 : 0;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    if (u) *u = t;
    return hypotf(px - (ax + t * dx), py - (ay + t * dy));
}

/* ================================================================ drawing helpers */

static void load_view(void)
{
    glMatrixMode(GL_PROJECTION);
    glLoadMatrixf(Pm.m);
    glMatrixMode(GL_MODELVIEW);
    glLoadMatrixf(Vm.m);
}

/* selected faces only, untextured (wireframe face-mode tint) */
static void draw_sel_tris(const Mesh *m)
{
    glBegin(GL_TRIANGLES);
    for (int f = 0; f < m->nf; f++) {
        if (!m->fs[f]) continue;
        int s = m->fo[f], n = m->fo[f + 1] - s;
        for (int k = 1; k + 1 < n; k++) {
            glVertex3fv(&m->p[m->fi[s]].x);
            glVertex3fv(&m->p[m->fi[s + k]].x);
            glVertex3fv(&m->p[m->fi[s + k + 1]].x);
        }
    }
    glEnd();
}

static void draw_edges(const Mesh *m, int selOnly)
{
    glBegin(GL_LINES);
    for (int e = 0; e < m->ne; e++) {
        if (selOnly && !m->es[e]) continue;
        glVertex3fv(&m->p[m->ev[2 * e]].x);
        glVertex3fv(&m->p[m->ev[2 * e + 1]].x);
    }
    glEnd();
}

/* ================================================================ draw cache

   Shaded triangles are expanded once into a vertex array (color, face normal, position per corner)
   and drawn with one glDrawArrays call. The arrays are rebuilt only when geoGen changes, which
   happens on every edit or selection change, so tumbling, hovering and idle redraws are cheap. */

typedef struct { const V3 *key; int nv, nf, ntri; unsigned gen; float *v; V3 lo, hi; } DrawCache;
static DrawCache *dcache;
static int ndcache;
static unsigned geoGen = 1;

static void geo_changed(void) { geoGen++; }

static DrawCache *cache_get(int i)
{
    if (i >= ndcache) {
        dcache = realloc(dcache, (i + 1) * sizeof *dcache);
        memset(dcache + ndcache, 0, (i + 1 - ndcache) * sizeof *dcache);
        ndcache = i + 1;
    }
    DrawCache *c = &dcache[i];
    const Obj *o = &ob[i];
    const Mesh *m = &o->m;
    if (c->gen == geoGen && c->key == m->p && c->nv == m->nv && c->nf == m->nf) return c;
    int tint = o->hil && shows(M_FACE), nt = 0;
    for (int f = 0; f < m->nf; f++) nt += m->fo[f + 1] - m->fo[f] - 2;
    c->v = realloc(c->v, (size_t)(nt > 0 ? nt : 1) * 27 * sizeof(float));
    float *w = c->v;
    for (int f = 0; f < m->nf; f++) {
        int s = m->fo[f], n = m->fo[f + 1] - s;
        V3 nn = mesh_fnormal(m, f);
        for (int k = 1; k + 1 < n; k++) {
            int idx[3] = { m->fi[s], m->fi[s + k], m->fi[s + k + 1] };
            for (int j = 0; j < 3; j++) {
                V3 col = m->c[idx[j]], p = m->p[idx[j]];
                if (tint && m->fs[f]) col = v3lerp(col, v3(1, 0.45f, 0.1f), 0.65f);
                *w++ = col.x; *w++ = col.y; *w++ = col.z;
                *w++ = nn.x; *w++ = nn.y; *w++ = nn.z;
                *w++ = p.x; *w++ = p.y; *w++ = p.z;
            }
        }
    }
    c->lo = v3(1e30f, 1e30f, 1e30f);
    c->hi = v3(-1e30f, -1e30f, -1e30f);
    for (int v = 0; v < m->nv; v++) {
        V3 p = m->p[v];
        c->lo = v3(fminf(c->lo.x, p.x), fminf(c->lo.y, p.y), fminf(c->lo.z, p.z));
        c->hi = v3(fmaxf(c->hi.x, p.x), fmaxf(c->hi.y, p.y), fmaxf(c->hi.z, p.z));
    }
    c->ntri = nt;
    c->key = m->p;
    c->nv = m->nv;
    c->nf = m->nf;
    c->gen = geoGen;
    return c;
}

/* shade: colors and normals for lighting; otherwise positions only (depth pass) */
static void draw_cached(int i, int shade)
{
    DrawCache *c = cache_get(i);
    if (!c->ntri) return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, 9 * sizeof(float), c->v + 6);
    if (shade) {
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glColorPointer(3, GL_FLOAT, 9 * sizeof(float), c->v);
        glNormalPointer(GL_FLOAT, 9 * sizeof(float), c->v + 3);
    }
    glDrawArrays(GL_TRIANGLES, 0, c->ntri * 3);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_NORMAL_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

/* all edges straight from the mesh arrays (ev holds vertex index pairs) */
static void draw_all_edges(const Mesh *m)
{
    if (!m->ne) return;
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(V3), m->p);
    glDrawElements(GL_LINES, m->ne * 2, GL_UNSIGNED_INT, m->ev);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static int ray_box(V3 o, V3 d, V3 lo, V3 hi)
{
    float t0 = -1e30f, t1 = 1e30f;
    for (int a = 0; a < 3; a++) {
        float oa = (&o.x)[a], da = (&d.x)[a], l = (&lo.x)[a], h = (&hi.x)[a];
        if (fabsf(da) < 1e-12f) { if (oa < l || oa > h) return 0; continue; }
        float ta = (l - oa) / da, tb = (h - oa) / da;
        if (ta > tb) { float t = ta; ta = tb; tb = t; }
        if (ta > t0) t0 = ta;
        if (tb < t1) t1 = tb;
    }
    return t0 <= t1 && t1 >= 0;
}

/* world space ray against object i, skipping the triangles when the ray misses its bounding box */
static int obj_raycast(int i, V3 o, V3 d, float *t)
{
    DrawCache *c = cache_get(i);
    M4 iM = m4_inv(obj_mat(&ob[i]));
    V3 lo = m4_pt(iM, o), ld = m4_dir(iM, d);
    V3 pad = v3mul(v3sub(c->hi, c->lo), 1e-3f);
    if (!ray_box(lo, ld, v3sub(c->lo, pad), v3add(c->hi, pad))) return -1;
    return mesh_raycast(&ob[i].m, lo, ld, t);
}

/* ================================================================ depth readback for occlusion-aware picking */

static float *dbuf;
static int dx0, dy0, dw, dh, dvalid;

static void depth_capture(int x0, int y0, int x1, int y1)
{
    dvalid = 0;
    if (wire) return;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > vw - 1) x1 = vw - 1;
    if (y1 > vh - 1) y1 = vh - 1;
    if (x1 < x0 || y1 < y0) return;
    cam_update();
    glViewport(0, 0, vw, vh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    load_view();
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glColorMask(0, 0, 0, 0);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1, 1);
    for (int i = 0; i < nob; i++) {
        M4 M = obj_mat(&ob[i]);
        glPushMatrix();
        glMultMatrixf(M.m);
        draw_cached(i, 0);
        glPopMatrix();
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glColorMask(1, 1, 1, 1);
    dw = x1 - x0 + 1;
    dh = y1 - y0 + 1;
    dx0 = x0;
    dy0 = y0;
    dbuf = realloc(dbuf, (size_t)dw * dh * sizeof(float));
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(x0, vh - 1 - y1, dw, dh, GL_DEPTH_COMPONENT, GL_FLOAT, dbuf);
    dvalid = 1;
}

static int visible(float x, float y, float z)
{
    if (wire || !dvalid) return 1;
    int ix = (int)x, iy = (int)y;
    if (ix < dx0 || iy < dy0 || ix >= dx0 + dw || iy >= dy0 + dh) return 0;
    return z <= dbuf[(dh - 1 - (iy - dy0)) * dw + (ix - dx0)] + 2e-5f;
}

/* ================================================================ selection */

static void set_flag(unsigned char *f, int mod) { *f = mod == 1 ? !*f : mod == 2 ? 0 : 1; }

static void select_obj_flag(Obj *o, int mod)
{
    o->sel = mod == 1 ? !o->sel : mod == 2 ? 0 : 1;
    if (o->sel) o->order = ++stamp;
}

static int pick_object(int mx, int my, float *tout)
{
    V3 o, d;
    mouse_ray((float)mx, (float)my, &o, &d);
    int best = -1;
    float bt = 1e30f;
    for (int i = 0; i < nob; i++) {
        float t;
        if (obj_raycast(i, o, d, &t) >= 0 && t < bt) { bt = t; best = i; }
    }
    if (tout) *tout = bt;
    return best;
}

/* Component of a hilited object under the cursor. Multi mode prefers a vertex, then an edge, then the
   face under the cursor, like Maya's multi-component selection. Returns 0 when nothing is close. */
static int pick_comp(int mx, int my, int *obj, int *kind, int *idx)
{
    int R = (int)(12 * ds), multi = mode == M_MULTI;
    *obj = -1;
    if (shows(M_VERT) || shows(M_EDGE)) depth_capture(mx - R - 4, my - R - 4, mx + R + 4, my + R + 4);
    if (shows(M_VERT)) {
        float best = multi ? 8 * ds : (float)R;
        for (int i = 0; i < nob; i++) {
            if (!ob[i].hil) continue;
            Mesh *m = &ob[i].m;
            M4 M = obj_mat(&ob[i]);
            for (int v = 0; v < m->nv; v++) {
                float x, y, z;
                if (!proj(m4_pt(M, m->p[v]), &x, &y, &z)) continue;
                float dd = hypotf(x - mx, y - my);
                if (dd < best && visible(x, y, z)) { best = dd; *obj = i; *kind = M_VERT; *idx = v; }
            }
        }
        if (*obj >= 0) return 1;
    }
    if (shows(M_EDGE)) {
        float best = multi ? 6 * ds : (float)R;
        for (int i = 0; i < nob; i++) {
            if (!ob[i].hil) continue;
            Mesh *m = &ob[i].m;
            M4 M = obj_mat(&ob[i]);
            for (int e = 0; e < m->ne; e++) {
                V3 a = m4_pt(M, m->p[m->ev[2 * e]]), b = m4_pt(M, m->p[m->ev[2 * e + 1]]);
                float ax, ay, bx, by, u, x, y, z;
                if (!proj(a, &ax, &ay, NULL) || !proj(b, &bx, &by, NULL)) continue;
                float dd = seg_dist((float)mx, (float)my, ax, ay, bx, by, &u);
                if (dd >= best) continue;
                if (!proj(v3lerp(a, b, u), &x, &y, &z) || !visible(x, y, z)) continue;
                best = dd; *obj = i; *kind = M_EDGE; *idx = e;
            }
        }
        if (*obj >= 0) return 1;
    }
    if (shows(M_FACE)) {
        V3 o, d;
        mouse_ray((float)mx, (float)my, &o, &d);
        float bt = 1e30f;
        for (int i = 0; i < nob; i++) {
            if (!ob[i].hil) continue;
            float t;
            int f = obj_raycast(i, o, d, &t);
            if (f >= 0 && t < bt) { bt = t; *obj = i; *kind = M_FACE; *idx = f; }
        }
    }
    return *obj >= 0;
}

static void select_click(int mx, int my, int mod)
{
    cam_update();
    if (mode == M_OBJ) {
        int hit = pick_object(mx, my, NULL);
        if (mod == 0) clear_sel();
        if (hit >= 0) select_obj_flag(&ob[hit], mod);
        return;
    }
    int bo, kind, bi;
    pick_comp(mx, my, &bo, &kind, &bi);
    if (mod == 0) clear_sel();
    if (bo >= 0) {
        int n;
        unsigned char *f = comp_flags(&ob[bo], kind, &n);
        set_flag(&f[bi], mod);
        return;
    }
    /* clicked another object: hilite it for component editing */
    int oh = pick_object(mx, my, NULL);
    if (oh >= 0 && !ob[oh].hil && mod != 2) {
        if (mod == 0)
            for (int i = 0; i < nob; i++) ob[i].hil = 0;
        ob[oh].hil = 1;
        ob[oh].order = ++stamp;
    }
}

static void select_rect(int x0, int y0, int x1, int y1, int mod)
{
    if (x0 > x1) { int t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { int t = y0; y0 = y1; y1 = t; }
    cam_update();
    if (mode != M_OBJ) depth_capture(x0, y0, x1, y1);
    if (mod == 0) clear_sel();
#define INSIDE(x, y) ((x) >= x0 && (x) <= x1 && (y) >= y0 && (y) <= y1)
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        M4 M = obj_mat(o);
        float x, y, z;
        if (mode == M_OBJ) {
            for (int v = 0; v < m->nv; v++)
                if (proj(m4_pt(M, m->p[v]), &x, &y, NULL) && INSIDE(x, y)) { select_obj_flag(o, mod); break; }
            continue;
        }
        if (!o->hil) continue;
        if (shows(M_VERT)) {
            for (int v = 0; v < m->nv; v++)
                if (proj(m4_pt(M, m->p[v]), &x, &y, &z) && INSIDE(x, y) && visible(x, y, z)) set_flag(&m->vs[v], mod);
        }
        if (shows(M_EDGE)) {
            for (int e = 0; e < m->ne; e++) {
                V3 c = v3mul(v3add(m->p[m->ev[2 * e]], m->p[m->ev[2 * e + 1]]), 0.5f);
                if (proj(m4_pt(M, c), &x, &y, &z) && INSIDE(x, y) && visible(x, y, z)) set_flag(&m->es[e], mod);
            }
        }
        if (shows(M_FACE)) {
            for (int f = 0; f < m->nf; f++)
                if (proj(m4_pt(M, mesh_fcenter(m, f)), &x, &y, &z) && INSIDE(x, y) && visible(x, y, z)) set_flag(&m->fs[f], mod);
        }
    }
#undef INSIDE
}

/* bounding box of the selection (or of everything) in world space */
static int sel_bbox(V3 *mn, V3 *mx, int all)
{
    int any = 0;
    *mn = v3(1e30f, 1e30f, 1e30f);
    *mx = v3(-1e30f, -1e30f, -1e30f);
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        unsigned char *mask = NULL;
        if (!all) {
            if (!active(o)) continue;
            if (mode != M_OBJ) {
                mask = malloc(m->nv + 1);
                if (!mesh_vmask(m, mode, mask)) { free(mask); continue; }
            }
        }
        M4 M = obj_mat(o);
        for (int v = 0; v < m->nv; v++) {
            if (mask && !mask[v]) continue;
            V3 p = m4_pt(M, m->p[v]);
            mn->x = fminf(mn->x, p.x); mn->y = fminf(mn->y, p.y); mn->z = fminf(mn->z, p.z);
            mx->x = fmaxf(mx->x, p.x); mx->y = fmaxf(mx->y, p.y); mx->z = fmaxf(mx->z, p.z);
            any = 1;
        }
        free(mask);
    }
    return any;
}

static V3 sel_pivot(void)
{
    if (mode == M_OBJ) {
        Obj *l = lead();
        return l ? l->t : v3(0, 0, 0);
    }
    V3 a, b;
    if (!sel_bbox(&a, &b, 0)) return v3(0, 0, 0);
    return v3mul(v3add(a, b), 0.5f);
}

static void frame(int all)
{
    V3 a, b;
    if (!sel_bbox(&a, &b, all) && !sel_bbox(&a, &b, 1)) {
        coi = v3(0, 0, 0);
        dist = 10;
        return;
    }
    coi = v3mul(v3add(a, b), 0.5f);
    float r = v3len(v3sub(b, a)) * 0.5f;
    if (r < 0.05f) r = 0.05f;
    float half = FOVY * 0.5f, hh = atanf(tanf(half) * vw / vh);
    dist = r / sinf(hh < half ? hh : half) * 1.1f;
}

/* ================================================================ modes & tools */

static void set_mode(int m)
{
    if (m == mode) return;
    if (m != M_OBJ) lastComp = m;
    if (mode == M_OBJ)
        for (int i = 0; i < nob; i++) ob[i].hil = ob[i].sel;
    if (m == M_OBJ)
        for (int i = 0; i < nob; i++) { ob[i].sel = ob[i].hil; ob[i].hil = 0; }
    mode = m;
    normalFrame = 0;
}

static void set_tool(int t)
{
    if (t != tool) lastHandle = H_NONE;
    tool = t;
    normalFrame = 0;
}

/* ================================================================ manipulator */

typedef struct { V3 piv, ax[3]; float L; } MG;

/* has_sel() and sel_pivot() walk every selected vertex, and the manipulator needs them on every
   mouse move and redraw, so keep them until the next edit or selection change (geoGen) */
static struct { unsigned gen; int any; V3 piv; } selc;

static void sel_cache(void)
{
    if (selc.gen == geoGen) return;
    selc.any = has_sel();
    selc.piv = selc.any ? sel_pivot() : v3(0, 0, 0);
    selc.gen = geoGen;
}

static int manip_vis(void)
{
    if (tool != T_MOVE && tool != T_ROTATE && tool != T_SCALE) return 0;
    sel_cache();
    return selc.any;
}

static M4 manip_frame(void)
{
    if (normalFrame && shows(M_FACE) && tool != T_ROTATE) return nframe;
    if (tool == T_SCALE && mode == M_OBJ) {
        Obj *l = lead();
        if (l) return m4_euler(l->r);
    }
    return m4_id();
}

typedef struct { int obj, n; int *vi; V3 *p0; V3 t0, r0, s0; } DEnt;
static struct {
    int on, handle, mx0, my0, dirty, planeMode;
    V3 piv, cur, ax[3], axis, pn, hit0, prevv;
    float L, Lpx, t0, ang, tdx, tdy, sdx, sdy;
    DEnt *e;
    int ne;
} dg;

static MG mg_get(void)
{
    MG g;
    sel_cache();
    g.piv = dg.on ? dg.cur : selc.piv;
    M4 F = dg.on ? m4_id() : manip_frame();
    for (int i = 0; i < 3; i++) g.ax[i] = dg.on ? dg.ax[i] : v3norm(m4_col(F, i));
    g.L = manipSize * 80 * ds * wpp(g.piv);
    return g;
}

static V3 ring_pt(const MG *g, int i, float a)
{
    V3 u, w;
    float r = g->L;
    if (i < 3) { u = g->ax[(i + 1) % 3]; w = g->ax[(i + 2) % 3]; }
    else { u = camR; w = camU; r *= 1.2f; }
    return v3add(g->piv, v3add(v3mul(u, cosf(a) * r), v3mul(w, sinf(a) * r)));
}

static int ring_front(const MG *g, V3 q)
{
    return v3dot(v3norm(v3sub(q, g->piv)), v3norm(v3sub(eye, g->piv))) > -0.02f;
}

static void plane_corners(const MG *g, int i, V3 c[4])
{
    V3 a = g->ax[(i + 1) % 3], b = g->ax[(i + 2) % 3];
    static const float uv[4][2] = { { .25f, .25f }, { .45f, .25f }, { .45f, .45f }, { .25f, .45f } };
    for (int k = 0; k < 4; k++) c[k] = v3add(g->piv, v3add(v3mul(a, uv[k][0] * g->L), v3mul(b, uv[k][1] * g->L)));
}

static int manip_hit(int mx, int my)
{
    cam_update();
    MG g = mg_get();
    float sx, sy, best = 9 * ds;
    int h = H_NONE;
    if (!proj(g.piv, &sx, &sy, NULL)) return H_NONE;
    if (tool == T_ROTATE) {
        for (int i = 0; i < 4; i++)
            for (int k = 0; k < 64; k++) {
                V3 q0 = ring_pt(&g, i, 2 * PI_F * k / 64), q1 = ring_pt(&g, i, 2 * PI_F * (k + 1) / 64);
                float ax, ay, bx, by;
                if (i < 3 && !ring_front(&g, v3mul(v3add(q0, q1), 0.5f))) continue;
                if (!proj(q0, &ax, &ay, NULL) || !proj(q1, &bx, &by, NULL)) continue;
                float d = seg_dist((float)mx, (float)my, ax, ay, bx, by, NULL);
                if (d < best) { best = d; h = i < 3 ? H_X + i : H_VIEW; }
            }
        return h;
    }
    if (hypotf(mx - sx, my - sy) < 9 * ds) return H_C;
    for (int i = 0; i < 3; i++) {
        V3 c[4];
        float px[4], py[4];
        int ok = 1, pos = 0, neg = 0;
        plane_corners(&g, i, c);
        for (int k = 0; k < 4; k++) ok &= proj(c[k], &px[k], &py[k], NULL);
        if (!ok) continue;
        for (int k = 0; k < 4; k++) {
            int j = (k + 1) % 4;
            float cr = (px[j] - px[k]) * (my - py[k]) - (py[j] - py[k]) * (mx - px[k]);
            pos += cr > 0;
            neg += cr < 0;
        }
        if (!pos || !neg) return H_YZ + i;
    }
    for (int i = 0; i < 3; i++) {
        float tx, ty;
        if (!proj(v3add(g.piv, v3mul(g.ax[i], g.L)), &tx, &ty, NULL)) continue;
        float d = seg_dist((float)mx, (float)my, sx, sy, tx, ty, NULL);
        if (d < best) { best = d; h = H_X + i; }
    }
    return h;
}

static void drag_begin(int h, int mx, int my)
{
    cam_update();
    MG g = mg_get();
    memset(&dg, 0, sizeof dg);
    if ((tool == T_MOVE || tool == T_SCALE) && h >= H_X && h <= H_Z && key(VK_CONTROL)) h = H_YZ + (h - H_X);
    if (h == H_NONE) h = tool == T_ROTATE ? H_VIEW : H_C;
    dg.handle = lastHandle = h;
    dg.mx0 = mx;
    dg.my0 = my;
    dg.piv = dg.cur = g.piv;
    for (int i = 0; i < 3; i++) dg.ax[i] = g.ax[i];
    dg.L = g.L;
    dg.Lpx = manipSize * 80 * ds;
    V3 o, d;
    mouse_ray((float)mx, (float)my, &o, &d);
    float psx = 0, psy = 0;
    proj(g.piv, &psx, &psy, NULL);

    if (tool == T_MOVE) {
        if (h >= H_X && h <= H_Z) {
            dg.axis = g.ax[h - H_X];
            dg.t0 = line_param(g.piv, dg.axis, o, d);
        } else {
            dg.pn = h == H_C ? camF : g.ax[h - H_YZ];
            if (!ray_plane(o, d, g.piv, dg.pn, &dg.hit0)) dg.hit0 = g.piv;
        }
    } else if (tool == T_ROTATE) {
        V3 toEye = v3norm(v3sub(eye, g.piv));
        dg.axis = h == H_VIEW ? toEye : g.ax[h - H_X];
        dg.planeMode = fabsf(v3dot(dg.axis, toEye)) > 0.2f;
        V3 hit;
        if (dg.planeMode && ray_plane(o, d, g.piv, dg.axis, &hit)) {
            dg.prevv = v3sub(hit, g.piv);
        } else {
            /* ring seen edge-on: drag along the ring tangent instead */
            dg.planeMode = 0;
            int ri = h == H_VIEW ? 3 : h - H_X;
            V3 q = g.piv;
            float bd = 1e30f;
            for (int k = 0; k < 64; k++) {
                V3 p = ring_pt(&g, ri, 2 * PI_F * k / 64);
                float x, y;
                if (proj(p, &x, &y, NULL) && hypotf(x - mx, y - my) < bd) { bd = hypotf(x - mx, y - my); q = p; }
            }
            V3 tan = v3norm(v3cross(dg.axis, v3sub(q, g.piv)));
            float ax, ay, bx, by;
            dg.tdx = 1;
            dg.tdy = 0;
            if (proj(q, &ax, &ay, NULL) && proj(v3add(q, v3mul(tan, g.L * 0.2f)), &bx, &by, NULL)) {
                float l = hypotf(bx - ax, by - ay);
                if (l > 2) { dg.tdx = (bx - ax) / l; dg.tdy = (by - ay) / l; }
            }
        }
    } else {
        V3 tip = g.piv;
        if (h >= H_X && h <= H_Z) tip = v3add(g.piv, v3mul(g.ax[h - H_X], g.L));
        else if (h >= H_YZ && h <= H_XY) {
            int i = h - H_YZ;
            tip = v3add(g.piv, v3mul(v3add(g.ax[(i + 1) % 3], g.ax[(i + 2) % 3]), g.L * 0.35f));
        }
        float tx, ty;
        dg.sdx = 1;
        dg.sdy = 0;
        if (h != H_C && proj(tip, &tx, &ty, NULL)) {
            float l = hypotf(tx - psx, ty - psy);
            if (l > 0.15f * dg.Lpx) { dg.sdx = (tx - psx) / l; dg.sdy = (ty - psy) / l; }
        }
    }

    /* remember the original state; every drag step is applied from it */
    dg.e = calloc(nob + 1, sizeof(DEnt));
    for (int i = 0; i < nob; i++) {
        Obj *ob_ = &ob[i];
        if (!active(ob_)) continue;
        DEnt *e = &dg.e[dg.ne];
        e->obj = i;
        e->t0 = ob_->t;
        e->r0 = ob_->r;
        e->s0 = ob_->s;
        if (mode != M_OBJ) {
            unsigned char *mask = malloc(ob_->m.nv + 1);
            int n = mesh_vmask(&ob_->m, mode, mask);
            if (!n) { free(mask); continue; }
            e->vi = malloc(n * sizeof(int));
            e->p0 = malloc(n * sizeof(V3));
            for (int v = 0; v < ob_->m.nv; v++)
                if (mask[v]) { e->vi[e->n] = v; e->p0[e->n] = ob_->m.p[v]; e->n++; }
            free(mask);
        }
        dg.ne++;
    }
    dg.on = 1;
}

static void drag_move(int mx, int my)
{
    cam_update();
    V3 o, d, delta = v3(0, 0, 0), sv = v3(1, 1, 1);
    M4 R = m4_id();
    int h = dg.handle;
    mouse_ray((float)mx, (float)my, &o, &d);
    if (tool == T_MOVE) {
        if (h >= H_X && h <= H_Z) {
            delta = v3mul(dg.axis, line_param(dg.piv, dg.axis, o, d) - dg.t0);
        } else {
            V3 hit;
            if (!ray_plane(o, d, dg.piv, dg.pn, &hit)) return;
            delta = v3sub(hit, dg.hit0);
        }
        int world = dg.ax[0].x > 0.9999f && dg.ax[1].y > 0.9999f;
        if (key('X') && world) {
            /* hold X: snap to the grid */
            V3 np = v3add(dg.piv, delta);
            for (int c = 0; c < 3; c++)
                if (fabsf((&delta.x)[c]) > 1e-6f) (&np.x)[c] = roundf((&np.x)[c]);
            delta = v3sub(np, dg.piv);
        }
        snprintf(g_msg, sizeof g_msg, "Move %.3f, %.3f, %.3f", delta.x, delta.y, delta.z);
    } else if (tool == T_ROTATE) {
        if (dg.planeMode) {
            V3 hit;
            if (ray_plane(o, d, dg.piv, dg.axis, &hit)) {
                V3 v = v3sub(hit, dg.piv);
                if (v3len(v) > 1e-6f * dg.L && v3len(dg.prevv) > 1e-6f * dg.L)
                    dg.ang += atan2f(v3dot(dg.axis, v3cross(dg.prevv, v)), v3dot(dg.prevv, v));
                dg.prevv = v;
            }
        } else {
            dg.ang = ((mx - dg.mx0) * dg.tdx + (my - dg.my0) * dg.tdy) / dg.Lpx;
        }
        float a = dg.ang;
        if (key('J')) a = roundf(a / (PI_F / 12)) * (PI_F / 12);   /* hold J: 15 degree steps */
        R = m4_axis_angle(dg.axis, a);
        snprintf(g_msg, sizeof g_msg, "Rotate %.1f deg", a * 180 / PI_F);
    } else {
        float f = 1 + ((mx - dg.mx0) * dg.sdx + (my - dg.my0) * dg.sdy) / dg.Lpx;
        if (mode != M_OBJ) {
            /* components stop at 0 like Maya, so dragging in past the pivot flattens them exactly */
            if (f < 0.02f) f = 0;
        } else if (fabsf(f) < 1e-3f) f = 1e-3f;
        snprintf(g_msg, sizeof g_msg, f == 0 ? "Scale 0 (flattened)" : "Scale %.3f", f);
        if (h == H_C) sv = v3(f, f, f);
        else if (h >= H_X && h <= H_Z) (&sv.x)[h - H_X] = f;
        else { int i = h - H_YZ; (&sv.x)[(i + 1) % 3] = f; (&sv.x)[(i + 2) % 3] = f; }
    }
    if (!dg.dirty) {
        if (mx == dg.mx0 && my == dg.my0) return;
        undo_push();
        dg.dirty = 1;
    }
    for (int k = 0; k < dg.ne; k++) {
        DEnt *e = &dg.e[k];
        Obj *ob_ = &ob[e->obj];
        if (mode == M_OBJ) {
            if (tool == T_MOVE) ob_->t = v3add(e->t0, delta);
            else if (tool == T_ROTATE) ob_->r = m4_to_euler(m4_mul(R, m4_euler(e->r0)));
            else ob_->s = v3(e->s0.x * sv.x, e->s0.y * sv.y, e->s0.z * sv.z);
            continue;
        }
        M4 M = obj_mat(ob_), iM = m4_inv(M);
        for (int j = 0; j < e->n; j++) {
            V3 w = m4_pt(M, e->p0[j]);
            if (tool == T_MOVE) {
                w = v3add(w, delta);
            } else if (tool == T_ROTATE) {
                w = v3add(dg.piv, m4_dir(R, v3sub(w, dg.piv)));
            } else {
                V3 dd = v3sub(w, dg.piv), n = dg.piv;
                for (int c = 0; c < 3; c++) n = v3add(n, v3mul(dg.ax[c], v3dot(dd, dg.ax[c]) * (&sv.x)[c]));
                w = n;
            }
            ob_->m.p[e->vi[j]] = m4_pt(iM, w);
        }
    }
    dg.cur = v3add(dg.piv, delta);
    geo_changed();
}

static void drag_end(void)
{
    for (int k = 0; k < dg.ne; k++) {
        if (dg.dirty && mode != M_OBJ) ob[dg.e[k].obj].prim.type = PRIM_NONE;
        free(dg.e[k].vi);
        free(dg.e[k].p0);
    }
    free(dg.e);
    dg.e = NULL;
    dg.on = 0;
}

static void draw_cone(V3 base, V3 a, float len, float r)
{
    V3 u = v3norm(v3cross(a, fabsf(a.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0))), w = v3cross(a, u);
    V3 tip = v3add(base, v3mul(a, len));
    glBegin(GL_TRIANGLE_FAN);
    glVertex3fv(&tip.x);
    for (int k = 0; k <= 10; k++) {
        float t = 2 * PI_F * k / 10;
        V3 p = v3add(base, v3add(v3mul(u, cosf(t) * r), v3mul(w, sinf(t) * r)));
        glVertex3fv(&p.x);
    }
    glEnd();
}

static void draw_manip(void)
{
    if (!manip_vis()) return;
    MG g = mg_get();
    int act = dg.on ? dg.handle : hover;
    static const float col[4][3] = { { .9f, .15f, .15f }, { .2f, .85f, .2f }, { .25f, .4f, 1 }, { .4f, .8f, 1 } };
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_LIGHTING);
    glLineWidth(2 * ds);
#define HCOL(i, hid) (act == (hid) ? glColor3f(1, 1, 0) : glColor3fv(col[i]))
    if (tool == T_ROTATE) {
        for (int i = 0; i < 4; i++) {
            HCOL(i, i < 3 ? H_X + i : H_VIEW);
            glBegin(GL_LINES);
            for (int k = 0; k < 64; k++) {
                V3 q0 = ring_pt(&g, i, 2 * PI_F * k / 64), q1 = ring_pt(&g, i, 2 * PI_F * (k + 1) / 64);
                if (i < 3 && !ring_front(&g, v3mul(v3add(q0, q1), 0.5f))) continue;
                glVertex3fv(&q0.x);
                glVertex3fv(&q1.x);
            }
            glEnd();
        }
    } else {
        for (int i = 0; i < 3; i++) {
            V3 tip = v3add(g.piv, v3mul(g.ax[i], g.L));
            HCOL(i, H_X + i);
            glBegin(GL_LINES);
            glVertex3fv(&g.piv.x);
            glVertex3fv(&tip.x);
            glEnd();
            if (tool == T_MOVE) {
                draw_cone(tip, g.ax[i], g.L * 0.18f, g.L * 0.06f);
            } else {
                glPointSize(9 * ds);
                glBegin(GL_POINTS);
                glVertex3fv(&tip.x);
                glEnd();
            }
        }
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        for (int i = 0; i < 3; i++) {
            V3 c[4];
            plane_corners(&g, i, c);
            if (act == H_YZ + i) glColor4f(1, 1, 0, .7f);
            else glColor4f(col[i][0], col[i][1], col[i][2], .45f);
            glBegin(GL_QUADS);
            for (int k = 0; k < 4; k++) glVertex3fv(&c[k].x);
            glEnd();
        }
        glDisable(GL_BLEND);
        HCOL(3, H_C);
        float s = 5 * ds * wpp(g.piv);
        V3 a = v3mul(camR, s), b = v3mul(camU, s);
        V3 q[4] = { v3sub(v3sub(g.piv, a), b), v3sub(v3add(g.piv, a), b), v3add(v3add(g.piv, a), b), v3add(v3sub(g.piv, a), b) };
        glBegin(tool == T_SCALE ? GL_QUADS : GL_LINE_LOOP);
        for (int k = 0; k < 4; k++) glVertex3fv(&q[k].x);
        glEnd();
    }
#undef HCOL
    glLineWidth(1);
    glEnable(GL_DEPTH_TEST);
}

/* ================================================================ vertex paint */

static struct { int on, dirty, n; int *obj; V3 **orig; float **wt; } ps;

static void paint_dab(int mx, int my)
{
    float r = paintRad * ds;
    for (int k = 0; k < ps.n; k++) {
        Obj *o = &ob[ps.obj[k]];
        Mesh *m = &o->m;
        M4 M = obj_mat(o);
        for (int v = 0; v < m->nv; v++) {
            float x, y, z;
            if (!proj(m4_pt(M, m->p[v]), &x, &y, &z)) continue;
            float dd = hypotf(x - mx, y - my);
            if (dd >= r || !visible(x, y, z)) continue;
            float t = dd / r, f = t < 0.5f ? 1 : 1 - (t - 0.5f) * 2;
            f = f * f * (3 - 2 * f);
            float w = paintOpa * f;
            if (w <= ps.wt[k][v]) continue;
            if (!ps.dirty) { undo_push(); ps.dirty = 1; }
            ps.wt[k][v] = w;
            m->c[v] = v3lerp(ps.orig[k][v], paintCol, w);
        }
    }
    geo_changed();
}

static void paint_begin(int mx, int my)
{
    cam_update();
    depth_capture(0, 0, vw - 1, vh - 1);
    int any = 0;
    for (int i = 0; i < nob; i++) any |= active(&ob[i]);
    memset(&ps, 0, sizeof ps);
    ps.obj = malloc((nob + 1) * sizeof(int));
    ps.orig = malloc((nob + 1) * sizeof(V3 *));
    ps.wt = malloc((nob + 1) * sizeof(float *));
    for (int i = 0; i < nob; i++) {
        if (any && !active(&ob[i])) continue;
        Mesh *m = &ob[i].m;
        ps.obj[ps.n] = i;
        ps.orig[ps.n] = malloc((m->nv + 1) * sizeof(V3));
        memcpy(ps.orig[ps.n], m->c, m->nv * sizeof(V3));
        ps.wt[ps.n] = calloc(m->nv + 1, sizeof(float));
        ps.n++;
    }
    ps.on = 1;
    paint_dab(mx, my);
}

static void paint_end(void)
{
    for (int k = 0; k < ps.n; k++) { free(ps.orig[k]); free(ps.wt[k]); }
    free(ps.obj); free(ps.orig); free(ps.wt);
    memset(&ps, 0, sizeof ps);
}

/* ================================================================ commands */

enum {
    CM_NEW = 100, CM_IMPORT, CM_EXPORT, CM_EXPORT_AS, CM_EXPORT_SEL, CM_EXIT,
    CM_UNDO, CM_REDO, CM_REPEAT, CM_DELETE, CM_DUP,
    CM_CUBE, CM_CYL, CM_SPHERE, CM_PLANE,
    CM_EXTRUDE, CM_MERGE,
    CM_APPLYCOL, CM_SETCOL,
    CM_WIRE, CM_SHADED, CM_FRAMESEL, CM_FRAMEALL, CM_PANEL,
    CM_TSEL, CM_TMOVE, CM_TROT, CM_TSCALE, CM_TPAINT,
    CM_MOBJ, CM_MVERT, CM_MEDGE, CM_MFACE, CM_MMULTI,
    CM_SELALL, CM_SELINV,
    CM_HELP,
    CM_THEME = 200               /* + theme index */
};

static void cmd_create(int type)
{
    static const char *names[] = { "", "pCube", "pCylinder", "pSphere", "pPlane" };
    undo_push();
    set_mode(M_OBJ);
    clear_sel();
    Obj *o = obj_new(names[type]);
    o->prim = defPrim[type];
    o->prim.type = type;
    o->m = mesh_prim(&o->prim);
    o->sel = 1;
    o->order = ++stamp;
    msg("Created primitive");
}

static void cmd_select_all(int invert)
{
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        if (mode == M_OBJ) {
            o->sel = invert ? !o->sel : 1;
            if (o->sel) o->order = ++stamp;
        } else if (o->hil) {
            for (int k = M_VERT; k <= M_FACE; k++) {
                int n;
                unsigned char *f = comp_flags(o, k, &n);
                if (shows(k))
                    for (int j = 0; j < n; j++) f[j] = invert ? !f[j] : 1;
            }
        }
    }
    normalFrame = 0;
}

static void cmd_extrude(void)
{
    if (mode != M_FACE && mode != M_EDGE && mode != M_MULTI) { msg("Extrude: select faces or edges"); return; }
    if (!has_sel()) { msg("Extrude: nothing selected"); return; }
    /* multi: extrude the faces if any are selected, else the edges */
    int faces = mode == M_FACE;
    for (int i = 0; i < nob && mode == M_MULTI; i++)
        for (int f = 0; ob[i].hil && f < ob[i].m.nf; f++) faces |= ob[i].m.fs[f];
    undo_push();
    V3 n = v3(0, 0, 0);
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        if (!o->hil) continue;
        int ok = faces ? mesh_extrude_faces(&o->m) : mesh_extrude_edges(&o->m);
        if (!ok) continue;
        o->prim.type = PRIM_NONE;
        if (faces) {
            M4 iM = m4_inv(obj_mat(o));
            for (int f = 0; f < o->m.nf; f++) {
                if (!o->m.fs[f]) continue;
                V3 ln = mesh_fnormal(&o->m, f);   /* normal to world: inverse transpose */
                n = v3add(n, v3norm(v3(v3dot(ln, m4_col(iM, 0)), v3dot(ln, m4_col(iM, 1)), v3dot(ln, m4_col(iM, 2)))));
            }
        }
    }
    set_tool(T_MOVE);
    if (faces && v3len(n) > 1e-6f) {
        /* move manipulator follows the extrude direction (Z = average normal) */
        V3 z = v3norm(n), x = v3norm(v3cross(fabsf(z.y) < 0.9f ? v3(0, 1, 0) : v3(1, 0, 0), z)), y = v3cross(z, x);
        nframe = m4_id();
        V3 cols[3] = { x, y, z };
        for (int c = 0; c < 3; c++) { nframe.m[c * 4] = cols[c].x; nframe.m[c * 4 + 1] = cols[c].y; nframe.m[c * 4 + 2] = cols[c].z; }
        normalFrame = 1;
        lastHandle = H_Z;
    }
    msg("Extruded - drag the blue (Z) handle to pull along the normal");
}

static void cmd_merge(void)
{
    if (mode == M_OBJ) { msg("Merge to Center: select components"); return; }
    int any = 0;
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        unsigned char *mask = malloc(ob[i].m.nv + 1);
        any |= mesh_vmask(&ob[i].m, mode, mask) >= 2;
        free(mask);
    }
    if (!any) { msg("Merge to Center: select at least two vertices"); return; }
    undo_push();
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        unsigned char *mask = malloc(ob[i].m.nv + 1);
        mesh_vmask(&ob[i].m, mode, mask);
        if (mesh_merge(&ob[i].m, mask)) ob[i].prim.type = PRIM_NONE;
        free(mask);
    }
    msg("Merged to center");
}

static void cmd_delete(void)
{
    if (mode == M_OBJ) {
        if (!has_sel()) return;
        undo_push();
        for (int i = nob - 1; i >= 0; i--)
            if (ob[i].sel) obj_remove(i);
        msg("Deleted");
    } else if (shows(M_FACE)) {
        int any = 0;
        for (int i = 0; i < nob; i++)
            for (int f = 0; ob[i].hil && f < ob[i].m.nf; f++) any |= ob[i].m.fs[f];
        if (!any) { msg("Delete: select objects or faces"); return; }
        undo_push();
        for (int i = 0; i < nob; i++)
            if (ob[i].hil && mesh_delete_faces(&ob[i].m)) ob[i].prim.type = PRIM_NONE;
        msg("Deleted faces");
    } else {
        msg("Delete: select objects or faces");
    }
}

static void cmd_dup(void)
{
    if (mode != M_OBJ || !has_sel()) { msg("Duplicate: select objects"); return; }
    undo_push();
    int n0 = nob;
    for (int i = 0; i < n0; i++) {
        if (!ob[i].sel) continue;
        Obj src = ob[i];
        ob[i].sel = 0;
        Obj *o = obj_new(src.name);
        char nm[32];
        memcpy(nm, o->name, 32);
        *o = src;
        memcpy(o->name, nm, 32);
        o->m = mesh_copy(&src.m);
        o->sel = 1;
        o->order = ++stamp;
    }
    msg("Duplicated");
}

static void cmd_apply_color(void)
{
    if (!has_sel()) { msg("Apply Color: select objects or components"); return; }
    undo_push();
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        if (!active(o)) continue;
        unsigned char *mask = malloc(o->m.nv + 1);
        if (mode == M_OBJ) memset(mask, 1, o->m.nv);
        else mesh_vmask(&o->m, mode, mask);
        for (int v = 0; v < o->m.nv; v++)
            if (mask[v]) o->m.c[v] = v3lerp(o->m.c[v], paintCol, paintOpa);
        free(mask);
    }
    msg("Applied color");
}

static int file_dlg(int save, char *path)
{
    OPENFILENAMEA of;
    memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = hMain;
    of.lpstrFilter = "Wavefront OBJ (*.obj)\0*.obj\0All files\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = "obj";
    of.Flags = save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST;
    return save ? GetSaveFileNameA(&of) : GetOpenFileNameA(&of);
}

static void cmd_export(int ask, int selOnly)
{
    char path[MAX_PATH];
    strcpy(path, g_path);
    if ((ask || !path[0]) && !file_dlg(1, path)) return;
    const Mesh **ms = malloc((nob + 1) * sizeof *ms);
    M4 *mats = malloc((nob + 1) * sizeof *mats);
    const char **names = malloc((nob + 1) * sizeof *names);
    int n = 0;
    for (int i = 0; i < nob; i++) {
        if (selOnly && !active(&ob[i])) continue;
        ms[n] = &ob[i].m;
        mats[n] = obj_mat(&ob[i]);
        names[n] = ob[i].name;
        n++;
    }
    if (obj_save(path, n, ms, mats, names)) {
        if (!selOnly) { strcpy(g_path, path); modified = 0; }
        snprintf(g_msg, sizeof g_msg, "Exported %d object(s) to %s", n, path);
    } else {
        MessageBoxA(hMain, "Could not write the file.", APP_NAME, MB_ICONERROR);
    }
    free(ms); free(mats); free(names);
}

/* ask before throwing away unexported work; 0 = cancel */
static int confirm_discard(void)
{
    if (!modified || !nob) return 1;
    char q[MAX_PATH + 64];
    snprintf(q, sizeof q, "Export changes to %s?", g_path[0] ? g_path : "an OBJ file");
    int r = MessageBoxA(hMain, q, APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) { cmd_export(0, 0); return !modified; }
    return 1;
}

static void cmd_import(void)
{
    char path[MAX_PATH] = "";
    if (!file_dlg(0, path)) return;
    Mesh *ms;
    char (*names)[32];
    int n = obj_load(path, &ms, &names);
    if (n < 0) { MessageBoxA(hMain, "Could not read the file.", APP_NAME, MB_ICONERROR); return; }
    undo_push();
    set_mode(M_OBJ);
    clear_sel();
    for (int i = 0; i < n; i++) {
        Obj *o = obj_new(names[i]);
        o->m = ms[i];
        o->sel = 1;
        o->order = ++stamp;
    }
    free(ms);
    free(names);
    snprintf(g_msg, sizeof g_msg, "Imported %d object(s)", n);
}

static void cmd_set_color(void)
{
    static COLORREF cust[16];
    CHOOSECOLORA cc;
    memset(&cc, 0, sizeof cc);
    cc.lStructSize = sizeof cc;
    cc.hwndOwner = hMain;
    cc.rgbResult = RGB((int)(paintCol.x * 255 + .5f), (int)(paintCol.y * 255 + .5f), (int)(paintCol.z * 255 + .5f));
    cc.lpCustColors = cust;
    cc.Flags = CC_RGBINIT | CC_FULLOPEN;
    if (!ChooseColorA(&cc)) return;
    paintCol = v3(GetRValue(cc.rgbResult) / 255.0f, GetGValue(cc.rgbResult) / 255.0f, GetBValue(cc.rgbResult) / 255.0f);
}

static const char *HELP_TEXT =
    "Navigation (Maya)\n"
    "  Alt + LMB drag\tTumble\n"
    "  Alt + MMB drag\tTrack (pan)\n"
    "  Alt + RMB drag / wheel\tDolly (zoom)\n"
    "  F / A\tFrame selected / frame all\n\n"
    "Selection\n"
    "  Click / drag\tSelect (marquee)\n"
    "  Shift / Ctrl / Ctrl+Shift\tToggle / deselect / add\n"
    "  RMB\tComponent mode menu\n"
    "  F8 / F9 / F10 / F11\tObject / vertex / edge / face\n"
    "  F7\tMulti: verts, edges and faces at once\n"
    "  Ctrl+Shift+A / Ctrl+Shift+I\tSelect all / invert\n\n"
    "Tools\n"
    "  Q W E R\tSelect / move / rotate / scale\n"
    "  Manip handles\tAxis = 1 axis, square = 2 axes, center = all 3\n"
    "  Ctrl + drag axis\tMove/scale on the other 2 axes\n"
    "  MMB drag\tDrag the last used handle\n"
    "  hold X / hold J\tSnap move to grid / snap rotate 15 deg\n"
    "  Scale past the pivot\tFlatten components (scale 0)\n"
    "  + / -\tManipulator size\n"
    "  B + LMB drag\tBrush radius (paint tool)\n\n"
    "Edit\n"
    "  Ctrl+E\tExtrude faces / edges\n"
    "  Ctrl+D\tDuplicate\n"
    "  Delete\tDelete objects / faces\n"
    "  Z, Ctrl+Z / Shift+Z, Ctrl+Y\tUndo / redo\n"
    "  G\tRepeat last command\n"
    "  4 / 5\tWireframe / shaded\n"
    "  Ctrl+N / Ctrl+O / Ctrl+S\tNew / import OBJ / export OBJ\n"
    "  Ctrl+A\tToggle side panel";

static void do_cmd(int id)
{
    g_msg[0] = 0;
    if (id >= CM_THEME && id < CM_THEME + NTHEME) {
        theme_load(id - CM_THEME);
        theme_save();
        theme_window(hMain);
        DrawMenuBar(hMain);
        RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
    }
    switch (id) {
    case CM_NEW:
        if (!confirm_discard()) break;
        if (nob) undo_push();
        for (int i = 0; i < nob; i++) mesh_free(&ob[i].m);
        nob = 0;
        mode = M_OBJ;
        g_path[0] = 0;
        modified = 0;
        msg("New scene");
        break;
    case CM_IMPORT: cmd_import(); break;
    case CM_EXPORT: cmd_export(0, 0); break;
    case CM_EXPORT_AS: cmd_export(1, 0); break;
    case CM_EXPORT_SEL:
        if (!has_sel() && mode == M_OBJ) { msg("Export Selection: nothing selected"); break; }
        cmd_export(1, 1);
        break;
    case CM_EXIT: PostMessage(hMain, WM_CLOSE, 0, 0); break;
    case CM_UNDO: do_undo(); break;
    case CM_REDO: do_redo(); break;
    case CM_REPEAT:
        if (lastCmd) { do_cmd(lastCmd); return; }
        break;
    case CM_DELETE: cmd_delete(); break;
    case CM_DUP: cmd_dup(); break;
    case CM_CUBE: cmd_create(PRIM_CUBE); break;
    case CM_CYL: cmd_create(PRIM_CYL); break;
    case CM_SPHERE: cmd_create(PRIM_SPHERE); break;
    case CM_PLANE: cmd_create(PRIM_PLANE); break;
    case CM_SELALL: cmd_select_all(0); break;
    case CM_SELINV: cmd_select_all(1); break;
    case CM_EXTRUDE: cmd_extrude(); break;
    case CM_MERGE: cmd_merge(); break;
    case CM_APPLYCOL: cmd_apply_color(); break;
    case CM_SETCOL: cmd_set_color(); break;
    case CM_WIRE: wire = 1; break;
    case CM_SHADED: wire = 0; break;
    case CM_FRAMESEL: frame(0); break;
    case CM_FRAMEALL: frame(1); break;
    case CM_PANEL: {
        RECT rc;
        panelOn = !panelOn;
        GetClientRect(hMain, &rc);
        SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
        break;
    }
    case CM_TSEL: case CM_TMOVE: case CM_TROT: case CM_TSCALE: case CM_TPAINT:
        set_tool(id - CM_TSEL);
        break;
    case CM_MOBJ: case CM_MVERT: case CM_MEDGE: case CM_MFACE: case CM_MMULTI:
        set_mode(id - CM_MOBJ);
        break;
    case CM_HELP: MessageBoxA(hMain, HELP_TEXT, APP_NAME " hotkeys", MB_OK); break;
    }
    if ((id >= CM_DELETE && id <= CM_MERGE) || id == CM_APPLYCOL) lastCmd = id;
    refresh_all();
}

/* ================================================================ side panel (channel box) */

enum {
    PF_TX, PF_TY, PF_TZ, PF_RX, PF_RY, PF_RZ, PF_SX, PF_SY, PF_SZ,
    PF_CW, PF_CH, PF_CD, PF_CSW, PF_CSH, PF_CSD,
    PF_YR, PF_YH, PF_YSIDES, PF_YHSEG, PF_YCAP, PF_YTOP, PF_YBOT,
    PF_SR, PF_SCOLS, PF_SROWS,
    PF_PW, PF_PD, PF_PSW, PF_PSD,
    PF_OPA, PF_RAD, PF_COLOR, PF_APPLY
};
enum { RK_HEAD = 1, RK_FLOAT, RK_INT, RK_BOOL, RK_COLOR, RK_BUTTON };
#define NROW 32
typedef struct { int kind, id; char label[40]; } Row;
static Row rows[NROW];
static int nrows, panelObj = -1, building;
static HWND rLab[NROW], rEdit[NROW], rChk[NROW], hSwatch, hApply;
static WNDPROC editProc0;

static float *fld_f(int id)
{
    Obj *o = panelObj >= 0 && panelObj < nob ? &ob[panelObj] : NULL;
    if (id == PF_OPA) return &paintOpa;
    if (id == PF_RAD) return &paintRad;
    if (!o) return NULL;
    if (id >= PF_TX && id <= PF_TZ) return &o->t.x + (id - PF_TX);
    if (id >= PF_RX && id <= PF_RZ) return &o->r.x + (id - PF_RX);
    if (id >= PF_SX && id <= PF_SZ) return &o->s.x + (id - PF_SX);
    switch (id) {
    case PF_CW: return &o->prim.w;
    case PF_CH: return &o->prim.h;
    case PF_CD: return &o->prim.d;
    case PF_YR: return &o->prim.cr;
    case PF_YH: return &o->prim.ch;
    case PF_SR: return &o->prim.sr;
    case PF_PW: return &o->prim.pw;
    case PF_PD: return &o->prim.pd;
    }
    return NULL;
}

static int *fld_i(int id)
{
    Obj *o = panelObj >= 0 && panelObj < nob ? &ob[panelObj] : NULL;
    if (!o) return NULL;
    switch (id) {
    case PF_CSW: return &o->prim.sw;
    case PF_CSH: return &o->prim.sh;
    case PF_CSD: return &o->prim.sd;
    case PF_YSIDES: return &o->prim.csides;
    case PF_YHSEG: return &o->prim.chseg;
    case PF_YCAP: return &o->prim.ccap;
    case PF_YTOP: return &o->prim.ctop;
    case PF_YBOT: return &o->prim.cbot;
    case PF_SCOLS: return &o->prim.scols;
    case PF_SROWS: return &o->prim.srows;
    case PF_PSW: return &o->prim.psw;
    case PF_PSD: return &o->prim.psd;
    }
    return NULL;
}

static void fmt_float(char *b, float v)
{
    if (fabsf(v) < 5e-5f) v = 0;
    sprintf(b, "%.4f", v);
    char *e = b + strlen(b) - 1;
    while (*e == '0') *e-- = 0;
    if (*e == '.') *e = 0;
}

static void row_text(int r)
{
    char b[64] = "";
    if (rows[r].kind == RK_FLOAT) { float *p = fld_f(rows[r].id); if (p) fmt_float(b, *p); }
    else { int *p = fld_i(rows[r].id); if (p) sprintf(b, "%d", *p); }
    SetWindowTextA(rEdit[r], b);
}

static void panel_sync(void)
{
    HWND f = GetFocus();
    for (int r = 0; r < nrows; r++) {
        if ((rows[r].kind == RK_FLOAT || rows[r].kind == RK_INT) && rEdit[r] != f) row_text(r);
        if (rows[r].kind == RK_BOOL) { int *p = fld_i(rows[r].id); SendMessage(rChk[r], BM_SETCHECK, p && *p ? BST_CHECKED : BST_UNCHECKED, 0); }
    }
    InvalidateRect(hSwatch, NULL, TRUE);
}

static void add_row(int kind, int id, const char *label)
{
    if (nrows >= NROW) return;
    rows[nrows].kind = kind;
    rows[nrows].id = id;
    snprintf(rows[nrows].label, sizeof rows[nrows].label, "%s", label);
    nrows++;
}

static void panel_build(void)
{
    static const char *trs[9] = { "Translate X", "Translate Y", "Translate Z", "Rotate X", "Rotate Y", "Rotate Z", "Scale X", "Scale Y", "Scale Z" };
    building = 1;
    nrows = 0;
    Obj *o = lead();
    panelObj = o ? (int)(o - ob) : -1;
    if (o) {
        add_row(RK_HEAD, 0, o->name);
        for (int i = 0; i < 9; i++) add_row(RK_FLOAT, PF_TX + i, trs[i]);
        if (o->prim.type == PRIM_CUBE) {
            add_row(RK_HEAD, 0, "polyCube");
            add_row(RK_FLOAT, PF_CW, "Width");
            add_row(RK_FLOAT, PF_CH, "Height");
            add_row(RK_FLOAT, PF_CD, "Depth");
            add_row(RK_INT, PF_CSW, "Width Divs");
            add_row(RK_INT, PF_CSH, "Height Divs");
            add_row(RK_INT, PF_CSD, "Depth Divs");
        } else if (o->prim.type == PRIM_CYL) {
            add_row(RK_HEAD, 0, "polyCylinder");
            add_row(RK_FLOAT, PF_YR, "Radius");
            add_row(RK_FLOAT, PF_YH, "Height");
            add_row(RK_INT, PF_YSIDES, "Sides");
            add_row(RK_INT, PF_YHSEG, "Height Divs");
            add_row(RK_INT, PF_YCAP, "Cap Divs");
            add_row(RK_BOOL, PF_YTOP, "Top Cap");
            add_row(RK_BOOL, PF_YBOT, "Bottom Cap");
        } else if (o->prim.type == PRIM_SPHERE) {
            add_row(RK_HEAD, 0, "polySphere");
            add_row(RK_FLOAT, PF_SR, "Radius");
            add_row(RK_INT, PF_SCOLS, "Columns");
            add_row(RK_INT, PF_SROWS, "Rows");
        } else if (o->prim.type == PRIM_PLANE) {
            add_row(RK_HEAD, 0, "polyPlane");
            add_row(RK_FLOAT, PF_PW, "Width");
            add_row(RK_FLOAT, PF_PD, "Height");
            add_row(RK_INT, PF_PSW, "Width Divs");
            add_row(RK_INT, PF_PSD, "Height Divs");
        }
    } else {
        add_row(RK_HEAD, 0, "Nothing selected");
    }
    add_row(RK_HEAD, 0, "Vertex Color");
    add_row(RK_COLOR, PF_COLOR, "Color");
    add_row(RK_FLOAT, PF_OPA, "Opacity");
    add_row(RK_FLOAT, PF_RAD, "Brush Radius");
    add_row(RK_BUTTON, PF_APPLY, "Apply Color to Selection");

    RECT rc;
    GetClientRect(hPanel, &rc);
    int W = rc.right, rh = (int)(22 * ds), pad = (int)(6 * ds), lw = (int)(92 * ds), y = pad;
    int ew = W - lw - 2 * pad;
    HWND swatchShown = NULL, applyShown = NULL;
    for (int r = 0; r < NROW; r++) {
        int k = r < nrows ? rows[r].kind : 0;
        if (k == RK_HEAD && r) y += pad;
        if (k == RK_HEAD || k == RK_FLOAT || k == RK_INT || k == RK_COLOR) {
            SendMessage(rLab[r], WM_SETFONT, (WPARAM)(k == RK_HEAD ? hBold : hFont), FALSE);
            SetWindowTextA(rLab[r], rows[r].label);
            MoveWindow(rLab[r], pad, y + (int)(3 * ds), k == RK_HEAD ? W - 2 * pad : lw - 4, rh - (int)(4 * ds), TRUE);
        }
        ShowWindow(rLab[r], k == RK_HEAD || k == RK_FLOAT || k == RK_INT || k == RK_COLOR ? SW_SHOW : SW_HIDE);
        if (k == RK_FLOAT || k == RK_INT) {
            MoveWindow(rEdit[r], pad + lw + 1, y + (int)(2 * ds), ew - 2, rh - 3 - (int)(2 * ds), TRUE);
            row_text(r);
        }
        ShowWindow(rEdit[r], k == RK_FLOAT || k == RK_INT ? SW_SHOW : SW_HIDE);
        if (k == RK_BOOL) {
            SetWindowTextA(rChk[r], rows[r].label);
            MoveWindow(rChk[r], pad + lw, y, ew, rh - 2, TRUE);
        }
        ShowWindow(rChk[r], k == RK_BOOL ? SW_SHOW : SW_HIDE);
        if (k == RK_COLOR) { MoveWindow(hSwatch, pad + lw + 1, y + 1, ew - 2, rh - 4, TRUE); swatchShown = hSwatch; }
        if (k == RK_BUTTON) { MoveWindow(hApply, pad, y, W - 2 * pad, rh + 2, TRUE); applyShown = hApply; y += 4; }
        if (k) y += rh;
    }
    ShowWindow(hSwatch, swatchShown ? SW_SHOW : SW_HIDE);
    ShowWindow(hApply, applyShown ? SW_SHOW : SW_HIDE);
    InvalidateRect(hPanel, NULL, TRUE);
    panel_sync();
    building = 0;
}

static void prim_rebuild(Obj *o)
{
    prim_clamp(&o->prim);
    Mesh n = mesh_prim(&o->prim);
    if (n.nv == o->m.nv) memcpy(n.c, o->m.c, n.nv * sizeof(V3));
    mesh_free(&o->m);
    o->m = n;
    defPrim[o->prim.type] = o->prim;
}

static void panel_commit(int r, double v)
{
    Row *row = &rows[r];
    int id = row->id;
    if (row->kind == RK_FLOAT) {
        float *p = fld_f(id);
        if (!p || fabs(*p - v) < 1e-7) return;
        if (id == PF_OPA) { paintOpa = (float)(v < 0 ? 0 : v > 1 ? 1 : v); return; }
        if (id == PF_RAD) { paintRad = (float)(v < 1 ? 1 : v > 2000 ? 2000 : v); return; }
        undo_push();
        if (id <= PF_SZ) {
            /* channel edits apply to every selected object */
            for (int i = 0; i < nob; i++)
                if (active(&ob[i]) || i == panelObj) (&ob[i].t.x)[id - PF_TX] = (float)v;
        } else {
            *p = (float)v;
            prim_rebuild(&ob[panelObj]);
        }
    } else {
        int *p = fld_i(id);
        if (!p || *p == (int)v) return;
        undo_push();
        *p = (int)v;
        prim_rebuild(&ob[panelObj]);
    }
    refresh_all();
}

static void panel_commit_edit(int r)
{
    if (building || r >= nrows) return;
    char b[64];
    GetWindowTextA(rEdit[r], b, sizeof b);
    char *end;
    double v = strtod(b, &end);
    if (end == b) { row_text(r); return; }
    panel_commit(r, v);
    row_text(r);
}

static LRESULT CALLBACK EditProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    if (m == WM_KEYDOWN && (wp == VK_RETURN || wp == VK_ESCAPE)) {
        if (wp == VK_ESCAPE) row_text(GetDlgCtrlID(h) - 1000);
        SetFocus(hView);
        return 0;
    }
    if (m == WM_CHAR && (wp == '\r' || wp == 27)) return 0;
    if (m == WM_LBUTTONDOWN && GetFocus() != h) {   /* first click selects the whole value */
        SetFocus(h);
        SendMessage(h, EM_SETSEL, 0, -1);
        return 0;
    }
    return CallWindowProc(editProc0, h, m, wp, lp);
}

static LRESULT CALLBACK PanelProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_COMMAND: {
        int id = LOWORD(wp), code = HIWORD(wp);
        if (id >= 1000 && id < 1000 + NROW && (code == EN_SETFOCUS || code == EN_KILLFOCUS)) InvalidateRect(h, NULL, TRUE);
        if (id >= 1000 && id < 1000 + NROW && code == EN_KILLFOCUS) panel_commit_edit(id - 1000);
        else if (id >= 2000 && id < 2000 + NROW && code == BN_CLICKED && !building) {
            int r = id - 2000;
            panel_commit(r, SendMessage(rChk[r], BM_GETCHECK, 0, 0) == BST_CHECKED);
            SetFocus(hView);
        } else if (id == 3000 && code == STN_CLICKED) { do_cmd(CM_SETCOL); SetFocus(hView); }
        else if (id == 3001 && code == BN_CLICKED) { do_cmd(CM_APPLYCOL); SetFocus(hView); }
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        if ((HWND)lp == hSwatch) {
            if (swatchBrush) DeleteObject(swatchBrush);
            swatchBrush = CreateSolidBrush(RGB((int)(paintCol.x * 255 + .5f), (int)(paintCol.y * 255 + .5f), (int)(paintCol.z * 255 + .5f)));
            return (LRESULT)swatchBrush;
        }
        int head = 0;
        for (int r = 0; r < nrows; r++)
            if (rLab[r] == (HWND)lp) head = rows[r].kind == RK_HEAD;
        SetTextColor((HDC)wp, head ? th.head : th.text);
        SetBkColor((HDC)wp, th.ui);
        return (LRESULT)brUi;
    }
    case WM_CTLCOLOREDIT:
        SetTextColor((HDC)wp, th.fieldText);
        SetBkColor((HDC)wp, th.field);
        return (LRESULT)brField;
    case WM_ERASEBKGND: {
        RECT rc;
        HDC dc = (HDC)wp;
        GetClientRect(h, &rc);
        FillRect(dc, &rc, brUi);
        rc.right = 1;
        FillRect(dc, &rc, brLine);
        for (int r = 0; r <= nrows; r++) {
            HWND c = r < nrows ? rEdit[r] : hSwatch;
            if (!IsWindowVisible(c)) continue;
            GetWindowRect(c, &rc);
            MapWindowPoints(NULL, h, (POINT *)&rc, 2);
            if (c != hSwatch) rc.top -= (int)(2 * ds) - 1;
            InflateRect(&rc, 1, 1);
            FillRect(dc, &rc, brField);
            FrameRect(dc, &rc, GetFocus() == c ? brAcc : brLine);   /* the field being typed in */
        }
        return 1;
    }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)lp;
        int down = d->itemState & ODS_SELECTED;
        RECT rc = d->rcItem;
        if (th.sys) DrawFrameControl(d->hDC, &rc, DFC_BUTTON, DFCS_BUTTONPUSH | (down ? DFCS_PUSHED : 0));
        else {
            FillRect(d->hDC, &rc, down ? brField : brHot);
            FrameRect(d->hDC, &rc, brLine);
        }
        char b[64];
        GetWindowTextA(d->hwndItem, b, sizeof b);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, th.text);
        if (down) OffsetRect(&rc, 1, 1);
        DrawTextA(d->hDC, b, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    case WM_SIZE:
        if (!building) panel_build();
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void panel_create(HINSTANCE inst)
{
    for (int r = 0; r < NROW; r++) {
        rLab[r] = CreateWindowA("STATIC", "", WS_CHILD | SS_LEFTNOWORDWRAP | SS_NOPREFIX, 0, 0, 1, 1, hPanel, NULL, inst, NULL);
        rEdit[r] = CreateWindowA("EDIT", "", WS_CHILD | ES_AUTOHSCROLL, 0, 0, 1, 1, hPanel, (HMENU)(INT_PTR)(1000 + r), inst, NULL);
        rChk[r] = CreateWindowA("BUTTON", "", WS_CHILD | BS_AUTOCHECKBOX, 0, 0, 1, 1, hPanel, (HMENU)(INT_PTR)(2000 + r), inst, NULL);
        SendMessage(rLab[r], WM_SETFONT, (WPARAM)hFont, 0);
        SendMessage(rEdit[r], WM_SETFONT, (WPARAM)hFont, 0);
        SendMessage(rChk[r], WM_SETFONT, (WPARAM)hFont, 0);
        SendMessage(rEdit[r], EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM((int)(3 * ds), (int)(3 * ds)));
        editProc0 = (WNDPROC)SetWindowLongPtrA(rEdit[r], GWLP_WNDPROC, (LONG_PTR)EditProc);
    }
    hSwatch = CreateWindowA("STATIC", "", WS_CHILD | SS_NOTIFY, 0, 0, 1, 1, hPanel, (HMENU)3000, inst, NULL);
    hApply = CreateWindowA("BUTTON", "Apply Color to Selection", WS_CHILD | BS_OWNERDRAW, 0, 0, 1, 1, hPanel, (HMENU)3001, inst, NULL);
    SendMessage(hApply, WM_SETFONT, (WPARAM)hFont, 0);
}

/* ================================================================ toolbar (shelf) */

/* Every button shows its hotkey under its name, so the toolbar doubles as a cheat sheet. */
typedef struct { const char *label, *hotkey; int cmd; } Tool;
static const Tool tbItems[] = {
    { "Select", "Q", CM_TSEL }, { "Move", "W", CM_TMOVE }, { "Rotate", "E", CM_TROT }, { "Scale", "R", CM_TSCALE },
    { "Paint", "", CM_TPAINT }, { 0 },
    { "Object", "F8", CM_MOBJ }, { "Vertex", "F9", CM_MVERT }, { "Edge", "F10", CM_MEDGE }, { "Face", "F11", CM_MFACE },
    { "Multi", "F7", CM_MMULTI }, { 0 },
    { "Cube", "", CM_CUBE }, { "Cylinder", "", CM_CYL }, { "Sphere", "", CM_SPHERE }, { "Plane", "", CM_PLANE }, { 0 },
    { "Extrude", "Ctrl+E", CM_EXTRUDE }, { "Merge", "", CM_MERGE }, { "Duplicate", "Ctrl+D", CM_DUP },
    { "Delete", "Del", CM_DELETE }, { 0 },
    { "Wire", "4", CM_WIRE }, { "Shaded", "5", CM_SHADED }, { "Frame", "F", CM_FRAMESEL }, { 0 },
    { "Undo", "Z", CM_UNDO }, { "Redo", "Shift+Z", CM_REDO },
};
#define NTB ((int)(sizeof tbItems / sizeof *tbItems))
static RECT tbRc[NTB];
static int tbHover = -1, tbDown = -1;

static int tb_on(int cmd)
{
    if (cmd >= CM_TSEL && cmd <= CM_TPAINT) return tool == cmd - CM_TSEL;
    if (cmd >= CM_MOBJ && cmd <= CM_MMULTI) return mode == cmd - CM_MOBJ;
    if (cmd == CM_WIRE || cmd == CM_SHADED) return wire == (cmd == CM_WIRE);
    return 0;
}

/* lays the buttons out in rows across width W and returns the bar height */
static int tb_layout(int W)
{
    HDC dc = GetDC(hMain);
    int pad = (int)(4 * ds), gap = (int)(10 * ds), bh = (int)(36 * ds), x = pad, y = pad;
    for (int i = 0; i < NTB; i++) {
        if (!tbItems[i].label) { x += gap; SetRectEmpty(&tbRc[i]); continue; }
        SIZE a, b;
        SelectObject(dc, hFont);
        GetTextExtentPoint32A(dc, tbItems[i].label, (int)strlen(tbItems[i].label), &a);
        SelectObject(dc, hSmall);
        GetTextExtentPoint32A(dc, tbItems[i].hotkey, (int)strlen(tbItems[i].hotkey), &b);
        int w = (a.cx > b.cx ? a.cx : b.cx) + (int)(16 * ds);
        if (x + w > W - pad && x > pad) { x = pad; y += bh + pad; }
        SetRect(&tbRc[i], x, y, x + w, y + bh);
        x += w + 2;
    }
    ReleaseDC(hMain, dc);
    return y + bh + pad + 1;
}

static int tb_hit(int x, int y)
{
    POINT pt = { x, y };
    for (int i = 0; i < NTB; i++)
        if (tbItems[i].label && PtInRect(&tbRc[i], pt)) return i;
    return -1;
}

static void tb_paint(HWND h)
{
    PAINTSTRUCT ps_;
    RECT rc;
    HDC wdc = BeginPaint(h, &ps_);
    GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc);    /* draw off screen so the bar never flickers */
    HBITMAP bm = CreateCompatibleBitmap(wdc, rc.right, rc.bottom), old = SelectObject(dc, bm);
    FillRect(dc, &rc, brUi);
    RECT ln = rc;
    ln.top = ln.bottom - 1;
    FillRect(dc, &ln, brLine);
    SetBkMode(dc, TRANSPARENT);
    for (int i = 0; i < NTB; i++) {
        const Tool *t = &tbItems[i];
        if (!t->label) continue;
        RECT r = tbRc[i];
        int on = tb_on(t->cmd), down = i == tbDown && i == tbHover;
        if (down) FillRect(dc, &r, brField);
        else if (on || i == tbHover) FillRect(dc, &r, brHot);
        if (on || i == tbHover || down) FrameRect(dc, &r, brLine);
        if (on) {
            RECT u = r;
            InflateRect(&u, -1, 0);
            u.top = u.bottom - (int)(3 * ds);
            FillRect(dc, &u, brAcc);
        }
        int mid = r.top + (r.bottom - r.top) * 11 / 20;
        RECT a = r, b = r;
        a.bottom = mid;
        b.top = mid;
        b.bottom -= (int)(3 * ds);
        SelectObject(dc, hFont);
        SetTextColor(dc, th.text);
        DrawTextA(dc, t->label, -1, &a, DT_CENTER | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
        SelectObject(dc, hSmall);
        SetTextColor(dc, mix(th.text, mix(th.text, th.ui)));
        DrawTextA(dc, t->hotkey, -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps_);
}

static LRESULT CALLBACK BarProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (m) {
    case WM_PAINT: tb_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_MOUSEMOVE: {
        int i = tb_hit(x, y);
        if (i != tbHover) {
            tbHover = i;
            InvalidateRect(h, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tbHover = -1;
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN:
        tbDown = tb_hit(x, y);
        SetCapture(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONUP: {
        int i = tbDown;
        tbDown = -1;
        ReleaseCapture();
        InvalidateRect(h, NULL, FALSE);
        if (i >= 0 && i == tb_hit(x, y)) do_cmd(tbItems[i].cmd);
        SetFocus(hView);
        return 0;
    }
    }
    return DefWindowProc(h, m, wp, lp);
}

/* ================================================================ status / refresh */

static void status_update(void)
{
    static const char *mn[] = { "Object mode", "Vertex mode", "Edge mode", "Face mode", "Multi mode" };
    static const char *tn[] = { "Select tool", "Move tool", "Rotate tool", "Scale tool", "Paint Vertex Color tool" };
    int nsel = 0, ncomp = 0, tv = 0, tf = 0;
    for (int i = 0; i < nob; i++) {
        nsel += mode == M_OBJ ? ob[i].sel : ob[i].hil;
        if (mode != M_OBJ && ob[i].hil) ncomp += comp_count(&ob[i]);
        tv += ob[i].m.nv;
        tf += ob[i].m.nf;
    }
    char s[400], c[64] = "";
    if (mode != M_OBJ) snprintf(c, sizeof c, ", %d component(s) selected", ncomp);
    snprintf(s, sizeof s, "  %s  |  %s  |  %d object(s), %d %s%s  |  %d verts, %d faces%s%s",
             mn[mode], tn[tool], nob, nsel, mode == M_OBJ ? "selected" : "hilited", c, tv, tf,
             g_msg[0] ? "  |  " : "", g_msg);
    SetWindowTextA(hStatus, s);
    const char *fn = g_path[0] ? g_path : "untitled";
    for (const char *c = g_path; *c; c++)
        if (*c == '\\' || *c == '/') fn = c + 1;
    snprintf(s, sizeof s, "%s%s - " APP_NAME, fn, modified ? "*" : "");
    SetWindowTextA(hMain, s);
    for (int i = 0; i < 5; i++) CheckMenuItem(hMenu, CM_TSEL + i, MF_BYCOMMAND | (tool == i ? MF_CHECKED : MF_UNCHECKED));
    for (int i = 0; i < 5; i++) CheckMenuItem(hMenu, CM_MOBJ + i, MF_BYCOMMAND | (mode == i ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_WIRE, MF_BYCOMMAND | (wire ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_SHADED, MF_BYCOMMAND | (!wire ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_PANEL, MF_BYCOMMAND | (panelOn ? MF_CHECKED : MF_UNCHECKED));
    InvalidateRect(hBar, NULL, FALSE);
    CheckMenuRadioItem(hThemeMenu, CM_THEME, CM_THEME + NTHEME - 1, CM_THEME + themeIdx, MF_BYCOMMAND);
}

static void refresh_all(void)
{
    geo_changed();
    if (hvObj >= nob) hvObj = -1;
    panel_build();
    status_update();
    redraw();
}

/* ================================================================ rendering */

static void draw_grid(void)
{
    glLineWidth(1);
    glBegin(GL_LINES);
    for (int i = -12; i <= 12; i++) {
        if (!i) continue;
        glColor3fv(i % 5 ? th.grid : th.gridMajor);
        glVertex3f((float)i, 0, -12); glVertex3f((float)i, 0, 12);
        glVertex3f(-12, 0, (float)i); glVertex3f(12, 0, (float)i);
    }
    glColor3fv(th.gridAxis);
    glVertex3f(0, 0, -12); glVertex3f(0, 0, 12);
    glVertex3f(-12, 0, 0); glVertex3f(12, 0, 0);
    glEnd();
}

static void draw_axis_gizmo(void)
{
    static const float col[3][3] = { { .9f, .2f, .2f }, { .3f, .9f, .3f }, { .35f, .5f, 1 } };
    static const float glyph[3][8] = {
        { -1, -1, 1, 1, -1, 1, 1, -1 },          /* X */
        { -1, 1, 0, 0, 1, 1, 0, -1 },            /* Y (third stroke added below) */
        { -1, 1, 1, 1, 1, 1, -1, -1 },           /* Z (third stroke added below) */
    };
    float s = 28 * ds, cx = 42 * ds, cy = vh - 42 * ds, gs = 4 * ds;
    glLineWidth(1.5f * ds);
    glBegin(GL_LINES);
    for (int i = 0; i < 3; i++) {
        V3 a = v3(i == 0, i == 1, i == 2);
        float x = cx + v3dot(a, camR) * s, y = cy - v3dot(a, camU) * s;
        float lx = cx + v3dot(a, camR) * s * 1.35f, ly = cy - v3dot(a, camU) * s * 1.35f;
        glColor3fv(col[i]);
        glVertex2f(cx, cy); glVertex2f(x, y);
        for (int k = 0; k < 2; k++) {
            glVertex2f(lx + glyph[i][4 * k] * gs, ly - glyph[i][4 * k + 1] * gs);
            glVertex2f(lx + glyph[i][4 * k + 2] * gs, ly - glyph[i][4 * k + 3] * gs);
        }
        if (i == 1) { glVertex2f(lx, ly); glVertex2f(lx, ly + gs); }
        if (i == 2) { glVertex2f(lx - gs, ly + gs); glVertex2f(lx + gs, ly + gs); }
    }
    glEnd();
    glLineWidth(1);
}

static int dk, dkBtn, mx0, my0, curx = -1000, cury = -1000, selMod;
static float rad0;
enum { D_NONE, D_PEND, D_MARQ, D_MANIP, D_TUMBLE, D_PAN, D_DOLLY, D_PAINT, D_BRUSH };

/* ---- preselection highlight: what a click would pick, drawn in red like Maya */

static const float HOV[3] = { 1, .25f, .25f };

static void draw_hover(const Obj *o)
{
    const Mesh *m = &o->m;
    glColor3fv(HOV);
    if (hvKind == M_VERT && hvIdx < m->nv) {
        glPointSize(9 * ds);
        glBegin(GL_POINTS);
        glVertex3fv(&m->p[hvIdx].x);
        glEnd();
    } else if (hvKind == M_EDGE && hvIdx < m->ne) {
        glLineWidth(3.5f * ds);
        glBegin(GL_LINES);
        glVertex3fv(&m->p[m->ev[2 * hvIdx]].x);
        glVertex3fv(&m->p[m->ev[2 * hvIdx + 1]].x);
        glEnd();
    } else if (hvKind == M_FACE && hvIdx < m->nf) {
        int s = m->fo[hvIdx], n = m->fo[hvIdx + 1] - s;
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1, -1);
        glColor4f(HOV[0], HOV[1], HOV[2], .35f);
        glBegin(GL_POLYGON);
        for (int k = 0; k < n; k++) glVertex3fv(&m->p[m->fi[s + k]].x);
        glEnd();
        glDisable(GL_POLYGON_OFFSET_FILL);
        glDisable(GL_BLEND);
        glColor3fv(HOV);
        glLineWidth(2 * ds);
        glBegin(GL_LINE_LOOP);
        for (int k = 0; k < n; k++) glVertex3fv(&m->p[m->fi[s + k]].x);
        glEnd();
    } else if (!hvKind) {
        glLineWidth(1.5f * ds);
        draw_all_edges(m);
    }
    glLineWidth(1);
}

/* x < -999: the mouse left the view */
static void hover_update(int x, int y)
{
    int o = -1, k = 0, i = 0;
    if (x > -1000 && dk == D_NONE && tool != T_PAINT && !(manip_vis() && hover)) {
        cam_update();
        if (mode != M_OBJ) pick_comp(x, y, &o, &k, &i);
        if (o < 0) {
            /* a whole object: selectable in object mode, or hilitable for component editing */
            o = pick_object(x, y, NULL);
            k = i = 0;
            if (o >= 0 && (mode == M_OBJ ? ob[o].sel : ob[o].hil)) o = -1;
        }
    }
    if (o != hvObj || k != hvKind || i != hvIdx) {
        hvObj = o;
        hvKind = k;
        hvIdx = i;
        redraw();
    }
}

static HCURSOR view_cursor(void)
{
    LPCSTR c = IDC_ARROW;
    if (dk == D_TUMBLE) c = IDC_SIZEALL;
    else if (dk == D_PAN) c = IDC_HAND;
    else if (dk == D_DOLLY) c = IDC_SIZENS;
    else if (dk == D_MARQ || tool == T_PAINT) c = IDC_CROSS;
    return LoadCursor(NULL, c);
}

static void render(void)
{
    cam_update();
    glViewport(0, 0, vw, vh);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glBegin(GL_QUADS);           /* background gradient */
    glColor3fv(th.bgBot); glVertex2f(-1, -1); glVertex2f(1, -1);
    glColor3fv(th.bgTop); glVertex2f(1, 1); glVertex2f(-1, 1);
    glEnd();
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_DEPTH_TEST);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    float lp[4] = { 0.3f, 0.5f, 1, 0 }, dif[4] = { .72f, .72f, .72f, 1 }, amb[4] = { .28f, .28f, .28f, 1 };
    glLightfv(GL_LIGHT0, GL_POSITION, lp);
    glLightfv(GL_LIGHT0, GL_DIFFUSE, dif);
    glLightModelfv(GL_LIGHT_MODEL_AMBIENT, amb);
    glLightModeli(GL_LIGHT_MODEL_TWO_SIDE, 1);
    glEnable(GL_LIGHT0);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    glEnable(GL_COLOR_MATERIAL);
    glEnable(GL_NORMALIZE);
    load_view();
    draw_grid();

    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        M4 M = obj_mat(o);
        int hil = mode != M_OBJ && o->hil;
        glPushMatrix();
        glMultMatrixf(M.m);
        if (!wire) {
            glEnable(GL_LIGHTING);
            glEnable(GL_POLYGON_OFFSET_FILL);
            glPolygonOffset(1, 1);
            draw_cached(i, 1);
            glDisable(GL_POLYGON_OFFSET_FILL);
            glDisable(GL_LIGHTING);
        } else if (hil && shows(M_FACE)) {
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glColor4f(1, 0.45f, 0.1f, 0.35f);
            draw_sel_tris(m);
            glDisable(GL_BLEND);
        }
        glLineWidth(1);
        if (hil) {
            glColor3f(0.35f, 0.75f, 1.0f);
            draw_all_edges(m);
            if (shows(M_EDGE)) {
                glLineWidth(2.5f * ds);
                glColor3f(1, 0.5f, 0.05f);
                draw_edges(m, 1);
                glLineWidth(1);
            }
            if (shows(M_VERT)) {
                glPointSize(4 * ds);
                glColor3f(0.75f, 0.1f, 0.75f);
                glEnableClientState(GL_VERTEX_ARRAY);
                glVertexPointer(3, GL_FLOAT, sizeof(V3), m->p);
                glDrawArrays(GL_POINTS, 0, m->nv);
                glDisableClientState(GL_VERTEX_ARRAY);
                glPointSize(6 * ds);
                glColor3f(1, 1, 0);
                glBegin(GL_POINTS);
                for (int v = 0; v < m->nv; v++) if (m->vs[v]) glVertex3fv(&m->p[v].x);
                glEnd();
            }
        } else if (mode == M_OBJ && o->sel) {
            Obj *l = lead();
            if (l == o) glColor3f(0.26f, 1, 0.64f);
            else glColor3fv(th.sel2);
            draw_all_edges(m);
        } else if (wire) {
            glColor3fv(th.wire);
            draw_all_edges(m);
        }
        if (i == hvObj) draw_hover(o);
        glPopMatrix();
    }

    draw_manip();

    /* 2D overlays */
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, vw, vh, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    if (dk == D_MARQ) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(th.sel2[0], th.sel2[1], th.sel2[2], .12f);
        glBegin(GL_QUADS);
        glVertex2f((float)mx0, (float)my0); glVertex2f((float)curx, (float)my0);
        glVertex2f((float)curx, (float)cury); glVertex2f((float)mx0, (float)cury);
        glEnd();
        glDisable(GL_BLEND);
        if (selMod) {
            /* modifier glyph by the cursor: + add, - deselect, +/- toggle */
            float gx = curx + 14 * ds, gy = cury + 14 * ds, g = 4 * ds;
            glColor3fv(th.sel2);
            glLineWidth(1.5f * ds);
            glBegin(GL_LINES);
            if (selMod == 1) {
                glVertex2f(gx - g, gy - g); glVertex2f(gx + g, gy - g);
                glVertex2f(gx, gy - 2 * g); glVertex2f(gx, gy);
                glVertex2f(gx - g, gy + g); glVertex2f(gx + g, gy + g);
            } else {
                glVertex2f(gx - g, gy); glVertex2f(gx + g, gy);
                if (selMod == 3) { glVertex2f(gx, gy - g); glVertex2f(gx, gy + g); }
            }
            glEnd();
            glLineWidth(1);
        }
        glColor3fv(th.sel2);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0xF0F0);
        glBegin(GL_LINE_LOOP);
        glVertex2f(mx0 + .5f, my0 + .5f); glVertex2f(curx + .5f, my0 + .5f);
        glVertex2f(curx + .5f, cury + .5f); glVertex2f(mx0 + .5f, cury + .5f);
        glEnd();
        glDisable(GL_LINE_STIPPLE);
    }
    if (tool == T_PAINT) {
        float cx = dk == D_BRUSH ? (float)mx0 : (float)curx, cy = dk == D_BRUSH ? (float)my0 : (float)cury;
        float r = paintRad * ds;
        glColor3f(paintCol.x, paintCol.y, paintCol.z);
        glBegin(GL_LINE_LOOP);
        for (int k = 0; k < 48; k++) glVertex2f(cx + r * cosf(2 * PI_F * k / 48), cy + r * sinf(2 * PI_F * k / 48));
        glEnd();
    }
    draw_axis_gizmo();
    SwapBuffers(hdc);
}

/* ================================================================ viewport window */

static void marking_menu(int x, int y)
{
    cam_update();
    int hit = pick_object(x, y, NULL);
    HMENU m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING | (mode == M_VERT ? MF_CHECKED : 0), CM_MVERT, "Vertex");
    AppendMenuA(m, MF_STRING | (mode == M_EDGE ? MF_CHECKED : 0), CM_MEDGE, "Edge");
    AppendMenuA(m, MF_STRING | (mode == M_FACE ? MF_CHECKED : 0), CM_MFACE, "Face");
    AppendMenuA(m, MF_STRING | (mode == M_MULTI ? MF_CHECKED : 0), CM_MMULTI, "Multi");
    AppendMenuA(m, MF_STRING | (mode == M_OBJ ? MF_CHECKED : 0), CM_MOBJ, "Object Mode");
    POINT pt = { x, y };
    ClientToScreen(hView, &pt);
    int cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, hView, NULL);
    DestroyMenu(m);
    if (!cmd) return;
    int nm = cmd - CM_MOBJ;
    set_mode(nm);
    if (nm != M_OBJ && hit >= 0) {
        if (!ob[hit].hil) {
            int any = 0;
            for (int i = 0; i < nob; i++) any |= ob[i].hil;
            if (!any || !ob[hit].sel) for (int i = 0; i < nob; i++) ob[i].hil = 0;
            ob[hit].hil = 1;
        }
        ob[hit].order = ++stamp;
    }
    refresh_all();
}

static void end_drag(int cancel)
{
    int k = dk;
    dk = D_NONE;
    if (k == D_PEND && !cancel) select_click(mx0, my0, selMod);
    else if (k == D_MARQ && !cancel) select_rect(mx0, my0, curx, cury, selMod);
    else if (k == D_MANIP) drag_end();
    else if (k == D_PAINT) paint_end();
    if (k == D_PEND || k == D_MARQ) normalFrame = 0;
    if (GetCapture() == hView) ReleaseCapture();
    refresh_all();
    hvObj = -1;
    hover_update(curx, cury);
    SetCursor(view_cursor());
}

static void handle_key(WPARAM wp)
{
    int ctrl = key(VK_CONTROL), shift = key(VK_SHIFT), id = 0;
    switch (wp) {
    case 'Q': id = CM_TSEL; break;
    case 'W': id = CM_TMOVE; break;
    case 'E': id = ctrl ? CM_EXTRUDE : CM_TROT; break;
    case 'R': id = CM_TSCALE; break;
    case 'F': id = CM_FRAMESEL; break;
    case 'A': id = ctrl ? (shift ? CM_SELALL : CM_PANEL) : CM_FRAMEALL; break;
    case 'I': if (ctrl && shift) id = CM_SELINV; break;
    case '4': id = CM_WIRE; break;
    case '5': id = CM_SHADED; break;
    case VK_F8: id = mode == M_OBJ ? CM_MOBJ + lastComp : CM_MOBJ; break;
    case VK_F9: id = CM_MVERT; break;
    case VK_F10: id = CM_MEDGE; break;
    case VK_F11: id = CM_MFACE; break;
    case VK_F7: id = CM_MMULTI; break;
    case 'Z': id = shift ? CM_REDO : CM_UNDO; break;
    case 'Y': if (ctrl) id = CM_REDO; break;
    case 'D': if (ctrl) id = CM_DUP; break;
    case 'N': if (ctrl) id = CM_NEW; break;
    case 'O': if (ctrl) id = CM_IMPORT; break;
    case 'S': if (ctrl) id = shift ? CM_EXPORT_AS : CM_EXPORT; break;
    case 'G': id = CM_REPEAT; break;
    case VK_F1: id = CM_HELP; break;
    case VK_DELETE: case VK_BACK: id = CM_DELETE; break;
    case VK_OEM_PLUS: case VK_ADD: manipSize *= 1.15f; redraw(); break;
    case VK_OEM_MINUS: case VK_SUBTRACT: manipSize /= 1.15f; redraw(); break;
    }
    if (id) do_cmd(id);
}

static LRESULT CALLBACK ViewProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (m) {
    case WM_CREATE: {
        PIXELFORMATDESCRIPTOR pfd;
        memset(&pfd, 0, sizeof pfd);
        pfd.nSize = sizeof pfd;
        pfd.nVersion = 1;
        pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        pfd.iPixelType = PFD_TYPE_RGBA;
        pfd.cColorBits = 32;
        pfd.cDepthBits = 24;
        pfd.iLayerType = PFD_MAIN_PLANE;
        hdc = GetDC(h);
        SetPixelFormat(hdc, ChoosePixelFormat(hdc, &pfd), &pfd);
        wglMakeCurrent(hdc, wglCreateContext(hdc));
        return 0;
    }
    case WM_SIZE:
        vw = LOWORD(lp) > 0 ? LOWORD(lp) : 1;
        vh = HIWORD(lp) > 0 ? HIWORD(lp) : 1;
        redraw();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT p;
        BeginPaint(h, &p);
        render();
        EndPaint(h, &p);
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN: {
        SetFocus(h);
        if (dk != D_NONE) return 0;
        int btn = m == WM_LBUTTONDOWN ? 0 : m == WM_MBUTTONDOWN ? 1 : 2;
        mx0 = curx = x;
        my0 = cury = y;
        if (key(VK_MENU)) {
            dk = btn == 0 ? D_TUMBLE : btn == 1 ? D_PAN : D_DOLLY;
        } else if (btn == 2) {
            marking_menu(x, y);
            return 0;
        } else if (btn == 1) {
            if (!manip_vis()) return 0;
            drag_begin(lastHandle, x, y);
            dk = D_MANIP;
        } else if (tool == T_PAINT) {
            dk = key('B') ? D_BRUSH : D_PAINT;
            rad0 = paintRad;
            if (dk == D_PAINT) paint_begin(x, y);
        } else {
            int hh = manip_vis() ? manip_hit(x, y) : H_NONE;
            if (hh) {
                drag_begin(hh, x, y);
                dk = D_MANIP;
            } else {
                int c = key(VK_CONTROL), s = key(VK_SHIFT);
                selMod = c && s ? 3 : s ? 1 : c ? 2 : 0;
                dk = D_PEND;
            }
        }
        dkBtn = btn;
        SetCapture(h);
        hover_update(-10000, 0);
        SetCursor(view_cursor());
        redraw();
        return 0;
    }
    case WM_MOUSEMOVE: {
        int dx = x - curx, dy = y - cury;
        curx = x;
        cury = y;
        switch (dk) {
        case D_TUMBLE:
            yaw -= dx * 0.006f;
            pitch += dy * 0.006f;
            if (pitch > 1.55f) pitch = 1.55f;
            if (pitch < -1.55f) pitch = -1.55f;
            break;
        case D_PAN: {
            cam_update();
            float s = wpp(coi);
            coi = v3add(coi, v3add(v3mul(camR, -dx * s), v3mul(camU, dy * s)));
            break;
        }
        case D_DOLLY: {
            float dd = abs(dx) > abs(dy) ? (float)dx : (float)-dy;
            dist *= expf(-dd * 0.008f);
            if (dist < 1e-3f) dist = 1e-3f;
            break;
        }
        case D_PEND:
            if (abs(x - mx0) > 3 || abs(y - my0) > 3) { dk = D_MARQ; SetCursor(view_cursor()); }
            break;
        case D_MANIP: drag_move(x, y); panel_sync(); status_update(); break;
        case D_PAINT: paint_dab(x, y); break;
        case D_BRUSH:
            paintRad = rad0 + (x - mx0) / ds;
            if (paintRad < 1) paintRad = 1;
            panel_sync();
            break;
        default:
            if (!tracking) {
                TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
                tracking = TrackMouseEvent(&tme);
            }
            if (manip_vis()) {
                int hh = manip_hit(x, y);
                if (hh != hover) { hover = hh; redraw(); }
            }
            hover_update(x, y);
            if (tool == T_PAINT) redraw();
            return 0;
        }
        redraw();
        return 0;
    }
    case WM_LBUTTONUP: case WM_MBUTTONUP: case WM_RBUTTONUP: {
        int btn = m == WM_LBUTTONUP ? 0 : m == WM_MBUTTONUP ? 1 : 2;
        if (dk != D_NONE && btn == dkBtn) end_drag(0);
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (dk != D_NONE) end_drag(1);
        return 0;
    case WM_MOUSELEAVE:
        tracking = 0;
        curx = cury = -1000;
        hover_update(-10000, 0);
        if (tool == T_PAINT) redraw();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(view_cursor()); return TRUE; }
        break;
    case WM_MOUSEWHEEL:
        dist *= powf(0.88f, (short)HIWORD(wp) / 120.0f);
        redraw();
        return 0;
    case WM_KEYDOWN:
        if (!(lp & (1 << 30)) && dk == D_NONE) handle_key(wp);
        return 0;
    case WM_SYSKEYDOWN:
        if (wp == VK_F10) { if (!(lp & (1 << 30)) && dk == D_NONE) handle_key(wp); return 0; }
        if (wp == VK_MENU) return 0;   /* Alt is for navigation, never the menu bar */
        break;
    case WM_SYSKEYUP:
        if (wp == VK_MENU || wp == VK_F10) return 0;
        break;
    case WM_SYSCHAR:
        if (wp != ' ' && !(lp & (1u << 29))) return 0;
        break;
    }
    return DefWindowProc(h, m, wp, lp);
}

/* ================================================================ main window */

static HMENU build_menu(void)
{
    HMENU bar = CreateMenu(), m;
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_NEW, "&New Scene\tCtrl+N");
    AppendMenuA(m, MF_STRING, CM_IMPORT, "&Import OBJ...\tCtrl+O");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_EXPORT, "&Export OBJ\tCtrl+S");
    AppendMenuA(m, MF_STRING, CM_EXPORT_AS, "Export OBJ &As...\tCtrl+Shift+S");
    AppendMenuA(m, MF_STRING, CM_EXPORT_SEL, "Export &Selection as OBJ...");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_EXIT, "E&xit");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&File");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_UNDO, "&Undo\tZ");
    AppendMenuA(m, MF_STRING, CM_REDO, "&Redo\tShift+Z");
    AppendMenuA(m, MF_STRING, CM_REPEAT, "Repeat &Last\tG");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_DELETE, "&Delete\tDel");
    AppendMenuA(m, MF_STRING, CM_DUP, "Dupli&cate\tCtrl+D");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Edit");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_CUBE, "Polygon &Cube");
    AppendMenuA(m, MF_STRING, CM_CYL, "Polygon C&ylinder");
    AppendMenuA(m, MF_STRING, CM_SPHERE, "Polygon &Sphere");
    AppendMenuA(m, MF_STRING, CM_PLANE, "Polygon &Plane");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Create");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_MOBJ, "&Object Mode\tF8");
    AppendMenuA(m, MF_STRING, CM_MVERT, "&Vertex\tF9");
    AppendMenuA(m, MF_STRING, CM_MEDGE, "&Edge\tF10");
    AppendMenuA(m, MF_STRING, CM_MFACE, "&Face\tF11");
    AppendMenuA(m, MF_STRING, CM_MMULTI, "&Multi (verts, edges, faces)\tF7");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_SELALL, "Select &All\tCtrl+Shift+A");
    AppendMenuA(m, MF_STRING, CM_SELINV, "&Invert Selection\tCtrl+Shift+I");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Select");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_EXTRUDE, "&Extrude\tCtrl+E");
    AppendMenuA(m, MF_STRING, CM_MERGE, "&Merge to Center");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Mesh");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_TSEL, "&Select Tool\tQ");
    AppendMenuA(m, MF_STRING, CM_TMOVE, "&Move Tool\tW");
    AppendMenuA(m, MF_STRING, CM_TROT, "&Rotate Tool\tE");
    AppendMenuA(m, MF_STRING, CM_TSCALE, "S&cale Tool\tR");
    AppendMenuA(m, MF_STRING, CM_TPAINT, "&Paint Vertex Color Tool");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Tools");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_SETCOL, "&Set Paint Color...");
    AppendMenuA(m, MF_STRING, CM_APPLYCOL, "&Apply Color to Selection");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "C&olor");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_WIRE, "&Wireframe\t4");
    AppendMenuA(m, MF_STRING, CM_SHADED, "&Shaded\t5");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_FRAMESEL, "&Frame Selected\tF");
    AppendMenuA(m, MF_STRING, CM_FRAMEALL, "Frame &All\tA");
    AppendMenuA(m, MF_STRING, CM_PANEL, "Side &Panel\tCtrl+A");
    hThemeMenu = CreatePopupMenu();
    for (int i = 0; i < NTHEME; i++) AppendMenuA(hThemeMenu, MF_STRING, CM_THEME + i, themes[i].name);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)hThemeMenu, "&Theme");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Display");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_HELP, "&Hotkeys\tF1");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Help");
    return bar;
}

static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SIZE: {
        if (wp == SIZE_MINIMIZED) return 0;
        int W = LOWORD(lp), H = HIWORD(lp);
        int sh = (int)(20 * ds), pw = panelOn ? (int)(215 * ds) : 0, bh = tb_layout(W);
        if (pw > W / 2) pw = W / 2;
        MoveWindow(hBar, 0, 0, W, bh, TRUE);
        MoveWindow(hView, 0, bh, W - pw, H - sh - bh, TRUE);
        MoveWindow(hPanel, W - pw, bh, pw, H - sh - bh, TRUE);
        ShowWindow(hPanel, panelOn ? SW_SHOW : SW_HIDE);
        MoveWindow(hStatus, 0, H - sh, W, sh, TRUE);
        return 0;
    }
    case WM_COMMAND:
        if (!lp) { do_cmd(LOWORD(wp)); SetFocus(hView); }
        return 0;
    case WM_SETFOCUS:
        SetFocus(hView);
        return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_KEYMENU && lp == 0) return 0;
        break;
    case WM_CTLCOLORSTATIC:
        SetTextColor((HDC)wp, th.text);
        SetBkColor((HDC)wp, th.ui);
        return (LRESULT)brUi;
    case WM_ERASEBKGND: {
        RECT rc;
        GetClientRect(h, &rc);
        FillRect((HDC)wp, &rc, brUi);
        return 1;
    }
    case WM_UAHDRAWMENU:
        if (th.sys) break;
        menubar_paint(h, lp);
        return 1;
    case WM_UAHDRAWMENUITEM:
        if (th.sys) break;
        menubar_item(lp);
        return 1;
    case WM_NCPAINT: case WM_NCACTIVATE: {
        LRESULT r = DefWindowProc(h, m, wp, lp);
        if (!th.sys) menubar_line(h);
        return r;
    }
    case WM_CLOSE:
        if (!confirm_discard()) return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev;
    (void)cmd;
    SetProcessDPIAware();
    HDC sdc = GetDC(NULL);
    ds = GetDeviceCaps(sdc, LOGPIXELSY) / 96.0f;
    ReleaseDC(NULL, sdc);
    if (ds < 1) ds = 1;
    for (int t = 0; t < PRIM_COUNT; t++) prim_defaults(&defPrim[t], t);
    theme_load(theme_saved());

    hFont = CreateFontA(-(int)(12 * ds), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hSmall = CreateFontA(-(int)(11 * ds), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hBold = CreateFontA(-(int)(12 * ds), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");

    WNDCLASSEXA wc;
    memset(&wc, 0, sizeof wc);
    wc.cbSize = sizeof wc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadImageA(inst, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    wc.hIconSm = LoadImageA(inst, MAKEINTRESOURCEA(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wc.lpfnWndProc = MainProc;
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "Tuff3DMain";
    RegisterClassExA(&wc);
    wc.lpfnWndProc = PanelProc;
    wc.lpszClassName = "Tuff3DPanel";
    RegisterClassExA(&wc);
    wc.lpfnWndProc = BarProc;
    wc.lpszClassName = "Tuff3DBar";
    RegisterClassExA(&wc);
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = ViewProc;
    wc.hbrBackground = NULL;
    wc.lpszClassName = "Tuff3DView";
    RegisterClassExA(&wc);

    hMenu = build_menu();
    hMain = CreateWindowA("Tuff3DMain", APP_NAME, WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                          (int)(1280 * ds), (int)(800 * ds), NULL, hMenu, inst, NULL);
    hBar = CreateWindowA("Tuff3DBar", "", WS_CHILD | WS_VISIBLE, 0, 0, 1, 1, hMain, NULL, inst, NULL);
    hView = CreateWindowA("Tuff3DView", "", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS, 0, 0, 1, 1, hMain, NULL, inst, NULL);
    hPanel = CreateWindowA("Tuff3DPanel", "", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 1, 1, hMain, NULL, inst, NULL);
    hStatus = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE | SS_LEFTNOWORDWRAP | SS_CENTERIMAGE | SS_NOPREFIX,
                            0, 0, 1, 1, hMain, NULL, inst, NULL);
    SendMessage(hStatus, WM_SETFONT, (WPARAM)hFont, 0);
    panel_create(inst);
    theme_window(hMain);

    ShowWindow(hMain, show);
    RECT rc;
    GetClientRect(hMain, &rc);
    SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    SetFocus(hView);
    refresh_all();

    MSG msgq;
    while (GetMessage(&msgq, NULL, 0, 0) > 0) {
        char cls[16] = "";
        if ((msgq.message == WM_KEYDOWN || msgq.message == WM_KEYUP) && msgq.hwnd != hView &&
            (msgq.hwnd == hMain || IsChild(hMain, msgq.hwnd)) &&
            !(GetClassNameA(msgq.hwnd, cls, sizeof cls) && !lstrcmpiA(cls, "Edit")))
            msgq.hwnd = hView;
        TranslateMessage(&msgq);
        DispatchMessage(&msgq);
    }
    return 0;
}
