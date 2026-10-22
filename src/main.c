/* tuff3d - minimalist polygon modeler with Maya style navigation and hotkeys.
   Win32 + OpenGL 1.1 only, so the result is a single small exe. */
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0601
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
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
    int hidden;          /* Ctrl+H; skipped when drawing and picking */
    unsigned order;      /* selection stamp, highest is the lead object */
    Prim prim;           /* construction history, PRIM_NONE once components are edited */
    float nangle;        /* normal angle: 0 = hard shade, 180 = smooth, between = hard over that angle */
} Obj;

enum { M_OBJ, M_VERT, M_EDGE, M_FACE, M_MULTI };   /* component modes match COMP_* */
enum { T_SELECT, T_MOVE, T_ROTATE, T_SCALE, T_PAINT, T_WELD, T_LOOP, T_CUT, NTOOL };
enum { H_NONE, H_X, H_Y, H_Z, H_YZ, H_XZ, H_XY, H_C, H_VIEW };

static Obj *ob;
static int nob, cob;
static int mode = M_OBJ, lastComp = M_VERT, tool = T_SELECT, wire;
static unsigned stamp;
static Prim defPrim[PRIM_COUNT];
static int normalFrame;
static M4 nframe;
static float manipSize = 1;
static float defAngle;          /* normal angle for new objects: the last shading picked */
static float mergeDist = 0.01f;     /* Mesh > Merge Vertices */
static float bevelW = 0.1f;         /* Edit Mesh > Bevel */
static int bridgeDivs;              /* Edit Mesh > Bridge */
static int liveCmd;                 /* the last step was this op: its settings redo it live */
static V3 paintCol = { 1, 0, 0 };
static float paintOpa = 1, paintRad = 40;
static char g_path[MAX_PATH];
static char g_msg[200];
static int lastCmd, lastHandle, hover, modified, snapGrid, snapPoint;
static unsigned editStamp;
static int hvObj = -1, hvKind, hvIdx, tracking, hudOn = 1;
static int dk, dkBtn, mx0, my0, curx = -1000, cury = -1000, selMod;
static float rad0;
enum { D_NONE, D_PEND, D_MARQ, D_MANIP, D_TUMBLE, D_PAN, D_DOLLY, D_PAINT, D_BRUSH, D_WELD };   /* preselection highlight (hvKind 0 = whole object) */

static HWND hMain, hView, hPanel, hStatus, hBar;
static HDC hdc;
static HMENU hMenu, hThemeMenu;
static HFONT hFont, hBold, hSmall, hHud;
static HBRUSH swatchBrush;
static int vw = 1, vh = 1, panelOn = 1;
static float ds = 1;

static void refresh_all(void);
static void status_text(void);
static int delete_ev(void);
static void status_update(void);
static void set_mode(int m);
static void draw_weld(void);
static void redraw(void) { InvalidateRect(hView, NULL, FALSE); }
static int key(int vk) { return GetKeyState(vk) < 0; }
static void toast(const char *s);
static DWORD msgT;
static void msg(const char *s) { snprintf(g_msg, sizeof g_msg, "%s", s); msgT = GetTickCount(); toast(s); }

static M4 obj_mat(const Obj *o) { return m4_trs(o->t, o->r, o->s); }

/* ================================================================ themes */

typedef struct {
    const char *name, *group;
    int sys, dark;          /* sys: Windows colors and menu bar; dark: dark title bar and popup menus */
    COLORREF ui, text, head, field, fieldText, line, hot;
    float bgTop[3], bgBot[3], grid[3], gridMajor[3], gridAxis[3], wire[3], sel2[3];
} Theme;

