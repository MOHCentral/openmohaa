/*
 * godot_scoreboard.c — Scoreboard data capture buffer for Godot-side rendering.
 *
 * The engine's own scoreboard pipeline (CG_ParseScores → UI_SetScoreBoardItem)
 * populates a UIListCtrl widget.  Under GODOT_GDEXTENSION, hooks in cl_ui.cpp
 * also copy every item into this buffer so that MoHAARunner.cpp can read the
 * pre-parsed, sorted, and coloured score entries and render them with the
 * RenderingServer 2D API — matching the original OPM appearance exactly.
 */

#include "../qcommon/q_shared.h"

#define GODOT_SB_MAX_ITEMS   64
#define GODOT_SB_MAX_COLUMNS 8
#define GODOT_SB_MAX_STRING  256

/* ── Per-item capture ── */
typedef struct {
    char  strings[8][GODOT_SB_MAX_STRING];
    float textColor[4];
    float backColor[4];
    int   isHeader;
} godot_sb_item_t;

/* ── Per-column definition ── */
typedef struct {
    char name[64];
    int  width;   /* virtual pixels (640×480 coordinate system) */
} godot_sb_column_t;

/* ── State ── */
static int               g_sb_visible      = 0;
static int               g_sb_item_count   = 0;
static godot_sb_item_t   g_sb_items[GODOT_SB_MAX_ITEMS];
static int               g_sb_column_count = 0;
static godot_sb_column_t g_sb_columns[GODOT_SB_MAX_COLUMNS];
static float             g_sb_pos[4]       = { 32.f, 56.f, 384.f, 392.f };
static float             g_sb_bg_color[4]  = { 0.f, 0.f, 0.f, 0.7f };
static float             g_sb_font_color[4]= { 1.f, 1.f, 1.f, 1.f };
static int               g_sb_draw_header  = 0;
static char              g_sb_menu_name[64] = {0};

/* ═══════════════════════════════════════════════════════════════════
 *  Hook functions — called from cl_ui.cpp under #ifdef GODOT_GDEXTENSION
 * ═══════════════════════════════════════════════════════════════════ */

void Godot_SB_SetVisible(int visible)
{
    g_sb_visible = visible;
}

void Godot_SB_SetMenuName(const char *name)
{
    if (name) {
        Q_strncpyz(g_sb_menu_name, name, sizeof(g_sb_menu_name));
    } else {
        g_sb_menu_name[0] = '\0';
    }
}

void Godot_SB_ResetColumns(void)
{
    g_sb_column_count = 0;
}

void Godot_SB_SetColumn(int index, const char *name, int width)
{
    if (index < 0 || index >= GODOT_SB_MAX_COLUMNS) return;
    Q_strncpyz(g_sb_columns[index].name, name, sizeof(g_sb_columns[index].name));
    g_sb_columns[index].width = width;
    if (index + 1 > g_sb_column_count)
        g_sb_column_count = index + 1;
}

void Godot_SB_SetLayout(float x, float y, float w, float h,
                         float bgR, float bgG, float bgB, float bgA,
                         float fR, float fG, float fB, float fA,
                         int drawHeader)
{
    g_sb_pos[0] = x; g_sb_pos[1] = y; g_sb_pos[2] = w; g_sb_pos[3] = h;
    g_sb_bg_color[0]  = bgR; g_sb_bg_color[1]  = bgG;
    g_sb_bg_color[2]  = bgB; g_sb_bg_color[3]  = bgA;
    g_sb_font_color[0]= fR;  g_sb_font_color[1]= fG;
    g_sb_font_color[2]= fB;  g_sb_font_color[3]= fA;
    g_sb_draw_header   = drawHeader;
}