static Theme themes[] = {
    { "Classic", "System", 1, 0, 0, 0, 0, 0, 0, 0, 0,
      { .36f, .36f, .36f }, { .36f, .36f, .36f }, { .44f, .44f, .44f }, { .5f, .5f, .5f }, { .12f, .12f, .12f }, { 0, .02f, .4f }, { 1, 1, 1 } },
    { "Light", "Light", 0, 0, RGB(243, 243, 243), RGB(30, 30, 30), RGB(0, 90, 180), RGB(255, 255, 255), RGB(20, 20, 20), RGB(200, 200, 200), RGB(214, 224, 238),
      { .80f, .82f, .85f }, { .62f, .64f, .68f }, { .53f, .55f, .59f }, { .45f, .47f, .51f }, { .2f, .2f, .22f }, { 0, .05f, .45f }, { .1f, .1f, .1f } },
    { "Dark", "Dark", 0, 1, RGB(37, 37, 38), RGB(220, 220, 220), RGB(255, 255, 255), RGB(28, 28, 28), RGB(230, 230, 230), RGB(70, 70, 72), RGB(62, 62, 64),
      { .27f, .27f, .28f }, { .13f, .13f, .14f }, { .31f, .31f, .32f }, { .38f, .38f, .39f }, { .05f, .05f, .05f }, { .6f, .7f, .9f }, { 1, 1, 1 } },
    { "Midnight", "Dark", 0, 1, RGB(20, 26, 42), RGB(200, 210, 230), RGB(110, 200, 255), RGB(12, 16, 28), RGB(220, 230, 250), RGB(45, 58, 88), RGB(40, 56, 92),
      { .13f, .19f, .31f }, { .04f, .06f, .12f }, { .18f, .25f, .38f }, { .24f, .32f, .48f }, { .02f, .03f, .06f }, { .45f, .75f, 1 }, { 1, 1, 1 } },
    { "Tuff", "Dark", 0, 1, RGB(58, 10, 34), RGB(240, 225, 235), RGB(165, 255, 20), RGB(36, 4, 20), RGB(255, 240, 250), RGB(110, 30, 70), RGB(150, 16, 205),
      { .08f, .36f, .80f }, { .03f, .12f, .32f }, { .15f, .40f, .75f }, { .25f, .52f, .90f }, { .35f, 0, .16f }, { .65f, 1, .08f }, { 1, 1, 1 } },
    { "Forest", "Dark", 0, 1, RGB(28, 38, 31), RGB(214, 228, 210), RGB(140, 210, 120), RGB(18, 26, 21), RGB(225, 238, 220), RGB(54, 74, 60), RGB(50, 78, 58),
      { .24f, .31f, .26f }, { .10f, .14f, .11f }, { .28f, .36f, .30f }, { .34f, .44f, .36f }, { .04f, .06f, .04f }, { .75f, .95f, .6f }, { 1, 1, 1 } },
    { "Sunset", "Dark", 0, 1, RGB(43, 29, 34), RGB(240, 222, 210), RGB(255, 154, 60), RGB(30, 20, 24), RGB(250, 235, 225), RGB(85, 55, 60), RGB(110, 58, 52),
      { .42f, .24f, .30f }, { .12f, .07f, .12f }, { .46f, .30f, .34f }, { .54f, .36f, .38f }, { .08f, .03f, .05f }, { 1, .75f, .45f }, { 1, 1, 1 } },
    /* made from a few key colors each by a palette script (field, line, hover and grid colors are
       blends of the key colors); new themes go at the end so saved choices keep their number */
    { "Graphite", "Dark", 0, 1, RGB(43, 45, 48), RGB(212, 214, 217), RGB(124, 196, 255), RGB(31, 32, 35), RGB(212, 214, 217), RGB(80, 82, 85), RGB(66, 87, 106),
      { 0.353f, 0.373f, 0.4f }, { 0.165f, 0.173f, 0.188f }, { 0.349f, 0.361f, 0.38f }, { 0.435f, 0.447f, 0.463f }, { 0.063f, 0.071f, 0.075f }, { 0.561f, 0.784f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Charcoal", "Dark", 0, 1, RGB(27, 27, 27), RGB(207, 207, 207), RGB(255, 179, 71), RGB(19, 19, 19), RGB(207, 207, 207), RGB(67, 67, 67), RGB(91, 70, 39),
      { 0.227f, 0.227f, 0.227f }, { 0.082f, 0.082f, 0.082f }, { 0.259f, 0.259f, 0.259f }, { 0.361f, 0.361f, 0.361f }, { 0.039f, 0.039f, 0.039f }, { 1.f, 0.753f, 0.439f }, { 1.f, 1.f, 1.f } },
    { "Studio", "Dark", 0, 1, RGB(68, 68, 68), RGB(224, 224, 224), RGB(90, 176, 255), RGB(49, 49, 49), RGB(224, 224, 224), RGB(102, 102, 102), RGB(74, 98, 120),
      { 0.522f, 0.592f, 0.659f }, { 0.18f, 0.208f, 0.251f }, { 0.431f, 0.471f, 0.522f }, { 0.51f, 0.545f, 0.584f }, { 0.086f, 0.102f, 0.114f }, { 0.071f, 0.227f, 0.541f }, { 1.f, 1.f, 1.f } },
    { "Deep Space", "Dark", 0, 1, RGB(13, 15, 26), RGB(185, 192, 216), RGB(167, 139, 250), RGB(9, 11, 19), RGB(185, 192, 216), RGB(51, 54, 68), RGB(56, 50, 89),
      { 0.102f, 0.122f, 0.227f }, { 0.02f, 0.024f, 0.051f }, { 0.176f, 0.18f, 0.243f }, { 0.286f, 0.294f, 0.349f }, { 0.016f, 0.016f, 0.035f }, { 0.706f, 0.612f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Ocean", "Dark", 0, 1, RGB(15, 42, 54), RGB(205, 232, 240), RGB(63, 208, 212), RGB(11, 30, 39), RGB(205, 232, 240), RGB(57, 84, 95), RGB(28, 88, 98),
      { 0.114f, 0.373f, 0.471f }, { 0.031f, 0.125f, 0.169f }, { 0.18f, 0.341f, 0.404f }, { 0.294f, 0.431f, 0.486f }, { 0.016f, 0.063f, 0.078f }, { 0.373f, 0.878f, 0.894f }, { 1.f, 1.f, 1.f } },
    { "Abyss", "Dark", 0, 1, RGB(6, 19, 28), RGB(169, 198, 214), RGB(46, 163, 255), RGB(4, 14, 20), RGB(169, 198, 214), RGB(42, 58, 69), RGB(17, 59, 92),
      { 0.059f, 0.208f, 0.314f }, { 0.008f, 0.035f, 0.059f }, { 0.149f, 0.227f, 0.286f }, { 0.263f, 0.333f, 0.384f }, { 0.008f, 0.031f, 0.047f }, { 0.298f, 0.706f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Nordic", "Dark", 0, 1, RGB(46, 52, 64), RGB(216, 222, 233), RGB(136, 192, 208), RGB(33, 37, 46), RGB(216, 222, 233), RGB(83, 89, 101), RGB(71, 91, 104),
      { 0.298f, 0.337f, 0.416f }, { 0.169f, 0.188f, 0.231f }, { 0.325f, 0.353f, 0.404f }, { 0.42f, 0.439f, 0.486f }, { 0.059f, 0.067f, 0.078f }, { 0.561f, 0.737f, 0.733f }, { 1.f, 1.f, 1.f } },
    { "Night City", "Dark", 0, 1, RGB(26, 27, 38), RGB(192, 202, 245), RGB(122, 162, 247), RGB(19, 19, 27), RGB(192, 202, 245), RGB(63, 66, 84), RGB(53, 65, 97),
      { 0.173f, 0.192f, 0.314f }, { 0.086f, 0.086f, 0.118f }, { 0.235f, 0.243f, 0.31f }, { 0.337f, 0.349f, 0.404f }, { 0.031f, 0.035f, 0.055f }, { 0.733f, 0.604f, 0.969f }, { 1.f, 1.f, 1.f } },
    { "Vampire", "Dark", 0, 1, RGB(40, 42, 54), RGB(248, 248, 242), RGB(255, 121, 198), RGB(29, 30, 39), RGB(248, 248, 242), RGB(86, 87, 95), RGB(100, 64, 94),
      { 0.267f, 0.278f, 0.353f }, { 0.118f, 0.122f, 0.161f }, { 0.29f, 0.294f, 0.349f }, { 0.384f, 0.392f, 0.435f }, { 0.047f, 0.051f, 0.063f }, { 0.741f, 0.576f, 0.976f }, { 1.f, 1.f, 1.f } },
    { "Mocha", "Dark", 0, 1, RGB(30, 30, 46), RGB(205, 214, 244), RGB(245, 194, 231), RGB(22, 22, 33), RGB(205, 214, 244), RGB(68, 70, 90), RGB(90, 76, 98),
      { 0.271f, 0.278f, 0.353f }, { 0.094f, 0.094f, 0.145f }, { 0.278f, 0.286f, 0.341f }, { 0.376f, 0.384f, 0.431f }, { 0.047f, 0.047f, 0.063f }, { 0.537f, 0.706f, 0.98f }, { 1.f, 1.f, 1.f } },
    { "Neon Moss", "Dark", 0, 1, RGB(39, 40, 34), RGB(248, 248, 242), RGB(166, 226, 46), RGB(28, 29, 24), RGB(248, 248, 242), RGB(85, 86, 80), RGB(75, 92, 37),
      { 0.286f, 0.282f, 0.243f }, { 0.118f, 0.122f, 0.102f }, { 0.298f, 0.298f, 0.271f }, { 0.396f, 0.396f, 0.373f }, { 0.051f, 0.051f, 0.043f }, { 0.902f, 0.859f, 0.455f }, { 1.f, 1.f, 1.f } },
    { "Solar Dark", "Dark", 0, 1, RGB(0, 43, 54), RGB(147, 161, 161), RGB(181, 137, 0), RGB(0, 31, 39), RGB(147, 161, 161), RGB(32, 69, 78), RGB(51, 69, 39),
      { 0.059f, 0.29f, 0.349f }, { 0.f, 0.129f, 0.169f }, { 0.149f, 0.306f, 0.349f }, { 0.263f, 0.4f, 0.435f }, { 0.008f, 0.055f, 0.063f }, { 0.165f, 0.631f, 0.596f }, { 1.f, 1.f, 1.f } },
    { "Retro Groove", "Dark", 0, 1, RGB(40, 40, 40), RGB(235, 219, 178), RGB(250, 189, 47), RGB(29, 29, 29), RGB(235, 219, 178), RGB(83, 79, 70), RGB(99, 82, 42),
      { 0.314f, 0.286f, 0.271f }, { 0.114f, 0.125f, 0.129f }, { 0.306f, 0.298f, 0.294f }, { 0.4f, 0.396f, 0.392f }, { 0.055f, 0.051f, 0.051f }, { 0.996f, 0.502f, 0.098f }, { 1.f, 1.f, 1.f } },
    { "Teal Night", "Dark", 0, 1, RGB(16, 34, 38), RGB(199, 230, 227), RGB(46, 230, 196), RGB(12, 24, 27), RGB(199, 230, 227), RGB(56, 77, 80), RGB(24, 89, 82),
      { 0.122f, 0.29f, 0.314f }, { 0.043f, 0.102f, 0.114f }, { 0.192f, 0.294f, 0.306f }, { 0.302f, 0.388f, 0.4f }, { 0.02f, 0.047f, 0.055f }, { 0.341f, 0.949f, 0.831f }, { 1.f, 1.f, 1.f } },
    { "Aurora", "Dark", 0, 1, RGB(14, 26, 36), RGB(209, 240, 234), RGB(92, 242, 166), RGB(10, 19, 26), RGB(209, 240, 234), RGB(57, 73, 80), RGB(36, 86, 72),
      { 0.114f, 0.353f, 0.353f }, { 0.102f, 0.063f, 0.212f }, { 0.216f, 0.302f, 0.369f }, { 0.322f, 0.396f, 0.455f }, { 0.027f, 0.051f, 0.071f }, { 0.616f, 0.949f, 0.784f }, { 1.f, 1.f, 1.f } },
    { "Plum", "Dark", 0, 1, RGB(42, 24, 48), RGB(236, 216, 240), RGB(224, 124, 255), RGB(30, 17, 35), RGB(236, 216, 240), RGB(85, 66, 90), RGB(93, 52, 106),
      { 0.329f, 0.188f, 0.373f }, { 0.102f, 0.059f, 0.122f }, { 0.31f, 0.231f, 0.337f }, { 0.404f, 0.337f, 0.427f }, { 0.055f, 0.031f, 0.063f }, { 0.941f, 0.659f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Royal", "Dark", 0, 1, RGB(27, 24, 64), RGB(220, 216, 255), RGB(255, 210, 63), RGB(19, 17, 46), RGB(220, 216, 255), RGB(69, 66, 106), RGB(91, 76, 64),
      { 0.227f, 0.204f, 0.471f }, { 0.071f, 0.059f, 0.169f }, { 0.251f, 0.239f, 0.404f }, { 0.353f, 0.341f, 0.486f }, { 0.039f, 0.031f, 0.078f }, { 1.f, 0.867f, 0.4f }, { 1.f, 1.f, 1.f } },
    { "Crimson", "Dark", 0, 1, RGB(42, 15, 18), RGB(240, 214, 214), RGB(255, 77, 94), RGB(30, 11, 13), RGB(240, 214, 214), RGB(86, 59, 61), RGB(102, 32, 39),
      { 0.361f, 0.114f, 0.141f }, { 0.11f, 0.027f, 0.035f }, { 0.325f, 0.18f, 0.196f }, { 0.42f, 0.294f, 0.306f }, { 0.059f, 0.016f, 0.024f }, { 1.f, 0.478f, 0.522f }, { 1.f, 1.f, 1.f } },
    { "Volcano", "Dark", 0, 1, RGB(31, 18, 16), RGB(242, 221, 210), RGB(255, 106, 0), RGB(22, 13, 12), RGB(242, 221, 210), RGB(77, 63, 59), RGB(94, 43, 12),
      { 0.353f, 0.165f, 0.102f }, { 0.078f, 0.039f, 0.031f }, { 0.31f, 0.208f, 0.18f }, { 0.404f, 0.318f, 0.29f }, { 0.055f, 0.024f, 0.016f }, { 1.f, 0.627f, 0.251f }, { 1.f, 1.f, 1.f } },
    { "Ember", "Dark", 0, 1, RGB(34, 26, 20), RGB(240, 224, 204), RGB(255, 159, 67), RGB(24, 19, 14), RGB(240, 224, 204), RGB(79, 70, 60), RGB(96, 63, 33),
      { 0.29f, 0.208f, 0.149f }, { 0.09f, 0.067f, 0.047f }, { 0.286f, 0.239f, 0.208f }, { 0.384f, 0.345f, 0.314f }, { 0.047f, 0.035f, 0.024f }, { 1.f, 0.753f, 0.471f }, { 1.f, 1.f, 1.f } },
    { "Coffee", "Dark", 0, 1, RGB(42, 32, 25), RGB(234, 220, 203), RGB(212, 163, 115), RGB(30, 23, 18), RGB(234, 220, 203), RGB(84, 73, 64), RGB(90, 69, 50),
      { 0.31f, 0.239f, 0.188f }, { 0.114f, 0.086f, 0.067f }, { 0.306f, 0.267f, 0.231f }, { 0.4f, 0.365f, 0.337f }, { 0.055f, 0.039f, 0.031f }, { 0.902f, 0.769f, 0.612f }, { 1.f, 1.f, 1.f } },
    { "Synthwave", "Dark", 0, 1, RGB(36, 23, 52), RGB(246, 230, 255), RGB(255, 47, 208), RGB(26, 17, 37), RGB(246, 230, 255), RGB(82, 69, 97), RGB(97, 30, 96),
      { 0.29f, 0.122f, 0.431f }, { 0.102f, 0.043f, 0.18f }, { 0.294f, 0.192f, 0.388f }, { 0.388f, 0.302f, 0.471f }, { 0.047f, 0.02f, 0.078f }, { 0.212f, 0.976f, 0.965f }, { 1.f, 1.f, 1.f } },
    { "Cyberpunk", "Dark", 0, 1, RGB(13, 13, 13), RGB(242, 242, 242), RGB(252, 238, 10), RGB(9, 9, 9), RGB(242, 242, 242), RGB(63, 63, 63), RGB(80, 76, 12),
      { 0.165f, 0.165f, 0.165f }, { 0.f, 0.f, 0.f }, { 0.192f, 0.192f, 0.192f }, { 0.302f, 0.302f, 0.302f }, { 0.02f, 0.02f, 0.02f }, { 0.f, 0.941f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Matrix", "Dark", 0, 1, RGB(4, 10, 4), RGB(140, 255, 140), RGB(0, 255, 65), RGB(3, 7, 3), RGB(140, 255, 140), RGB(34, 64, 34), RGB(3, 79, 21),
      { 0.051f, 0.165f, 0.051f }, { 0.f, 0.02f, 0.f }, { 0.141f, 0.204f, 0.141f }, { 0.259f, 0.31f, 0.259f }, { 0.008f, 0.024f, 0.008f }, { 0.f, 1.f, 0.255f }, { 1.f, 1.f, 1.f } },
    { "Amber Terminal", "Dark", 0, 1, RGB(18, 12, 0), RGB(255, 204, 102), RGB(255, 176, 0), RGB(13, 9, 0), RGB(255, 204, 102), RGB(70, 54, 22), RGB(84, 58, 0),
      { 0.18f, 0.122f, 0.f }, { 0.031f, 0.02f, 0.f }, { 0.212f, 0.18f, 0.122f }, { 0.322f, 0.294f, 0.239f }, { 0.027f, 0.016f, 0.f }, { 1.f, 0.69f, 0.f }, { 1.f, 1.f, 1.f } },
    { "Blueprint", "Dark", 0, 1, RGB(15, 47, 92), RGB(230, 240, 255), RGB(255, 255, 255), RGB(11, 34, 66), RGB(230, 240, 255), RGB(62, 89, 128), RGB(82, 105, 138),
      { 0.114f, 0.353f, 0.659f }, { 0.063f, 0.227f, 0.451f }, { 0.286f, 0.447f, 0.655f }, { 0.451f, 0.573f, 0.733f }, { 0.902f, 0.941f, 1.f }, { 0.902f, 0.941f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Ice Cave", "Dark", 0, 1, RGB(26, 42, 51), RGB(227, 244, 251), RGB(143, 227, 255), RGB(19, 30, 37), RGB(227, 244, 251), RGB(70, 86, 95), RGB(59, 94, 108),
      { 0.247f, 0.435f, 0.522f }, { 0.086f, 0.188f, 0.231f }, { 0.267f, 0.396f, 0.451f }, { 0.365f, 0.478f, 0.525f }, { 0.039f, 0.078f, 0.094f }, { 0.741f, 0.937f, 1.f }, { 1.f, 1.f, 1.f } },
    { "High Contrast", "Dark", 0, 1, RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 255, 0), RGB(0, 0, 0), RGB(255, 255, 255), RGB(56, 56, 56), RGB(71, 71, 0),
      { 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f }, { 0.235f, 0.235f, 0.235f }, { 0.431f, 0.431f, 0.431f }, { 1.f, 1.f, 1.f }, { 0.f, 1.f, 1.f }, { 1.f, 1.f, 1.f } },
    { "Paper", "Light", 0, 0, RGB(251, 250, 247), RGB(43, 43, 43), RGB(51, 102, 204), RGB(254, 254, 253), RGB(43, 43, 43), RGB(205, 204, 202), RGB(195, 209, 235),
      { 0.949f, 0.941f, 0.918f }, { 0.851f, 0.839f, 0.8f }, { 0.792f, 0.784f, 0.757f }, { 0.686f, 0.678f, 0.651f }, { 0.455f, 0.451f, 0.439f }, { 0.122f, 0.231f, 0.502f }, { 0.067f, 0.067f, 0.067f } },
    { "Sepia", "Light", 0, 0, RGB(241, 231, 208), RGB(67, 52, 34), RGB(160, 82, 45), RGB(251, 248, 241), RGB(67, 52, 34), RGB(203, 192, 170), RGB(218, 189, 162),
      { 0.902f, 0.839f, 0.698f }, { 0.769f, 0.678f, 0.502f }, { 0.733f, 0.671f, 0.529f }, { 0.635f, 0.576f, 0.455f }, { 0.427f, 0.4f, 0.333f }, { 0.361f, 0.227f, 0.102f }, { 0.165f, 0.102f, 0.039f } },
    { "Solar Light", "Light", 0, 0, RGB(253, 246, 227), RGB(88, 110, 117), RGB(38, 139, 210), RGB(254, 252, 247), RGB(88, 110, 117), RGB(217, 216, 203), RGB(193, 216, 222),
      { 0.933f, 0.91f, 0.835f }, { 0.812f, 0.776f, 0.659f }, { 0.765f, 0.741f, 0.655f }, { 0.663f, 0.639f, 0.565f }, { 0.443f, 0.431f, 0.392f }, { 0.027f, 0.212f, 0.259f }, { 0.f, 0.169f, 0.212f } },
    { "Latte", "Light", 0, 0, RGB(239, 241, 245), RGB(76, 79, 105), RGB(136, 57, 239), RGB(250, 251, 252), RGB(76, 79, 105), RGB(203, 205, 214), RGB(210, 189, 243),
      { 0.863f, 0.878f, 0.91f }, { 0.737f, 0.753f, 0.8f }, { 0.706f, 0.718f, 0.753f }, { 0.608f, 0.62f, 0.651f }, { 0.416f, 0.42f, 0.435f }, { 0.118f, 0.4f, 0.961f }, { 0.067f, 0.067f, 0.106f } },
    { "Mint", "Light", 0, 0, RGB(233, 247, 241), RGB(30, 61, 51), RGB(15, 157, 110), RGB(248, 253, 251), RGB(30, 61, 51), RGB(188, 206, 199), RGB(172, 222, 204),
      { 0.816f, 0.937f, 0.89f }, { 0.624f, 0.831f, 0.753f }, { 0.635f, 0.78f, 0.725f }, { 0.549f, 0.675f, 0.627f }, { 0.384f, 0.447f, 0.424f }, { 0.043f, 0.361f, 0.251f }, { 0.039f, 0.165f, 0.122f } },
    { "Lavender", "Light", 0, 0, RGB(241, 236, 250), RGB(58, 47, 85), RGB(123, 79, 214), RGB(251, 249, 254), RGB(58, 47, 85), RGB(201, 194, 214), RGB(208, 192, 240),
      { 0.867f, 0.824f, 0.949f }, { 0.718f, 0.651f, 0.867f }, { 0.698f, 0.647f, 0.8f }, { 0.604f, 0.561f, 0.69f }, { 0.412f, 0.388f, 0.459f }, { 0.239f, 0.165f, 0.502f }, { 0.102f, 0.063f, 0.2f } },
    { "Rose", "Light", 0, 0, RGB(251, 236, 239), RGB(77, 38, 48), RGB(214, 51, 108), RGB(254, 249, 250), RGB(77, 38, 48), RGB(213, 192, 197), RGB(241, 184, 202),
      { 0.961f, 0.827f, 0.863f }, { 0.867f, 0.639f, 0.702f }, { 0.804f, 0.647f, 0.69f }, { 0.694f, 0.557f, 0.596f }, { 0.459f, 0.388f, 0.408f }, { 0.478f, 0.122f, 0.239f }, { 0.165f, 0.039f, 0.078f } },
    { "Bubblegum", "Light", 0, 0, RGB(255, 230, 244), RGB(74, 26, 58), RGB(255, 62, 165), RGB(255, 248, 252), RGB(74, 26, 58), RGB(215, 185, 203), RGB(255, 183, 222),
      { 1.f, 0.816f, 0.925f }, { 1.f, 0.624f, 0.824f }, { 0.878f, 0.635f, 0.769f }, { 0.761f, 0.549f, 0.663f }, { 0.494f, 0.384f, 0.443f }, { 0.541f, 0.039f, 0.353f }, { 0.2f, 0.f, 0.122f } },
    { "Sky", "Light", 0, 0, RGB(234, 244, 252), RGB(29, 53, 80), RGB(30, 136, 229), RGB(249, 252, 254), RGB(29, 53, 80), RGB(189, 202, 214), RGB(177, 214, 246),
      { 0.812f, 0.902f, 0.98f }, { 0.565f, 0.761f, 0.933f }, { 0.608f, 0.733f, 0.843f }, { 0.525f, 0.631f, 0.725f }, { 0.369f, 0.427f, 0.478f }, { 0.051f, 0.239f, 0.451f }, { 0.02f, 0.094f, 0.18f } },
    { "Arctic", "Light", 0, 0, RGB(244, 248, 250), RGB(38, 50, 56), RGB(0, 172, 193), RGB(252, 253, 254), RGB(38, 50, 56), RGB(199, 204, 207), RGB(176, 227, 234),
      { 0.89f, 0.929f, 0.949f }, { 0.714f, 0.792f, 0.831f }, { 0.706f, 0.761f, 0.784f }, { 0.608f, 0.655f, 0.678f }, { 0.416f, 0.439f, 0.451f }, { 0.f, 0.412f, 0.471f }, { 0.063f, 0.125f, 0.153f } },
    { "Sand", "Light", 0, 0, RGB(245, 236, 220), RGB(74, 59, 37), RGB(201, 123, 26), RGB(252, 249, 244), RGB(74, 59, 37), RGB(207, 197, 180), RGB(233, 204, 166),
      { 0.918f, 0.859f, 0.749f }, { 0.812f, 0.725f, 0.561f }, { 0.761f, 0.698f, 0.576f }, { 0.655f, 0.604f, 0.498f }, { 0.439f, 0.412f, 0.357f }, { 0.42f, 0.271f, 0.063f }, { 0.169f, 0.102f, 0.02f } },
    { "Lemonade", "Light", 0, 0, RGB(255, 251, 224), RGB(61, 58, 18), RGB(230, 168, 0), RGB(255, 254, 246), RGB(61, 58, 18), RGB(212, 209, 179), RGB(248, 228, 161),
      { 1.f, 0.953f, 0.659f }, { 0.949f, 0.847f, 0.376f }, { 0.855f, 0.792f, 0.455f }, { 0.737f, 0.686f, 0.392f }, { 0.482f, 0.455f, 0.302f }, { 0.42f, 0.325f, 0.f }, { 0.165f, 0.133f, 0.f } },
    { "Retro 95", "Light", 0, 0, RGB(192, 192, 192), RGB(0, 0, 0), RGB(0, 0, 128), RGB(236, 236, 236), RGB(0, 0, 0), RGB(150, 150, 150), RGB(138, 138, 174),
      { 0.f, 0.502f, 0.502f }, { 0.f, 0.376f, 0.376f }, { 0.122f, 0.506f, 0.506f }, { 0.239f, 0.573f, 0.573f }, { 0.f, 0.11f, 0.11f }, { 0.f, 0.f, 0.502f }, { 1.f, 1.f, 1.f } },
};
#define NTHEME ((int)(sizeof themes / sizeof *themes))
static Theme th;            /* active theme, with the Classic colors filled in from Windows */
static int themeIdx;
static HBRUSH brUi, brField, brLine, brHot, brAcc;

static COLORREF mix(COLORREF a, COLORREF b)
{
    return RGB((GetRValue(a) + GetRValue(b)) / 2, (GetGValue(a) + GetGValue(b)) / 2, (GetBValue(a) + GetBValue(b)) / 2);
}

/* a theme with the Classic colors filled in from Windows */
static Theme theme_get(int i)
{
    Theme t = themes[i < 0 || i >= NTHEME ? 0 : i];
    if (t.sys) {
        t.ui = GetSysColor(COLOR_BTNFACE);
        t.text = t.head = GetSysColor(COLOR_BTNTEXT);
        t.field = GetSysColor(COLOR_WINDOW);
        t.fieldText = GetSysColor(COLOR_WINDOWTEXT);
        t.line = GetSysColor(COLOR_BTNSHADOW);
        t.hot = GetSysColor(COLOR_BTNHIGHLIGHT);
    }
    return t;
}

static void theme_load(int i)
{
    themeIdx = i < 0 || i >= NTHEME ? 0 : i;
    th = theme_get(themeIdx);
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

/* ---- settings, stored under HKEY_CURRENT_USER\Software\tuff3d */

#define REG_KEY "Software\\" APP_NAME

static int reg_get(const char *name, DWORD type, void *out, DWORD n)
{
    DWORD flags = type == REG_DWORD ? RRF_RT_REG_DWORD : type == REG_SZ ? RRF_RT_REG_SZ : RRF_RT_REG_BINARY;
    return RegGetValueA(HKEY_CURRENT_USER, REG_KEY, name, flags, NULL, out, &n) == ERROR_SUCCESS;
}

static void reg_set(const char *name, DWORD type, const void *data, DWORD n)
{
    HKEY k;
    if (RegCreateKeyExA(HKEY_CURRENT_USER, REG_KEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL)) return;
    RegSetValueExA(k, name, 0, type, (const BYTE *)data, n);
    RegCloseKey(k);
}

static int reg_int(const char *name, int def)
{
    DWORD v;
    return reg_get(name, REG_DWORD, &v, sizeof v) ? (int)v : def;
}

static void reg_set_int(const char *name, int v)
{
    DWORD d = (DWORD)v;
    reg_set(name, REG_DWORD, &d, sizeof d);
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
    o->nangle = defAngle;
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

typedef struct { Obj *o; int n, mode; char what[40]; } Snap;   /* what: the action it undoes */
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

static void undo_push(const char *what)
{
    liveCmd = 0;
    Snap s = snap_take();
    snprintf(s.what, sizeof s.what, "%s", what);
    modified = 1;
    editStamp++;
    stack_push(undo_s, &nundo, s);
    while (nredo) snap_free(&redo_s[--nredo]);
}

/* the step just pushed changed nothing after all: forget it */
static void undo_drop(void)
{
    if (nundo) snap_free(&undo_s[--nundo]);
}

static void do_undo(void)
{
    liveCmd = 0;
    if (!nundo) { msg("Nothing to undo"); return; }
    Snap s = snap_take();
    memcpy(s.what, undo_s[nundo - 1].what, sizeof s.what);
    stack_push(redo_s, &nredo, s);
    snap_restore(&undo_s[--nundo]);
    modified = 1;
    editStamp++;
    snprintf(g_msg, sizeof g_msg, "Undo %s", s.what);
    toast(g_msg);
}

static void do_redo(void)
{
    liveCmd = 0;
    if (!nredo) { msg("Nothing to redo"); return; }
    Snap s = snap_take();
    memcpy(s.what, redo_s[nredo - 1].what, sizeof s.what);
    stack_push(undo_s, &nundo, s);
    snap_restore(&redo_s[--nredo]);
    modified = 1;
    editStamp++;
    snprintf(g_msg, sizeof g_msg, "Redo %s", s.what);
    toast(g_msg);
}

/* ================================================================ camera */

static V3 coi;
static float yaw = 0.7854f, pitch = 0.46f, dist = 10.0f;
static const float FOVY = 0.6981f;
static M4 Pm, Vm, VPm, iVPm;
static V3 eye, camF, camR, camU;
static int ortho;              /* orthographic projection (front/side/top views); orbiting leaves it */

static void cam_update(void)
{
    V3 dir = v3(cosf(pitch) * sinf(yaw), sinf(pitch), cosf(pitch) * cosf(yaw));
    eye = v3add(coi, v3mul(dir, dist));
    V3 up = v3(0, 1, 0);
    if (fabsf(cosf(pitch)) < 1e-3f) {
        /* straight down (top view) or up: screen up is away from where the camera came from */
        float sg = pitch > 0 ? 1.0f : -1.0f;
        up = v3(-sinf(yaw) * sg, 0, -cosf(yaw) * sg);
    }
    Vm = m4_lookat(eye, coi, up);
    if (ortho) {
        float hh = dist * tanf(FOVY * 0.5f), hw = hh * vw / vh;
        Pm = m4_ortho(-hw, hw, -hh, hh, -dist * 100.0f, dist * 100.0f);
    } else {
        Pm = m4_persp(FOVY, (float)vw / vh, dist * 0.01f, dist * 1000.0f);
    }
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
    if (ortho) return 2 * dist * tanf(FOVY * 0.5f) / vh;
    float depth = v3dot(v3sub(p, eye), camF);
    if (depth < 1e-4f) depth = 1e-4f;
    return 2 * depth * tanf(FOVY * 0.5f) / vh;
}

/* direction from p toward the viewer (the same everywhere in an orthographic view) */
static V3 to_eye(V3 p)
{
    return ortho ? v3mul(camF, -1) : v3norm(v3sub(eye, p));
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

   Shaded triangles are expanded once into a vertex array (color, shading normal, position per corner)
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
    V3 *cn = malloc((m->fo[m->nf] + 1) * sizeof(V3));
    mesh_cnormals(m, o->nangle, cn);
    float *w = c->v;
    for (int f = 0; f < m->nf; f++) {
        int s = m->fo[f], n = m->fo[f + 1] - s;
        for (int k = 1; k + 1 < n; k++) {
            int cs[3] = { s, s + k, s + k + 1 };
            for (int j = 0; j < 3; j++) {
                V3 col = m->c[m->fi[cs[j]]], p = m->p[m->fi[cs[j]]], nn = cn[cs[j]];
                if (tint && m->fs[f]) col = v3lerp(col, v3(1, 0.45f, 0.1f), 0.65f);
                *w++ = col.x; *w++ = col.y; *w++ = col.z;
                *w++ = nn.x; *w++ = nn.y; *w++ = nn.z;
                *w++ = p.x; *w++ = p.y; *w++ = p.z;
            }
        }
    }
    free(cn);
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
    if (ob[i].hidden) return -1;
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
        if (ob[i].hidden) continue;
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
        if (o->hidden) continue;
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

/* nearest visible edge of any shown object (within a few pixels); u: where along it (ev order) */
static int edge_pick(int mx, int my, int *obj, int *edge, float *u)
{
    int R = (int)(14 * ds);
    float best = (float)R;
    *obj = -1;
    depth_capture(mx - R - 4, my - R - 4, mx + R + 4, my + R + 4);
    for (int i = 0; i < nob; i++) {
        if (ob[i].hidden) continue;
        Mesh *m = &ob[i].m;
        M4 M = obj_mat(&ob[i]);
        for (int e = 0; e < m->ne; e++) {
            V3 a = m4_pt(M, m->p[m->ev[2 * e]]), b = m4_pt(M, m->p[m->ev[2 * e + 1]]);
            float ax, ay, bx, by, t, x, y, z;
            if (!proj(a, &ax, &ay, NULL) || !proj(b, &bx, &by, NULL)) continue;
            float d = seg_dist((float)mx, (float)my, ax, ay, bx, by, &t);
            if (d >= best || !proj(v3lerp(a, b, t), &x, &y, &z) || !visible(x, y, z)) continue;
            best = d; *obj = i; *edge = e; *u = t;
        }
    }
    return *obj >= 0;
}

/* ---- Insert Edge Loop tool: the edge under the cursor and where the new loop would run */

static struct { int obj, e, n, closed; float u; V3 *pts; } elp = { -1, 0, 0, 0, 0, NULL };

static void loop_hover(int x, int y)
{
    int o, e = 0;
    float u = 0;
    cam_update();
    if (!edge_pick(x, y, &o, &e, &u)) {
        if (elp.obj >= 0) { elp.obj = -1; redraw(); }
        return;
    }
    /* the screen fraction is close enough to the edge's own for a preview; snap to the middle */
    if (fabsf(u - 0.5f) < 0.04f) u = 0.5f;
    if (u < 0.02f) u = 0.02f;
    if (u > 0.98f) u = 0.98f;
    Mesh *m = &ob[o].m;
    int *ra = malloc((m->ne + 1) * sizeof(int)), *rb = malloc((m->ne + 1) * sizeof(int));
    elp.n = mesh_edge_ring(m, e, ra, rb, &elp.closed);
    elp.pts = realloc(elp.pts, (elp.n + 1) * sizeof(V3));
    M4 M = obj_mat(&ob[o]);
    for (int i = 0; i < elp.n; i++) elp.pts[i] = m4_pt(M, v3lerp(m->p[ra[i]], m->p[rb[i]], u));
    free(ra); free(rb);
    elp.obj = o; elp.e = e; elp.u = u;
    redraw();
}

static void draw_loop_preview(void)
{
    if (tool != T_LOOP || elp.obj < 0 || elp.obj >= nob || dk != D_NONE) return;
    glColor3f(1, 1, 0);
    glLineWidth(2 * ds);
    glBegin(elp.closed ? GL_LINE_LOOP : GL_LINE_STRIP);
    for (int i = 0; i < elp.n; i++) {
        float x, y;
        if (proj(elp.pts[i], &x, &y, NULL)) glVertex2f(x, y);
    }
    glEnd();
    if (elp.n == 1) {   /* a single quad: show the point on the edge */
        float x, y;
        glPointSize(7 * ds);
        glBegin(GL_POINTS);
        if (proj(elp.pts[0], &x, &y, NULL)) glVertex2f(x, y);
        glEnd();
    }
    glLineWidth(1);
}

static void loop_click(void)
{
    if (elp.obj < 0 || elp.obj >= nob) return;
    Obj *o = &ob[elp.obj];
    undo_push("Insert Edge Loop");
    if (!mesh_insert_loop(&o->m, elp.e, elp.u)) {
        undo_drop();
        msg("Insert Edge Loop: needs four-sided faces on at least one side of the edge");
        return;
    }
    o->prim.type = PRIM_NONE;
    if (mode != M_EDGE && mode != M_MULTI) set_mode(M_EDGE);
    for (int i = 0; i < nob; i++)
        if (i != elp.obj && ob[i].hil) memset(ob[i].m.es, 0, ob[i].m.ne);
    o->hil = 1;
    o->order = ++stamp;
    elp.obj = -1;
    msg("Inserted an edge loop (it is selected)");
}

/* ---- Multi-Cut tool: click points on edges or vertices; each one cuts across the face it shares
   with the one before. Enter or right click finishes, Backspace takes the last point back, Esc
   drops them all. Ctrl+click an edge inserts an edge loop instead. */

static struct {
    int obj, n, cap, hv;        /* hv: 0 nothing under the cursor, 1 a point, 2 one that can't connect */
    const V3 *key;              /* the mesh the points belong to (dropped if it changes) */
    CutPt *p, hp;
    V3 *w, hw;
} mc = { -1, 0, 0, 0, NULL, NULL, { 0, 0, 0 }, NULL, { 0, 0, 0 } };

static int cut_on_face(const Mesh *m, int f, CutPt q)
{
    int s = m->fo[f], n = m->fo[f + 1] - s;
    for (int k = 0; k < n; k++) {
        int a = m->fi[s + k], b = m->fi[s + (k + 1) % n];
        if (q.b < 0 ? a == q.a : (a == q.a && b == q.b) || (a == q.b && b == q.a)) return 1;
    }
    return 0;
}

static int cut_same(CutPt a, CutPt b)
{
    if (a.b < 0 || b.b < 0) return a.b < 0 && b.b < 0 && a.a == b.a;
    if (a.a == b.b && a.b == b.a) b.t = 1 - b.t, b.a = a.a, b.b = a.b;
    return a.a == b.a && a.b == b.b && fabsf(a.t - b.t) < 1e-3f;
}

/* can a cut run from a to b inside one face? */
static int cut_link(const Mesh *m, CutPt a, CutPt b)
{
    if (cut_same(a, b)) return 0;
    for (int f = 0; f < m->nf; f++)
        if (cut_on_face(m, f, a) && cut_on_face(m, f, b)) return 1;
    return 0;
}

static void mc_reset(void)
{
    mc.obj = -1;
    mc.n = mc.hv = 0;
}

static int mc_valid(void)
{
    if (mc.obj < 0 || mc.obj >= nob || ob[mc.obj].m.p != mc.key) { mc_reset(); return 0; }
    return 1;
}

static void mc_hover(int x, int y)
{
    int o = -1, e = 0, kind = 0, idx = 0;
    float u = 0;
    mc_valid();
    if (!mc.n) mc.obj = -1;   /* before the first click any object will do */
    cam_update();
    mc.hv = 0;
    /* a vertex close by wins, else a point on the nearest edge */
    int R = (int)(10 * ds);
    float best = (float)R;
    depth_capture(x - 20, y - 20, x + 20, y + 20);
    for (int i = 0; i < nob; i++) {
        if (ob[i].hidden || (mc.obj >= 0 && i != mc.obj)) continue;
        Mesh *m = &ob[i].m;
        M4 M = obj_mat(&ob[i]);
        for (int v = 0; v < m->nv; v++) {
            float sx, sy, sz;
            if (!proj(m4_pt(M, m->p[v]), &sx, &sy, &sz)) continue;
            float d = hypotf(sx - x, sy - y);
            if (d < best && visible(sx, sy, sz)) { best = d; o = i; kind = 1; idx = v; }
        }
    }
    if (o < 0) {
        int oo, ee;
        float uu;
        if (edge_pick(x, y, &oo, &ee, &uu) && (mc.obj < 0 || oo == mc.obj)) { o = oo; e = ee; u = uu; kind = 2; }
    }
    if (o < 0) { redraw(); return; }
    Mesh *m = &ob[o].m;
    M4 M = obj_mat(&ob[o]);
    if (kind == 1) {
        mc.hp.a = idx; mc.hp.b = -1; mc.hp.t = 0;
        mc.hw = m4_pt(M, m->p[idx]);
    } else {
        if (fabsf(u - 0.5f) < 0.04f) u = 0.5f;   /* the middle is easy to hit */
        mc.hp.a = m->ev[2 * e]; mc.hp.b = m->ev[2 * e + 1]; mc.hp.t = u;
        mc.hw = m4_pt(M, v3lerp(m->p[mc.hp.a], m->p[mc.hp.b], u));
    }
    mc.hv = mc.n && !cut_link(m, mc.p[mc.n - 1], mc.hp) ? 2 : 1;
    if (!mc.n) mc.obj = o;   /* provisional until the first click */
    redraw();
}

static void mc_commit(void)
{
    if (!mc_valid() || mc.n < 2) { mc_reset(); return; }
    Obj *o = &ob[mc.obj];
    undo_push("Multi-Cut");
    int k = mesh_cut(&o->m, mc.p, mc.n);
    if (!k) { undo_drop(); mc_reset(); msg("Multi-Cut: nothing to cut"); return; }
    o->prim.type = PRIM_NONE;
    if (mode != M_EDGE && mode != M_MULTI) set_mode(M_EDGE);
    o->hil = 1;
    o->order = ++stamp;
    char b[64];
    snprintf(b, sizeof b, "Cut %d face(s) - the new edges are selected", k);
    msg(b);
    mc_reset();
}

/* returns 1 when the click was used */
static int mc_click(int ctrl)
{
    if (!mc.hv) return mc.n > 0;
    Mesh *m = &ob[mc.obj].m;
    if (ctrl && mc.hp.b >= 0) {
        /* Ctrl+click an edge: a whole edge loop through that point */
        int e = mesh_edge_find(m, mc.hp.a, mc.hp.b), o = mc.obj;
        float t = m->ev[2 * e] == mc.hp.a ? mc.hp.t : 1 - mc.hp.t;
        mc_reset();
        undo_push("Insert Edge Loop");
        if (!mesh_insert_loop(&ob[o].m, e, t)) { undo_drop(); msg("Insert Edge Loop: needs four-sided faces beside the edge"); return 1; }
        ob[o].prim.type = PRIM_NONE;
        if (mode != M_EDGE && mode != M_MULTI) set_mode(M_EDGE);
        ob[o].hil = 1;
        msg("Inserted an edge loop");
        return 1;
    }
    if (mc.hv == 2) { msg("Multi-Cut: the next point has to be on a face the last point touches"); return 1; }
    if (mc.n >= 2 && cut_same(mc.hp, mc.p[0])) {
        /* back on the first point: close the cut and finish */
        if (mc.n + 1 > mc.cap) { mc.cap = mc.cap * 2 + 8; mc.p = realloc(mc.p, mc.cap * sizeof *mc.p); mc.w = realloc(mc.w, mc.cap * sizeof *mc.w); }
        mc.p[mc.n] = mc.hp;
        mc.w[mc.n++] = mc.hw;
        mc_commit();
        return 1;
    }
    if (mc.n + 1 > mc.cap) { mc.cap = mc.cap * 2 + 8; mc.p = realloc(mc.p, mc.cap * sizeof *mc.p); mc.w = realloc(mc.w, mc.cap * sizeof *mc.w); }
    if (!mc.n) mc.key = m->p;
    mc.p[mc.n] = mc.hp;
    mc.w[mc.n++] = mc.hw;
    mc.hv = 0;
    return 1;
}

static void draw_cut(void)
{
    if (tool != T_CUT) return;
    if (mc.n && !mc_valid()) return;
    float x, y, px = 0, py = 0, r = 4 * ds;
    glLineWidth(2 * ds);
    glColor3f(1, 1, 0);
    glBegin(GL_LINE_STRIP);
    for (int i = 0; i < mc.n; i++) if (proj(mc.w[i], &x, &y, NULL)) glVertex2f(x, y);
    glEnd();
    if (mc.hv && mc.n && proj(mc.w[mc.n - 1], &px, &py, NULL) && proj(mc.hw, &x, &y, NULL)) {
        if (mc.hv == 2) glColor3f(1, .25f, .25f);
        glEnable(GL_LINE_STIPPLE);
        glLineStipple(1, 0xF0F0);
        glBegin(GL_LINES);
        glVertex2f(px, py); glVertex2f(x, y);
        glEnd();
        glDisable(GL_LINE_STIPPLE);
    }
    glPointSize(7 * ds);
    glBegin(GL_POINTS);
    glColor3f(1, 1, 0);
    for (int i = 0; i < mc.n; i++) if (proj(mc.w[i], &x, &y, NULL)) glVertex2f(x, y);
    if (mc.hv) {
        if (mc.hv == 2) glColor3f(1, .25f, .25f);
        if (proj(mc.hw, &x, &y, NULL)) glVertex2f(x, y);
    }
    glEnd();
    glLineWidth(1);
    (void)r;
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
        if (o->hidden) continue;
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

/* ---- animation: smooth camera moves and fading toasts. A timer runs only while something
   is moving, so the app still sits at 0% CPU when idle. */

#define TOAST_MS 2200
static struct { int on; DWORD t0; V3 c0, c1; float d0, d1, y0, y1, p0, p1; } tween;
static char toastS[160];
static DWORD toastT;
static int animOn;

static void anim_start(void)
{
    if (!animOn && hView) { SetTimer(hView, 1, 15, NULL); animOn = 1; }
}

static void toast(const char *s)
{
    snprintf(toastS, sizeof toastS, "%s", s);
    toastT = GetTickCount();
    anim_start();
    if (hView) redraw();
}

/* glide the camera to a new center/distance/angles */
static void cam_to(V3 c, float d, float yw, float pt)
{
    float dy = fmodf(yw - yaw, 2 * PI_F);
    if (dy > PI_F) dy -= 2 * PI_F;
    if (dy < -PI_F) dy += 2 * PI_F;
    tween.c0 = coi; tween.c1 = c;
    tween.d0 = dist; tween.d1 = d;
    tween.y0 = yaw; tween.y1 = yaw + dy;
    tween.p0 = pitch; tween.p1 = pt;
    tween.t0 = GetTickCount();
    tween.on = 1;
    anim_start();
}

/* one timer tick; returns 0 when nothing is animating any more */
static int anim_step(void)
{
    if (tween.on) {
        float t = (GetTickCount() - tween.t0) / 260.0f;
        if (t >= 1) { t = 1; tween.on = 0; }
        float e = t * t * (3 - 2 * t);
        coi = v3lerp(tween.c0, tween.c1, e);
        dist = tween.d0 * powf(tween.d1 / tween.d0, e);
        yaw = tween.y0 + (tween.y1 - tween.y0) * e;
        pitch = tween.p0 + (tween.p1 - tween.p0) * e;
    }
    return tween.on || (toastS[0] && GetTickCount() - toastT < TOAST_MS);
}

static void frame(int all)
{
    V3 a, b;
    if (!sel_bbox(&a, &b, all) && !sel_bbox(&a, &b, 1)) {
        cam_to(v3(0, 0, 0), 10, yaw, pitch);
        return;
    }
    float r = v3len(v3sub(b, a)) * 0.5f;
    if (r < 0.05f) r = 0.05f;
    float half = FOVY * 0.5f, hh = atanf(tanf(half) * vw / vh);
    cam_to(v3mul(v3add(a, b), 0.5f), r / sinf(hh < half ? hh : half) * 1.1f, yaw, pitch);
}

/* look along an axis (0 X, 1 Y, 2 Z; sign picks the side), keeping the current framing */
static void view_axis(int axis, int neg)
{
    float yw = axis == 0 ? PI_F / 2 : 0, pt = axis == 1 ? PI_F / 2 : 0;
    ortho = 1;
    if (neg) { if (axis == 1) pt = -pt; else yw += PI_F; }
    cam_to(coi, dist, yw, pt);
    static const char *names[3][2] = { { "Side view (+X)", "Side view (-X)" }, { "Top view", "Bottom view" }, { "Front view", "Back view" } };
    toast(names[axis][neg]);
}

/* ================================================================ modes & tools */

static void set_mode(int m)
{
    if (m == mode) return;
    liveCmd = 0;
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
    elp.obj = -1;
    if (tool == T_CUT && t != T_CUT) mc_commit();   /* leaving Multi-Cut finishes the cut */
    tool = t;
    normalFrame = 0;
    if (t == T_WELD && mode != M_VERT && mode != M_MULTI) set_mode(M_VERT);
    if ((t == T_LOOP || t == T_CUT) && mode != M_EDGE && mode != M_MULTI) set_mode(M_EDGE);
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
    return v3dot(v3norm(v3sub(q, g->piv)), to_eye(g->piv)) > -0.02f;
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
        V3 toEye = to_eye(g.piv);
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

static float nz(float v) { return fabsf(v) < 5e-4f ? 0 : v; }   /* no "-0.000" in readouts */

static int snapOn;      /* point snap found a target this drag step */
static V3 snapAt;

/* nearest vertex on screen (within 20 px) that is not itself being moved */
static int snap_target(int mx, int my, V3 *out)
{
    float best = 20 * ds;
    int found = 0;
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        DEnt *e = NULL;
        if (o->hidden) continue;
        for (int k = 0; k < dg.ne; k++) if (dg.e[k].obj == i) e = &dg.e[k];
        if (e && mode == M_OBJ) continue;
        unsigned char *skip = e ? calloc(o->m.nv + 1, 1) : NULL;
        for (int j = 0; e && j < e->n; j++) skip[e->vi[j]] = 1;
        M4 M = obj_mat(o);
        for (int v = 0; v < o->m.nv; v++) {
            float x, y;
            if (skip && skip[v]) continue;
            V3 w = m4_pt(M, o->m.p[v]);
            if (!proj(w, &x, &y, NULL)) continue;
            float d = hypotf(x - mx, y - my);
            if (d < best) { best = d; *out = w; found = 1; }
        }
        free(skip);
    }
    return found;
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
        if ((key('X') || snapGrid) && world) {
            /* hold X: snap to the grid */
            V3 np = v3add(dg.piv, delta);
            for (int c = 0; c < 3; c++)
                if (fabsf((&delta.x)[c]) > 1e-6f) (&np.x)[c] = roundf((&np.x)[c]);
            delta = v3sub(np, dg.piv);
        }
        snapOn = 0;
        if (key('V') || snapPoint) {
            /* point snap: put the pivot on the nearest vertex, within the handle's constraint */
            V3 tp;
            if (snap_target(mx, my, &tp)) {
                V3 want = v3sub(tp, dg.piv);
                if (h >= H_X && h <= H_Z) delta = v3mul(dg.axis, v3dot(want, dg.axis));
                else if (h != H_C) delta = v3sub(want, v3mul(dg.pn, v3dot(want, dg.pn)));
                else delta = want;
                snapOn = 1;
                snapAt = tp;
            }
        }
        snprintf(g_msg, sizeof g_msg, "Move %.3f, %.3f, %.3f%s", nz(delta.x), nz(delta.y), nz(delta.z),
                 snapOn ? "  (point snap)" : (key('X') || snapGrid) && world ? "  (grid snap)" : "");
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
        snprintf(g_msg, sizeof g_msg, "Rotate %.1f deg%s", nz(a * 180 / PI_F), key('J') ? "  (15 deg steps)" : "");
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
        undo_push(tool == T_MOVE ? "Move" : tool == T_ROTATE ? "Rotate" : "Scale");
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
    if (dg.on && tool != T_ROTATE && dg.handle >= H_X && dg.handle <= H_Z) {
        /* the line the drag is locked to */
        V3 a = v3mul(dg.ax[dg.handle - H_X], g.L * 200), p0 = v3sub(dg.piv, a), p1 = v3add(dg.piv, a);
        const float *c = col[dg.handle - H_X];
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(c[0], c[1], c[2], .55f);
        glLineWidth(1);
        glBegin(GL_LINES);
        glVertex3fv(&p0.x);
        glVertex3fv(&p1.x);
        glEnd();
        glDisable(GL_BLEND);
    }
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
            if (!ps.dirty) { undo_push("Paint Vertex Color"); ps.dirty = 1; }
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

/* ================================================================ reference image planes

   One picture each for the front, side and top views, drawn behind everything, facing its axis.
   Images load through GDI+ (gdiplus.dll ships with Windows), found at run time. */

typedef struct { char path[MAX_PATH]; GLuint tex; int w, h; float size, ox, oy, opa; } ImgPlane;
static ImgPlane img[3];
static const char *IMG_NAME[3] = { "Front", "Side", "Top" };

typedef struct { UINT32 ver; void *cb; BOOL noThread, noCodecs; } GpStartIn;
typedef struct { UINT w, h; INT stride, fmt; void *scan0; UINT_PTR res; } GpBits;
typedef struct { INT x, y, w, h; } GpRect;

/* RGBA pixels (bottom row first, ready for OpenGL) of an image file, or NULL */
static unsigned char *image_load(const char *path, int *w, int *h)
{
    static HMODULE gp;
    static int (WINAPI *Startup)(ULONG_PTR *, const GpStartIn *, void *);
    static int (WINAPI *FromFile)(const WCHAR *, void **);
    static int (WINAPI *GetW)(void *, UINT *), (WINAPI *GetH)(void *, UINT *);
    static int (WINAPI *Lock)(void *, const GpRect *, UINT, INT, GpBits *), (WINAPI *Unlock)(void *, GpBits *);
    static int (WINAPI *Dispose)(void *);
    if (!gp) {
        gp = LoadLibraryA("gdiplus.dll");
        if (!gp) return NULL;
        Startup = (void *)GetProcAddress(gp, "GdiplusStartup");
        FromFile = (void *)GetProcAddress(gp, "GdipCreateBitmapFromFile");
        GetW = (void *)GetProcAddress(gp, "GdipGetImageWidth");
        GetH = (void *)GetProcAddress(gp, "GdipGetImageHeight");
        Lock = (void *)GetProcAddress(gp, "GdipBitmapLockBits");
        Unlock = (void *)GetProcAddress(gp, "GdipBitmapUnlockBits");
        Dispose = (void *)GetProcAddress(gp, "GdipDisposeImage");
        ULONG_PTR tok;
        GpStartIn in = { 1, NULL, FALSE, FALSE };
        if (!Startup || !FromFile || !GetW || !GetH || !Lock || !Unlock || !Dispose || Startup(&tok, &in, NULL)) { FreeLibrary(gp); gp = NULL; return NULL; }
    }
    WCHAR wp[MAX_PATH];
    if (!MultiByteToWideChar(CP_ACP, 0, path, -1, wp, MAX_PATH)) return NULL;
    void *bm = NULL;
    UINT iw = 0, ih = 0;
    if (FromFile(wp, &bm) || !bm) return NULL;
    GetW(bm, &iw);
    GetH(bm, &ih);
    unsigned char *out = NULL;
    GpRect r = { 0, 0, (INT)iw, (INT)ih };
    GpBits d;
    if (iw && ih && iw < 30000 && ih < 30000 && !Lock(bm, &r, 1 /* read */, 0x26200A /* 32bpp ARGB */, &d)) {
        out = malloc((size_t)iw * ih * 4);
        for (UINT y = 0; y < ih && out; y++) {
            const unsigned char *src = (const unsigned char *)d.scan0 + (size_t)y * d.stride;
            unsigned char *dst = out + (size_t)(ih - 1 - y) * iw * 4;
            for (UINT x = 0; x < iw; x++) {
                dst[4 * x] = src[4 * x + 2];
                dst[4 * x + 1] = src[4 * x + 1];
                dst[4 * x + 2] = src[4 * x];
                dst[4 * x + 3] = src[4 * x + 3];
            }
        }
        Unlock(bm, &d);
    }
    Dispose(bm);
    *w = (int)iw;
    *h = (int)ih;
    return out;
}

/* OpenGL 1.1 textures must be a power of two on each side: resample (bilinear) to fit */
static GLuint image_texture(const unsigned char *px, int w, int h)
{
    GLint maxs = 1024;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxs);
    if (maxs > 2048) maxs = 2048;
    int tw = 1, th_ = 1;
    while (tw < w && tw < maxs) tw *= 2;
    while (th_ < h && th_ < maxs) th_ *= 2;
    unsigned char *t = malloc((size_t)tw * th_ * 4);
    if (!t) return 0;
    for (int y = 0; y < th_; y++) {
        float fy = (y + 0.5f) * h / th_ - 0.5f;
        int y0 = fy < 0 ? 0 : (int)fy, y1 = y0 + 1 < h ? y0 + 1 : h - 1;
        float ay = fy - y0 < 0 ? 0 : fy - y0;
        for (int x = 0; x < tw; x++) {
            float fx = (x + 0.5f) * w / tw - 0.5f;
            int x0 = fx < 0 ? 0 : (int)fx, x1 = x0 + 1 < w ? x0 + 1 : w - 1;
            float ax = fx - x0 < 0 ? 0 : fx - x0;
            for (int c = 0; c < 4; c++) {
                float a = px[((size_t)y0 * w + x0) * 4 + c] * (1 - ax) + px[((size_t)y0 * w + x1) * 4 + c] * ax;
                float b = px[((size_t)y1 * w + x0) * 4 + c] * (1 - ax) + px[((size_t)y1 * w + x1) * 4 + c] * ax;
                t[((size_t)y * tw + x) * 4 + c] = (unsigned char)(a * (1 - ay) + b * ay + 0.5f);
            }
        }
    }
    GLuint id;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, tw, th_, 0, GL_RGBA, GL_UNSIGNED_BYTE, t);
    free(t);
    return id;
}

static void img_clear(int i)
{
    if (img[i].tex) glDeleteTextures(1, &img[i].tex);
    memset(&img[i], 0, sizeof img[i]);
}

/* load image file 'path' into slot i (keeping its size/offset/opacity when given) */
static int img_set(int i, const char *path, float size, float ox, float oy, float opa)
{
    int w, h;
    unsigned char *px = image_load(path, &w, &h);
    if (!px) return 0;
    img_clear(i);
    snprintf(img[i].path, MAX_PATH, "%s", path);
    img[i].tex = image_texture(px, w, h);
    free(px);
    img[i].w = w;
    img[i].h = h;
    img[i].size = size > 0 ? size : 10;
    img[i].ox = ox;
    img[i].oy = oy;
    img[i].opa = opa >= 0 ? opa : 0.6f;
    return img[i].tex != 0;
}

/* corner k (0..3, counter-clockwise from bottom left) of plane i in world space */
static V3 img_corner(int i, int k)
{
    float hw = img[i].size * 0.5f, hh = hw * img[i].h / (img[i].w ? img[i].w : 1);
    float u = img[i].ox + (k == 1 || k == 2 ? hw : -hw), v = img[i].oy + (k >= 2 ? hh : -hh);
    if (i == 0) return v3(u, v, 0);        /* front: seen from +Z, X right, Y up */
    if (i == 1) return v3(0, v, -u);       /* side: seen from +X, -Z right */
    return v3(u, 0, -v);                   /* top: seen from +Y, -Z up */
}

static void draw_images(void)
{
    static const V3 axis[3] = { { 0, 0, 1 }, { 1, 0, 0 }, { 0, 1, 0 } };
    int any = 0;
    for (int i = 0; i < 3; i++) any |= img[i].tex != 0;
    if (!any) return;
    glDepthMask(GL_FALSE);
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_TEXTURE_2D);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    for (int i = 0; i < 3; i++) {
        if (!img[i].tex) continue;
        /* only while looking along its axis; fades out as the view turns away */
        float f = fabsf(v3dot(camF, axis[i])), a = (f - 0.9f) / 0.07f;
        if (a <= 0) continue;
        if (a > 1) a = 1;
        glBindTexture(GL_TEXTURE_2D, img[i].tex);
        glColor4f(1, 1, 1, a * img[i].opa);
        static const float uv[4][2] = { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } };
        glBegin(GL_QUADS);
        for (int k = 0; k < 4; k++) {
            V3 c = img_corner(i, k);
            glTexCoord2fv(uv[k]);
            glVertex3fv(&c.x);
        }
        glEnd();
    }
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

static void cmd_image(int i)
{
    char p[MAX_PATH] = "";
    OPENFILENAMEA of;
    memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = hMain;
    of.lpstrFilter = "Images (*.png, *.jpg, *.bmp, *.gif)\0*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff\0All files\0*.*\0";
    of.lpstrFile = p;
    of.nMaxFile = MAX_PATH;
    of.Flags = OFN_FILEMUSTEXIST;
    if (!GetOpenFileNameA(&of)) return;
    if (!img_set(i, p, img[i].size, img[i].ox, img[i].oy, img[i].tex ? img[i].opa : -1)) {
        MessageBoxA(hMain, "Could not read that image.", APP_NAME, MB_ICONERROR);
        return;
    }
    modified = 1;
    view_axis(i == 0 ? 2 : i == 1 ? 0 : 1, 0);
    char b[96];
    snprintf(b, sizeof b, "%s image plane loaded - size, offset and opacity are in the side panel", IMG_NAME[i]);
    msg(b);
}

/* ================================================================ commands */

enum {
    CM_NEW = 100, CM_IMPORT, CM_EXPORT, CM_EXPORT_AS, CM_EXPORT_SEL, CM_EXIT,
    CM_UNDO, CM_REDO, CM_REPEAT, CM_DELETE, CM_DUP,
    CM_CUBE, CM_CYL, CM_SPHERE, CM_PLANE,
    CM_EXTRUDE, CM_MERGE,
    CM_APPLYCOL, CM_SETCOL,
    CM_WIRE, CM_SHADED, CM_HARD, CM_SMOOTH, CM_AUTOSMOOTH, CM_FRAMESEL, CM_FRAMEALL, CM_PANEL,
    CM_TSEL, CM_TMOVE, CM_TROT, CM_TSCALE, CM_TPAINT, CM_TWELD, CM_TLOOP, CM_TCUT,   /* CM_TSEL + T_* */
    CM_MOBJ, CM_MVERT, CM_MEDGE, CM_MFACE, CM_MMULTI,
    CM_SELALL, CM_SELINV,
    CM_OPEN, CM_GROW, CM_SHRINK, CM_LOOP, CM_TOVERT, CM_TOEDGE, CM_TOFACE,
    CM_HIDE, CM_SHOWALL, CM_FREEZE, CM_CENTERPIV, CM_RESETXF, CM_FILLHOLE,
    CM_VFRONT, CM_VSIDE, CM_VTOP, CM_VHOME, CM_HUD, CM_SNAPGRID, CM_SNAPPOINT, CM_WIRETOG,
    CM_HELP, CM_THEMEPICK, CM_THEMENEXT, CM_THEMEPREV, CM_THEMERAND,
    CM_REVERSE, CM_TRIANG, CM_QUAD, CM_COMBINE, CM_SEPARATE, CM_MERGEDIST, CM_DELEV,
    CM_BEVEL, CM_BRIDGE, CM_HARDEN, CM_SOFTEN,
    CM_ORTHO, CM_IMGFRONT, CM_IMGSIDE, CM_IMGTOP, CM_IMGCLEAR, CM_EXPORT_OBJ,
    CM_MANIPUP, CM_MANIPDOWN, CM_COMPTOG, CM_HOTKEYS,
    CM_RECENT = 300,             /* + recent file index */
    CM_THEME = 200               /* + theme index */
};

/* ---- hotkeys: each command can have two keys. The defaults are Maya's; Help > Hotkey Editor
   changes them, and the menus and toolbar show whatever is set. A key is its virtual key code plus
   KC_CTRL / KC_SHIFT. (Held keys like X, V, J and B while dragging are not commands.) */

#define KC_CTRL 0x10000
#define KC_SHIFT 0x20000

typedef struct { int cmd; const char *group, *name; int def[2], key[2]; } Bind;
static Bind binds[] = {
    { CM_TSEL, "Tools", "Select Tool", { 'Q' }, { 0 } },
    { CM_TMOVE, "Tools", "Move Tool", { 'W' }, { 0 } },
    { CM_TROT, "Tools", "Rotate Tool", { 'E' }, { 0 } },
    { CM_TSCALE, "Tools", "Scale Tool", { 'R' }, { 0 } },
    { CM_TPAINT, "Tools", "Paint Vertex Color Tool", { 0 }, { 0 } },
    { CM_TWELD, "Tools", "Target Weld Tool", { 0 }, { 0 } },
    { CM_TLOOP, "Tools", "Insert Edge Loop Tool", { 0 }, { 0 } },
    { CM_TCUT, "Tools", "Multi-Cut Tool", { 0 }, { 0 } },
    { CM_MANIPUP, "Tools", "Bigger Manipulator", { VK_OEM_PLUS, VK_ADD }, { 0 } },
    { CM_MANIPDOWN, "Tools", "Smaller Manipulator", { VK_OEM_MINUS, VK_SUBTRACT }, { 0 } },
    { CM_COMPTOG, "Selection", "Object / Component Mode", { VK_F8 }, { 0 } },
    { CM_MVERT, "Selection", "Vertex Mode", { VK_F9 }, { 0 } },
    { CM_MEDGE, "Selection", "Edge Mode", { VK_F10 }, { 0 } },
    { CM_MFACE, "Selection", "Face Mode", { VK_F11 }, { 0 } },
    { CM_MMULTI, "Selection", "Multi Mode", { VK_F7 }, { 0 } },
    { CM_TOVERT, "Selection", "Convert to Vertices", { KC_CTRL | VK_F9 }, { 0 } },
    { CM_TOEDGE, "Selection", "Convert to Edges", { KC_CTRL | VK_F10 }, { 0 } },
    { CM_TOFACE, "Selection", "Convert to Faces", { KC_CTRL | VK_F11 }, { 0 } },
    { CM_SELALL, "Selection", "Select All", { KC_CTRL | KC_SHIFT | 'A' }, { 0 } },
    { CM_SELINV, "Selection", "Invert Selection", { KC_CTRL | KC_SHIFT | 'I' }, { 0 } },
    { CM_GROW, "Selection", "Grow Selection", { VK_OEM_PERIOD }, { 0 } },
    { CM_SHRINK, "Selection", "Shrink Selection", { VK_OEM_COMMA }, { 0 } },
    { CM_LOOP, "Selection", "Select Edge Loop", { 0 }, { 0 } },
    { CM_UNDO, "Edit", "Undo", { 'Z', KC_CTRL | 'Z' }, { 0 } },
    { CM_REDO, "Edit", "Redo", { KC_SHIFT | 'Z', KC_CTRL | 'Y' }, { 0 } },
    { CM_REPEAT, "Edit", "Repeat Last", { 'G' }, { 0 } },
    { CM_DELETE, "Edit", "Delete", { VK_DELETE, VK_BACK }, { 0 } },
    { CM_DELEV, "Edit", "Delete Edge/Vertex", { KC_CTRL | VK_DELETE, KC_CTRL | VK_BACK }, { 0 } },
    { CM_DUP, "Edit", "Duplicate", { KC_CTRL | 'D' }, { 0 } },
    { CM_HIDE, "Edit", "Hide Selected", { KC_CTRL | 'H' }, { 0 } },
    { CM_SHOWALL, "Edit", "Show All", { KC_CTRL | KC_SHIFT | 'H' }, { 0 } },
    { CM_EXTRUDE, "Mesh", "Extrude", { KC_CTRL | 'E' }, { 0 } },
    { CM_BEVEL, "Mesh", "Bevel", { KC_CTRL | 'B' }, { 0 } },
    { CM_BRIDGE, "Mesh", "Bridge", { 0 }, { 0 } },
    { CM_MERGE, "Mesh", "Merge to Center", { 0 }, { 0 } },
    { CM_MERGEDIST, "Mesh", "Merge Vertices", { 0 }, { 0 } },
    { CM_FILLHOLE, "Mesh", "Fill Hole", { 0 }, { 0 } },
    { CM_COMBINE, "Mesh", "Combine", { 0 }, { 0 } },
    { CM_SEPARATE, "Mesh", "Separate", { 0 }, { 0 } },
    { CM_TRIANG, "Mesh", "Triangulate", { 0 }, { 0 } },
    { CM_QUAD, "Mesh", "Quadrangulate", { 0 }, { 0 } },
    { CM_REVERSE, "Mesh", "Reverse Normals", { 0 }, { 0 } },
    { CM_FREEZE, "Modify", "Freeze Transformations", { 0 }, { 0 } },
    { CM_CENTERPIV, "Modify", "Center Pivot", { 0 }, { 0 } },
    { CM_RESETXF, "Modify", "Reset Transformations", { 0 }, { 0 } },
    { CM_SNAPGRID, "Modify", "Grid Snap On/Off", { 0 }, { 0 } },
    { CM_SNAPPOINT, "Modify", "Point Snap On/Off", { 0 }, { 0 } },
    { CM_WIRE, "Display", "Wireframe", { '4' }, { 0 } },
    { CM_SHADED, "Display", "Shaded", { '5' }, { 0 } },
    { CM_HARD, "Display", "Hard Shade", { 0 }, { 0 } },
    { CM_SMOOTH, "Display", "Smooth Shade", { 0 }, { 0 } },
    { CM_AUTOSMOOTH, "Display", "Auto Smooth", { 0 }, { 0 } },
    { CM_HARDEN, "Display", "Harden Edge", { 0 }, { 0 } },
    { CM_SOFTEN, "Display", "Soften Edge", { 0 }, { 0 } },
    { CM_FRAMESEL, "Display", "Frame Selected", { 'F' }, { 0 } },
    { CM_FRAMEALL, "Display", "Frame All", { 'A' }, { 0 } },
    { CM_VHOME, "Display", "Home View", { VK_HOME }, { 0 } },
    { CM_VFRONT, "Display", "Front View", { 0 }, { 0 } },
    { CM_VSIDE, "Display", "Side View", { 0 }, { 0 } },
    { CM_VTOP, "Display", "Top View", { 0 }, { 0 } },
    { CM_ORTHO, "Display", "Orthographic On/Off", { 0 }, { 0 } },
    { CM_PANEL, "Display", "Side Panel", { KC_CTRL | 'A' }, { 0 } },
    { CM_HUD, "Display", "Heads-Up Display", { 0 }, { 0 } },
    { CM_THEMEPICK, "Display", "Theme Picker", { KC_CTRL | 'T' }, { 0 } },
    { CM_THEMENEXT, "Display", "Next Theme", { KC_CTRL | KC_SHIFT | 'T' }, { 0 } },
    { CM_NEW, "File", "New Scene", { KC_CTRL | 'N' }, { 0 } },
    { CM_OPEN, "File", "Open", { KC_CTRL | 'O' }, { 0 } },
    { CM_IMPORT, "File", "Import", { KC_CTRL | KC_SHIFT | 'O' }, { 0 } },
    { CM_EXPORT, "File", "Save", { KC_CTRL | 'S' }, { 0 } },
    { CM_EXPORT_AS, "File", "Save As", { KC_CTRL | KC_SHIFT | 'S' }, { 0 } },
    { CM_EXPORT_OBJ, "File", "Export OBJ", { 0 }, { 0 } },
    { CM_HELP, "Help", "Hotkey List", { VK_F1 }, { 0 } },
    { CM_HOTKEYS, "Help", "Hotkey Editor", { 0 }, { 0 } },
};
#define NBIND ((int)(sizeof binds / sizeof *binds))

static void key_text(int code, char *out, int n)
{
    int vk = code & 0xFFFF;
    char name[32] = "";
    if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) snprintf(name, sizeof name, "%c", vk);
    else if (vk >= VK_F1 && vk <= VK_F24) snprintf(name, sizeof name, "F%d", vk - VK_F1 + 1);
    else if (vk >= VK_NUMPAD0 && vk <= VK_NUMPAD9) snprintf(name, sizeof name, "Num %d", vk - VK_NUMPAD0);
    else {
        static const struct { int vk; const char *s; } nm[] = {
            { VK_DELETE, "Del" }, { VK_BACK, "Backspace" }, { VK_HOME, "Home" }, { VK_END, "End" }, { VK_INSERT, "Ins" },
            { VK_PRIOR, "PgUp" }, { VK_NEXT, "PgDn" }, { VK_LEFT, "Left" }, { VK_RIGHT, "Right" }, { VK_UP, "Up" },
            { VK_DOWN, "Down" }, { VK_OEM_PERIOD, "." }, { VK_OEM_COMMA, "," }, { VK_OEM_PLUS, "+" }, { VK_OEM_MINUS, "-" },
            { VK_ADD, "Num +" }, { VK_SUBTRACT, "Num -" }, { VK_MULTIPLY, "Num *" }, { VK_DIVIDE, "Num /" },
            { VK_DECIMAL, "Num ." }, { VK_RETURN, "Enter" }, { VK_SPACE, "Space" }, { VK_TAB, "Tab" },
        };
        for (size_t i = 0; i < sizeof nm / sizeof *nm && !name[0]; i++) if (nm[i].vk == vk) snprintf(name, sizeof name, "%s", nm[i].s);
        if (!name[0] && !GetKeyNameTextA((LONG)MapVirtualKeyA(vk, 0) << 16, name, sizeof name)) snprintf(name, sizeof name, "Key %d", vk);
    }
    snprintf(out, n, "%s%s%s", code & KC_CTRL ? "Ctrl+" : "", code & KC_SHIFT ? "Shift+" : "", name);
}

static Bind *bind_of(int cmd)
{
    for (int i = 0; i < NBIND; i++) if (binds[i].cmd == cmd) return &binds[i];
    return NULL;
}

/* the first key of a command as text ("" if it has none); NULL if it can't have keys */
static const char *cmd_key(int cmd, char *buf, int n)
{
    Bind *b = bind_of(cmd);
    if (!b) return NULL;
    buf[0] = 0;
    if (b->key[0] || b->key[1]) key_text(b->key[0] ? b->key[0] : b->key[1], buf, n);
    return buf;
}

static void binds_save(void)
{
    int v[3 * NBIND];
    for (int i = 0; i < NBIND; i++) { v[3 * i] = binds[i].cmd; v[3 * i + 1] = binds[i].key[0]; v[3 * i + 2] = binds[i].key[1]; }
    reg_set("Hotkeys", REG_BINARY, v, sizeof v);
}

static void binds_load(void)
{
    int v[3 * 200];
    for (int i = 0; i < NBIND; i++) { binds[i].key[0] = binds[i].def[0]; binds[i].key[1] = binds[i].def[1]; }
    DWORD n = sizeof v;
    if (RegGetValueA(HKEY_CURRENT_USER, REG_KEY, "Hotkeys", RRF_RT_REG_BINARY, NULL, v, &n) != ERROR_SUCCESS) return;
    for (DWORD i = 0; i + 3 <= n / sizeof(int); i += 3) {
        Bind *b = bind_of(v[i]);
        if (b) { b->key[0] = v[i + 1]; b->key[1] = v[i + 2]; }
    }
}

/* menu items show the current key after a tab */
static void menu_keys(void)
{
    for (int i = 0; i < NBIND; i++) {
        Bind *b = &binds[i];
        char t[160], k[48];
        if (!b->def[0] && !b->key[0] && !b->key[1]) continue;   /* leave hints like "double-click edge" alone */
        if (!GetMenuStringA(hMenu, b->cmd, t, sizeof t - 50, MF_BYCOMMAND)) continue;
        char *tab = strchr(t, '\t');
        if (tab) *tab = 0;
        cmd_key(b->cmd, k, sizeof k);
        if (k[0]) { strcat(t, "\t"); strcat(t, k); }
        UINT st = GetMenuState(hMenu, b->cmd, MF_BYCOMMAND);
        ModifyMenuA(hMenu, b->cmd, MF_BYCOMMAND | MF_STRING | (st & (MF_CHECKED | MF_GRAYED)), b->cmd, t);
    }
}


static void cmd_create(int type)
{
    static const char *names[] = { "", "pCube", "pCylinder", "pSphere", "pPlane" };
    static const char *what[] = { "", "Create Cube", "Create Cylinder", "Create Sphere", "Create Plane" };
    undo_push(what[type]);
    set_mode(M_OBJ);
    clear_sel();
    Obj *o = obj_new(names[type]);
    o->prim = defPrim[type];
    o->prim.type = type;
    o->m = mesh_prim(&o->prim);
    o->sel = 1;
    o->order = ++stamp;
    char b[64];
    snprintf(b, sizeof b, "Created %s", o->name);
    msg(b);
}

static void cmd_select_all(int invert)
{
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        if (o->hidden) continue;
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
    undo_push("Extrude");
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

/* Mesh > Fill Hole: caps the open borders that the selected vertices, edges or faces touch, or every
   hole of the selected objects in object mode. The new faces end up selected, ready to extrude. */
static void cmd_fill_hole(void)
{
    unsigned char **touch = calloc(nob + 1, sizeof *touch);
    int any = 0, made = 0;
    for (int i = 0; i < nob; i++) {
        Mesh *m = &ob[i].m;
        if (!active(&ob[i])) continue;
        unsigned char *t = mode == M_OBJ ? NULL : malloc(m->nv + 1);
        if (t && !mesh_vmask(m, mode, t)) { free(t); continue; }
        for (int e = 0; e < m->ne && !any; e++)
            any = m->ef[2 * e + 1] < 0 && (!t || t[m->ev[2 * e]] || t[m->ev[2 * e + 1]]);
        touch[i] = t ? t : (unsigned char *)1;   /* 1: whole object */
    }
    if (any) {
        undo_push("Fill Hole");
        for (int i = 0; i < nob; i++) {
            if (!touch[i]) continue;
            int n = mesh_fill_holes(&ob[i].m, touch[i] == (unsigned char *)1 ? NULL : touch[i]);
            if (n) { made += n; ob[i].prim.type = PRIM_NONE; }
        }
    }
    for (int i = 0; i < nob; i++) if (touch[i] != (unsigned char *)1) free(touch[i]);
    free(touch);
    if (!made) {
        msg(mode == M_OBJ ? "Fill Hole: this object has no holes"
                          : "Fill Hole: select a vertex, edge or face on the edge of a hole");
        return;
    }
    if (mode != M_OBJ && mode != M_MULTI) set_mode(M_FACE);
    char b[64];
    snprintf(b, sizeof b, made == 1 ? "Filled 1 hole" : "Filled %d holes", made);
    msg(b);
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
    undo_push("Merge to Center");
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
        undo_push("Delete");
        for (int i = nob - 1; i >= 0; i--)
            if (ob[i].sel) obj_remove(i);
        msg("Deleted");
    } else if (shows(M_FACE)) {
        int any = 0;
        for (int i = 0; i < nob; i++)
            for (int f = 0; ob[i].hil && f < ob[i].m.nf; f++) any |= ob[i].m.fs[f];
        if (!any) { msg("Delete: select objects or faces"); return; }
        undo_push("Delete Faces");
        for (int i = 0; i < nob; i++)
            if (ob[i].hil && mesh_delete_faces(&ob[i].m)) ob[i].prim.type = PRIM_NONE;
        msg("Deleted faces");
    } else if (!delete_ev()) {
        msg("Delete: select objects, faces, edges or vertices");
    }
}

static void cmd_dup(void)
{
    if (mode != M_OBJ || !has_sel()) { msg("Duplicate: select objects"); return; }
    undo_push("Duplicate");
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

/* Mesh > Combine: the selected objects become one object, in world space */
static void cmd_combine(void)
{
    int n = 0;
    for (int i = 0; i < nob; i++) n += ob[i].sel;
    if (mode != M_OBJ || n < 2) { msg("Combine: select two or more objects (object mode)"); return; }
    undo_push("Combine");
    static const int fo0 = 0;
    Mesh acc = mesh_from_arrays(0, NULL, NULL, 0, &fo0, NULL);
    float ang = lead()->nangle;
    for (int i = 0; i < nob; i++) {
        if (!ob[i].sel) continue;
        M4 M = obj_mat(&ob[i]);
        mesh_append(&acc, &ob[i].m, &M);
    }
    for (int i = nob - 1; i >= 0; i--) if (ob[i].sel) obj_remove(i);
    memset(acc.vs, 0, acc.nv);
    memset(acc.es, 0, acc.ne);
    memset(acc.fs, 0, acc.nf);
    Obj *o = obj_new("polySurface");
    o->m = acc;
    o->nangle = ang;
    o->sel = 1;
    o->order = ++stamp;
    char b[64];
    snprintf(b, sizeof b, "Combined %d objects into %s", n, o->name);
    msg(b);
}

/* the selected faces leave their object and become a new object of their own (Maya's Extract) */
static void extract_faces(void)
{
    int made = 0, n0 = nob;
    undo_push("Separate Faces");
    for (int i = 0; i < n0; i++) {
        Mesh *m = &ob[i].m;
        int k = 0;
        if (!ob[i].hil) continue;
        for (int f = 0; f < m->nf; f++) k += m->fs[f] != 0;
        if (!k || k == m->nf) continue;   /* nothing, or the whole object: nothing to split off */
        unsigned char *keep = malloc(m->nf + 1);
        for (int f = 0; f < m->nf; f++) keep[f] = !m->fs[f];
        Mesh part = mesh_subset(m, m->fs), rest = mesh_subset(m, keep);
        free(keep);
        mesh_free(m);
        ob[i].m = rest;
        ob[i].prim.type = PRIM_NONE;
        memset(part.fs, 0, part.nf);
        memset(ob[i].m.fs, 0, ob[i].m.nf);
        Obj src = ob[i];
        Obj *o = obj_new(src.name);   /* may move ob */
        char nm[32];
        memcpy(nm, o->name, 32);
        *o = src;
        memcpy(o->name, nm, 32);
        o->m = part;
        o->hil = 1;   /* becomes the selection when we go back to object mode */
        o->sel = 0;
        o->order = ++stamp;
        made++;
    }
    if (!made) { undo_drop(); msg("Separate: select some (not all) of an object's faces"); return; }
    set_mode(M_OBJ);
    for (int i = 0; i < n0; i++) ob[i].sel = 0;
    char b[96];
    snprintf(b, sizeof b, made == 1 ? "Separated the faces into a new object" : "Separated faces into %d new objects", made);
    msg(b);
}

/* Mesh > Separate: in face mode the selected faces become their own object; in object mode each
   selected object splits into one object per connected piece */
static void cmd_separate(void)
{
    if (mode != M_OBJ && shows(M_FACE)) {
        int any = 0;
        for (int i = 0; i < nob; i++)
            for (int f = 0; ob[i].hil && f < ob[i].m.nf && !any; f++) any = ob[i].m.fs[f];
        if (any) { extract_faces(); return; }
    }
    if (mode != M_OBJ || !has_sel()) { msg("Separate: select objects, or faces to split off (face mode)"); return; }
    int made = 0, *sh = NULL, pushed = 0;
    for (int i = nob - 1; i >= 0; i--) {
        if (!ob[i].sel) continue;
        sh = realloc(sh, (ob[i].m.nf + 1) * sizeof(int));
        int k = mesh_shells(&ob[i].m, sh);
        if (k < 2) continue;
        if (!pushed) { undo_push("Separate"); pushed = 1; }
        Obj src = ob[i];
        unsigned char *fm = malloc(src.m.nf + 1);
        for (int s = 0; s < k; s++) {
            for (int f = 0; f < src.m.nf; f++) fm[f] = sh[f] == s;
            Mesh part = mesh_subset(&src.m, fm);
            Obj *o = obj_new(src.name);
            char nm[32];
            memcpy(nm, o->name, 32);
            *o = src;
            memcpy(o->name, nm, 32);
            o->m = part;
            o->prim.type = PRIM_NONE;
            o->order = ++stamp;
            made++;
        }
        free(fm);
        obj_remove(i);   /* the pieces were added after it, so i still names the original */
    }
    free(sh);
    char b[64];
    snprintf(b, sizeof b, "Separated into %d objects", made);
    msg(made ? b : "Separate: the selection is already in one piece each");
}

/* faces an op works on: all of an object in object mode, else the selected faces, or the faces
   whose vertices are all selected. NULL with *all = 1 means every face; NULL with 0: none. */
static unsigned char *op_faces(Obj *o, int *all)
{
    Mesh *m = &o->m;
    *all = mode == M_OBJ;
    if (*all) return NULL;
    unsigned char *fm = calloc(m->nf + 1, 1), *vm = malloc(m->nv + 1);
    int n = 0;
    if (shows(M_FACE)) for (int f = 0; f < m->nf; f++) n += fm[f] = m->fs[f] != 0;
    if (!n && mesh_vmask(m, mode, vm))
        for (int f = 0; f < m->nf; f++) {
            int in = 1;
            for (int c = m->fo[f]; c < m->fo[f + 1] && in; c++) in = vm[m->fi[c]];
            n += fm[f] = (unsigned char)in;
        }
    free(vm);
    if (!n) { free(fm); return NULL; }
    return fm;
}

static int quad30(Mesh *m, const unsigned char *fm) { return mesh_quadrangulate(m, fm, 30); }

/* Reverse / Triangulate / Quadrangulate on the selected objects or faces */
static void cmd_face_op(int (*fn)(Mesh *, const unsigned char *), const char *what, const char *done)
{
    int n = 0;
    undo_push(what);
    for (int i = 0; i < nob; i++) {
        if (!active(&ob[i])) continue;
        int all;
        unsigned char *fm = op_faces(&ob[i], &all);
        if (!fm && !all) continue;
        int k = fn(&ob[i].m, fm);
        free(fm);
        if (k) { n += k; ob[i].prim.type = PRIM_NONE; }
    }
    char b[96];
    if (!n) {
        undo_drop();
        snprintf(b, sizeof b, "%s: nothing to change (select objects or faces)", what);
    } else {
        snprintf(b, sizeof b, done, n);
    }
    msg(b);
}

/* Mesh > Merge Vertices: everything within Merge Distance (side panel) of each other */
static void cmd_merge_dist(void)
{
    int n = 0;
    undo_push("Merge Vertices");
    for (int i = 0; i < nob; i++) {
        if (!active(&ob[i])) continue;
        unsigned char *vm = NULL;
        if (mode != M_OBJ) {
            vm = malloc(ob[i].m.nv + 1);
            if (!mesh_vmask(&ob[i].m, mode, vm)) { free(vm); continue; }
        }
        int k = mesh_merge_dist(&ob[i].m, vm, mergeDist);
        free(vm);
        if (k) { n += k; ob[i].prim.type = PRIM_NONE; }
    }
    char b[96];
    if (!n) {
        undo_drop();
        snprintf(b, sizeof b, "Merge Vertices: nothing closer than %g (Merge Distance, side panel)", mergeDist);
    } else {
        snprintf(b, sizeof b, "Merged away %d vertices", n);
    }
    msg(b);
}

/* Delete Edge/Vertex: removes the selected edges or vertices and joins the faces around them */
static int delete_ev(void)
{
    int edges = 0, verts = 0;
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        Mesh *m = &ob[i].m;
        for (int e = 0; e < m->ne && shows(M_EDGE); e++) edges |= m->es[e];
        for (int v = 0; v < m->nv && shows(M_VERT); v++) verts |= m->vs[v];
    }
    if (!edges && !verts) return 0;
    undo_push(edges ? "Delete Edges" : "Delete Vertices");
    int n = 0;
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        int k = edges ? mesh_delete_edges(&ob[i].m, ob[i].m.es, 1) : mesh_delete_verts(&ob[i].m, ob[i].m.vs);
        if (k) { n += k; ob[i].prim.type = PRIM_NONE; }
    }
    if (!n) { undo_drop(); msg("Delete: those can't be removed without leaving a hole (try deleting faces)"); return 1; }
    msg(edges ? "Deleted edges" : "Deleted vertices");
    return 1;
}

/* edges an op works on: every edge in object mode, else the selected edges, the edges of the
   selected faces, or edges with both ends selected. NULL: none. */
static unsigned char *op_edges(Obj *o)
{
    Mesh *m = &o->m;
    unsigned char *em = calloc(m->ne + 1, 1);
    int n = 0;
    if (mode == M_OBJ) { memset(em, 1, m->ne); return em; }
    if (shows(M_EDGE)) for (int e = 0; e < m->ne; e++) n += em[e] = m->es[e] != 0;
    if (!n && shows(M_FACE))
        for (int e = 0; e < m->ne; e++) {
            int f0 = m->ef[2 * e], f1 = m->ef[2 * e + 1];
            n += em[e] = (f0 >= 0 && m->fs[f0]) || (f1 >= 0 && m->fs[f1]);
        }
    if (!n && shows(M_VERT))
        for (int e = 0; e < m->ne; e++) n += em[e] = m->vs[m->ev[2 * e]] && m->vs[m->ev[2 * e + 1]];
    if (!n) { free(em); return NULL; }
    return em;
}

/* Edit Mesh > Bevel (Ctrl+B): Bevel Width in the side panel redoes it live until the next step */
static void cmd_bevel(void)
{
    int n = 0;
    undo_push("Bevel");
    for (int i = 0; i < nob; i++) {
        if (!active(&ob[i])) continue;
        unsigned char *em = op_edges(&ob[i]);
        if (!em) continue;
        int k = mesh_bevel(&ob[i].m, em, bevelW);
        free(em);
        if (k) { n += k; ob[i].prim.type = PRIM_NONE; }
    }
    if (!n) { undo_drop(); msg("Bevel: select edges (or faces, or objects) that have faces on both sides"); return; }
    char b[96];
    snprintf(b, sizeof b, "Bevelled %d edge(s) - Bevel Width in the side panel adjusts it", n);
    msg(b);
    liveCmd = CM_BEVEL;
}

/* Edit Mesh > Bridge: two runs of selected border edges on one object */
static void cmd_bridge(void)
{
    int objs = 0, n = 0, err = 0;
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        int any = 0;
        for (int e = 0; e < ob[i].m.ne && shows(M_EDGE); e++) any |= ob[i].m.es[e];
        objs += any;
    }
    if (!objs) { msg("Bridge: select two open borders (edge mode; double-click an edge for its loop)"); return; }
    if (objs > 1) { msg("Bridge: the borders are on different objects - Mesh > Combine them first"); return; }
    undo_push("Bridge");
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        int k = mesh_bridge(&ob[i].m, ob[i].m.es, bridgeDivs);
        if (k > 0) { n += k; ob[i].prim.type = PRIM_NONE; }
        else if (k < 0) err = k;
    }
    if (!n) {
        undo_drop();
        msg(err == -2 ? "Bridge: the two borders need the same number of edges"
                      : "Bridge: select edges on exactly two open borders");
        return;
    }
    char b[96];
    snprintf(b, sizeof b, "Bridged with %d face(s) - Bridge Divisions in the side panel adjusts it", n);
    msg(b);
    liveCmd = CM_BRIDGE;
}

/* Harden / Soften Edge: per edge shading on the selected edges; on whole objects it is
   the same as Hard / Smooth Shade */
static void cmd_shade(float angle, const char *what);

static void cmd_edge_shade(int hard)
{
    if (mode == M_OBJ) { cmd_shade(hard ? 0 : 180, hard ? "Harden Edge" : "Soften Edge"); return; }
    int n = 0;
    undo_push(hard ? "Harden Edge" : "Soften Edge");
    for (int i = 0; i < nob; i++) {
        if (!ob[i].hil) continue;
        unsigned char *em = op_edges(&ob[i]);
        if (!em) continue;
        for (int e = 0; e < ob[i].m.ne; e++)
            if (em[e]) { ob[i].m.eh[e] = hard ? EH_HARD : EH_SOFT; n++; }
        free(em);
    }
    if (!n) { undo_drop(); msg("Harden / Soften Edge: select edges, faces or objects"); return; }
    char b[64];
    snprintf(b, sizeof b, hard ? "Hardened %d edge(s)" : "Softened %d edge(s)", n);
    msg(b);
}

/* a tool setting changed right after its op: undo the op and run it again with the new value */
static void live_redo(int cmd)
{
    if (liveCmd != cmd || !nundo) return;
    snap_restore(&undo_s[--nundo]);
    if (cmd == CM_BEVEL) cmd_bevel();
    else cmd_bridge();
}

/* Hard / smooth / auto smooth shading on the selected (or hilited) objects.
   The choice also becomes the default for new objects. */
static void cmd_shade(float angle, const char *what)
{
    if (angle > 0 && angle < 180) {
        Obj *l = lead();
        float a = l ? l->nangle : defAngle;
        angle = a > 0 && a < 180 ? a : 30;   /* keep a custom angle */
    }
    defAngle = angle;
    int n = 0;
    for (int i = 0; i < nob; i++) {
        if (!active(&ob[i])) continue;
        int over = 0;
        for (int e = 0; e < ob[i].m.ne && !over; e++) over = ob[i].m.eh[e];
        n += ob[i].nangle != angle || over;
    }
    if (n) {
        /* an object-wide choice also drops per edge Harden / Soften overrides */
        undo_push(what);
        for (int i = 0; i < nob; i++)
            if (active(&ob[i])) { ob[i].nangle = angle; memset(ob[i].m.eh, 0, ob[i].m.ne); }
    }
    char b[96];
    snprintf(b, sizeof b, "%s%s", what, lead() ? "" : " (for new objects)");
    msg(b);
}

static void cmd_apply_color(void)
{
    if (!has_sel()) { msg("Apply Color: select objects or components"); return; }
    undo_push("Apply Color");
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

static int has_ext(const char *p, const char *ext)
{
    const char *e = strrchr(p, '.');
    return e && !lstrcmpiA(e, ext);
}

/* save: 1 = scene or OBJ (picked by the type list), 2 = OBJ only */
static int file_dlg(int save, char *path)
{
    OPENFILENAMEA of;
    memset(&of, 0, sizeof of);
    of.lStructSize = sizeof of;
    of.hwndOwner = hMain;
    of.lpstrFilter = save == 2 ? "Wavefront OBJ (*.obj)\0*.obj\0All files\0*.*\0"
                   : save ? "tuff3d scene (*.tuff)\0*.tuff\0Wavefront OBJ (*.obj)\0*.obj\0"
                   : "3D files (*.tuff, *.obj, *.mlt)\0*.tuff;*.obj;*.mlt\0tuff3d scene (*.tuff)\0*.tuff\0"
                     "Wavefront OBJ (*.obj)\0*.obj\0Maya LT scene (*.mlt)\0*.mlt\0All files\0*.*\0";
    of.lpstrFile = path;
    of.nMaxFile = MAX_PATH;
    of.lpstrDefExt = save == 2 ? "obj" : "tuff";
    of.nFilterIndex = save == 1 && has_ext(path, ".obj") ? 2 : 1;
    of.Flags = save ? OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST : OFN_FILEMUSTEXIST;
    if (!(save ? GetSaveFileNameA(&of) : GetOpenFileNameA(&of))) return 0;
    if (save == 1 && !has_ext(path, ".tuff") && !has_ext(path, ".obj")) {
        size_t l = strlen(path);
        if (l + 6 < MAX_PATH) strcat(path, of.nFilterIndex == 2 ? ".obj" : ".tuff");
    }
    return 1;
}

/* ---- tuff3d scene files (.tuff): plain text that keeps everything tuff3d knows about a scene:
   objects with their transforms, shading, hidden state, primitive settings, vertex colors and
   edge overrides, plus the camera and the image planes. Lines:
     tuff3d scene 1
     camera cx cy cz yaw pitch dist ortho
     image slot size ox oy opacity path
     object name / xform t r s / shade angle / hidden 0|1 / prim type and its settings
     v x y z r g b / f i j k ... (0 based) / e a b override / end */

static int tuff_save(const char *path, int selOnly)
{
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    int n = 0;
    fprintf(f, "tuff3d scene 1\n");
    fprintf(f, "camera %.9g %.9g %.9g %.9g %.9g %.9g %d\n", coi.x, coi.y, coi.z, yaw, pitch, dist, ortho);
    for (int i = 0; i < 3; i++)
        if (img[i].tex) fprintf(f, "image %d %.9g %.9g %.9g %.9g %s\n", i, img[i].size, img[i].ox, img[i].oy, img[i].opa, img[i].path);
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        const Prim *q = &o->prim;
        if (selOnly && !active(o)) continue;
        fprintf(f, "object %s\n", o->name);
        fprintf(f, "xform %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g %.9g\n", o->t.x, o->t.y, o->t.z, o->r.x, o->r.y, o->r.z, o->s.x, o->s.y, o->s.z);
        fprintf(f, "shade %.9g\nhidden %d\n", o->nangle, o->hidden);
        fprintf(f, "prim %d %.9g %.9g %.9g %d %d %d %.9g %.9g %d %d %d %d %d %.9g %d %d %.9g %.9g %d %d\n", q->type,
                q->w, q->h, q->d, q->sw, q->sh, q->sd, q->cr, q->ch, q->csides, q->chseg, q->ccap, q->ctop, q->cbot,
                q->sr, q->scols, q->srows, q->pw, q->pd, q->psw, q->psd);
        for (int v = 0; v < m->nv; v++)
            fprintf(f, "v %.9g %.9g %.9g %.6g %.6g %.6g\n", m->p[v].x, m->p[v].y, m->p[v].z, m->c[v].x, m->c[v].y, m->c[v].z);
        for (int fc = 0; fc < m->nf; fc++) {
            fputc('f', f);
            for (int k = m->fo[fc]; k < m->fo[fc + 1]; k++) fprintf(f, " %d", m->fi[k]);
            fputc('\n', f);
        }
        for (int e = 0; e < m->ne; e++)
            if (m->eh[e]) fprintf(f, "e %d %d %d\n", m->ev[2 * e], m->ev[2 * e + 1], m->eh[e]);
        fprintf(f, "end\n");
        n++;
    }
    int ok = !ferror(f);
    if (fclose(f)) ok = 0;
    return ok ? n : -1;
}

typedef struct { Obj o; int nv, nf, ni, ne, cv, cf, ci, ce; V3 *p, *c; int *fo, *fi, *eh; } TObj;

static void tobj_free(TObj *t) { free(t->p); free(t->c); free(t->fo); free(t->fi); free(t->eh); }

/* reads a scene's objects into the scene; returns how many, or -1. camera / images: take those too */
static int tuff_load(const char *path, int camera, int images)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[1 << 16];
    if (!fgets(line, sizeof line, f) || strncmp(line, "tuff3d scene", 12)) { fclose(f); return -1; }
    TObj *t = NULL;
    int nt = 0, bad = 0;
    while (fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        TObj *c = nt ? &t[nt - 1] : NULL;
        if (!strncmp(line, "object ", 7)) {
            t = realloc(t, (nt + 1) * sizeof *t);
            c = &t[nt++];
            memset(c, 0, sizeof *c);
            snprintf(c->o.name, sizeof c->o.name, "%s", line + 7);
            c->o.s = v3(1, 1, 1);
            c->o.nangle = defAngle;
            c->fo = malloc(sizeof(int));
            c->fo[0] = 0;
            c->cf = 1;
        } else if (!strncmp(line, "camera ", 7)) {
            V3 cc;
            float y, pt, d;
            int orth;
            if (camera && sscanf(line + 7, "%f %f %f %f %f %f %d", &cc.x, &cc.y, &cc.z, &y, &pt, &d, &orth) == 7 && d > 0) {
                coi = cc; yaw = y; pitch = pt; dist = d; ortho = orth != 0;
            }
        } else if (!strncmp(line, "image ", 6)) {
            int slot, pos = 0;
            float sz, ox, oy, op;
            if (images && sscanf(line + 6, "%d %f %f %f %f %n", &slot, &sz, &ox, &oy, &op, &pos) == 5 && slot >= 0 && slot < 3 && pos)
                if (!img_set(slot, line + 6 + pos, sz, ox, oy, op)) bad |= 2;
        } else if (!c) {
            continue;
        } else if (!strncmp(line, "xform ", 6)) {
            Obj *o = &c->o;
            sscanf(line + 6, "%f %f %f %f %f %f %f %f %f", &o->t.x, &o->t.y, &o->t.z, &o->r.x, &o->r.y, &o->r.z, &o->s.x, &o->s.y, &o->s.z);
        } else if (!strncmp(line, "shade ", 6)) {
            c->o.nangle = (float)atof(line + 6);
        } else if (!strncmp(line, "hidden ", 7)) {
            c->o.hidden = atoi(line + 7) != 0;
        } else if (!strncmp(line, "prim ", 5)) {
            Prim *q = &c->o.prim;
            int k = sscanf(line + 5, "%d %f %f %f %d %d %d %f %f %d %d %d %d %d %f %d %d %f %f %d %d", &q->type,
                           &q->w, &q->h, &q->d, &q->sw, &q->sh, &q->sd, &q->cr, &q->ch, &q->csides, &q->chseg, &q->ccap,
                           &q->ctop, &q->cbot, &q->sr, &q->scols, &q->srows, &q->pw, &q->pd, &q->psw, &q->psd);
            if (k != 21 || q->type < 0 || q->type >= PRIM_COUNT) q->type = PRIM_NONE;
            else prim_clamp(q);
        } else if (line[0] == 'v' && line[1] == ' ') {
            if (c->nv == c->cv) { c->cv = c->cv * 2 + 64; c->p = realloc(c->p, c->cv * sizeof(V3)); c->c = realloc(c->c, c->cv * sizeof(V3)); }
            V3 *p = &c->p[c->nv], *cl = &c->c[c->nv];
            *cl = v3(1, 1, 1);
            if (sscanf(line + 2, "%f %f %f %f %f %f", &p->x, &p->y, &p->z, &cl->x, &cl->y, &cl->z) >= 3) c->nv++;
        } else if (line[0] == 'f' && line[1] == ' ') {
            char *q = line + 2, *end;
            int k = 0;
            for (;;) {
                long v = strtol(q, &end, 10);
                if (end == q) break;
                q = end;
                if (v < 0 || v >= c->nv) { bad |= 1; k = -1; break; }
                if (c->ni + k + 1 > c->ci) { c->ci = c->ci * 2 + 256; c->fi = realloc(c->fi, c->ci * sizeof(int)); }
                c->fi[c->ni + k++] = (int)v;
            }
            if (k >= 3) {
                if (c->nf + 2 > c->cf) { c->cf = c->cf * 2 + 64; c->fo = realloc(c->fo, c->cf * sizeof(int)); }
                c->ni += k;
                c->fo[++c->nf] = c->ni;
            }
        } else if (line[0] == 'e' && line[1] == ' ') {
            if (c->ne == c->ce) { c->ce = c->ce * 2 + 32; c->eh = realloc(c->eh, c->ce * 3 * sizeof(int)); }
            int *e = c->eh + 3 * c->ne;
            if (sscanf(line + 2, "%d %d %d", &e[0], &e[1], &e[2]) == 3 && e[2] >= EH_AUTO && e[2] <= EH_SOFT) c->ne++;
        }
    }
    fclose(f);
    int n = 0;
    for (int i = 0; i < nt; i++) {
        TObj *c = &t[i];
        if (!c->nf) { tobj_free(c); continue; }
        int taken = name_used(c->o.name);
        Obj *o = obj_new(c->o.name);
        char nm[32];
        memcpy(nm, o->name, 32);
        *o = c->o;
        memcpy(o->name, taken ? nm : c->o.name, 32);   /* keep the saved name unless it is in use */
        o->m = mesh_from_arrays(c->nv, c->p, c->c, c->nf, c->fo, c->fi);
        if (c->ne) mesh_set_eh(&o->m, c->eh, c->ne);
        o->sel = o->hil = 0;
        o->order = ++stamp;
        tobj_free(c);
        n++;
    }
    free(t);
    if (bad & 2) msg("Some image plane pictures could not be found");
    return n;
}


static void recent_add(const char *path);

/* writes the scene (or the selected objects): a .tuff scene, or OBJ in world space;
   returns the object count or -1 */
static int save_scene(const char *path, int selOnly)
{
    if (has_ext(path, ".tuff")) return tuff_save(path, selOnly);
    const Mesh **ms = malloc((nob + 1) * sizeof *ms);
    M4 *mats = malloc((nob + 1) * sizeof *mats);
    const char **names = malloc((nob + 1) * sizeof *names);
    float *angles = malloc((nob + 1) * sizeof *angles);
    int n = 0;
    for (int i = 0; i < nob; i++) {
        if (selOnly && !active(&ob[i])) continue;
        ms[n] = &ob[i].m;
        mats[n] = obj_mat(&ob[i]);
        names[n] = ob[i].name;
        angles[n] = ob[i].nangle;
        n++;
    }
    int ok = obj_save(path, n, ms, mats, names, angles);
    free(ms); free(mats); free(names); free(angles);
    return ok ? n : -1;
}

static char g_suggest[MAX_PATH];   /* default export name for a scene that has no OBJ path yet */

/* Save (Ctrl+S) writes the current file in its own format; Save As (ask 1) asks for a scene or OBJ.
   Export OBJ (ask 2) / Export Selection write an OBJ and leave the current file alone. */
static void cmd_export(int ask, int selOnly)
{
    char path[MAX_PATH], b[MAX_PATH + 64];
    int objOnly = selOnly || ask == 2;
    strcpy(path, g_path[0] && !objOnly ? g_path : g_suggest[0] ? g_suggest : g_path);
    if (objOnly && has_ext(path, ".tuff")) strcpy(path + strlen(path) - 5, ".obj");
    if (!objOnly && !g_path[0] && has_ext(path, ".obj")) strcpy(path + strlen(path) - 4, ".tuff");
    if ((ask || !g_path[0] || objOnly) && !file_dlg(objOnly ? 2 : 1, path)) return;
    int n = save_scene(path, selOnly);
    if (n < 0) { MessageBoxA(hMain, "Could not write the file.", APP_NAME, MB_ICONERROR); return; }
    if (!objOnly || !lstrcmpiA(path, g_path)) { strcpy(g_path, path); modified = 0; recent_add(path); }
    snprintf(b, sizeof b, "%s %d object(s) to %s", has_ext(path, ".tuff") ? "Saved" : "Exported", n, path);
    msg(b);
}

/* ask before throwing away unexported work; 0 = cancel */
static int confirm_discard(void)
{
    if (!modified || !nob) return 1;
    char q[MAX_PATH + 64];
    snprintf(q, sizeof q, "Save changes to %s?", g_path[0] ? g_path : "a scene file");
    int r = MessageBoxA(hMain, q, APP_NAME, MB_YESNOCANCEL | MB_ICONQUESTION);
    if (r == IDCANCEL) return 0;
    if (r == IDYES) { cmd_export(0, 0); return !modified; }
    return 1;
}

/* Open replaces the scene, Import adds to it. path NULL asks with a file dialog. */
static void load_file(const char *path, int replace)
{
    char p[MAX_PATH] = "", b[MAX_PATH + 64];
    if (replace && !confirm_discard()) return;
    if (path) snprintf(p, sizeof p, "%s", path);
    else if (!file_dlg(0, p)) return;
    const char *ext = strrchr(p, '.');
    if (ext && (!lstrcmpiA(ext, ".mb") || !lstrcmpiA(ext, ".ma"))) {
        MessageBoxA(hMain, "tuff3d reads Maya LT scenes (.mlt) and OBJ files, but not full Maya scenes (.mb, .ma) yet.",
                    APP_NAME, MB_ICONINFORMATION);
        return;
    }
    if (ext && !lstrcmpiA(ext, ".tuff")) {
        char head[16] = "";
        FILE *tf = fopen(p, "r");
        if (tf) { if (!fgets(head, sizeof head, tf)) head[0] = 0; fclose(tf); }
        if (strncmp(head, "tuff3d scene", 12)) {
            snprintf(b, sizeof b, "Could not read %s (not a tuff3d scene?)", p);
            MessageBoxA(hMain, b, APP_NAME, MB_ICONERROR);
            return;
        }
        undo_push(replace ? "Open" : "Import");
        if (replace) {
            for (int i = 0; i < nob; i++) mesh_free(&ob[i].m);
            nob = 0;
            for (int i = 0; i < 3; i++) img_clear(i);
        }
        set_mode(M_OBJ);
        clear_sel();
        int n0 = nob, n = tuff_load(p, replace, replace);
        if (n < 0) {
            snap_restore(&undo_s[--nundo]);   /* put the scene back */
            snprintf(b, sizeof b, "Could not read %s", p);
            MessageBoxA(hMain, b, APP_NAME, MB_ICONERROR);
            return;
        }
        for (int i = n0; i < nob; i++) ob[i].sel = !replace;
        if (replace) {
            snprintf(g_path, sizeof g_path, "%s", p);
            snprintf(g_suggest, sizeof g_suggest, "%s", p);
            modified = 0;
            recent_add(p);
        }
        geo_changed();
        const char *fn = strrchr(p, '\\') ? strrchr(p, '\\') + 1 : p;
        snprintf(b, sizeof b, "%s %d object(s) from %s", replace ? "Opened" : "Imported", n, fn);
        msg(b);
        return;
    }
    /* Maya LT scenes are rebuilt from their construction history */
    int mlt = ext && !lstrcmpiA(ext, ".mlt");
    char report[600] = "";
    Mesh *ms;
    char (*names)[32];
    float *angles = NULL;
    int n = mlt ? mlt_load(p, &ms, &names, report, sizeof report) : obj_load(p, &ms, &names, &angles);
    if (n <= 0) {
        if (n == 0) free(ms), free(names), free(angles);
        snprintf(b, sizeof b, n < 0 ? "Could not read %s" : "Nothing in %s could be rebuilt yet", p);
        if (mlt && report[0]) {
            size_t l = strlen(b);
            snprintf(b + l, sizeof b - l, "\n\n(%s)\n\nIf you send this file to the tuff3d developer, support for it can be added.", report);
        }
        MessageBoxA(hMain, b, APP_NAME, MB_ICONERROR);
        return;
    }
    undo_push(replace ? "Open" : "Import");
    if (replace) {
        for (int i = 0; i < nob; i++) mesh_free(&ob[i].m);
        nob = 0;
    }
    set_mode(M_OBJ);
    clear_sel();
    for (int i = 0; i < n; i++) {
        Obj *o = obj_new(names[i]);
        o->m = ms[i];
        if (angles && angles[i] >= 0) o->nangle = angles[i];
        o->sel = !replace;
        o->order = ++stamp;
    }
    free(ms);
    free(names);
    free(angles);
    if (replace) {
        /* an .mlt opens as an unsaved OBJ scene: Export must never write over the .mlt itself */
        snprintf(g_path, sizeof g_path, "%s", mlt ? "" : p);
        snprintf(g_suggest, sizeof g_suggest, "%.*s.obj", ext ? (int)(ext - p) : (int)strlen(p), p);
        modified = mlt;
        recent_add(p);
        geo_changed();
        frame(1);
    }
    const char *fn = strrchr(p, '\\') ? strrchr(p, '\\') + 1 : p;
    snprintf(b, sizeof b, "%s %d object(s) from %s", replace ? "Opened" : "Imported", n, fn);
    msg(b);
    if (mlt && strstr(report, "not supported")) {
        snprintf(b, sizeof b, "Rebuilt %s from its Maya LT history.\n\n%s\n\nThose steps were skipped, so parts of the model "
                 "may differ from Maya. If you send this file to the tuff3d developer, support can be added.", fn, report);
        MessageBoxA(hMain, b, APP_NAME, MB_ICONWARNING);
    }
}

/* ---- recent files (File > Open Recent), kept in the registry as Recent0..4 */

#define NRECENT 5
static char recent[NRECENT][MAX_PATH];
static HMENU hRecentMenu;

static void recent_menu(void)
{
    if (!hRecentMenu) return;
    while (GetMenuItemCount(hRecentMenu) > 0) DeleteMenu(hRecentMenu, 0, MF_BYPOSITION);
    for (int i = 0; i < NRECENT; i++) {
        if (!recent[i][0]) continue;
        char b[MAX_PATH + 8];
        snprintf(b, sizeof b, "&%d  %s", i + 1, recent[i]);
        AppendMenuA(hRecentMenu, MF_STRING, CM_RECENT + i, b);
    }
    if (!recent[0][0]) AppendMenuA(hRecentMenu, MF_STRING | MF_GRAYED, 0, "(none yet)");
}

static void recent_load(void)
{
    for (int i = 0; i < NRECENT; i++) {
        char k[16];
        sprintf(k, "Recent%d", i);
        if (!reg_get(k, REG_SZ, recent[i], MAX_PATH)) recent[i][0] = 0;
    }
}

static void recent_add(const char *path)
{
    int j = NRECENT - 1;
    for (int i = 0; i < NRECENT; i++)
        if (!lstrcmpiA(recent[i], path)) j = i;
    memmove(recent[1], recent[0], j * MAX_PATH);
    snprintf(recent[0], MAX_PATH, "%s", path);
    for (int i = 0; i < NRECENT; i++) {
        char k[16];
        sprintf(k, "Recent%d", i);
        reg_set(k, REG_SZ, recent[i], (DWORD)strlen(recent[i]) + 1);
    }
    recent_menu();
}

/* ---- autosave: every minute while there are unexported changes, to
   %LOCALAPPDATA%\tuff3d\autosave-<process id>-<start time>.tuff, deleted on a normal exit. A file
   left behind by a process that is no longer running means a crash, and is offered back on start.
   (The start time keeps a new process that reuses an old process id from clobbering its file.) */

static unsigned autosaveStamp;

static int autosave_dir(char *out)
{
    char base[MAX_PATH];
    DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", base, sizeof base);
    if (!n || n >= sizeof base) return 0;
    snprintf(out, MAX_PATH, "%s\\" APP_NAME, base);
    CreateDirectoryA(out, NULL);
    return 1;
}

static void autosave_path(char *out)
{
    char d[MAX_PATH];
    out[0] = 0;
    static DWORD started;
    if (!started) started = GetTickCount() | 1;
    if (autosave_dir(d)) snprintf(out, MAX_PATH, "%s\\autosave-%lu-%lu.tuff", d, GetCurrentProcessId(), started);
}

static void autosave_tick(void)
{
    char p[MAX_PATH];
    if (!modified || editStamp == autosaveStamp || dk != D_NONE) return;
    autosave_path(p);
    if (p[0] && save_scene(p, 0) >= 0) autosaveStamp = editStamp;
}

static void autosave_remove(void)
{
    char p[MAX_PATH];
    autosave_path(p);
    if (p[0]) DeleteFileA(p);
}

static void autosave_recover(void)
{
    char d[MAX_PATH], pat[MAX_PATH], best[MAX_PATH] = "";
    FILETIME bt = { 0, 0 };
    WIN32_FIND_DATAA fd;
    if (!autosave_dir(d)) return;
    snprintf(pat, sizeof pat, "%s\\autosave-*.*", d);   /* .tuff now; .obj from older versions */
    HANDLE f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return;
    do {
        DWORD pid = strtoul(fd.cFileName + 9, NULL, 10);
        HANDLE pr = OpenProcess(SYNCHRONIZE, FALSE, pid);
        int alive = pr && WaitForSingleObject(pr, 0) == WAIT_TIMEOUT;
        if (pr) CloseHandle(pr);
        if (alive && pid != GetCurrentProcessId()) continue;   /* another running tuff3d owns it */
        char p[MAX_PATH];
        snprintf(p, sizeof p, "%s\\%s", d, fd.cFileName);
        if (CompareFileTime(&fd.ftLastWriteTime, &bt) > 0) {
            if (best[0]) DeleteFileA(best);
            bt = fd.ftLastWriteTime;
            strcpy(best, p);
        } else {
            DeleteFileA(p);
        }
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    if (!best[0]) return;
    if (MessageBoxA(hMain, "tuff3d did not close normally last time.\n\nRecover the unsaved work it autosaved?",
                    APP_NAME, MB_YESNO | MB_ICONQUESTION) == IDYES) {
        load_file(best, 1);
        g_path[0] = 0;          /* recovered work is untitled until exported */
        modified = 1;
        autosave_tick();        /* safe in our own autosave before the old one goes */
        msg("Recovered autosaved work - save it to keep it");
    }
    DeleteFileA(best);
}

/* ---- selection tools */

static void cmd_grow(int shrink)
{
    if (mode == M_OBJ) { msg(shrink ? "Shrink: switch to a component mode" : "Grow: switch to a component mode"); return; }
    for (int i = 0; i < nob; i++)
        for (int k = M_VERT; k <= M_FACE && ob[i].hil; k++)
            if (shows(k)) mesh_grow(&ob[i].m, k, shrink);
    msg(shrink ? "Shrank selection" : "Grew selection");
}

static void cmd_loop(void)
{
    int n = 0;
    for (int i = 0; i < nob; i++) {
        Mesh *m = &ob[i].m;
        if (!ob[i].hil) continue;
        unsigned char *was = malloc(m->ne + 1);
        memcpy(was, m->es, m->ne);
        for (int e = 0; e < m->ne; e++) if (was[e]) { mesh_edge_loop(m, e); n++; }
        free(was);
    }
    if (n) set_mode(mode == M_MULTI ? M_MULTI : M_EDGE);
    msg(n ? "Selected edge loops (tip: double-click an edge)" : "Edge Loop: select an edge first, or double-click one");
}

/* Ctrl+F9/F10/F11: convert the selection to vertices/edges/faces and switch to that mode */
static void cmd_convert(int to)
{
    static const char *nm[] = { "", "vertices", "edges", "faces" };
    char b[64];
    if (mode == to) return;
    if (mode == M_OBJ) {
        /* objects: their components become the selection */
        int any = 0;
        for (int i = 0; i < nob; i++) any |= ob[i].sel;
        if (!any) { msg("Convert: select something first"); return; }
        set_mode(to);
        for (int i = 0; i < nob; i++)
            if (ob[i].hil) {
                int n;
                unsigned char *f = comp_flags(&ob[i], to, &n);
                memset(f, 1, n);
            }
    } else {
        for (int i = 0; i < nob; i++) {
            Obj *o = &ob[i];
            int n;
            if (!o->hil) continue;
            unsigned char *dst = comp_flags(o, to, &n), *keep = malloc(n + 1);
            memcpy(keep, dst, n);
            if (mode != M_MULTI) memset(keep, 0, n);
            for (int k = M_VERT; k <= M_FACE; k++) {
                if (k == to || !shows(k)) continue;
                mesh_convert(&o->m, k, to);
                for (int j = 0; j < n; j++) keep[j] |= dst[j];
            }
            memcpy(dst, keep, n);
            free(keep);
        }
        set_mode(to);
    }
    snprintf(b, sizeof b, "Converted selection to %s", nm[to]);
    msg(b);
}

static void cmd_hide(void)
{
    int n = 0;
    for (int i = 0; i < nob; i++)
        if (active(&ob[i])) { ob[i].hidden = 1; ob[i].sel = ob[i].hil = 0; n++; }
    char b[64];
    if (n) snprintf(b, sizeof b, "Hid %d object(s) - Ctrl+Shift+H shows them again", n);
    msg(n ? b : "Hide: select objects first");
}

static void cmd_show_all(void)
{
    int n = 0;
    for (int i = 0; i < nob; i++) if (ob[i].hidden) { ob[i].hidden = 0; n++; }
    char b[64];
    snprintf(b, sizeof b, "Showing %d hidden object(s)", n);
    msg(n ? b : "Nothing is hidden");
}

/* ---- Modify menu: bake or reset object transforms */

enum { XF_FREEZE, XF_CENTER, XF_RESET };

static void cmd_transform(int what)
{
    static const char *names[] = { "Freeze Transformations", "Center Pivot", "Reset Transformations" };
    int any = 0;
    for (int i = 0; i < nob; i++) any |= active(&ob[i]);
    if (!any) { msg("Select objects first"); return; }
    undo_push(names[what]);
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        if (!active(o)) continue;
        M4 M = obj_mat(o);
        if (what == XF_FREEZE) {
            /* bake into the vertices; a mirrored scale also flips the winding */
            for (int v = 0; v < m->nv; v++) m->p[v] = m4_pt(M, m->p[v]);
            if (o->s.x * o->s.y * o->s.z < 0) mesh_flip(m);
            o->t = o->r = v3(0, 0, 0);
            o->s = v3(1, 1, 1);
            o->prim.type = PRIM_NONE;
        } else if (what == XF_CENTER) {
            V3 lo = v3(1e30f, 1e30f, 1e30f), hi = v3(-1e30f, -1e30f, -1e30f);
            for (int v = 0; v < m->nv; v++) {
                lo = v3(fminf(lo.x, m->p[v].x), fminf(lo.y, m->p[v].y), fminf(lo.z, m->p[v].z));
                hi = v3(fmaxf(hi.x, m->p[v].x), fmaxf(hi.y, m->p[v].y), fmaxf(hi.z, m->p[v].z));
            }
            V3 c = v3mul(v3add(lo, hi), 0.5f);
            if (v3len(c) < 1e-6f) continue;
            for (int v = 0; v < m->nv; v++) m->p[v] = v3sub(m->p[v], c);
            o->t = m4_pt(M, c);
            o->prim.type = PRIM_NONE;
        } else {
            o->t = o->r = v3(0, 0, 0);
            o->s = v3(1, 1, 1);
        }
    }
    msg(names[what]);
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

/* ================================================================ theme picker */

/* switch the whole window to theme i (save: remember it for next time) */
static void theme_apply(int i, int save)
{
    if (i == themeIdx && !save) return;
    theme_load(i);
    if (save) reg_set_int("Theme", themeIdx);
    theme_window(hMain);
    DrawMenuBar(hMain);
    RedrawWindow(hMain, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN);
}

/* A window of theme tiles, each a little picture of tuff3d in that theme. Hovering a tile
   previews it on the main window, clicking keeps it; leaving the tiles goes back to the kept one. */
static HWND hPick;
static int pkKept, pkHover = -1, pkScroll, pkCols, pkTileW, pkTileH, pkGap, pkTop, pkContent;
static int pkTileX[64], pkTileY[64], pkHeadY[8];
static const char *pkGroups[] = { "System", "Dark", "Light" };

static void pk_layout(int w)
{
    pkGap = (int)(10 * ds);
    pkTileW = (int)(150 * ds);
    pkTileH = (int)(118 * ds);
    pkCols = (w - pkGap) / (pkTileW + pkGap);
    if (pkCols < 1) pkCols = 1;
    int y = pkTop = (int)(46 * ds);
    for (int g = 0; g < 3; g++) {
        pkHeadY[g] = y;
        y += (int)(22 * ds);
        int col = 0;
        for (int i = 0; i < NTHEME; i++) {
            if (strcmp(themes[i].group, pkGroups[g])) continue;
            if (col == pkCols) { col = 0; y += pkTileH + pkGap; }
            pkTileX[i] = pkGap + col * (pkTileW + pkGap);
            pkTileY[i] = y;
            col++;
        }
        y += pkTileH + pkGap + (int)(6 * ds);
    }
    pkContent = y;
}

static int pk_hit(int x, int y)
{
    y += pkScroll;
    for (int i = 0; i < NTHEME; i++)
        if (x >= pkTileX[i] && x < pkTileX[i] + pkTileW && y >= pkTileY[i] && y < pkTileY[i] + pkTileH) return i;
    return -1;
}

static void fill(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

static COLORREF f2c(const float *c) { return RGB((int)(c[0] * 255 + .5f), (int)(c[1] * 255 + .5f), (int)(c[2] * 255 + .5f)); }

static COLORREF lerpc(COLORREF a, COLORREF b, float t)
{
    return RGB((int)(GetRValue(a) + (GetRValue(b) - GetRValue(a)) * t), (int)(GetGValue(a) + (GetGValue(b) - GetGValue(a)) * t),
               (int)(GetBValue(a) + (GetBValue(b) - GetBValue(a)) * t));
}

static void line(HDC dc, int x0, int y0, int x1, int y1, COLORREF c, int w)
{
    HPEN p = CreatePen(PS_SOLID, w, c), old = SelectObject(dc, p);
    MoveToEx(dc, x0, y0, NULL);
    LineTo(dc, x1, y1);
    SelectObject(dc, old);
    DeleteObject(p);
}

/* tuff3d in miniature: menu bar, toolbar, viewport with grid and a cube, side panel, status bar */
static void pk_tile(HDC dc, int i, int x, int y, int w, int h, int hot, int kept)
{
    Theme t = theme_get(i);
    int u = (int)ds > 0 ? (int)ds : 1, nameH = (int)(20 * ds), mh = h - nameH;
    fill(dc, x, y, w, h, t.ui);
    /* menu bar text and toolbar buttons */
    for (int k = 0; k < 4; k++) fill(dc, x + 5 * u + k * 17 * u, y + 4 * u, 12 * u, 3 * u, lerpc(t.text, t.ui, .35f));
    for (int k = 0; k < 7; k++) fill(dc, x + 5 * u + k * 12 * u, y + 11 * u, 9 * u, 9 * u, k == 1 ? t.hot : t.line);
    fill(dc, x + 5 * u + 12 * u + 2 * u, y + 13 * u, 5 * u, 5 * u, t.head);
    int vx = x + 4 * u, vy = y + 24 * u, pw = 34 * u, vw2 = w - 8 * u - pw - 3 * u, vh2 = mh - 24 * u - 9 * u;
    /* viewport: gradient, grid, axis lines and a wireframe cube with one selected edge */
    COLORREF top = f2c(t.bgTop), bot = f2c(t.bgBot);
    for (int r = 0; r < vh2; r++) fill(dc, vx, vy + r, vw2, 1, lerpc(top, bot, vh2 > 1 ? (float)r / (vh2 - 1) : 0));
    int hy = vy + vh2 * 55 / 100, cx = vx + vw2 / 2;
    HRGN clip = CreateRectRgn(vx, vy, vx + vw2, vy + vh2);
    SelectClipRgn(dc, clip);
    for (int k = -6; k <= 6; k++) {
        COLORREF gc = k == 0 ? f2c(t.gridAxis) : k % 3 == 0 ? f2c(t.gridMajor) : f2c(t.grid);
        line(dc, cx + k * 6 * u, hy, cx + k * 14 * u, vy + vh2, gc, 1);
    }
    for (int k = 0; k < 5; k++) {
        int yy = hy + (vy + vh2 - hy) * k * k / 16;
        line(dc, vx, yy, vx + vw2, yy, k == 2 ? f2c(t.gridAxis) : f2c(t.grid), 1);
    }
    int s = vh2 / 4, ox = cx - s / 2, oy = hy - s / 2, dx = s / 2, dy = s / 3;
    POINT f[4] = { { ox, oy }, { ox + s, oy }, { ox + s, oy + s }, { ox, oy + s } };
    POINT b[4] = { { ox + dx, oy - dy }, { ox + s + dx, oy - dy }, { ox + s + dx, oy + s - dy }, { ox + dx, oy + s - dy } };
    COLORREF wc = f2c(t.wire);
    for (int k = 0; k < 4; k++) {
        line(dc, f[k].x, f[k].y, f[(k + 1) % 4].x, f[(k + 1) % 4].y, wc, u);
        line(dc, b[k].x, b[k].y, b[(k + 1) % 4].x, b[(k + 1) % 4].y, wc, u);
        line(dc, f[k].x, f[k].y, b[k].x, b[k].y, wc, u);
    }
    line(dc, f[0].x, f[0].y, f[1].x, f[1].y, RGB(255, 140, 0), 2 * u);   /* a selected edge */
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
    /* side panel: heading, two fields, a highlighted row */
    int px = vx + vw2 + 3 * u;
    fill(dc, px, vy, pw, vh2, t.ui);
    fill(dc, px + 3 * u, vy + 3 * u, pw - 10 * u, 3 * u, t.head);
    for (int k = 0; k < 4; k++) {
        fill(dc, px + 3 * u, vy + 10 * u + k * 9 * u, 10 * u, 3 * u, lerpc(t.text, t.ui, .4f));
        fill(dc, px + 15 * u, vy + 9 * u + k * 9 * u, pw - 18 * u, 6 * u, k == 1 ? t.hot : t.field);
    }
    fill(dc, x + 4 * u, y + mh - 7 * u, w / 3, 3 * u, lerpc(t.text, t.ui, .4f));   /* status bar */
    /* name strip */
    fill(dc, x, y + mh, w, nameH, kept ? t.head : t.ui);
    RECT nr = { x + 6 * u, y + mh, x + w - 4 * u, y + h };
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, kept ? t.ui : t.text);
    SelectObject(dc, kept ? hBold : hFont);
    DrawTextA(dc, t.name, -1, &nr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);
    /* frame: the kept theme thick in its accent, the hovered one in ours */
    HBRUSH fb = CreateSolidBrush(kept ? t.head : hot ? th.head : t.line);
    int fw = kept || hot ? 3 * u : u;
    for (int k = 0; k < fw; k++) {
        RECT fr = { x - k, y - k, x + w + k, y + h + k };
        FrameRect(dc, &fr, fb);
    }
    DeleteObject(fb);
}

static void pk_paint(HWND h)
{
    PAINTSTRUCT ps;
    HDC wdc = BeginPaint(h, &ps);
    RECT rc;
    GetClientRect(h, &rc);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, rc.right, rc.bottom), oldbm = SelectObject(dc, bm);
    FillRect(dc, &rc, brUi);
    SetBkMode(dc, TRANSPARENT);
    for (int g = 0; g < 3; g++) {
        RECT hr = { pkGap, pkHeadY[g] - pkScroll, rc.right, pkHeadY[g] - pkScroll + (int)(20 * ds) };
        SelectObject(dc, hBold);
        SetTextColor(dc, th.head);
        char buf[64];
        int n = 0;
        for (int i = 0; i < NTHEME; i++) n += !strcmp(themes[i].group, pkGroups[g]);
        snprintf(buf, sizeof buf, g ? "%s (%d)" : "%s", pkGroups[g], n);
        DrawTextA(dc, buf, -1, &hr, DT_SINGLELINE | DT_BOTTOM | DT_LEFT);
    }
    for (int i = 0; i < NTHEME; i++) {
        int y = pkTileY[i] - pkScroll;
        if (y + pkTileH < 0 || y > rc.bottom) continue;
        pk_tile(dc, i, pkTileX[i], y, pkTileW, pkTileH, i == pkHover, i == pkKept);
    }
    /* header over the tiles: what to do, and the theme shown right now */
    RECT top = { 0, 0, rc.right, pkTop - (int)(6 * ds) };
    FillRect(dc, &top, brUi);
    RECT tr = { pkGap, 0, rc.right - pkGap, top.bottom };
    char hint[160];
    snprintf(hint, sizeof hint, "%s%s  -  hover to preview, click to keep  (arrows move, R random, Enter/Esc close)",
             themes[pkHover >= 0 ? pkHover : pkKept].name, pkHover >= 0 && pkHover != pkKept ? " (preview)" : "");
    SelectObject(dc, hFont);
    SetTextColor(dc, th.text);
    DrawTextA(dc, hint, -1, &tr, DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    BitBlt(wdc, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, oldbm);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps);
}

static void pk_scroll_to(HWND h, int i)
{
    RECT rc;
    GetClientRect(h, &rc);
    if (pkTileY[i] - pkScroll < pkTop) pkScroll = pkTileY[i] - pkTop - (int)(24 * ds);
    if (pkTileY[i] + pkTileH - pkScroll > rc.bottom) pkScroll = pkTileY[i] + pkTileH + pkGap - rc.bottom;
    int maxs = pkContent - rc.bottom;
    if (pkScroll > maxs) pkScroll = maxs;
    if (pkScroll < 0) pkScroll = 0;
}

static void pk_keep(HWND h, int i)
{
    pkKept = i;
    theme_apply(i, 1);
    pk_scroll_to(h, i);
    InvalidateRect(h, NULL, FALSE);
}

static LRESULT CALLBACK PickProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_SIZE:
        pk_layout(LOWORD(lp));
        pk_scroll_to(h, pkKept);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        pk_paint(h);
        return 0;
    case WM_MOUSEMOVE: {
        int i = pk_hit((short)LOWORD(lp), (short)HIWORD(lp));
        if ((short)HIWORD(lp) < pkTop) i = -1;
        if (i != pkHover) {
            pkHover = i;
            theme_apply(i >= 0 ? i : pkKept, 0);
            InvalidateRect(h, NULL, FALSE);
        }
        TRACKMOUSEEVENT te = { sizeof te, TME_LEAVE, h, 0 };
        TrackMouseEvent(&te);
        return 0;
    }
    case WM_MOUSELEAVE:
        pkHover = -1;
        theme_apply(pkKept, 0);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_LBUTTONDOWN: {
        int i = pk_hit((short)LOWORD(lp), (short)HIWORD(lp));
        if (i >= 0 && (short)HIWORD(lp) >= pkTop) pk_keep(h, i);
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        if (pk_hit((short)LOWORD(lp), (short)HIWORD(lp)) >= 0) DestroyWindow(h);
        return 0;
    case WM_MOUSEWHEEL: {
        RECT rc;
        GetClientRect(h, &rc);
        pkScroll -= (short)HIWORD(wp) * (pkTileH + pkGap) / WHEEL_DELTA;
        int maxs = pkContent - rc.bottom;
        if (pkScroll > maxs) pkScroll = maxs;
        if (pkScroll < 0) pkScroll = 0;
        POINT pt = { (short)LOWORD(lp), (short)HIWORD(lp) };
        ScreenToClient(h, &pt);
        int i = pk_hit(pt.x, pt.y);
        if (i != pkHover && pt.y >= pkTop) { pkHover = i; theme_apply(i >= 0 ? i : pkKept, 0); }
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: {
        /* arrows walk the tiles in screen order, R picks one at random */
        int i = pkKept, best = -1;
        long bd = 0x7fffffff;
        if (wp == VK_ESCAPE || wp == VK_RETURN) { DestroyWindow(h); return 0; }
        if (wp == 'R') { int r; do r = rand() % NTHEME; while (r == pkKept && NTHEME > 1); pk_keep(h, r); return 0; }
        int dx = wp == VK_LEFT ? -1 : wp == VK_RIGHT ? 1 : 0, dy = wp == VK_UP ? -1 : wp == VK_DOWN ? 1 : 0;
        if (wp == VK_HOME) { pk_keep(h, 0); return 0; }
        if (!dx && !dy) return 0;
        for (int k = 0; k < NTHEME; k++) {
            int ex = pkTileX[k] - pkTileX[i], ey = pkTileY[k] - pkTileY[i];
            if (k == i || (dx && (ex * dx <= 0 || ey)) || (dy && ey * dy <= 0)) continue;
            long d = dy ? (long)abs(ey) * 4 + abs(ex) : abs(ex);
            if (d < bd) { bd = d; best = k; }
        }
        if (best < 0 && dx) best = (i + dx + NTHEME) % NTHEME;   /* wrap at a row end */
        if (best >= 0) pk_keep(h, best);
        return 0;
    }
    case WM_DESTROY:
        theme_apply(pkKept, 0);
        hPick = NULL;
        SetFocus(hView);
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void theme_picker(void)
{
    if (hPick) { SetForegroundWindow(hPick); return; }
    static int reg;
    if (!reg) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.style = CS_DBLCLKS;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_HAND);
        wc.lpfnWndProc = PickProc;
        wc.lpszClassName = "Tuff3DThemes";
        RegisterClassExA(&wc);
        reg = 1;
    }
    pkKept = themeIdx;
    pkHover = -1;
    pkScroll = 0;
    /* five tiles across, as tall as fits on the screen */
    RECT wa, mr;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    GetWindowRect(hMain, &mr);
    int cw = 5 * (int)(160 * ds) + (int)(10 * ds);
    pk_layout(cw);
    int chh = pkContent, maxh = (wa.bottom - wa.top) * 85 / 100;
    RECT r = { 0, 0, cw, chh < maxh ? chh : maxh };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE);
    int w = r.right - r.left, hh = r.bottom - r.top;
    int x = (mr.left + mr.right - w) / 2, y = (mr.top + mr.bottom - hh) / 2;
    if (y < wa.top) y = wa.top;
    hPick = CreateWindowExA(WS_EX_TOOLWINDOW, "Tuff3DThemes", "Themes", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
                            x, y, w, hh, hMain, NULL, GetModuleHandle(NULL), NULL);
    theme_window(hPick);
    SetFocus(hPick);
}

/* ================================================================ hotkey editor

   A list of every command with its two key slots. Click a slot and press the new key (Esc cancels);
   right click a slot to clear it. A key already used elsewhere moves to the new command. */

static HWND hKeys;
static int hkScroll, hkHover = -1, hkHoverSlot, hkCap = -1, hkCapSlot, hkContent;
static char hkNote[160];
typedef struct { int bind; const char *head; int y; } HkRow;
static HkRow hkRows[NBIND * 2];
static int hkN;

static int hk_rowh(void) { return (int)(24 * ds); }
static int hk_foot(void) { return (int)(64 * ds); }

static void hk_layout(void)
{
    const char *g = NULL;
    int y = (int)(6 * ds), rh = hk_rowh();
    hkN = 0;
    for (int i = 0; i < NBIND; i++) {
        if (!g || strcmp(g, binds[i].group)) {
            g = binds[i].group;
            if (hkN) y += (int)(6 * ds);
            hkRows[hkN].bind = -1;
            hkRows[hkN].head = g;
            hkRows[hkN++].y = y;
            y += rh;
        }
        hkRows[hkN].bind = i; hkRows[hkN].head = NULL; hkRows[hkN++].y = y;
        y += rh;
    }
    hkContent = y + (int)(6 * ds);
}

/* key slot rectangle (in content coordinates) of row r */
static RECT hk_slot(int r, int slot, int w)
{
    int sw = (int)(118 * ds), pad = (int)(8 * ds);
    RECT rc;
    rc.left = w - pad - (2 - slot) * (sw + (int)(4 * ds));
    rc.right = rc.left + sw;
    rc.top = hkRows[r].y + 2;
    rc.bottom = hkRows[r].y + hk_rowh() - 2;
    return rc;
}

static int hk_hit(int x, int y, int w, int *slot)
{
    y += hkScroll;
    for (int r = 0; r < hkN; r++) {
        if (hkRows[r].bind < 0) continue;
        for (int s = 0; s < 2; s++) {
            RECT rc = hk_slot(r, s, w);
            POINT pt = { x, y };
            if (PtInRect(&rc, pt)) { *slot = s; return r; }
        }
    }
    return -1;
}

static RECT hk_button(int i, int w, int h)
{
    int bw = (int)(150 * ds), bh = (int)(26 * ds), pad = (int)(8 * ds);
    RECT rc;
    rc.right = w - pad - (1 - i) * (bw + pad);
    rc.left = rc.right - bw;
    rc.bottom = h - pad;
    rc.top = rc.bottom - bh;
    return rc;
}

static void hk_paint(HWND h)
{
    PAINTSTRUCT ps_;
    RECT cr;
    HDC wdc = BeginPaint(h, &ps_);
    GetClientRect(h, &cr);
    HDC dc = CreateCompatibleDC(wdc);
    HBITMAP bm = CreateCompatibleBitmap(wdc, cr.right, cr.bottom), old = SelectObject(dc, bm);
    FillRect(dc, &cr, brUi);
    SetBkMode(dc, TRANSPARENT);
    int w = cr.right, listh = cr.bottom - hk_foot(), pad = (int)(10 * ds);
    for (int r = 0; r < hkN; r++) {
        int y = hkRows[r].y - hkScroll;
        if (y + hk_rowh() < 0 || y > listh) continue;
        RECT t = { pad, y, w - pad, y + hk_rowh() };
        if (hkRows[r].bind < 0) {
            SelectObject(dc, hBold);
            SetTextColor(dc, th.head);
            DrawTextA(dc, hkRows[r].head, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            continue;
        }
        Bind *b = &binds[hkRows[r].bind];
        SelectObject(dc, hFont);
        SetTextColor(dc, th.text);
        t.left += (int)(10 * ds);
        DrawTextA(dc, b->name, -1, &t, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        for (int s = 0; s < 2; s++) {
            RECT k = hk_slot(r, s, w);
            OffsetRect(&k, 0, -hkScroll);
            int cap = hkCap == r && hkCapSlot == s, hot = hkHover == r && hkHoverSlot == s;
            FillRect(dc, &k, cap ? brHot : brField);
            FrameRect(dc, &k, cap ? brAcc : hot ? brAcc : brLine);
            char txt[48] = "";
            if (cap) snprintf(txt, sizeof txt, "press a key...");
            else if (b->key[s]) key_text(b->key[s], txt, sizeof txt);
            SetTextColor(dc, cap || !b->key[s] ? mix(th.fieldText, th.field) : th.fieldText);
            DrawTextA(dc, txt, -1, &k, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        }
    }
    /* footer: a note line and the two buttons */
    RECT f = { 0, listh, w, cr.bottom };
    FillRect(dc, &f, brUi);
    RECT ln = { 0, listh, w, listh + 1 };
    FillRect(dc, &ln, brLine);
    SelectObject(dc, hFont);
    SetTextColor(dc, mix(th.text, th.ui));
    RECT nt = { pad, listh + (int)(4 * ds), w - pad, listh + (int)(24 * ds) };
    DrawTextA(dc, hkNote[0] ? hkNote : "Click a key box, then press the key. Right click clears it. Esc closes.", -1, &nt,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    static const char *bl[2] = { "Reset to Defaults", "Close" };
    for (int i = 0; i < 2; i++) {
        RECT b = hk_button(i, w, cr.bottom);
        FillRect(dc, &b, brHot);
        FrameRect(dc, &b, brLine);
        SetTextColor(dc, th.text);
        DrawTextA(dc, bl[i], -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
    BitBlt(wdc, 0, 0, cr.right, cr.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
    EndPaint(h, &ps_);
}

/* hotkeys changed: save them and refresh everything that shows them */
static void hk_apply(void)
{
    binds_save();
    menu_keys();
    RECT rc;
    GetClientRect(hMain, &rc);
    SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));   /* toolbar labels may change width */
    InvalidateRect(hBar, NULL, FALSE);
    status_update();
}

static void hk_assign(int code)
{
    Bind *b = &binds[hkRows[hkCap].bind];
    hkNote[0] = 0;
    for (int i = 0; i < NBIND; i++)
        for (int s = 0; s < 2; s++)
            if (binds[i].key[s] == code && !(&binds[i] == b && s == hkCapSlot)) {
                char k[48];
                key_text(code, k, sizeof k);
                snprintf(hkNote, sizeof hkNote, "%s was on %s - it is now %s", k, binds[i].name, b->name);
                binds[i].key[s] = 0;
            }
    b->key[hkCapSlot] = code;
    hkCap = -1;
    hk_apply();
}

static void hk_clamp_scroll(HWND h)
{
    RECT rc;
    GetClientRect(h, &rc);
    int maxs = hkContent - (rc.bottom - hk_foot());
    if (hkScroll > maxs) hkScroll = maxs;
    if (hkScroll < 0) hkScroll = 0;
}

static LRESULT CALLBACK KeysProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    RECT cr;
    GetClientRect(h, &cr);
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (m) {
    case WM_PAINT: hk_paint(h); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_SIZE: hk_clamp_scroll(h); InvalidateRect(h, NULL, FALSE); return 0;
    case WM_MOUSEWHEEL:
        hkScroll -= (short)HIWORD(wp) / 120 * hk_rowh() * 3;
        hk_clamp_scroll(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        int s = 0, r = y < cr.bottom - hk_foot() ? hk_hit(x, y, cr.right, &s) : -1;
        if (r != hkHover || s != hkHoverSlot) { hkHover = r; hkHoverSlot = s; InvalidateRect(h, NULL, FALSE); }
        return 0;
    }
    case WM_LBUTTONDOWN: case WM_RBUTTONDOWN: {
        POINT pt = { x, y };
        for (int i = 0; i < 2; i++) {
            RECT b = hk_button(i, cr.right, cr.bottom);
            if (m == WM_LBUTTONDOWN && PtInRect(&b, pt)) {
                if (i == 0) {
                    for (int k = 0; k < NBIND; k++) { binds[k].key[0] = binds[k].def[0]; binds[k].key[1] = binds[k].def[1]; }
                    snprintf(hkNote, sizeof hkNote, "Every hotkey is back to its default");
                    hkCap = -1;
                    hk_apply();
                    InvalidateRect(h, NULL, FALSE);
                } else {
                    DestroyWindow(h);
                }
                return 0;
            }
        }
        int s = 0, r = y < cr.bottom - hk_foot() ? hk_hit(x, y, cr.right, &s) : -1;
        if (r >= 0 && m == WM_RBUTTONDOWN) {
            binds[hkRows[r].bind].key[s] = 0;
            hkCap = -1;
            hk_apply();
        } else {
            hkCap = r;
            hkCapSlot = s;
        }
        SetFocus(h);
        InvalidateRect(h, NULL, FALSE);
        return 0;
    }
    case WM_KEYDOWN: case WM_SYSKEYDOWN:
        if (wp == VK_SHIFT || wp == VK_CONTROL || wp == VK_MENU || wp == VK_LWIN || wp == VK_RWIN) return 0;
        if (wp == VK_ESCAPE) {
            if (hkCap >= 0) { hkCap = -1; InvalidateRect(h, NULL, FALSE); }
            else DestroyWindow(h);
            return 0;
        }
        if (hkCap >= 0) {
            hk_assign((int)wp | (key(VK_CONTROL) ? KC_CTRL : 0) | (key(VK_SHIFT) ? KC_SHIFT : 0));
            InvalidateRect(h, NULL, FALSE);
        }
        return 0;
    case WM_SYSCHAR: case WM_CHAR:
        return 0;
    case WM_DESTROY:
        hKeys = NULL;
        hkCap = -1;
        return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

static void hotkey_editor(void)
{
    if (hKeys) { SetForegroundWindow(hKeys); return; }
    static int reg;
    if (!reg) {
        WNDCLASSEXA wc;
        memset(&wc, 0, sizeof wc);
        wc.cbSize = sizeof wc;
        wc.hInstance = GetModuleHandle(NULL);
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpfnWndProc = KeysProc;
        wc.lpszClassName = "Tuff3DKeys";
        RegisterClassExA(&wc);
        reg = 1;
    }
    hk_layout();
    hkScroll = 0;
    hkCap = hkHover = -1;
    hkNote[0] = 0;
    RECT wa, mr;
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &wa, 0);
    GetWindowRect(hMain, &mr);
    int cw = (int)(520 * ds), chh = hkContent + hk_foot(), maxh = (wa.bottom - wa.top) * 85 / 100;
    RECT r = { 0, 0, cw, chh < maxh ? chh : maxh };
    AdjustWindowRect(&r, WS_CAPTION | WS_SYSMENU | WS_THICKFRAME, FALSE);
    int w = r.right - r.left, hh = r.bottom - r.top;
    int x = (mr.left + mr.right - w) / 2, y = (mr.top + mr.bottom - hh) / 2;
    if (y < wa.top) y = wa.top;
    hKeys = CreateWindowExA(WS_EX_TOOLWINDOW, "Tuff3DKeys", "Hotkey Editor", WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_VISIBLE,
                            x, y, w, hh, hMain, NULL, GetModuleHandle(NULL), NULL);
    theme_window(hKeys);
    SetFocus(hKeys);
}

static const char *HELP_TEXT =
    "Default hotkeys (Help > Hotkey Editor changes them)\n\n"
    "Navigation (Maya)\n"
    "  Alt + LMB / MMB / RMB drag\tOrbit / pan / zoom (orbiting leaves an ortho view)\n"
    "  Wheel\tZoom\n"
    "  F / A / Home\tFrame selected / frame all / home view\n"
    "  Click X, Y or Z on the corner gizmo\tOrthographic view down that axis (again: other side)\n\n"
    "Selection\n"
    "  Click / drag\tSelect / box select (red highlight = what a click picks)\n"
    "  Shift / Ctrl / Ctrl+Shift\tToggle / remove / add\n"
    "  RMB on an object\tComponent mode menu\n"
    "  F8 / F9 / F10 / F11 / F7\tObject / vertex / edge / face / multi\n"
    "  Ctrl+F9 / F10 / F11\tConvert selection to vertices / edges / faces\n"
    "  Double-click an edge\tSelect its edge loop\n"
    "  > / <\tGrow / shrink selection\n"
    "  Ctrl+Shift+A / Ctrl+Shift+I\tSelect all / invert\n\n"
    "Tools\n"
    "  Q W E R\tSelect / move / rotate / scale\n"
    "  Handles\tArrow = 1 axis, square = 2 axes, center = all / view plane\n"
    "  Ctrl + drag arrow\tMove/scale on the other two axes\n"
    "  MMB drag\tDrag the last used handle\n"
    "  hold X / V / J\tSnap to grid / to vertices / rotate in 15 deg steps\n"
    "  Esc while dragging\tCancel the drag\n"
    "  + / -\tManipulator size\n"
    "  Target Weld\tDrag one vertex onto another\n"
    "  Insert Edge Loop\tClick an edge (yellow preview)\n"
    "  Multi-Cut\tClick points; Enter/RMB finish, Backspace, Esc, Ctrl+click = loop\n\n"
    "Edit\n"
    "  Ctrl+E / Ctrl+B\tExtrude / bevel (Bevel Width in the side panel)\n"
    "  Ctrl+D / Delete\tDuplicate / delete (edges and vertices: their faces join)\n"
    "  Ctrl+Delete\tDelete Edge/Vertex\n"
    "  Ctrl+H / Ctrl+Shift+H\tHide selected / show all\n"
    "  Z, Ctrl+Z / Shift+Z, Ctrl+Y\tUndo / redo\n"
    "  G\tRepeat last command\n"
    "  4 / 5\tWireframe / shaded\n"
    "  Ctrl+N / Ctrl+O / Ctrl+S\tNew / open / save (.tuff scene or OBJ)\n"
    "  Ctrl+Shift+O / Ctrl+Shift+S\tImport into the scene / save as\n"
    "  Ctrl+A\tToggle side panel\n"
    "  Ctrl+T\tTheme picker (hover to preview, click to keep)\n\n"
    "Side panel: drag a label left/right to scrub its value (Shift fast, Ctrl fine).\n"
    "Drop .tuff, .obj or Maya LT .mlt files on the window to open them (Shift adds them).";

static void do_cmd(int id)
{
    g_msg[0] = 0;
    if (id >= CM_THEME && id < CM_THEME + NTHEME) {
        theme_apply(id - CM_THEME, 1);
        if (hPick) { pkKept = themeIdx; InvalidateRect(hPick, NULL, FALSE); }
    }
    if (id >= CM_RECENT && id < CM_RECENT + NRECENT && recent[id - CM_RECENT][0]) {
        char p[MAX_PATH];
        strcpy(p, recent[id - CM_RECENT]);
        load_file(p, 1);
    }
    switch (id) {
    case CM_NEW:
        if (!confirm_discard()) break;
        if (nob) undo_push("New Scene");
        for (int i = 0; i < nob; i++) mesh_free(&ob[i].m);
        nob = 0;
        for (int i = 0; i < 3; i++) img_clear(i);
        mode = M_OBJ;
        g_path[0] = 0;
        g_suggest[0] = 0;
        modified = 0;
        msg("New scene");
        break;
    case CM_OPEN: load_file(NULL, 1); break;
    case CM_IMPORT: load_file(NULL, 0); break;
    case CM_GROW: cmd_grow(0); break;
    case CM_SHRINK: cmd_grow(1); break;
    case CM_LOOP: cmd_loop(); break;
    case CM_TOVERT: case CM_TOEDGE: case CM_TOFACE: cmd_convert(M_VERT + id - CM_TOVERT); break;
    case CM_HIDE: cmd_hide(); break;
    case CM_SHOWALL: cmd_show_all(); break;
    case CM_FREEZE: cmd_transform(XF_FREEZE); break;
    case CM_CENTERPIV: cmd_transform(XF_CENTER); break;
    case CM_RESETXF: cmd_transform(XF_RESET); break;
    case CM_VFRONT: view_axis(2, 0); break;
    case CM_VSIDE: view_axis(0, 0); break;
    case CM_VTOP: view_axis(1, 0); break;
    case CM_VHOME: {
        V3 a, b;
        float d = 10;
        V3 c = v3(0, 0, 0);
        if (sel_bbox(&a, &b, 1)) {
            float r = fmaxf(v3len(v3sub(b, a)) * 0.5f, 0.05f), half = FOVY * 0.5f, hh = atanf(tanf(half) * vw / vh);
            c = v3mul(v3add(a, b), 0.5f);
            d = r / sinf(hh < half ? hh : half) * 1.1f;
        }
        ortho = 0;
        cam_to(c, d, 0.7854f, 0.46f);
        toast("Home view");
        break;
    }
    case CM_HUD: hudOn = !hudOn; break;
    case CM_COMPTOG: set_mode(mode == M_OBJ ? lastComp : M_OBJ); break;
    case CM_HOTKEYS: hotkey_editor(); break;
    case CM_MANIPUP: case CM_MANIPDOWN: {
        char b[48];
        manipSize *= id == CM_MANIPUP ? 1.15f : 1 / 1.15f;
        snprintf(b, sizeof b, "Manipulator size %d%%", (int)(manipSize * 100 + .5f));
        toast(b);
        break;
    }
    case CM_ORTHO: ortho = !ortho; toast(ortho ? "Orthographic view" : "Perspective view"); break;
    case CM_IMGFRONT: case CM_IMGSIDE: case CM_IMGTOP: cmd_image(id - CM_IMGFRONT); break;
    case CM_IMGCLEAR:
        for (int i = 0; i < 3; i++) if (img[i].tex) { img_clear(i); modified = 1; }
        msg("Removed the image planes");
        break;
    case CM_SNAPGRID: snapGrid = !snapGrid; msg(snapGrid ? "Grid snap on" : "Grid snap off"); break;
    case CM_SNAPPOINT: snapPoint = !snapPoint; msg(snapPoint ? "Point snap on" : "Point snap off"); break;
    case CM_EXPORT: cmd_export(0, 0); break;
    case CM_EXPORT_AS: cmd_export(1, 0); break;
    case CM_EXPORT_OBJ: cmd_export(2, 0); break;
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
    case CM_FILLHOLE: cmd_fill_hole(); break;
    case CM_APPLYCOL: cmd_apply_color(); break;
    case CM_SETCOL: cmd_set_color(); break;
    case CM_WIRE: wire = 1; break;
    case CM_SHADED: wire = 0; break;
    case CM_HARD: cmd_shade(0, "Hard Shade"); break;
    case CM_SMOOTH: cmd_shade(180, "Smooth Shade"); break;
    case CM_AUTOSMOOTH: cmd_shade(30, "Auto Smooth"); break;
    case CM_WIRETOG: wire = !wire; break;
    case CM_FRAMESEL: frame(0); break;
    case CM_FRAMEALL: frame(1); break;
    case CM_PANEL: {
        RECT rc;
        panelOn = !panelOn;
        GetClientRect(hMain, &rc);
        SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
        break;
    }
    case CM_TSEL: case CM_TMOVE: case CM_TROT: case CM_TSCALE: case CM_TPAINT: case CM_TWELD: case CM_TLOOP: case CM_TCUT:
        set_tool(id - CM_TSEL);
        break;
    case CM_REVERSE: cmd_face_op(mesh_reverse, "Reverse Normals", "Reversed %d face(s)"); break;
    case CM_TRIANG: cmd_face_op(mesh_triangulate, "Triangulate", "Triangulated %d face(s)"); break;
    case CM_QUAD: cmd_face_op(quad30, "Quadrangulate", "Joined %d triangle pair(s) into quads"); break;
    case CM_COMBINE: cmd_combine(); break;
    case CM_SEPARATE: cmd_separate(); break;
    case CM_MERGEDIST: cmd_merge_dist(); break;
    case CM_BEVEL: cmd_bevel(); break;
    case CM_BRIDGE: cmd_bridge(); break;
    case CM_HARDEN: cmd_edge_shade(1); break;
    case CM_SOFTEN: cmd_edge_shade(0); break;
    case CM_DELEV:
        if (mode == M_OBJ || !delete_ev()) msg("Delete Edge/Vertex: select edges or vertices");
        break;
    case CM_MOBJ: case CM_MVERT: case CM_MEDGE: case CM_MFACE: case CM_MMULTI:
        set_mode(id - CM_MOBJ);
        break;
    case CM_HELP: MessageBoxA(hMain, HELP_TEXT, APP_NAME " hotkeys", MB_OK); break;
    case CM_THEMEPICK: theme_picker(); break;
    case CM_THEMENEXT: case CM_THEMEPREV: case CM_THEMERAND: {
        int i = themeIdx + (id == CM_THEMENEXT ? 1 : NTHEME - 1);
        if (id == CM_THEMERAND) do i = rand() % NTHEME; while (i == themeIdx && NTHEME > 1);
        theme_apply(i % NTHEME, 1);
        if (hPick) { pkKept = themeIdx; InvalidateRect(hPick, NULL, FALSE); }
        char b[64];
        snprintf(b, sizeof b, "Theme: %s", themes[themeIdx].name);
        toast(b);
        break;
    }
    }
    if ((id >= CM_DELETE && id <= CM_MERGE) || id == CM_APPLYCOL || id == CM_GROW || id == CM_SHRINK ||
        (id >= CM_FREEZE && id <= CM_RESETXF) || id == CM_FILLHOLE || (id >= CM_REVERSE && id <= CM_SOFTEN))
        lastCmd = id;
    refresh_all();
}

/* ================================================================ side panel (channel box) */

enum {
    PF_TX, PF_TY, PF_TZ, PF_RX, PF_RY, PF_RZ, PF_SX, PF_SY, PF_SZ,
    PF_CW, PF_CH, PF_CD, PF_CSW, PF_CSH, PF_CSD,
    PF_YR, PF_YH, PF_YSIDES, PF_YHSEG, PF_YCAP, PF_YTOP, PF_YBOT,
    PF_SR, PF_SCOLS, PF_SROWS,
    PF_NANGLE,
    PF_PW, PF_PD, PF_PSW, PF_PSD,
    PF_OPA, PF_RAD, PF_COLOR, PF_APPLY,
    PF_MERGED, PF_BEVELW, PF_BRIDGED,
    PF_IMG   /* + plane * 4 + (size, offset x, offset y, opacity) */
};
enum { RK_HEAD = 1, RK_FLOAT, RK_INT, RK_BOOL, RK_COLOR, RK_BUTTON, RK_NAME, RK_TIP };
#define NROW 64
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
    if (id == PF_MERGED) return &mergeDist;
    if (id == PF_BEVELW) return &bevelW;
    if (id >= PF_IMG && id < PF_IMG + 12) {
        ImgPlane *q = &img[(id - PF_IMG) / 4];
        float *f[4] = { &q->size, &q->ox, &q->oy, &q->opa };
        return f[(id - PF_IMG) % 4];
    }
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
    case PF_NANGLE: return &o->nangle;
    case PF_PW: return &o->prim.pw;
    case PF_PD: return &o->prim.pd;
    }
    return NULL;
}

static int *fld_i(int id)
{
    Obj *o = panelObj >= 0 && panelObj < nob ? &ob[panelObj] : NULL;
    if (id == PF_BRIDGED) return &bridgeDivs;
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
    if (rows[r].kind == RK_NAME) { if (panelObj >= 0 && panelObj < nob) SetWindowTextA(rEdit[r], ob[panelObj].name); return; }
    if (rows[r].kind == RK_FLOAT) { float *p = fld_f(rows[r].id); if (p) fmt_float(b, *p); }
    else { int *p = fld_i(rows[r].id); if (p) sprintf(b, "%d", *p); }
    SetWindowTextA(rEdit[r], b);
}

static void panel_sync(void)
{
    HWND f = GetFocus();
    for (int r = 0; r < nrows; r++) {
        if ((rows[r].kind == RK_FLOAT || rows[r].kind == RK_INT || rows[r].kind == RK_NAME) && rEdit[r] != f) row_text(r);
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
        add_row(RK_NAME, 0, "Name");
        for (int i = 0; i < 9; i++) add_row(RK_FLOAT, PF_TX + i, trs[i]);
        add_row(RK_FLOAT, PF_NANGLE, "Normal Angle");
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
        add_row(RK_TIP, 0, "Click an object to edit it here.");
        add_row(RK_TIP, 0, "Tip: drag a label left or right");
        add_row(RK_TIP, 0, "to scrub its value (Shift = fast).");
    }
    add_row(RK_HEAD, 0, "Vertex Color");
    add_row(RK_COLOR, PF_COLOR, "Color");
    add_row(RK_FLOAT, PF_OPA, "Opacity");
    add_row(RK_FLOAT, PF_RAD, "Brush Radius");
    add_row(RK_BUTTON, PF_APPLY, "Apply Color to Selection");
    add_row(RK_HEAD, 0, "Mesh Tools");
    add_row(RK_FLOAT, PF_MERGED, "Merge Distance");
    add_row(RK_FLOAT, PF_BEVELW, "Bevel Width");
    add_row(RK_INT, PF_BRIDGED, "Bridge Divisions");
    for (int i = 0; i < 3; i++) {
        static const char *lab[4] = { "Size", "Offset X", "Offset Y", "Opacity" };
        char b[40];
        if (!img[i].tex) continue;
        snprintf(b, sizeof b, "%s Image", IMG_NAME[i]);
        add_row(RK_HEAD, 0, b);
        for (int k = 0; k < 4; k++) add_row(RK_FLOAT, PF_IMG + i * 4 + k, lab[k]);
    }

    RECT rc;
    GetClientRect(hPanel, &rc);
    int W = rc.right, rh = (int)(22 * ds), pad = (int)(6 * ds), lw = (int)(92 * ds), y = pad;
    int ew = W - lw - 2 * pad;
    HWND swatchShown = NULL, applyShown = NULL;
    for (int r = 0; r < NROW; r++) {
        int k = r < nrows ? rows[r].kind : 0;
        if (k == RK_HEAD && r) y += pad;
        int lab = k == RK_HEAD || k == RK_FLOAT || k == RK_INT || k == RK_COLOR || k == RK_TIP;
        if (lab) {
            int wide = k == RK_HEAD || k == RK_TIP;
            SendMessage(rLab[r], WM_SETFONT, (WPARAM)(k == RK_HEAD ? hBold : hFont), FALSE);
            SetWindowTextA(rLab[r], rows[r].label);
            MoveWindow(rLab[r], pad, y + (int)(3 * ds), wide ? W - 2 * pad : lw - 4, rh - (int)(4 * ds), TRUE);
        }
        ShowWindow(rLab[r], lab ? SW_SHOW : SW_HIDE);
        if (k == RK_FLOAT || k == RK_INT || k == RK_NAME) {
            int x0 = k == RK_NAME ? pad : pad + lw, w = k == RK_NAME ? W - 2 * pad : ew;
            SendMessage(rEdit[r], WM_SETFONT, (WPARAM)(k == RK_NAME ? hBold : hFont), FALSE);
            MoveWindow(rEdit[r], x0 + 1, y + (int)(2 * ds), w - 2, rh - 3 - (int)(2 * ds), TRUE);
            row_text(r);
        }
        ShowWindow(rEdit[r], k == RK_FLOAT || k == RK_INT || k == RK_NAME ? SW_SHOW : SW_HIDE);
        if (k == RK_BOOL) {
            SetWindowTextA(rChk[r], rows[r].label);
            MoveWindow(rChk[r], pad + lw, y, ew, rh - 2, TRUE);
        }
        ShowWindow(rChk[r], k == RK_BOOL ? SW_SHOW : SW_HIDE);
        if (k == RK_COLOR) { MoveWindow(hSwatch, pad + lw + 1, y + 1, ew - 2, rh - 4, TRUE); swatchShown = hSwatch; }
        if (k == RK_BUTTON) { MoveWindow(hApply, pad, y, W - 2 * pad, rh + 2, TRUE); applyShown = hApply; y += 4; }
        if (k) y += k == RK_TIP ? rh * 3 / 4 : rh;
    }
    ShowWindow(hSwatch, swatchShown ? SW_SHOW : SW_HIDE);
    ShowWindow(hApply, applyShown ? SW_SHOW : SW_HIDE);
    RedrawWindow(hPanel, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
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

static int panel_get(int r, double *v)
{
    if (rows[r].kind == RK_FLOAT) { float *p = fld_f(rows[r].id); if (p) *v = *p; return p != NULL; }
    if (rows[r].kind == RK_INT || rows[r].kind == RK_BOOL) { int *p = fld_i(rows[r].id); if (p) *v = *p; return p != NULL; }
    return 0;
}

/* writes a value with no undo step or refresh (scrubbing calls this on every mouse move) */
static void panel_set(int r, double v)
{
    int id = rows[r].id;
    if (rows[r].kind == RK_FLOAT) {
        if (id == PF_OPA) { paintOpa = (float)(v < 0 ? 0 : v > 1 ? 1 : v); return; }
        if (id == PF_RAD) { paintRad = (float)(v < 1 ? 1 : v > 2000 ? 2000 : v); return; }
        if (id == PF_MERGED) { mergeDist = (float)(v < 0 ? 0 : v > 1e4 ? 1e4 : v); return; }
        if (id >= PF_IMG) {
            float *f = fld_f(id);
            int k = (id - PF_IMG) % 4;
            *f = (float)(k == 0 ? (v < 1e-3 ? 1e-3 : v) : k == 3 ? (v < 0 ? 0 : v > 1 ? 1 : v) : v);
            modified = 1;
            return;
        }
        if (id == PF_BEVELW) { bevelW = (float)(v < 1e-4 ? 1e-4 : v > 1e4 ? 1e4 : v); live_redo(CM_BEVEL); return; }
        if (id <= PF_SZ) {
            /* channel edits apply to every selected object */
            for (int i = 0; i < nob; i++)
                if (active(&ob[i]) || i == panelObj) (&ob[i].t.x)[id - PF_TX] = (float)v;
            return;
        }
        if (id == PF_NANGLE) {
            defAngle = (float)(v < 0 ? 0 : v > 180 ? 180 : v);
            for (int i = 0; i < nob; i++)
                if (active(&ob[i]) || i == panelObj) ob[i].nangle = defAngle;
            return;
        }
        *fld_f(id) = (float)v;
    } else {
        if (id == PF_BRIDGED) { bridgeDivs = v < 0 ? 0 : v > 50 ? 50 : (int)v; live_redo(CM_BRIDGE); return; }
        *fld_i(id) = (int)v;
    }
    prim_rebuild(&ob[panelObj]);
}

/* tool settings are not part of the scene, so changing them is not an undo step */
static int panel_undoable(int r) { return rows[r].id != PF_OPA && rows[r].id != PF_RAD && rows[r].id < PF_MERGED; }

static void panel_commit(int r, double v)
{
    double cur;
    if (!panel_get(r, &cur) || (rows[r].kind == RK_FLOAT ? fabs(cur - v) < 1e-7 : (int)cur == (int)v)) return;
    if (panel_undoable(r)) undo_push(rows[r].label);
    panel_set(r, v);
    refresh_all();
}

static void panel_rename(int r)
{
    char b[64], nm[32];
    Obj *o = panelObj >= 0 && panelObj < nob ? &ob[panelObj] : NULL;
    GetWindowTextA(rEdit[r], b, sizeof b);
    char *p = b;
    while (*p == ' ') p++;
    for (char *c = p; *c; c++) if (*c == ' ' || *c == '\t') *c = '_';    /* OBJ names are one word */
    if (!o || !*p || !strcmp(p, o->name)) { row_text(r); return; }
    snprintf(nm, sizeof nm, "%s", p);
    if (name_used(nm)) unique_name(nm, nm);
    undo_push("Rename");
    strcpy(o->name, nm);
    snprintf(b, sizeof b, "Renamed to %s", nm);
    msg(b);
    refresh_all();
}

static void panel_commit_edit(int r)
{
    if (building || r >= nrows) return;
    if (rows[r].kind == RK_NAME) { panel_rename(r); return; }
    char b[64];
    GetWindowTextA(rEdit[r], b, sizeof b);
    char *end;
    double v = strtod(b, &end);
    if (end == b) { row_text(r); return; }
    panel_commit(r, v);
    row_text(r);
}

/* ---- scrubbing: drag a value's label left/right, like Maya's virtual slider */

static struct { int row, x0, pushed; double v0; } scrub = { -1, 0, 0, 0 };

static int scrub_row(int x, int y)
{
    POINT pt = { x, y };
    for (int r = 0; r < nrows; r++) {
        RECT rc;
        if ((rows[r].kind != RK_FLOAT && rows[r].kind != RK_INT) || !IsWindowVisible(rLab[r])) continue;
        GetWindowRect(rLab[r], &rc);
        MapWindowPoints(NULL, hPanel, (POINT *)&rc, 2);
        InflateRect(&rc, 0, 2);
        if (PtInRect(&rc, pt)) return r;
    }
    return -1;
}

static void scrub_move(int x)
{
    int id = rows[scrub.row].id, dx = x - scrub.x0;
    float mul = key(VK_SHIFT) ? 10 : key(VK_CONTROL) ? 0.1f : 1;
    double step = (id >= PF_RX && id <= PF_RZ) || id == PF_NANGLE ? 0.5 : id == PF_RAD ? 1 : id == PF_OPA ? 0.005 : id == PF_MERGED ? 0.001 : id == PF_BEVELW ? 0.002 : id >= PF_IMG && (id - PF_IMG) % 4 == 3 ? 0.005 : 0.01;
    double v = rows[scrub.row].kind == RK_INT ? scrub.v0 + (int)(dx * mul / 6) : scrub.v0 + dx * step * mul;
    double cur;
    if (rows[scrub.row].kind == RK_FLOAT) v = round(v * 1000) / 1000;
    if (!panel_get(scrub.row, &cur) || cur == v) return;
    if (!scrub.pushed && panel_undoable(scrub.row)) { undo_push(rows[scrub.row].label); scrub.pushed = 1; }
    panel_set(scrub.row, v);
    geo_changed();
    panel_sync();
    redraw();
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
        int kind = 0;
        for (int r = 0; r < nrows; r++)
            if (rLab[r] == (HWND)lp) kind = rows[r].kind;
        SetTextColor((HDC)wp, kind == RK_HEAD ? th.head : kind == RK_TIP ? mix(th.text, th.ui) : th.text);
        SetBkColor((HDC)wp, th.ui);
        return (LRESULT)brUi;
    }
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(h, &pt);
        if (LOWORD(lp) == HTCLIENT && (scrub.row >= 0 || scrub_row(pt.x, pt.y) >= 0)) {
            SetCursor(LoadCursor(NULL, IDC_SIZEWE));
            return TRUE;
        }
        break;
    }
    case WM_LBUTTONDOWN: {
        int r = scrub_row((short)LOWORD(lp), (short)HIWORD(lp));
        if (r < 0 || !panel_get(r, &scrub.v0)) return 0;
        scrub.row = r;
        scrub.x0 = (short)LOWORD(lp);
        scrub.pushed = 0;
        SetFocus(hView);
        SetCapture(h);
        return 0;
    }
    case WM_MOUSEMOVE:
        if (scrub.row >= 0) scrub_move((short)LOWORD(lp));
        return 0;
    case WM_LBUTTONUP: case WM_CAPTURECHANGED:
        if (scrub.row >= 0) {
            scrub.row = -1;
            if (GetCapture() == h) ReleaseCapture();
            refresh_all();
        }
        return 0;
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
typedef struct { const char *label, *hotkey; int cmd; const char *desc; } Tool;
static const Tool tbItems[] = {
    { "Select", "Q", CM_TSEL, "Select tool: click or drag a box to select" },
    { "Move", "W", CM_TMOVE, "Move tool: drag the arrows (1 axis), squares (2 axes) or center" },
    { "Rotate", "E", CM_TROT, "Rotate tool: drag a ring; hold J for 15 degree steps" },
    { "Scale", "R", CM_TSCALE, "Scale tool: drag a handle; drag components past the center to flatten them" },
    { "Paint", "", CM_TPAINT, "Paint vertex colors: drag over a mesh; B + drag sets the brush size" }, { 0 },
    { "Object", "F8", CM_MOBJ, "Object mode: pick whole objects (F8 toggles object/component)" },
    { "Vertex", "F9", CM_MVERT, "Vertex mode (Ctrl+F9 converts the selection to vertices)" },
    { "Edge", "F10", CM_MEDGE, "Edge mode: double-click an edge to select its loop" },
    { "Face", "F11", CM_MFACE, "Face mode (Ctrl+F11 converts the selection to faces)" },
    { "Multi", "F7", CM_MMULTI, "Multi mode: vertices, edges and faces at once" }, { 0 },
    { "Cube", "", CM_CUBE, "Create a polygon cube (edit its size and divisions in the side panel)" },
    { "Cylinder", "", CM_CYL, "Create a polygon cylinder" },
    { "Sphere", "", CM_SPHERE, "Create a polygon sphere" },
    { "Plane", "", CM_PLANE, "Create a polygon plane" }, { 0 },
    { "Extrude", "Ctrl+E", CM_EXTRUDE, "Extrude the selected faces or edges, then drag the blue handle" },
    { "Bevel", "Ctrl+B", CM_BEVEL, "Bevel the selected edges (Bevel Width in the side panel adjusts it)" },
    { "Bridge", "", CM_BRIDGE, "Join two selected open borders with faces" },
    { "Edge Loop", "", CM_TLOOP, "Insert Edge Loop tool: click an edge to add a loop across it" },
    { "Multi-Cut", "", CM_TCUT, "Multi-Cut tool: click points on edges to cut faces; Enter finishes" },
    { "Merge", "", CM_MERGE, "Merge the selected vertices to their center" },
    { "Fill Hole", "", CM_FILLHOLE, "Cap the hole the selection is on (an object: all its holes)" },
    { "Combine", "", CM_COMBINE, "Combine the selected objects into one" },
    { "Separate", "", CM_SEPARATE, "Split objects into their loose pieces, or selected faces into a new object" },
    { "Duplicate", "Ctrl+D", CM_DUP, "Duplicate the selected objects" },
    { "Delete", "Del", CM_DELETE, "Delete the selected objects or faces" }, { 0 },
    { "Grid Snap", "X", CM_SNAPGRID, "Toggle snapping moves to the grid (or hold X while dragging)" },
    { "Point Snap", "V", CM_SNAPPOINT, "Toggle snapping moves onto vertices (or hold V while dragging)" }, { 0 },
    { "Wire", "4 / 5", CM_WIRETOG, "Toggle wireframe (4) and shaded (5) display" },
    { "Hard", "", CM_HARD, "Hard Shade: every edge looks sharp (faceted)" },
    { "Smooth", "", CM_SMOOTH, "Smooth Shade: every edge looks soft" },
    { "Auto", "", CM_AUTOSMOOTH, "Auto Smooth: edges sharper than the Normal Angle (side panel) stay hard" },
    { "Frame", "F", CM_FRAMESEL, "Frame the selection (A frames everything, Home resets the view)" }, { 0 },
    { "Undo", "Z", CM_UNDO, "Undo the last change" },
    { "Redo", "Shift+Z", CM_REDO, "Redo" },
};
#define NTB ((int)(sizeof tbItems / sizeof *tbItems))
static RECT tbRc[NTB];
static int tbHover = -1, tbDown = -1;

static int tb_on(int cmd)
{
    if (cmd >= CM_TSEL && cmd < CM_TSEL + NTOOL) return tool == cmd - CM_TSEL;
    if (cmd >= CM_HARD && cmd <= CM_AUTOSMOOTH) {
        Obj *l = lead();
        float a = l ? l->nangle : defAngle;
        return cmd == (a <= 0 ? CM_HARD : a >= 180 ? CM_SMOOTH : CM_AUTOSMOOTH);
    }
    if (cmd >= CM_MOBJ && cmd <= CM_MMULTI) return mode == cmd - CM_MOBJ;
    if (cmd == CM_WIRE || cmd == CM_SHADED) return wire == (cmd == CM_WIRE);
    if (cmd == CM_WIRETOG) return wire;
    if (cmd == CM_SNAPGRID) return snapGrid;
    if (cmd == CM_SNAPPOINT) return snapPoint;
    return 0;
}

/* lays the buttons out in rows across width W and returns the bar height */
static int tb_layout(int W)
{
    HDC dc = GetDC(hMain);
    int pad = (int)(4 * ds), gap = (int)(7 * ds), bh = (int)(36 * ds), x = pad, y = pad;
    for (int i = 0; i < NTB; i++) {
        if (!tbItems[i].label) { x += gap; SetRectEmpty(&tbRc[i]); continue; }
        SIZE a, b;
        SelectObject(dc, hFont);
        GetTextExtentPoint32A(dc, tbItems[i].label, (int)strlen(tbItems[i].label), &a);
        SelectObject(dc, hSmall);
        char hk[48];
        const char *ht = cmd_key(tbItems[i].cmd, hk, sizeof hk);
        if (!ht) ht = tbItems[i].hotkey;
        GetTextExtentPoint32A(dc, ht, (int)strlen(ht), &b);
        int w = (a.cx > b.cx ? a.cx : b.cx) + (int)(9 * ds);
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
        char hk[48];
        const char *ht = cmd_key(t->cmd, hk, sizeof hk);
        DrawTextA(dc, ht ? ht : t->hotkey, -1, &b, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
            status_text();
            InvalidateRect(h, NULL, FALSE);
            TRACKMOUSEEVENT tme = { sizeof tme, TME_LEAVE, h, 0 };
            TrackMouseEvent(&tme);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        tbHover = -1;
        status_text();
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

/* The status line: mode, tool, selection, then the most useful hint for right now: the toolbar
   button under the mouse, the live drag amount, what a click would pick, the last message, or how
   to use the current tool. */
static void status_text(void)
{
    static const char *mn[] = { "Object mode", "Vertex mode", "Edge mode", "Face mode", "Multi mode" };
    static const char *tn[] = { "Select tool", "Move tool", "Rotate tool", "Scale tool", "Paint tool", "Target Weld", "Insert Edge Loop", "Multi-Cut" };
    static const char *hint[] = {
        "Click to select, drag for a box. Shift: toggle, Ctrl: remove, Ctrl+Shift: add. Alt+drag: orbit/pan/zoom",
        "Drag a handle to move. Hold X: grid snap, V: point snap, Ctrl+handle: other two axes, Esc: cancel",
        "Drag a ring to rotate. Hold J: 15 degree steps. Esc cancels the drag",
        "Drag a handle to scale. Drag components past the center to flatten them. Esc cancels",
        "Drag over a mesh to paint. B+drag: brush size. Color, opacity and radius are in the side panel",
        "Drag from one vertex onto another to weld them together (the first one moves). Esc cancels",
        "Click an edge to add an edge loop across it, where the yellow preview shows",
        "Click points on edges or vertices to cut. Enter/right click: finish, Backspace: undo point, Esc: drop, Ctrl+click: edge loop",
    };
    int nsel = 0, ncomp = 0;
    for (int i = 0; i < nob; i++) {
        nsel += mode == M_OBJ ? ob[i].sel : ob[i].hil;
        if (mode != M_OBJ && ob[i].hil) ncomp += comp_count(&ob[i]);
    }
    char s[600], c[64] = "", h[300] = "";
    if (mode != M_OBJ) snprintf(c, sizeof c, ", %d component(s)", ncomp);
    if (tbHover >= 0) {
        snprintf(h, sizeof h, "%s", tbItems[tbHover].desc);
    } else if (dk == D_MANIP || (g_msg[0] && GetTickCount() - msgT < 5000)) {
        snprintf(h, sizeof h, "%s", g_msg);
    } else if (hvObj >= 0 && hvObj < nob) {
        static const char *kn[] = { "", "vertex", "edge", "face" };
        if (hvKind) snprintf(h, sizeof h, "%s  %s %d - click to select it%s", ob[hvObj].name, kn[hvKind], hvIdx,
                             hvKind == M_EDGE ? ", double-click for its loop" : "");
        else snprintf(h, sizeof h, "%s - click to %s", ob[hvObj].name, mode == M_OBJ ? "select it" : "edit its components");
    } else if (mode != M_OBJ && !nsel) {
        static const char *kn[] = { "", "vertices", "edges", "faces", "components" };
        snprintf(h, sizeof h, "Click an object to start editing its %s", kn[mode]);
    } else {
        snprintf(h, sizeof h, "%s", hint[tool]);
    }
    snprintf(s, sizeof s, "  %s  |  %s  |  %d %s%s  |  %s", mn[mode], tn[tool], nsel,
             mode == M_OBJ ? "selected" : "hilited", c, h);
    SetWindowTextA(hStatus, s);
}

static void status_update(void)
{
    char s[400];
    status_text();
    const char *fn = g_path[0] ? g_path : "untitled";
    for (const char *c = g_path; *c; c++)
        if (*c == '\\' || *c == '/') fn = c + 1;
    snprintf(s, sizeof s, "%s%s - " APP_NAME, fn, modified ? "*" : "");
    SetWindowTextA(hMain, s);
    for (int i = 0; i < NTOOL; i++) CheckMenuItem(hMenu, CM_TSEL + i, MF_BYCOMMAND | (tool == i ? MF_CHECKED : MF_UNCHECKED));
    for (int i = 0; i < 5; i++) CheckMenuItem(hMenu, CM_MOBJ + i, MF_BYCOMMAND | (mode == i ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_WIRE, MF_BYCOMMAND | (wire ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_SHADED, MF_BYCOMMAND | (!wire ? MF_CHECKED : MF_UNCHECKED));
    Obj *l = lead();
    float a = l ? l->nangle : defAngle;
    CheckMenuRadioItem(hMenu, CM_HARD, CM_AUTOSMOOTH, a <= 0 ? CM_HARD : a >= 180 ? CM_SMOOTH : CM_AUTOSMOOTH, MF_BYCOMMAND);
    CheckMenuItem(hMenu, CM_PANEL, MF_BYCOMMAND | (panelOn ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_HUD, MF_BYCOMMAND | (hudOn ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_ORTHO, MF_BYCOMMAND | (ortho ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_SNAPGRID, MF_BYCOMMAND | (snapGrid ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(hMenu, CM_SNAPPOINT, MF_BYCOMMAND | (snapPoint ? MF_CHECKED : MF_UNCHECKED));
    char u[96], k[48];
    cmd_key(CM_UNDO, k, sizeof k);
    snprintf(u, sizeof u, "&Undo%s%s%s%s", nundo ? " " : "", nundo ? undo_s[nundo - 1].what : "", k[0] ? "\t" : "", k);
    ModifyMenuA(hMenu, CM_UNDO, MF_BYCOMMAND | MF_STRING | (nundo ? 0 : MF_GRAYED), CM_UNDO, u);
    cmd_key(CM_REDO, k, sizeof k);
    snprintf(u, sizeof u, "&Redo%s%s%s%s", nredo ? " " : "", nredo ? redo_s[nredo - 1].what : "", k[0] ? "\t" : "", k);
    ModifyMenuA(hMenu, CM_REDO, MF_BYCOMMAND | MF_STRING | (nredo ? 0 : MF_GRAYED), CM_REDO, u);
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

/* point (u, v) on the grid plane: the ground (XZ), or the view plane in a side/front ortho view */
static void grid_vtx(int k, float u, float v)
{
    float p[3] = { 0, 0, 0 };
    p[(k + 1) % 3] = u;
    p[(k + 2) % 3] = v;
    glVertex3fv(p);
}

static void draw_grid(void)
{
    int k = 1;
    if (ortho && fabsf(camF.x) > 0.999f) k = 0;
    if (ortho && fabsf(camF.z) > 0.999f) k = 2;
    glLineWidth(1);
    glBegin(GL_LINES);
    for (int i = -12; i <= 12; i++) {
        if (!i) continue;
        glColor3fv(i % 5 ? th.grid : th.gridMajor);
        grid_vtx(k, (float)i, -12); grid_vtx(k, (float)i, 12);
        grid_vtx(k, -12, (float)i); grid_vtx(k, 12, (float)i);
    }
    glColor3fv(th.gridAxis);
    grid_vtx(k, 0, -12); grid_vtx(k, 0, 12);
    grid_vtx(k, -12, 0); grid_vtx(k, 12, 0);
    glEnd();
}

static int gizmoHover = -1;

/* axis label of the corner gizmo under the mouse (0 X, 1 Y, 2 Z) or -1 */
static int gizmo_hit(int mx, int my)
{
    float s = 28 * ds, cx = 42 * ds, cy = vh - 42 * ds;
    int best = -1;
    float bd = 10 * ds;
    for (int i = 0; i < 3; i++) {
        V3 a = v3(i == 0, i == 1, i == 2);
        float d = hypotf(cx + v3dot(a, camR) * s * 1.35f - mx, cy - v3dot(a, camU) * s * 1.35f - my);
        if (d < bd) { bd = d; best = i; }
    }
    return best;
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
    if (gizmoHover >= 0) {
        /* clickable: ring the axis label under the cursor */
        V3 a = v3(gizmoHover == 0, gizmoHover == 1, gizmoHover == 2);
        float lx = cx + v3dot(a, camR) * s * 1.35f, ly = cy - v3dot(a, camU) * s * 1.35f;
        glColor3f(1, 1, 0);
        glBegin(GL_LINE_LOOP);
        for (int k = 0; k < 20; k++) glVertex2f(lx + 8 * ds * cosf(2 * PI_F * k / 20), ly + 8 * ds * sinf(2 * PI_F * k / 20));
        glEnd();
    }
    glBegin(GL_LINES);
    for (int i = 0; i < 3; i++) {
        V3 a = v3(i == 0, i == 1, i == 2);
        float x = cx + v3dot(a, camR) * s, y = cy - v3dot(a, camU) * s;
        float lx = cx + v3dot(a, camR) * s * 1.35f, ly = cy - v3dot(a, camU) * s * 1.35f;
        if (i == gizmoHover) glColor3f(1, 1, 0);
        else glColor3fv(col[i]);
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
    if (x > -1000 && dk == D_NONE && tool != T_PAINT && tool != T_LOOP && tool != T_CUT && !(manip_vis() && hover)) {
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
        status_text();
        redraw();
    }
}

/* ---- heads-up text, drawn from GDI font bitmaps (wglUseFontBitmaps) */

static GLuint fontBase;
static int hudAsc;
static const float WHITE3[3] = { 1, 1, 1 }, GREY3[3] = { .78f, .78f, .78f }, GOLD3[3] = { 1, .82f, .3f };

static int text_w(const char *s)
{
    SIZE z;
    GetTextExtentPoint32A(hdc, s, (int)strlen(s), &z);
    return z.cx;
}

static int textOnBox;   /* drawing over a dark shade_box: keep light text */

/* y is the top of the line; a drop shadow keeps it readable on any background */
static void text_at(float x, float y, const char *s, const float *rgb, float a)
{
    float by = y + hudAsc;
    if (!fontBase || x < 0 || x > vw - 2 || by < 1 || by > vh - 2) return;
    int n = (int)strlen(s);
    /* light viewport backgrounds (Light theme) get dark text with a light shadow */
    int light = th.bgTop[1] + th.bgBot[1] > 1.1f && !textOnBox;
    glColor4f(light, light, light, a * (light ? .5f : .7f));
    glRasterPos2f(x + 1, by + 1);
    glCallLists(n, GL_UNSIGNED_BYTE, s);
    if (light) glColor4f(.9f - rgb[0] * .85f, .9f - rgb[1] * .85f, .9f - rgb[2] * .85f, a);
    else glColor4f(rgb[0], rgb[1], rgb[2], a);
    glRasterPos2f(x, by);
    glCallLists(n, GL_UNSIGNED_BYTE, s);
}

static void shade_box(float x0, float y0, float x1, float y1, float a)
{
    glColor4f(0, 0, 0, a);
    glBegin(GL_QUADS);
    glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
    glEnd();
}

static void fmt_int(char *b, int v)
{
    char t[24];
    int n = sprintf(t, "%d", v), o = 0;
    for (int i = 0; i < n; i++) {
        if (i && (n - i) % 3 == 0) b[o++] = ',';
        b[o++] = t[i];
    }
    b[o] = 0;
}

/* scene totals and selected counts, recounted only after a change */
static struct { unsigned gen; int tot[5], sel[5]; } hc;

static void hud_counts(void)
{
    if (hc.gen == geoGen) return;
    memset(&hc, 0, sizeof hc);
    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        if (o->hidden) continue;
        hc.tot[0] += m->nv; hc.tot[1] += m->ne; hc.tot[2] += m->nf; hc.tot[3] += cache_get(i)->ntri; hc.tot[4]++;
        hc.sel[4] += active(o);
        if (mode == M_OBJ || !o->hil) continue;
        for (int v = 0; v < m->nv && shows(M_VERT); v++) hc.sel[0] += m->vs[v] != 0;
        for (int e = 0; e < m->ne && shows(M_EDGE); e++) hc.sel[1] += m->es[e] != 0;
        for (int f = 0; f < m->nf && shows(M_FACE); f++) hc.sel[2] += m->fs[f] != 0;
    }
    hc.gen = geoGen;
}

static void draw_hud(void)
{
    static const char *lab[5] = { "Verts", "Edges", "Faces", "Tris", "Objects" };
    char b[32];
    hud_counts();
    float x = 10 * ds, y = 8 * ds, lh = hudAsc * 1.35f, c2 = x + text_w("Objects") + text_w("0,000,000") + 12 * ds;
    for (int i = 0; i < 5; i++, y += lh) {
        text_at(x, y, lab[i], GREY3, .9f);
        fmt_int(b, hc.tot[i]);
        text_at(c2 - text_w(b), y, b, WHITE3, .95f);
        int sel = i == 3 ? 0 : hc.sel[i];
        if (sel) {
            fmt_int(b, sel);
            text_at(c2 + 10 * ds, y, b, GOLD3, 1);
        }
    }
}

/* centered tips while the scene is empty */
static void draw_empty_hint(void)
{
    static const char *lines[] = {
        "Empty scene",
        "Make something with Cube, Cylinder, Sphere or Plane in the toolbar,",
        "or drop a .tuff, .obj or .mlt file here (Ctrl+O opens one).",
        "",
        "Alt + left drag: orbit     Alt + middle drag: pan     Alt + right drag / wheel: zoom",
        "F1: every hotkey",
    };
    float lh = hudAsc * 1.5f, y = vh * 0.5f - lh * 3;
    for (int i = 0; i < 6; i++, y += lh)
        text_at((vw - text_w(lines[i])) * 0.5f, y, lines[i], i ? GREY3 : WHITE3, i ? .85f : 1);
}

static void draw_toast(void)
{
    DWORD age = GetTickCount() - toastT;
    if (!toastS[0] || age >= TOAST_MS) return;
    float a = age < TOAST_MS - 500 ? 1 : (TOAST_MS - age) / 500.0f;
    float w = (float)text_w(toastS), x = (vw - w) * 0.5f, y = vh - 70 * ds, p = 8 * ds;
    shade_box(x - p * 1.5f, y - p, x + w + p * 1.5f, y + hudAsc + p, .6f * a);
    textOnBox = 1;
    text_at(x, y, toastS, WHITE3, a);
    textOnBox = 0;
}

/* live move/rotate/scale amount beside the cursor */
static void draw_drag_label(void)
{
    if (dk != D_MANIP || !g_msg[0]) return;
    float x = curx + 18 * ds, y = cury + 18 * ds, w = (float)text_w(g_msg), p = 4 * ds;
    if (x + w + p > vw) x = curx - 18 * ds - w;
    shade_box(x - p, y - p, x + w + p, y + hudAsc + p, .55f);
    textOnBox = 1;
    text_at(x, y, g_msg, WHITE3, 1);
    textOnBox = 0;
}

static HCURSOR view_cursor(void)
{
    LPCSTR c = IDC_ARROW;
    if (dk == D_TUMBLE) c = IDC_SIZEALL;
    else if (dk == D_PAN) c = IDC_HAND;
    else if (dk == D_DOLLY) c = IDC_SIZENS;
    else if (dk == D_MARQ || tool == T_PAINT || tool == T_WELD || tool == T_LOOP || tool == T_CUT) c = IDC_CROSS;
    else if (gizmoHover >= 0) c = IDC_HAND;
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
    draw_images();

    for (int i = 0; i < nob; i++) {
        Obj *o = &ob[i];
        Mesh *m = &o->m;
        M4 M = obj_mat(o);
        int hil = mode != M_OBJ && o->hil;
        if (o->hidden) continue;
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
            /* Harden Edge overrides: thicker, like Maya's hard edge display */
            glLineWidth(2.5f * ds);
            glColor3f(0.1f, 0.3f, 0.9f);
            glBegin(GL_LINES);
            for (int e = 0; e < m->ne; e++)
                if (m->eh[e] == EH_HARD) { glVertex3fv(&m->p[m->ev[2 * e]].x); glVertex3fv(&m->p[m->ev[2 * e + 1]].x); }
            glEnd();
            glLineWidth(1);
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
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (dk == D_MANIP && snapOn && tool == T_MOVE) {
        float sx, sy, r = 7 * ds;
        if (proj(snapAt, &sx, &sy, NULL)) {
            glColor3f(1, 1, 0);
            glLineWidth(2);
            glBegin(GL_LINE_LOOP);
            glVertex2f(sx - r, sy - r); glVertex2f(sx + r, sy - r); glVertex2f(sx + r, sy + r); glVertex2f(sx - r, sy + r);
            glEnd();
            glLineWidth(1);
        }
    }
    draw_weld();
    draw_loop_preview();
    draw_cut();
    if (hudOn) draw_hud();
    if (!nob) draw_empty_hint();
    draw_drag_label();
    draw_toast();
    glDisable(GL_BLEND);
    draw_axis_gizmo();
    SwapBuffers(hdc);
}

/* ================================================================ viewport window */

/* ---- Target Weld: press on a vertex, release on another one of the same object */

static struct { int obj, v, tgt; } wd = { -1, -1, -1 };

static int weld_pick(int x, int y, int obj)
{
    int o, k, i;
    cam_update();
    if (!pick_comp(x, y, &o, &k, &i) || k != M_VERT || (obj >= 0 && o != obj)) return -1;
    if (obj < 0) wd.obj = o;
    return i;
}

static void draw_weld(void)
{
    if (dk != D_WELD || wd.obj < 0 || wd.obj >= nob) return;
    M4 M = obj_mat(&ob[wd.obj]);
    float sx, sy, tx = (float)curx, ty = (float)cury, r = 7 * ds;
    if (!proj(m4_pt(M, ob[wd.obj].m.p[wd.v]), &sx, &sy, NULL)) return;
    if (wd.tgt >= 0) proj(m4_pt(M, ob[wd.obj].m.p[wd.tgt]), &tx, &ty, NULL);
    glColor3f(1, 1, 0);
    glLineWidth(2 * ds);
    glBegin(GL_LINES);
    glVertex2f(sx, sy); glVertex2f(tx, ty);
    glEnd();
    if (wd.tgt >= 0) {
        glBegin(GL_LINE_LOOP);
        for (int k = 0; k < 20; k++) glVertex2f(tx + r * cosf(2 * PI_F * k / 20), ty + r * sinf(2 * PI_F * k / 20));
        glEnd();
    }
    glLineWidth(1);
}

static void weld_end(int cancel)
{
    if (!cancel && wd.obj >= 0 && wd.obj < nob && wd.tgt >= 0) {
        undo_push("Target Weld");
        if (mesh_weld(&ob[wd.obj].m, wd.v, wd.tgt)) {
            ob[wd.obj].prim.type = PRIM_NONE;
            msg("Welded");
        } else {
            undo_drop();
        }
    }
    wd.obj = wd.v = wd.tgt = -1;
}

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
    else if (k == D_WELD) weld_end(cancel);
    if (k == D_PEND || k == D_MARQ) normalFrame = 0;
    if (GetCapture() == hView) ReleaseCapture();
    refresh_all();
    hvObj = -1;
    hover_update(curx, cury);
    SetCursor(view_cursor());
}

/* Esc while dragging: put everything back as it was when the drag started */
static void cancel_drag(void)
{
    if ((dk == D_MANIP && dg.dirty) || (dk == D_PAINT && ps.dirty)) {
        snap_restore(&undo_s[--nundo]);
        dg.dirty = ps.dirty = 0;
    }
    end_drag(1);
    msg("Cancelled");
}

/* double-click an edge: select its edge loop. Returns 0 when it was not on an edge. */
static int dbl_click(int x, int y)
{
    if (!shows(M_EDGE) || key(VK_MENU) || tool == T_PAINT) return 0;
    int o, k, i;
    cam_update();
    if (!pick_comp(x, y, &o, &k, &i) || k != M_EDGE) return 0;
    if (!key(VK_SHIFT) && !key(VK_CONTROL)) clear_sel();
    char b[64];
    snprintf(b, sizeof b, "Edge loop: %d edges", mesh_edge_loop(&ob[o].m, i));
    msg(b);
    refresh_all();
    return 1;
}

static void handle_key(WPARAM wp)
{
    int ctrl = key(VK_CONTROL), shift = key(VK_SHIFT), id = 0;
    if (tool == T_CUT && mc.n && (wp == VK_RETURN || wp == VK_ESCAPE || wp == VK_BACK)) {
        if (wp == VK_RETURN) mc_commit();
        else if (wp == VK_ESCAPE) { mc_reset(); msg("Multi-Cut: points dropped"); }
        else mc.n--;
        refresh_all();
        return;
    }
    int code = (int)wp | (ctrl ? KC_CTRL : 0) | (shift ? KC_SHIFT : 0);
    for (int i = 0; i < NBIND && !id; i++)
        if (binds[i].key[0] == code || binds[i].key[1] == code) id = binds[i].cmd;
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
        TEXTMETRICA tm;
        SelectObject(hdc, hHud);
        GetTextMetricsA(hdc, &tm);
        hudAsc = tm.tmAscent;
        fontBase = glGenLists(96);
        if (!wglUseFontBitmapsA(hdc, 32, 96, fontBase) && !wglUseFontBitmapsA(hdc, 32, 96, fontBase)) fontBase = 0;
        glListBase(fontBase - 32);
        return 0;
    }
    case WM_TIMER:
        if (!anim_step()) { KillTimer(h, 1); animOn = 0; }
        redraw();
        return 0;
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
    case WM_LBUTTONDBLCLK:
        if (dk == D_NONE && dbl_click(x, y)) return 0;   /* otherwise it is just another click */
        /* fall through */
    case WM_LBUTTONDOWN: case WM_MBUTTONDOWN: case WM_RBUTTONDOWN: case WM_MBUTTONDBLCLK: case WM_RBUTTONDBLCLK: {
        SetFocus(h);
        if (dk != D_NONE) return 0;
        int btn = m == WM_LBUTTONDOWN || m == WM_LBUTTONDBLCLK ? 0 : m == WM_MBUTTONDOWN || m == WM_MBUTTONDBLCLK ? 1 : 2;
        mx0 = curx = x;
        my0 = cury = y;
        cam_update();
        if (btn == 0 && !key(VK_MENU) && gizmo_hit(x, y) >= 0) {
            /* corner gizmo: look down that axis; click again for the opposite side */
            int a = gizmo_hit(x, y);
            V3 ax = v3(a == 0, a == 1, a == 2);
            view_axis(a, v3dot(camF, ax) < -0.99f);
            return 0;
        }
        tween.on = 0;
        if (key(VK_MENU)) {
            dk = btn == 0 ? D_TUMBLE : btn == 1 ? D_PAN : D_DOLLY;
        } else if (btn == 2 && !(tool == T_CUT && mc.n)) {
            marking_menu(x, y);
            return 0;
        } else if (btn == 1) {
            if (!manip_vis()) return 0;
            drag_begin(lastHandle, x, y);
            dk = D_MANIP;
        } else if (btn == 2 && tool == T_CUT && mc.n) {
            mc_commit();
            refresh_all();
            return 0;
        } else if (btn == 0 && tool == T_CUT && (mc.hv || mc.n)) {
            mc_click(key(VK_CONTROL));
            refresh_all();
            return 0;
        } else if (tool == T_LOOP && elp.obj >= 0) {
            loop_click();
            refresh_all();
            return 0;
        } else if (tool == T_WELD && (wd.v = weld_pick(x, y, -1)) >= 0) {
            wd.tgt = -1;
            dk = D_WELD;
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
            if (ortho && (dx || dy)) { ortho = 0; toast("Perspective view"); }
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
        case D_MANIP: drag_move(x, y); panel_sync(); status_text(); break;
        case D_PAINT: paint_dab(x, y); break;
        case D_WELD: {
            int t = weld_pick(x, y, wd.obj);
            wd.tgt = t == wd.v ? -1 : t;
            break;
        }
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
            cam_update();
            int gh = gizmo_hit(x, y);
            if (gh != gizmoHover) { gizmoHover = gh; SetCursor(view_cursor()); redraw(); }
            hover_update(gh >= 0 ? -10000 : x, y);
            if (tool == T_LOOP) loop_hover(x, y);
            if (tool == T_CUT) mc_hover(x, y);
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
        elp.obj = -1;
        gizmoHover = -1;
        curx = cury = -1000;
        hover_update(-10000, 0);
        if (tool == T_PAINT) redraw();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) { SetCursor(view_cursor()); return TRUE; }
        break;
    case WM_MOUSEWHEEL:
        tween.on = 0;
        dist *= powf(0.88f, (short)HIWORD(wp) / 120.0f);
        redraw();
        return 0;
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE && dk != D_NONE) { cancel_drag(); return 0; }
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
    AppendMenuA(m, MF_STRING, CM_OPEN, "&Open...\tCtrl+O");
    hRecentMenu = CreatePopupMenu();
    AppendMenuA(m, MF_POPUP, (UINT_PTR)hRecentMenu, "Open &Recent");
    AppendMenuA(m, MF_STRING, CM_IMPORT, "&Import (add to scene)...\tCtrl+Shift+O");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_EXPORT, "&Save\tCtrl+S");
    AppendMenuA(m, MF_STRING, CM_EXPORT_AS, "Save &As...\tCtrl+Shift+S");
    AppendMenuA(m, MF_STRING, CM_EXPORT_OBJ, "&Export OBJ...");
    AppendMenuA(m, MF_STRING, CM_EXPORT_SEL, "Export Se&lection as OBJ...");
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
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_HIDE, "&Hide Selected\tCtrl+H");
    AppendMenuA(m, MF_STRING, CM_SHOWALL, "Sh&ow All\tCtrl+Shift+H");
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
    AppendMenuA(m, MF_STRING, CM_GROW, "&Grow Selection\t>");
    AppendMenuA(m, MF_STRING, CM_SHRINK, "S&hrink Selection\t<");
    AppendMenuA(m, MF_STRING, CM_LOOP, "Edge &Loop\tdouble-click edge");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_TOVERT, "Convert to Ve&rtices\tCtrl+F9");
    AppendMenuA(m, MF_STRING, CM_TOEDGE, "Convert to E&dges\tCtrl+F10");
    AppendMenuA(m, MF_STRING, CM_TOFACE, "Convert to Fa&ces\tCtrl+F11");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Select");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_COMBINE, "&Combine");
    AppendMenuA(m, MF_STRING, CM_SEPARATE, "&Separate (pieces, or selected faces)");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_MERGEDIST, "&Merge Vertices (by distance)");
    AppendMenuA(m, MF_STRING, CM_FILLHOLE, "&Fill Hole");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_TRIANG, "&Triangulate");
    AppendMenuA(m, MF_STRING, CM_QUAD, "&Quadrangulate");
    AppendMenuA(m, MF_STRING, CM_REVERSE, "&Reverse Normals");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Mesh");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_EXTRUDE, "&Extrude\tCtrl+E");
    AppendMenuA(m, MF_STRING, CM_BEVEL, "&Bevel\tCtrl+B");
    AppendMenuA(m, MF_STRING, CM_BRIDGE, "B&ridge");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_TLOOP, "&Insert Edge Loop Tool");
    AppendMenuA(m, MF_STRING, CM_TCUT, "Multi-&Cut Tool");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_MERGE, "&Merge to Center");
    AppendMenuA(m, MF_STRING, CM_TWELD, "&Target Weld Tool");
    AppendMenuA(m, MF_STRING, CM_DELEV, "&Delete Edge/Vertex\tCtrl+Del");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "Edit Mes&h");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_FREEZE, "&Freeze Transformations");
    AppendMenuA(m, MF_STRING, CM_CENTERPIV, "&Center Pivot");
    AppendMenuA(m, MF_STRING, CM_RESETXF, "&Reset Transformations");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_SNAPGRID, "Snap to &Grid\thold X");
    AppendMenuA(m, MF_STRING, CM_SNAPPOINT, "Snap to &Point\thold V");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "Mo&dify");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_TSEL, "&Select Tool\tQ");
    AppendMenuA(m, MF_STRING, CM_TMOVE, "&Move Tool\tW");
    AppendMenuA(m, MF_STRING, CM_TROT, "&Rotate Tool\tE");
    AppendMenuA(m, MF_STRING, CM_TSCALE, "S&cale Tool\tR");
    AppendMenuA(m, MF_STRING, CM_TPAINT, "&Paint Vertex Color Tool");
    AppendMenuA(m, MF_STRING, CM_TWELD, "Target &Weld Tool");
    AppendMenuA(m, MF_STRING, CM_TLOOP, "&Insert Edge Loop Tool");
    AppendMenuA(m, MF_STRING, CM_TCUT, "Multi-&Cut Tool");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Tools");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_SETCOL, "&Set Paint Color...");
    AppendMenuA(m, MF_STRING, CM_APPLYCOL, "&Apply Color to Selection");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "C&olor");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_WIRE, "&Wireframe\t4");
    AppendMenuA(m, MF_STRING, CM_SHADED, "&Shaded\t5");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_HARD, "Hard Shade");
    AppendMenuA(m, MF_STRING, CM_SMOOTH, "Smooth Shade");
    AppendMenuA(m, MF_STRING, CM_AUTOSMOOTH, "Auto Smooth (Normal Angle)");
    AppendMenuA(m, MF_STRING, CM_HARDEN, "Har&den Edge");
    AppendMenuA(m, MF_STRING, CM_SOFTEN, "S&often Edge");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_FRAMESEL, "&Frame Selected\tF");
    AppendMenuA(m, MF_STRING, CM_FRAMEALL, "Frame &All\tA");
    AppendMenuA(m, MF_STRING, CM_PANEL, "Side &Panel\tCtrl+A");
    AppendMenuA(m, MF_STRING, CM_HUD, "&Heads-Up Display (poly count)");
    AppendMenuA(m, MF_SEPARATOR, 0, NULL);
    AppendMenuA(m, MF_STRING, CM_VHOME, "&Home View\tHome");
    AppendMenuA(m, MF_STRING, CM_VFRONT, "Fr&ont View");
    AppendMenuA(m, MF_STRING, CM_VSIDE, "S&ide View");
    AppendMenuA(m, MF_STRING, CM_VTOP, "&Top View");
    AppendMenuA(m, MF_STRING, CM_ORTHO, "O&rthographic");
    HMENU im = CreatePopupMenu();
    AppendMenuA(im, MF_STRING, CM_IMGFRONT, "&Front Image...");
    AppendMenuA(im, MF_STRING, CM_IMGSIDE, "&Side Image...");
    AppendMenuA(im, MF_STRING, CM_IMGTOP, "&Top Image...");
    AppendMenuA(im, MF_SEPARATOR, 0, NULL);
    AppendMenuA(im, MF_STRING, CM_IMGCLEAR, "&Remove Image Planes");
    AppendMenuA(m, MF_POPUP, (UINT_PTR)im, "&Image Planes");
    hThemeMenu = CreatePopupMenu();
    AppendMenuA(hThemeMenu, MF_STRING, CM_THEMEPICK, "Theme &Picker...\tCtrl+T");
    AppendMenuA(hThemeMenu, MF_STRING, CM_THEMENEXT, "&Next Theme\tCtrl+Shift+T");
    AppendMenuA(hThemeMenu, MF_STRING, CM_THEMEPREV, "Pre&vious Theme");
    AppendMenuA(hThemeMenu, MF_STRING, CM_THEMERAND, "&Random Theme");
    AppendMenuA(hThemeMenu, MF_SEPARATOR, 0, NULL);
    for (int i = 0; i < NTHEME; i++)   /* the full list, in columns */
        AppendMenuA(hThemeMenu, MF_STRING | (i && i % 16 == 0 ? MF_MENUBARBREAK : 0), CM_THEME + i, themes[i].name);
    AppendMenuA(m, MF_POPUP, (UINT_PTR)hThemeMenu, "&Theme");
    AppendMenuA(bar, MF_POPUP, (UINT_PTR)m, "&Display");
    m = CreatePopupMenu();
    AppendMenuA(m, MF_STRING, CM_HELP, "&Hotkeys\tF1");
    AppendMenuA(m, MF_STRING, CM_HOTKEYS, "Hotkey &Editor...");
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
    case WM_DROPFILES: {
        /* drop an .obj: open it (hold Shift to add it to the scene instead) */
        char p[MAX_PATH];
        HDROP hd = (HDROP)wp;
        UINT n = DragQueryFileA(hd, 0xFFFFFFFF, NULL, 0);
        int add = key(VK_SHIFT);
        for (UINT i = 0; i < n; i++)
            if (DragQueryFileA(hd, i, p, sizeof p)) {
                load_file(p, !add && i == 0);
                if (!add && i == 0) add = 1;    /* more files join the first one */
            }
        DragFinish(hd);
        SetForegroundWindow(h);
        refresh_all();
        return 0;
    }
    case WM_TIMER:
        if (wp == 2) autosave_tick();
        return 0;
    case WM_APP: {
        const char *arg = (const char *)lp;
        if (arg[0]) load_file(arg, 1);
        else autosave_recover();
        refresh_all();
        return 0;
    }
    case WM_CLOSE:
        if (!confirm_discard()) return 0;
        break;
    case WM_DESTROY: {
        /* remember the window and display settings for next time */
        WINDOWPLACEMENT pl;
        pl.length = sizeof pl;
        if (GetWindowPlacement(h, &pl)) reg_set("Window", REG_BINARY, &pl, sizeof pl);
        reg_set_int("Panel", panelOn);
        reg_set_int("Hud", hudOn);
        reg_set_int("Wire", wire);
        reg_set_int("NormalAngle", (int)(defAngle * 100));
        reg_set_int("ManipSize", (int)(manipSize * 1000));
        reg_set_int("MergeDist", (int)(mergeDist * 1e5f));
        reg_set_int("BevelWidth", (int)(bevelW * 1e5f));
        autosave_remove();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProc(h, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    (void)prev;
    SetProcessDPIAware();
    HDC sdc = GetDC(NULL);
    ds = GetDeviceCaps(sdc, LOGPIXELSY) / 96.0f;
    ReleaseDC(NULL, sdc);
    if (ds < 1) ds = 1;
    for (int t = 0; t < PRIM_COUNT; t++) prim_defaults(&defPrim[t], t);
    srand(GetTickCount());
    theme_load(reg_int("Theme", 0));
    panelOn = reg_int("Panel", 1);
    hudOn = reg_int("Hud", 1);
    wire = reg_int("Wire", 0);
    defAngle = reg_int("NormalAngle", 0) / 100.0f;
    if (defAngle < 0 || defAngle > 180) defAngle = 0;
    manipSize = reg_int("ManipSize", 1000) / 1000.0f;
    if (manipSize < .05f || manipSize > 50) manipSize = 1;
    mergeDist = reg_int("MergeDist", 1000) / 1e5f;
    bevelW = reg_int("BevelWidth", 10000) / 1e5f;
    if (bevelW <= 0) bevelW = 0.1f;
    recent_load();
    binds_load();

    hFont = CreateFontA(-(int)(12 * ds), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    hHud = CreateFontA(-(int)(13 * ds), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, NONANTIALIASED_QUALITY, 0, "Segoe UI");
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
    wc.style = CS_OWNDC | CS_DBLCLKS;
    wc.lpfnWndProc = ViewProc;
    wc.hbrBackground = NULL;
    wc.lpszClassName = "Tuff3DView";
    RegisterClassExA(&wc);

    hMenu = build_menu();
    menu_keys();
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
    recent_menu();
    DragAcceptFiles(hMain, TRUE);
    SetTimer(hMain, 2, 60 * 1000, NULL);

    WINDOWPLACEMENT pl;
    if (reg_get("Window", REG_BINARY, &pl, sizeof pl) && pl.length == sizeof pl) {
        if (pl.showCmd != SW_SHOWMAXIMIZED) pl.showCmd = SW_SHOWNORMAL;
        SetWindowPlacement(hMain, &pl);
    } else {
        ShowWindow(hMain, show);
    }
    RECT rc;
    GetClientRect(hMain, &rc);
    SendMessage(hMain, WM_SIZE, 0, MAKELPARAM(rc.right, rc.bottom));
    SetFocus(hView);
    refresh_all();
    UpdateWindow(hMain);

    /* a file passed on the command line (Open With, or dropped on the exe), opened once the
       window has painted, like the crash recovery question */
    static char arg[MAX_PATH];
    snprintf(arg, sizeof arg, "%s", cmd[0] == '"' ? cmd + 1 : cmd);
    char *q = strchr(arg, '"');
    if (q) *q = 0;
    for (size_t l = strlen(arg); l && (arg[l - 1] == ' ' || arg[l - 1] == '\t'); ) arg[--l] = 0;
    PostMessage(hMain, WM_APP, 0, (LPARAM)arg);

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