void Godot_SB_SetItem(int index,
                      const char *s1, const char *s2,
                      const char *s3, const char *s4,
                      const char *s5, const char *s6,
                      const char *s7, const char *s8,
                      const float *textColor, const float *backColor,
                      int isHeader)
{
    if (index < 0 || index >= GODOT_SB_MAX_ITEMS) return;

    Q_strncpyz(g_sb_items[index].strings[0], s1 ? s1 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[1], s2 ? s2 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[2], s3 ? s3 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[3], s4 ? s4 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[4], s5 ? s5 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[5], s6 ? s6 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[6], s7 ? s7 : "", GODOT_SB_MAX_STRING);
    Q_strncpyz(g_sb_items[index].strings[7], s8 ? s8 : "", GODOT_SB_MAX_STRING);

    if (textColor) {
        g_sb_items[index].textColor[0] = textColor[0];
        g_sb_items[index].textColor[1] = textColor[1];
        g_sb_items[index].textColor[2] = textColor[2];
        g_sb_items[index].textColor[3] = textColor[3];
    } else {
        g_sb_items[index].textColor[0] = 1.f;
        g_sb_items[index].textColor[1] = 1.f;
        g_sb_items[index].textColor[2] = 1.f;
        g_sb_items[index].textColor[3] = 1.f;
    }

    if (backColor) {
        g_sb_items[index].backColor[0] = backColor[0];
        g_sb_items[index].backColor[1] = backColor[1];
        g_sb_items[index].backColor[2] = backColor[2];
        g_sb_items[index].backColor[3] = backColor[3];
    } else {
        g_sb_items[index].backColor[0] = 0.f;
        g_sb_items[index].backColor[1] = 0.f;
        g_sb_items[index].backColor[2] = 0.f;
        g_sb_items[index].backColor[3] = 0.f;
    }

    g_sb_items[index].isHeader = isHeader;

    if (index + 1 > g_sb_item_count)
        g_sb_item_count = index + 1;
}

void Godot_SB_DeleteItemsAfter(int maxIndex)
{
    if (maxIndex < g_sb_item_count)
        g_sb_item_count = maxIndex;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Accessor functions — called from MoHAARunner.cpp via extern "C"
 * ═══════════════════════════════════════════════════════════════════ */

int Godot_SB_IsVisible(void)
{
    return g_sb_visible;
}

int Godot_SB_GetItemCount(void)
{
    return g_sb_item_count;
}

int Godot_SB_GetColumnCount(void)
{
    return g_sb_column_count;
}

const char *Godot_SB_GetItemString(int item, int field)
{
    if (item < 0 || item >= g_sb_item_count) return "";
    if (field < 0 || field >= 8) return "";
    return g_sb_items[item].strings[field];
}

void Godot_SB_GetItemTextColor(int item, float *r, float *g, float *b, float *a)
{
    if (item < 0 || item >= g_sb_item_count) {
        *r = 1.f; *g = 1.f; *b = 1.f; *a = 1.f;
        return;
    }
    *r = g_sb_items[item].textColor[0];
    *g = g_sb_items[item].textColor[1];
    *b = g_sb_items[item].textColor[2];
    *a = g_sb_items[item].textColor[3];
}

void Godot_SB_GetItemBackColor(int item, float *r, float *g, float *b, float *a)
{
    if (item < 0 || item >= g_sb_item_count) {
        *r = 0.f; *g = 0.f; *b = 0.f; *a = 0.f;
        return;
    }
    *r = g_sb_items[item].backColor[0];
    *g = g_sb_items[item].backColor[1];
    *b = g_sb_items[item].backColor[2];
    *a = g_sb_items[item].backColor[3];
}

int Godot_SB_GetItemIsHeader(int item)
{
    if (item < 0 || item >= g_sb_item_count) return 0;
    return g_sb_items[item].isHeader;
}

const char *Godot_SB_GetColumnName(int col)
{
    if (col < 0 || col >= g_sb_column_count) return "";
    return g_sb_columns[col].name;
}

int Godot_SB_GetColumnWidth(int col)
{
    if (col < 0 || col >= g_sb_column_count) return 0;
    return g_sb_columns[col].width;
}

void Godot_SB_GetPosition(float *x, float *y, float *w, float *h)
{
    *x = g_sb_pos[0]; *y = g_sb_pos[1];
    *w = g_sb_pos[2]; *h = g_sb_pos[3];
}

void Godot_SB_GetBGColor(float *r, float *g, float *b, float *a)
{
    *r = g_sb_bg_color[0]; *g = g_sb_bg_color[1];
    *b = g_sb_bg_color[2]; *a = g_sb_bg_color[3];
}

void Godot_SB_GetFontColor(float *r, float *g, float *b, float *a)
{
    *r = g_sb_font_color[0]; *g = g_sb_font_color[1];
    *b = g_sb_font_color[2]; *a = g_sb_font_color[3];
}

int Godot_SB_GetDrawHeader(void)
{
    return g_sb_draw_header;
}

const char *Godot_SB_GetMenuName(void)
{
    return g_sb_menu_name;
}
