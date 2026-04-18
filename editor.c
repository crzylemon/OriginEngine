/*
 * editor.c — Origin Engine Map Editor (Hammer-style 4-view)
 *
 * GTK3 + Cairo for 2D grid views, with a 3D wireframe preview.
 * Layout: 2x2 grid — Top(XY), Front(XZ), Side(YZ), 3D perspective.
 *
 * Usage: ./origin_editor --game <folder> [map.oem]
 *
 * Controls:
 *   2D views:
 *     LMB drag        = select / create brush (drag rectangle)
 *     RMB drag        = pan view
 *     Scroll           = zoom
 *     Shift+LMB drag  = move selected
 *
 *   3D view:
 *     RMB drag        = orbit camera
 *     Scroll           = zoom
 *
 *   Global:
 *     B               = brush tool
 *     S               = select tool
 *     E               = entity tool
 *     G               = cycle grid (8/16/32/64/128)
 *     X / Delete      = delete selected
 *     T               = texture dialog
 *     Ctrl+S          = save
 *     Ctrl+Z          = undo
 *     Ctrl+Y          = redo
 *     Ctrl+D          = duplicate
 */
#include <gtk/gtk.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Engine includes (for data structures only) ──────────────── */
#include "engine/vec3.h"
#include "engine/brush.h"
#include "engine/entity.h"
#include "engine/brush_entity.h"
#include "engine/map_format.h"
#include "engine/entity_io.h"
#include "engine/trigger.h"
#include "engine/console.h"
#include "engine/texture.h"

/* ── Constants ───────────────────────────────────────────────── */
#define GRID_SIZES_COUNT 5
static const int GRID_SIZES[GRID_SIZES_COUNT] = {8, 16, 32, 64, 128};

static float fminf2(float a, float b) { return a < b ? a : b; }
static float fmaxf2(float a, float b) { return a > b ? a : b; }

/* View types */
#define VIEW_TOP   0  /* XY plane, looking down Z */
#define VIEW_FRONT 1  /* XZ plane, looking down Y */
#define VIEW_SIDE  2  /* YZ plane, looking down X */
#define VIEW_3D    3

/* Tools */
#define TOOL_SELECT 0
#define TOOL_BRUSH  1
#define TOOL_ENTITY 2
#define TOOL_CLIP   3

/* Gizmo modes */
#define GIZMO_MOVE   0
#define GIZMO_RESIZE 1
#define GIZMO_ROTATE 2
#define GIZMO_COUNT  3

/* ── Editor State ────────────────────────────────────────────── */
static int    g_grid_idx = 2;  /* index into GRID_SIZES, default 32 */
static int    g_grid = 32;
static int    g_tool = TOOL_SELECT;
static int    g_gizmo = GIZMO_MOVE;
static int    g_selected_brush = -1;
static int    g_selected_entity = -1;
static int    g_selected_face = -1;
static int    g_dirty = 0;
static char   g_map_path[512] = "";
static char   g_tex_folder[512] = "game/textures";
static char   g_status[256] = "Ready";
static const char* g_current_texture = "concrete";

/* Texture list */
static const char* g_tex_names[64];
static int         g_tex_count = 0;

/* Entity classes */
static const char* g_ent_classes[] = {
    "info_player_start", "func_door", "func_button",
    "trigger_once", "trigger_multiple", "trigger_hurt",
    "logic_relay", "logic_auto", "light", "npc_citizen",
};
#define ENT_CLASS_COUNT 10
static int g_ent_class_idx = 0;

/* Per-view state */
typedef struct {
    int    type;          /* VIEW_TOP, VIEW_FRONT, VIEW_SIDE, VIEW_3D */
    double pan_x, pan_y;  /* pan offset in pixels */
    double zoom;          /* pixels per game unit */
    /* Interaction state */
    int    dragging;       /* 0=none, 1=pan(RMB), 2=create(LMB), 3=move(shift+LMB) */
    double drag_start_x, drag_start_y;  /* mouse start in widget coords */
    double drag_world_x, drag_world_y;  /* world coords at drag start */
    /* For brush creation */
    double create_x0, create_y0;  /* world coords of first corner */
    double create_x1, create_y1;  /* world coords of current corner */
    /* For resize: which edge/corner is being dragged */
    int    resize_edge;  /* bitmask: 1=min_a, 2=max_a, 4=min_b, 8=max_b */
    double resize_orig_min_a, resize_orig_max_a;
    double resize_orig_min_b, resize_orig_max_b;
    /* 3D camera */
    float  cam_yaw, cam_pitch, cam_dist;
    float  cam_target[3];
    /* Widget */
    GtkWidget* drawing_area;
} ViewState;

static ViewState g_views[4];

/* Undo */
#define MAX_UNDO 64
typedef struct {
    int type; /* 0=create_brush, 1=delete_brush, 2=move_brush, 3=move_entity */
    int brush_idx;
    int entity_id;
    Vec3 old_mins, old_maxs, new_mins, new_maxs;
    Vec3 old_origin, new_origin;
    char old_tex[64], new_tex[64];
    int face_idx;
} UndoEntry;
static UndoEntry g_undo[MAX_UNDO];
static int g_undo_pos = 0, g_undo_count = 0;

/* GTK widgets */
static GtkWidget* g_window;
static GtkWidget* g_statusbar;
static GtkWidget* g_toolbar;
static GtkWidget* g_tex_combo;

/* ── Coordinate conversion ───────────────────────────────────── */

/* Widget pixel -> world coordinate for a 2D view */
static void widget_to_world(ViewState* v, double wx, double wy,
                             double* out_a, double* out_b) {
    /* Widget center is the origin of the pan */
    int w = gtk_widget_get_allocated_width(v->drawing_area);
    int h = gtk_widget_get_allocated_height(v->drawing_area);
    *out_a = (wx - w * 0.5 - v->pan_x) / v->zoom;
    *out_b = -(wy - h * 0.5 - v->pan_y) / v->zoom; /* Y flipped in screen */
}

/* World coordinate -> widget pixel */
static void world_to_widget(ViewState* v, double a, double b,
                             double* out_wx, double* out_wy) {
    int w = gtk_widget_get_allocated_width(v->drawing_area);
    int h = gtk_widget_get_allocated_height(v->drawing_area);
    *out_wx = a * v->zoom + w * 0.5 + v->pan_x;
    *out_wy = -b * v->zoom + h * 0.5 + v->pan_y;
}

/* Snap a value to grid */
static double snap_to_grid(double v) {
    return floor(v / g_grid + 0.5) * g_grid;
}

/* Get the two world axes for a 2D view type */
static void view_axes(int type, int* ax_a, int* ax_b) {
    switch (type) {
    case VIEW_TOP:   *ax_a = 0; *ax_b = 1; break; /* X, Y */
    case VIEW_FRONT: *ax_a = 0; *ax_b = 2; break; /* X, Z */
    case VIEW_SIDE:  *ax_a = 1; *ax_b = 2; break; /* Y, Z */
    default:         *ax_a = 0; *ax_b = 1; break;
    }
}

static double brush_axis(Vec3 v, int axis) {
    if (axis == 0) return v.x;
    if (axis == 1) return v.y;
    return v.z;
}

/* ── Undo system ─────────────────────────────────────────────── */

static void undo_push(UndoEntry e) {
    if (g_undo_pos < MAX_UNDO) {
        g_undo[g_undo_pos++] = e;
        g_undo_count = g_undo_pos;
    }
}

static void do_undo(void) {
    if (g_undo_pos <= 0) return;
    g_undo_pos--;
    UndoEntry* u = &g_undo[g_undo_pos];
    int bc; Brush** bs = brush_get_all(&bc);
    switch (u->type) {
    case 0: /* undo create brush — zero it out */
        if (u->brush_idx >= 0 && u->brush_idx < bc && bs[u->brush_idx]) {
            bs[u->brush_idx]->face_count = 0;
            bs[u->brush_idx]->mins = bs[u->brush_idx]->maxs = VEC3_ZERO;
            bs[u->brush_idx]->solid = 0;
        }
        break;
    case 2: { /* undo move brush */
        if (u->brush_idx >= 0 && u->brush_idx < bc && bs[u->brush_idx]) {
            Brush* b = bs[u->brush_idx];
            Vec3 delta = vec3_sub(u->old_mins, b->mins);
            for (int f = 0; f < b->face_count; f++)
                for (int v = 0; v < b->faces[f].vertex_count; v++)
                    b->faces[f].vertices[v] = vec3_add(b->faces[f].vertices[v], delta);
            brush_recompute_bounds(b);
        }
        break;
    }
    case 3: { /* undo move entity */
        Entity* e = entity_get(u->entity_id);
        if (e) e->origin = u->old_origin;
        break;
    }
    }
    g_dirty = 1;
    snprintf(g_status, sizeof(g_status), "Undo");
    /* Redraw all views */
    for (int i = 0; i < 4; i++)
        if (g_views[i].drawing_area)
            gtk_widget_queue_draw(g_views[i].drawing_area);
}

static void do_redo(void) {
    if (g_undo_pos >= g_undo_count) return;
    UndoEntry* u = &g_undo[g_undo_pos];
    g_undo_pos++;
    int bc; Brush** bs = brush_get_all(&bc);
    switch (u->type) {
    case 2: {
        if (u->brush_idx >= 0 && u->brush_idx < bc && bs[u->brush_idx]) {
            Brush* b = bs[u->brush_idx];
            Vec3 delta = vec3_sub(u->new_mins, b->mins);
            for (int f = 0; f < b->face_count; f++)
                for (int v = 0; v < b->faces[f].vertex_count; v++)
                    b->faces[f].vertices[v] = vec3_add(b->faces[f].vertices[v], delta);
            brush_recompute_bounds(b);
        }
        break;
    }
    case 3: {
        Entity* e = entity_get(u->entity_id);
        if (e) e->origin = u->new_origin;
        break;
    }
    }
    g_dirty = 1;
    for (int i = 0; i < 4; i++)
        if (g_views[i].drawing_area)
            gtk_widget_queue_draw(g_views[i].drawing_area);
}

static void redraw_all(void) {
    for (int i = 0; i < 4; i++)
        if (g_views[i].drawing_area)
            gtk_widget_queue_draw(g_views[i].drawing_area);
}

static void update_status(const char* msg) {
    strncpy(g_status, msg, sizeof(g_status)-1);
    if (g_statusbar)
        gtk_statusbar_push(GTK_STATUSBAR(g_statusbar), 0, g_status);
}

/* ── Save/Load ───────────────────────────────────────────────── */

static void save_map(void) {
    if (g_map_path[0] == '\0') return;
    MapInfo info;
    strcpy(info.title, "Editor Map");
    strcpy(info.description, "Created with Origin Editor");
    if (map_save(g_map_path, &info)) {
        char msg[300];
        snprintf(msg, sizeof(msg), "Saved: %s", g_map_path);
        update_status(msg);
        g_dirty = 0;
    } else {
        update_status("SAVE FAILED!");
    }
}

/* ── Brush operations ────────────────────────────────────────── */

static void move_brush(int idx, Vec3 delta) {
    int bc; Brush** bs = brush_get_all(&bc);
    if (idx < 0 || idx >= bc || !bs[idx]) return;
    Brush* b = bs[idx];
    for (int f = 0; f < b->face_count; f++)
        for (int v = 0; v < b->faces[f].vertex_count; v++)
            b->faces[f].vertices[v] = vec3_add(b->faces[f].vertices[v], delta);
    brush_recompute_bounds(b);
}

/* Resize an AABB brush to new mins/maxs, rebuilding face vertices */
static void resize_brush(int idx, Vec3 new_mins, Vec3 new_maxs) {
    int bc; Brush** bs = brush_get_all(&bc);
    if (idx < 0 || idx >= bc || !bs[idx]) return;
    Brush* b = bs[idx];
    if (b->face_count != 6) return; /* only works for AABB brushes */

    float x0 = fminf2(new_mins.x, new_maxs.x);
    float y0 = fminf2(new_mins.y, new_maxs.y);
    float z0 = fminf2(new_mins.z, new_maxs.z);
    float x1 = fmaxf2(new_mins.x, new_maxs.x);
    float y1 = fmaxf2(new_mins.y, new_maxs.y);
    float z1 = fmaxf2(new_mins.z, new_maxs.z);

    Vec3 fv[6][4] = {
        {{x0,y1,z0},{x1,y1,z0},{x1,y0,z0},{x0,y0,z0}},
        {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}},
        {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}},
        {{x1,y1,z0},{x0,y1,z0},{x0,y1,z1},{x1,y1,z1}},
        {{x0,y1,z0},{x0,y0,z0},{x0,y0,z1},{x0,y1,z1}},
        {{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}},
    };
    for (int f = 0; f < 6; f++) {
        b->faces[f].vertex_count = 4;
        for (int vi = 0; vi < 4; vi++)
            b->faces[f].vertices[vi] = fv[f][vi];
    }
    b->mins = VEC3(x0, y0, z0);
    b->maxs = VEC3(x1, y1, z1);
}

static void delete_selected(void) {
    if (g_selected_brush >= 0) {
        int bc; Brush** bs = brush_get_all(&bc);
        if (g_selected_brush < bc && bs[g_selected_brush]) {
            UndoEntry ue = {0};
            ue.type = 1; /* delete */
            ue.brush_idx = g_selected_brush;
            ue.old_mins = bs[g_selected_brush]->mins;
            ue.old_maxs = bs[g_selected_brush]->maxs;
            undo_push(ue);
            bs[g_selected_brush]->face_count = 0;
            bs[g_selected_brush]->mins = bs[g_selected_brush]->maxs = VEC3_ZERO;
            bs[g_selected_brush]->solid = 0;
            g_selected_brush = -1;
            g_dirty = 1;
            update_status("Deleted brush");
        }
    } else if (g_selected_entity >= 0) {
        Entity* e = entity_get(g_selected_entity);
        if (e) { e->flags |= EF_DEAD; g_selected_entity = -1; g_dirty = 1; }
        update_status("Deleted entity");
    }
    redraw_all();
}

static void duplicate_selected(void) {
    if (g_selected_brush < 0) return;
    int bc; Brush** bs = brush_get_all(&bc);
    if (g_selected_brush >= bc || !bs[g_selected_brush]) return;
    Brush* src = bs[g_selected_brush];
    Vec3 off = VEC3((float)g_grid, 0, 0);
    Brush* nb = brush_create(vec3_add(src->mins, off), vec3_add(src->maxs, off),
                              src->faces[0].texture, src->solid);
    if (!nb) return;
    for (int f = 0; f < src->face_count && f < nb->face_count; f++) {
        brush_set_face_texture(nb, f, src->faces[f].texture);
        brush_set_face_scale(nb, f, src->faces[f].scale_u, src->faces[f].scale_v);
        brush_set_face_offset(nb, f, src->faces[f].offset_u, src->faces[f].offset_v);
    }
    g_selected_brush = brush_count() - 1;
    UndoEntry ue = {0}; ue.type = 0; ue.brush_idx = g_selected_brush;
    undo_push(ue);
    g_dirty = 1;
    update_status("Duplicated brush");
    redraw_all();
}

/* ── 2D View Drawing (Cairo) ─────────────────────────────────── */

static void draw_grid(cairo_t* cr, ViewState* v, int width, int height) {
    /* Determine visible world range */
    double w0, h0, w1, h1;
    widget_to_world(v, 0, 0, &w0, &h1);
    widget_to_world(v, width, height, &w1, &h0);

    /* Draw grid lines */
    double grid = (double)g_grid;

    /* If zoomed out too far, use larger grid */
    double vis_grid = grid;
    while (vis_grid * v->zoom < 8) vis_grid *= 4;

    /* Minor grid */
    cairo_set_source_rgba(cr, 0.15, 0.15, 0.15, 1);
    cairo_set_line_width(cr, 0.5);
    double start_a = floor(w0 / vis_grid) * vis_grid;
    double start_b = floor(h0 / vis_grid) * vis_grid;
    for (double a = start_a; a <= w1; a += vis_grid) {
        double sx, sy, ex, ey;
        world_to_widget(v, a, h0, &sx, &sy);
        world_to_widget(v, a, h1, &ex, &ey);
        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
    }
    for (double b = start_b; b <= h1; b += vis_grid) {
        double sx, sy, ex, ey;
        world_to_widget(v, w0, b, &sx, &sy);
        world_to_widget(v, w1, b, &ex, &ey);
        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
    }
    cairo_stroke(cr);

    /* Major grid (every 4x) */
    double major = vis_grid * 4;
    cairo_set_source_rgba(cr, 0.25, 0.25, 0.25, 1);
    cairo_set_line_width(cr, 1.0);
    start_a = floor(w0 / major) * major;
    start_b = floor(h0 / major) * major;
    for (double a = start_a; a <= w1; a += major) {
        double sx, sy, ex, ey;
        world_to_widget(v, a, h0, &sx, &sy);
        world_to_widget(v, a, h1, &ex, &ey);
        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
    }
    for (double b = start_b; b <= h1; b += major) {
        double sx, sy, ex, ey;
        world_to_widget(v, w0, b, &sx, &sy);
        world_to_widget(v, w1, b, &ex, &ey);
        cairo_move_to(cr, sx, sy);
        cairo_line_to(cr, ex, ey);
    }
    cairo_stroke(cr);

    /* Origin axes */
    cairo_set_line_width(cr, 1.5);
    /* Horizontal axis */
    double ox1, oy1, ox2, oy2;
    world_to_widget(v, w0, 0, &ox1, &oy1);
    world_to_widget(v, w1, 0, &ox2, &oy2);
    cairo_set_source_rgba(cr, 0.4, 0.0, 0.0, 1); /* red for horizontal */
    cairo_move_to(cr, ox1, oy1);
    cairo_line_to(cr, ox2, oy2);
    cairo_stroke(cr);
    /* Vertical axis */
    world_to_widget(v, 0, h0, &ox1, &oy1);
    world_to_widget(v, 0, h1, &ox2, &oy2);
    cairo_set_source_rgba(cr, 0.0, 0.4, 0.0, 1); /* green for vertical */
    cairo_move_to(cr, ox1, oy1);
    cairo_line_to(cr, ox2, oy2);
    cairo_stroke(cr);
}

static void draw_brush_2d(cairo_t* cr, ViewState* v, Brush* b, int idx) {
    if (!b || b->face_count == 0) return;
    int ax_a, ax_b;
    view_axes(v->type, &ax_a, &ax_b);

    double mn_a = brush_axis(b->mins, ax_a);
    double mn_b = brush_axis(b->mins, ax_b);
    double mx_a = brush_axis(b->maxs, ax_a);
    double mx_b = brush_axis(b->maxs, ax_b);

    double x0, y0, x1, y1;
    world_to_widget(v, mn_a, mn_b, &x0, &y0);
    world_to_widget(v, mx_a, mx_b, &x1, &y1);

    /* Normalize */
    if (x0 > x1) { double t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { double t = y0; y0 = y1; y1 = t; }

    /* Fill */
    if (idx == g_selected_brush) {
        cairo_set_source_rgba(cr, 1.0, 0.3, 0.3, 0.15);
    } else if (b->entity_owned) {
        cairo_set_source_rgba(cr, 0.2, 0.5, 0.8, 0.1);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.05);
    }
    cairo_rectangle(cr, x0, y0, x1-x0, y1-y0);
    cairo_fill(cr);

    /* Outline */
    if (idx == g_selected_brush) {
        cairo_set_source_rgba(cr, 1.0, 0.3, 0.3, 1.0);
        cairo_set_line_width(cr, 2.0);
    } else if (b->entity_owned) {
        cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.7);
        cairo_set_line_width(cr, 1.0);
    } else if (!b->solid) {
        cairo_set_source_rgba(cr, 0.5, 0.5, 0.0, 0.5);
        cairo_set_line_width(cr, 1.0);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.6);
        cairo_set_line_width(cr, 1.0);
    }
    cairo_rectangle(cr, x0, y0, x1-x0, y1-y0);
    cairo_stroke(cr);
}

static void draw_entity_2d(cairo_t* cr, ViewState* v, Entity* e) {
    if (!e || (e->flags & EF_DEAD)) return;
    int ax_a, ax_b;
    view_axes(v->type, &ax_a, &ax_b);

    double a = brush_axis(e->origin, ax_a);
    double b = brush_axis(e->origin, ax_b);
    double wx, wy;
    world_to_widget(v, a, b, &wx, &wy);

    double sz = 6;
    int selected = (e->id == g_selected_entity);

    /* Diamond shape for entities */
    if (selected) {
        cairo_set_source_rgba(cr, 0.3, 1.0, 0.3, 1.0);
        sz = 8;
    } else if (strncmp(e->classname, "info_player", 11) == 0) {
        cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.8);
    } else if (strncmp(e->classname, "trigger_", 8) == 0) {
        cairo_set_source_rgba(cr, 1.0, 0.5, 0.0, 0.8);
    } else if (strncmp(e->classname, "func_", 5) == 0) {
        cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.8);
    } else {
        cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.8);
    }

    cairo_move_to(cr, wx, wy - sz);
    cairo_line_to(cr, wx + sz, wy);
    cairo_line_to(cr, wx, wy + sz);
    cairo_line_to(cr, wx - sz, wy);
    cairo_close_path(cr);
    cairo_fill_preserve(cr);
    cairo_set_line_width(cr, selected ? 2.0 : 1.0);
    cairo_stroke(cr);

    /* Label */
    cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.7);
    cairo_set_font_size(cr, 10);
    const char* label = e->targetname[0] ? e->targetname : e->classname;
    cairo_move_to(cr, wx + sz + 3, wy + 3);
    cairo_show_text(cr, label);
}

static void draw_creation_preview(cairo_t* cr, ViewState* v) {
    if (g_tool != TOOL_BRUSH || v->dragging != 2) return;

    double x0, y0, x1, y1;
    world_to_widget(v, v->create_x0, v->create_y0, &x0, &y0);
    world_to_widget(v, v->create_x1, v->create_y1, &x1, &y1);
    if (x0 > x1) { double t = x0; x0 = x1; x1 = t; }
    if (y0 > y1) { double t = y0; y0 = y1; y1 = t; }

    /* Dashed outline */
    cairo_set_source_rgba(cr, 0.0, 1.0, 0.0, 0.8);
    cairo_set_line_width(cr, 2.0);
    double dashes[] = {6, 4};
    cairo_set_dash(cr, dashes, 2, 0);
    cairo_rectangle(cr, x0, y0, x1-x0, y1-y0);
    cairo_stroke(cr);
    cairo_set_dash(cr, NULL, 0, 0);

    /* Size label */
    double wa = fabs(v->create_x1 - v->create_x0);
    double wb = fabs(v->create_y1 - v->create_y0);
    char sz[64];
    snprintf(sz, sizeof(sz), "%.0f x %.0f", wa, wb);
    cairo_set_source_rgba(cr, 0, 1, 0, 1);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, (x0+x1)/2 - 20, (y0+y1)/2 + 4);
    cairo_show_text(cr, sz);
}

/* Selection gizmo: visual handles on selected brush */
static void draw_selection_gizmo(cairo_t* cr, ViewState* v) {
    if (g_selected_brush < 0) return;
    int bc; Brush** bs = brush_get_all(&bc);
    if (g_selected_brush >= bc || !bs[g_selected_brush]) return;
    Brush* b = bs[g_selected_brush];
    if (b->face_count == 0) return;

    int ax_a, ax_b;
    view_axes(v->type, &ax_a, &ax_b);
    double mn_a = brush_axis(b->mins, ax_a), mx_a = brush_axis(b->maxs, ax_a);
    double mn_b = brush_axis(b->mins, ax_b), mx_b = brush_axis(b->maxs, ax_b);
    double ca = (mn_a + mx_a) * 0.5, cb = (mn_b + mx_b) * 0.5;
    double wx, wy;
    world_to_widget(v, ca, cb, &wx, &wy);

    if (g_gizmo == GIZMO_MOVE) {
        /* Move gizmo: arrows from center */
        double len = 25;
        /* Horizontal arrow (axis A — red) */
        cairo_set_source_rgba(cr, 1, 0.2, 0.2, 0.9);
        cairo_set_line_width(cr, 2.5);
        cairo_move_to(cr, wx, wy);
        cairo_line_to(cr, wx + len, wy);
        cairo_stroke(cr);
        cairo_move_to(cr, wx + len, wy);
        cairo_line_to(cr, wx + len - 6, wy - 4);
        cairo_line_to(cr, wx + len - 6, wy + 4);
        cairo_close_path(cr); cairo_fill(cr);

        /* Vertical arrow (axis B — green) */
        cairo_set_source_rgba(cr, 0.2, 1, 0.2, 0.9);
        cairo_move_to(cr, wx, wy);
        cairo_line_to(cr, wx, wy - len);
        cairo_stroke(cr);
        cairo_move_to(cr, wx, wy - len);
        cairo_line_to(cr, wx - 4, wy - len + 6);
        cairo_line_to(cr, wx + 4, wy - len + 6);
        cairo_close_path(cr); cairo_fill(cr);

        /* Center square (both axes — yellow) */
        cairo_set_source_rgba(cr, 1, 1, 0.2, 0.4);
        cairo_rectangle(cr, wx, wy - 8, 8, 8);
        cairo_fill(cr);
    } else if (g_gizmo == GIZMO_RESIZE) {
        /* Resize gizmo: squares on edges and corners */
        double x0w, y0w, x1w, y1w;
        world_to_widget(v, mn_a, mn_b, &x0w, &y0w);
        world_to_widget(v, mx_a, mx_b, &x1w, &y1w);
        if (x0w > x1w) { double t=x0w; x0w=x1w; x1w=t; }
        if (y0w > y1w) { double t=y0w; y0w=y1w; y1w=t; }
        double hs = 4; /* handle half-size */
        cairo_set_source_rgba(cr, 0.2, 0.6, 1, 0.9);
        cairo_set_line_width(cr, 1.5);
        /* Corner handles */
        double cx[] = {x0w, x1w, x0w, x1w};
        double cy[] = {y0w, y0w, y1w, y1w};
        for (int i = 0; i < 4; i++) {
            cairo_rectangle(cr, cx[i]-hs, cy[i]-hs, hs*2, hs*2);
            cairo_fill(cr);
        }
        /* Edge midpoint handles */
        double ex[] = {(x0w+x1w)/2, (x0w+x1w)/2, x0w, x1w};
        double ey[] = {y0w, y1w, (y0w+y1w)/2, (y0w+y1w)/2};
        cairo_set_source_rgba(cr, 0.2, 0.6, 1, 0.6);
        for (int i = 0; i < 4; i++) {
            cairo_rectangle(cr, ex[i]-hs, ey[i]-hs, hs*2, hs*2);
            cairo_fill(cr);
        }
    } else if (g_gizmo == GIZMO_ROTATE) {
        /* Rotate gizmo: circle around center */
        double radius = 30;
        cairo_set_source_rgba(cr, 1, 0.5, 0, 0.7);
        cairo_set_line_width(cr, 2);
        cairo_arc(cr, wx, wy, radius, 0, 2 * M_PI);
        cairo_stroke(cr);
        /* Tick mark at top */
        cairo_set_source_rgba(cr, 1, 0.5, 0, 0.9);
        cairo_move_to(cr, wx, wy - radius - 5);
        cairo_line_to(cr, wx - 4, wy - radius + 2);
        cairo_line_to(cr, wx + 4, wy - radius + 2);
        cairo_close_path(cr); cairo_fill(cr);
    }
}

/* ── 3D View Drawing ─────────────────────────────────────────── */

static void project_3d(ViewState* v, float wx, float wy, float wz,
                        int vw, int vh, double* sx, double* sy) {
    /* Simple perspective projection from orbiting camera */
    float cy = cosf(v->cam_yaw), sy2 = sinf(v->cam_yaw);
    float cp = cosf(v->cam_pitch), sp = sinf(v->cam_pitch);

    float cam_x = v->cam_target[0] + v->cam_dist * cp * cy;
    float cam_y = v->cam_target[1] + v->cam_dist * cp * sy2;
    float cam_z = v->cam_target[2] + v->cam_dist * sp;

    /* View direction */
    float dx = wx - cam_x, dy = wy - cam_y, dz = wz - cam_z;

    /* Camera basis: forward, right, up (Z-up coordinate system) */
    float fx = -cp * cy, fy = -cp * sy2, fz = -sp;
    float rx = -sy2, ry = cy, rz = 0;
    float ux = -sp * cy, uy = -sp * sy2, uz = cp;

    float depth = dx*fx + dy*fy + dz*fz;
    if (depth < 1.0f) { *sx = -9999; *sy = -9999; return; }

    float screen_r = dx*rx + dy*ry + dz*rz;
    float screen_u = dx*ux + dy*uy + dz*uz;

    float fov_scale = (float)vw * 0.8f;
    *sx = vw * 0.5 + (screen_r / depth) * fov_scale;
    *sy = vh * 0.5 - (screen_u / depth) * fov_scale;
}

static void draw_line_3d(cairo_t* cr, ViewState* v, int vw, int vh,
                          float x0, float y0, float z0,
                          float x1, float y1, float z1) {
    double sx0, sy0, sx1, sy1;
    project_3d(v, x0, y0, z0, vw, vh, &sx0, &sy0);
    project_3d(v, x1, y1, z1, vw, vh, &sx1, &sy1);
    if (sx0 < -9000 || sx1 < -9000) return;
    cairo_move_to(cr, sx0, sy0);
    cairo_line_to(cr, sx1, sy1);
}

static void draw_brush_3d(cairo_t* cr, ViewState* v, int vw, int vh,
                           Brush* b, int idx) {
    if (!b || b->face_count == 0) return;

    if (idx == g_selected_brush) {
        cairo_set_source_rgba(cr, 1, 0.3, 0.3, 0.9);
        cairo_set_line_width(cr, 2.0);
    } else if (b->entity_owned) {
        cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.6);
        cairo_set_line_width(cr, 1.0);
    } else {
        cairo_set_source_rgba(cr, 0.8, 0.8, 0.8, 0.5);
        cairo_set_line_width(cr, 1.0);
    }

    for (int f = 0; f < b->face_count; f++) {
        BrushFace* bf = &b->faces[f];
        for (int i = 0; i < bf->vertex_count; i++) {
            Vec3 a = bf->vertices[i];
            Vec3 bn = bf->vertices[(i+1) % bf->vertex_count];
            draw_line_3d(cr, v, vw, vh, a.x, a.y, a.z, bn.x, bn.y, bn.z);
        }
    }
    cairo_stroke(cr);
}

static void draw_3d_view(cairo_t* cr, ViewState* v, int width, int height) {
    /* Background */
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.12);
    cairo_paint(cr);

    /* Origin axes */
    cairo_set_line_width(cr, 1.5);
    cairo_set_source_rgba(cr, 0.8, 0.0, 0.0, 0.6);
    draw_line_3d(cr, v, width, height, 0,0,0, 100,0,0);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 0.0, 0.8, 0.0, 0.6);
    draw_line_3d(cr, v, width, height, 0,0,0, 0,100,0);
    cairo_stroke(cr);
    cairo_set_source_rgba(cr, 0.0, 0.0, 0.8, 0.6);
    draw_line_3d(cr, v, width, height, 0,0,0, 0,0,100);
    cairo_stroke(cr);

    /* Brushes */
    int bc; Brush** bs = brush_get_all(&bc);
    for (int i = 0; i < bc; i++)
        draw_brush_3d(cr, v, width, height, bs[i], i);

    /* Entities */
    int ec; Entity** ents = entity_get_all(&ec);
    for (int i = 0; i < ec; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        BrushEntData* bd = brush_entity_data(e);
        if (bd) {
            Vec3 off = vec3_sub(e->origin, bd->spawn_origin);
            for (int j = 0; j < bd->brush_count; j++) {
                /* Temporarily offset brush for drawing */
                Brush* b = bd->brushes[j];
                if (!b) continue;
                /* Draw with offset */
                if (e->id == g_selected_entity)
                    cairo_set_source_rgba(cr, 0.3, 1.0, 0.3, 0.8);
                else
                    cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.5);
                cairo_set_line_width(cr, 1.0);
                for (int f = 0; f < b->face_count; f++) {
                    BrushFace* bf = &b->faces[f];
                    for (int vi = 0; vi < bf->vertex_count; vi++) {
                        Vec3 a = vec3_add(bf->vertices[vi], off);
                        Vec3 bn = vec3_add(bf->vertices[(vi+1) % bf->vertex_count], off);
                        draw_line_3d(cr, v, width, height, a.x,a.y,a.z, bn.x,bn.y,bn.z);
                    }
                }
                cairo_stroke(cr);
            }
        } else {
            /* Point entity — draw as small cross */
            float x = e->origin.x, y = e->origin.y, z = e->origin.z;
            if (e->id == g_selected_entity)
                cairo_set_source_rgba(cr, 0.3, 1.0, 0.3, 1.0);
            else
                cairo_set_source_rgba(cr, 1.0, 1.0, 0.0, 0.8);
            cairo_set_line_width(cr, 2.0);
            draw_line_3d(cr, v, width, height, x-8,y,z, x+8,y,z);
            draw_line_3d(cr, v, width, height, x,y-8,z, x,y+8,z);
            draw_line_3d(cr, v, width, height, x,y,z-8, x,y,z+8);
            cairo_stroke(cr);
        }
    }

    /* View label */
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.7);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, 5, 15);
    cairo_show_text(cr, "3D");
}

/* ── 2D View Draw Callback ───────────────────────────────────── */

static gboolean on_draw_2d(GtkWidget* widget, cairo_t* cr, gpointer data) {
    ViewState* v = (ViewState*)data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);

    /* Background */
    cairo_set_source_rgb(cr, 0.08, 0.08, 0.08);
    cairo_paint(cr);

    /* Grid */
    draw_grid(cr, v, width, height);

    /* Brushes */
    int bc; Brush** bs = brush_get_all(&bc);
    for (int i = 0; i < bc; i++)
        draw_brush_2d(cr, v, bs[i], i);

    /* Brush entity brushes */
    int ec; Entity** ents = entity_get_all(&ec);
    for (int i = 0; i < ec; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        BrushEntData* bd = brush_entity_data(e);
        if (bd) {
            /* Draw brush entity brushes offset by entity position */
            /* (simplified: just draw their AABB in the 2D view) */
            int ax_a, ax_b;
            view_axes(v->type, &ax_a, &ax_b);
            Vec3 off = vec3_sub(e->origin, bd->spawn_origin);
            for (int j = 0; j < bd->brush_count; j++) {
                Brush* b = bd->brushes[j];
                if (!b || b->face_count == 0) continue;
                Vec3 mn = vec3_add(b->mins, off);
                Vec3 mx = vec3_add(b->maxs, off);
                double x0, y0, x1, y1;
                world_to_widget(v, brush_axis(mn, ax_a), brush_axis(mn, ax_b), &x0, &y0);
                world_to_widget(v, brush_axis(mx, ax_a), brush_axis(mx, ax_b), &x1, &y1);
                if (x0 > x1) { double t=x0; x0=x1; x1=t; }
                if (y0 > y1) { double t=y0; y0=y1; y1=t; }
                if (e->id == g_selected_entity)
                    cairo_set_source_rgba(cr, 0.3, 1.0, 0.3, 0.6);
                else
                    cairo_set_source_rgba(cr, 0.3, 0.6, 1.0, 0.5);
                cairo_set_line_width(cr, 1.0);
                cairo_rectangle(cr, x0, y0, x1-x0, y1-y0);
                cairo_stroke(cr);
            }
        }
    }

    /* Point entities */
    for (int i = 0; i < ec; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        if (!brush_entity_data(e))
            draw_entity_2d(cr, v, e);
    }

    /* Creation preview */
    draw_creation_preview(cr, v);

    /* Selection gizmo */
    draw_selection_gizmo(cr, v);

    /* View label */
    const char* labels[] = {"Top (XY)", "Front (XZ)", "Side (YZ)", "3D"};
    cairo_set_source_rgba(cr, 0.5, 0.5, 0.5, 0.7);
    cairo_set_font_size(cr, 12);
    cairo_move_to(cr, 5, 15);
    cairo_show_text(cr, labels[v->type]);

    /* Zoom indicator */
    char zoom_str[32];
    snprintf(zoom_str, sizeof(zoom_str), "%.1fx", v->zoom);
    cairo_move_to(cr, width - 50, 15);
    cairo_show_text(cr, zoom_str);

    return FALSE;
}

static gboolean on_draw_3d(GtkWidget* widget, cairo_t* cr, gpointer data) {
    ViewState* v = (ViewState*)data;
    int width = gtk_widget_get_allocated_width(widget);
    int height = gtk_widget_get_allocated_height(widget);
    draw_3d_view(cr, v, width, height);
    return FALSE;
}

/* ── Mouse events for 2D views ───────────────────────────────── */

static gboolean on_button_press_2d(GtkWidget* widget, GdkEventButton* ev, gpointer data) {
    ViewState* v = (ViewState*)data;
    gtk_widget_grab_focus(widget);

    if (ev->button == 3) {
        /* RMB: start panning */
        v->dragging = 1;
        v->drag_start_x = ev->x;
        v->drag_start_y = ev->y;
        return TRUE;
    }

    if (ev->button == 1) {
        double wa, wb;
        widget_to_world(v, ev->x, ev->y, &wa, &wb);

        if (g_tool == TOOL_BRUSH) {
            /* Start brush creation */
            v->dragging = 2;
            v->create_x0 = snap_to_grid(wa);
            v->create_y0 = snap_to_grid(wb);
            v->create_x1 = v->create_x0;
            v->create_y1 = v->create_y0;
            return TRUE;
        }

        if (g_tool == TOOL_ENTITY) {
            /* Place entity */
            double sx = snap_to_grid(wa);
            double sy = snap_to_grid(wb);
            int ax_a, ax_b;
            view_axes(v->type, &ax_a, &ax_b);
            Vec3 pos = VEC3_ZERO;
            if (ax_a == 0) pos.x = (float)sx; else if (ax_a == 1) pos.y = (float)sx; else pos.z = (float)sx;
            if (ax_b == 0) pos.x = (float)sy; else if (ax_b == 1) pos.y = (float)sy; else pos.z = (float)sy;

            const char* cls = g_ent_classes[g_ent_class_idx];
            if (strncmp(cls, "func_", 5) == 0 || strncmp(cls, "trigger_", 8) == 0) {
                Entity* ent = brush_entity_create(cls, "");
                ent->origin = pos;
                brush_entity_data(ent)->spawn_origin = pos;
                brush_entity_add_brush(ent, VEC3(-32,-32,0), VEC3(32,32,64), g_current_texture, 1);
            } else {
                Entity* ent = entity_spawn();
                strncpy(ent->classname, cls, ENT_NAME_LEN-1);
                ent->origin = pos;
            }
            g_dirty = 1;
            char msg[128];
            snprintf(msg, sizeof(msg), "Placed %s at (%.0f, %.0f)", cls, sx, sy);
            update_status(msg);
            redraw_all();
            return TRUE;
        }

        /* Select tool: try to pick a brush or entity */
        if (g_tool == TOOL_SELECT) {
            int ax_a, ax_b;
            view_axes(v->type, &ax_a, &ax_b);

            /* Check shift for move */
            int shift = (ev->state & GDK_SHIFT_MASK) != 0;

            if (shift && g_selected_brush >= 0) {
                int bc2; Brush** bs2 = brush_get_all(&bc2);
                Brush* sb = (g_selected_brush < bc2) ? bs2[g_selected_brush] : NULL;

                if (g_gizmo == GIZMO_MOVE) {
                    v->dragging = 3;
                    v->drag_start_x = ev->x;
                    v->drag_start_y = ev->y;
                    widget_to_world(v, ev->x, ev->y, &v->drag_world_x, &v->drag_world_y);
                    return TRUE;
                }

                if (g_gizmo == GIZMO_RESIZE && sb) {
                    /* Determine which edge/corner to drag based on click position */
                    double mn_a = brush_axis(sb->mins, ax_a);
                    double mx_a = brush_axis(sb->maxs, ax_a);
                    double mn_b = brush_axis(sb->mins, ax_b);
                    double mx_b = brush_axis(sb->maxs, ax_b);
                    double tol = (double)g_grid * 0.6;
                    int edge = 0;
                    if (fabs(wa - mn_a) < tol) edge |= 1;       /* near min_a */
                    else if (fabs(wa - mx_a) < tol) edge |= 2;  /* near max_a */
                    if (fabs(wb - mn_b) < tol) edge |= 4;       /* near min_b */
                    else if (fabs(wb - mx_b) < tol) edge |= 8;  /* near max_b */
                    if (edge == 0) edge = 2 | 8; /* default: drag max corner */

                    v->resize_edge = edge;
                    v->resize_orig_min_a = mn_a;
                    v->resize_orig_max_a = mx_a;
                    v->resize_orig_min_b = mn_b;
                    v->resize_orig_max_b = mx_b;
                    v->dragging = 4;
                    v->drag_start_x = ev->x;
                    v->drag_start_y = ev->y;
                    widget_to_world(v, ev->x, ev->y, &v->drag_world_x, &v->drag_world_y);
                    return TRUE;
                }

                /* GIZMO_ROTATE or fallback: just move */
                v->dragging = 3;
                v->drag_start_x = ev->x;
                v->drag_start_y = ev->y;
                widget_to_world(v, ev->x, ev->y, &v->drag_world_x, &v->drag_world_y);
                return TRUE;
            }

            if (shift && g_selected_entity >= 0) {
                /* Entity move (always move, regardless of gizmo) */
                v->dragging = 3;
                v->drag_start_x = ev->x;
                v->drag_start_y = ev->y;
                widget_to_world(v, ev->x, ev->y, &v->drag_world_x, &v->drag_world_y);
                return TRUE;
            }

            /* Try picking brush */
            int bc; Brush** bs = brush_get_all(&bc);
            int best = -1;
            double best_area = 1e18;
            for (int i = 0; i < bc; i++) {
                Brush* b = bs[i];
                if (!b || b->face_count == 0) continue;
                double mn_a = brush_axis(b->mins, ax_a);
                double mn_b = brush_axis(b->mins, ax_b);
                double mx_a = brush_axis(b->maxs, ax_a);
                double mx_b = brush_axis(b->maxs, ax_b);
                if (wa >= mn_a && wa <= mx_a && wb >= mn_b && wb <= mx_b) {
                    double area = (mx_a - mn_a) * (mx_b - mn_b);
                    if (area < best_area) { best_area = area; best = i; }
                }
            }

            if (best >= 0) {
                g_selected_brush = best;
                g_selected_entity = -1;
                Brush* b = bs[best];
                char msg[128];
                snprintf(msg, sizeof(msg), "Brush %d: %.0fx%.0fx%.0f [%s]",
                         best, b->maxs.x-b->mins.x, b->maxs.y-b->mins.y,
                         b->maxs.z-b->mins.z, b->faces[0].texture);
                update_status(msg);
            } else {
                /* Try picking entity */
                int ec2; Entity** ents2 = entity_get_all(&ec2);
                int best_ent = -1;
                double best_dist = 20.0 / v->zoom; /* 20 pixel tolerance */
                for (int i = 0; i < ec2; i++) {
                    Entity* e = ents2[i];
                    if (!e || (e->flags & EF_DEAD)) continue;
                    double ea = brush_axis(e->origin, ax_a);
                    double eb = brush_axis(e->origin, ax_b);
                    double dist = sqrt((wa-ea)*(wa-ea) + (wb-eb)*(wb-eb));
                    if (dist < best_dist) { best_dist = dist; best_ent = e->id; }
                }
                if (best_ent >= 0) {
                    g_selected_entity = best_ent;
                    g_selected_brush = -1;
                    Entity* e = entity_get(best_ent);
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Entity: %s '%s'", e->classname, e->targetname);
                    update_status(msg);
                } else {
                    g_selected_brush = -1;
                    g_selected_entity = -1;
                    update_status("Deselected");
                }
            }
            redraw_all();
            return TRUE;
        }
    }
    return FALSE;
}

static gboolean on_button_release_2d(GtkWidget* widget, GdkEventButton* ev, gpointer data) {
    (void)widget;
    ViewState* v = (ViewState*)data;

    if (ev->button == 3 && v->dragging == 1) {
        v->dragging = 0;
        return TRUE;
    }

    if (ev->button == 1 && v->dragging == 2) {
        /* Finish brush creation */
        v->dragging = 0;
        double x0 = fmin(v->create_x0, v->create_x1);
        double y0 = fmin(v->create_y0, v->create_y1);
        double x1 = fmax(v->create_x0, v->create_x1);
        double y1 = fmax(v->create_y0, v->create_y1);
        if (x1 - x0 < 1 || y1 - y0 < 1) return TRUE; /* too small */

        int ax_a, ax_b;
        view_axes(v->type, &ax_a, &ax_b);

        /* Build 3D mins/maxs. The third axis gets default height = grid */
        Vec3 mins = VEC3_ZERO, maxs = VEC3_ZERO;
        float* mn = (float*)&mins;
        float* mx = (float*)&maxs;
        mn[ax_a] = (float)x0; mx[ax_a] = (float)x1;
        mn[ax_b] = (float)y0; mx[ax_b] = (float)y1;
        /* Third axis: default height */
        int ax_c = 3 - ax_a - ax_b; /* the remaining axis */
        mn[ax_c] = 0;
        mx[ax_c] = (float)g_grid;

        Brush* nb = brush_create(mins, maxs, g_current_texture, 1);
        if (nb) {
            g_selected_brush = brush_count() - 1;
            UndoEntry ue = {0}; ue.type = 0; ue.brush_idx = g_selected_brush;
            undo_push(ue);
            g_dirty = 1;
            char msg[128];
            snprintf(msg, sizeof(msg), "Created brush %.0fx%.0fx%.0f",
                     maxs.x-mins.x, maxs.y-mins.y, maxs.z-mins.z);
            update_status(msg);
        }
        redraw_all();
        return TRUE;
    }

    if (ev->button == 1 && v->dragging == 3) {
        /* Finish move */
        v->dragging = 0;
        g_dirty = 1;
        update_status("Moved");
        redraw_all();
        return TRUE;
    }

    if (ev->button == 1 && v->dragging == 4) {
        /* Finish resize */
        v->dragging = 0;
        g_dirty = 1;
        update_status("Resized");
        redraw_all();
        return TRUE;
    }

    return FALSE;
}

static gboolean on_motion_2d(GtkWidget* widget, GdkEventMotion* ev, gpointer data) {
    ViewState* v = (ViewState*)data;

    if (v->dragging == 1) {
        /* Panning */
        v->pan_x += ev->x - v->drag_start_x;
        v->pan_y += ev->y - v->drag_start_y;
        v->drag_start_x = ev->x;
        v->drag_start_y = ev->y;
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    if (v->dragging == 2) {
        /* Brush creation drag */
        double wa, wb;
        widget_to_world(v, ev->x, ev->y, &wa, &wb);
        v->create_x1 = snap_to_grid(wa);
        v->create_y1 = snap_to_grid(wb);
        gtk_widget_queue_draw(widget);
        return TRUE;
    }

    if (v->dragging == 3) {
        /* Move selected */
        double wa, wb;
        widget_to_world(v, ev->x, ev->y, &wa, &wb);
        double da = snap_to_grid(wa - v->drag_world_x);
        double db = snap_to_grid(wb - v->drag_world_y);

        int ax_a, ax_b;
        view_axes(v->type, &ax_a, &ax_b);
        Vec3 delta = VEC3_ZERO;
        float* d = (float*)&delta;
        d[ax_a] = (float)da;
        d[ax_b] = (float)db;

        if (g_selected_brush >= 0) {
            move_brush(g_selected_brush, delta);
        }
        if (g_selected_entity >= 0) {
            Entity* e = entity_get(g_selected_entity);
            if (e) e->origin = vec3_add(e->origin, delta);
        }

        v->drag_world_x = wa;
        v->drag_world_y = wb;
        redraw_all();
        return TRUE;
    }

    if (v->dragging == 4 && g_selected_brush >= 0) {
        /* Resize selected brush */
        double wa, wb;
        widget_to_world(v, ev->x, ev->y, &wa, &wb);
        wa = snap_to_grid(wa);
        wb = snap_to_grid(wb);

        int ax_a, ax_b;
        view_axes(v->type, &ax_a, &ax_b);

        double new_min_a = v->resize_orig_min_a;
        double new_max_a = v->resize_orig_max_a;
        double new_min_b = v->resize_orig_min_b;
        double new_max_b = v->resize_orig_max_b;

        if (v->resize_edge & 1) new_min_a = wa;  /* dragging min_a */
        if (v->resize_edge & 2) new_max_a = wa;  /* dragging max_a */
        if (v->resize_edge & 4) new_min_b = wb;  /* dragging min_b */
        if (v->resize_edge & 8) new_max_b = wb;  /* dragging max_b */

        /* Enforce minimum size */
        if (new_max_a - new_min_a < g_grid) {
            if (v->resize_edge & 1) new_min_a = new_max_a - g_grid;
            else new_max_a = new_min_a + g_grid;
        }
        if (new_max_b - new_min_b < g_grid) {
            if (v->resize_edge & 4) new_min_b = new_max_b - g_grid;
            else new_max_b = new_min_b + g_grid;
        }

        int bc; Brush** bs = brush_get_all(&bc);
        if (g_selected_brush < bc && bs[g_selected_brush]) {
            Brush* b = bs[g_selected_brush];
            Vec3 new_mins = b->mins, new_maxs = b->maxs;
            float* nmn = (float*)&new_mins;
            float* nmx = (float*)&new_maxs;
            nmn[ax_a] = (float)new_min_a; nmx[ax_a] = (float)new_max_a;
            nmn[ax_b] = (float)new_min_b; nmx[ax_b] = (float)new_max_b;
            resize_brush(g_selected_brush, new_mins, new_maxs);
        }

        redraw_all();
        return TRUE;
    }

    return FALSE;
}

static gboolean on_scroll_2d(GtkWidget* widget, GdkEventScroll* ev, gpointer data) {
    ViewState* v = (ViewState*)data;
    double factor = 1.15;
    if (ev->direction == GDK_SCROLL_UP) {
        v->zoom *= factor;
    } else if (ev->direction == GDK_SCROLL_DOWN) {
        v->zoom /= factor;
    }
    if (v->zoom < 0.01) v->zoom = 0.01;
    if (v->zoom > 20) v->zoom = 20;
    gtk_widget_queue_draw(widget);
    return TRUE;
}

/* ── Mouse events for 3D view ────────────────────────────────── */

static gboolean on_button_press_3d(GtkWidget* widget, GdkEventButton* ev, gpointer data) {
    ViewState* v = (ViewState*)data;
    gtk_widget_grab_focus(widget);
    if (ev->button == 3) {
        v->dragging = 1;
        v->drag_start_x = ev->x;
        v->drag_start_y = ev->y;
    }
    return TRUE;
}

static gboolean on_button_release_3d(GtkWidget* widget, GdkEventButton* ev, gpointer data) {
    (void)widget;
    ViewState* v = (ViewState*)data;
    if (ev->button == 3) v->dragging = 0;
    return TRUE;
}

static gboolean on_motion_3d(GtkWidget* widget, GdkEventMotion* ev, gpointer data) {
    ViewState* v = (ViewState*)data;
    if (v->dragging == 1) {
        float dx = (float)(ev->x - v->drag_start_x);
        float dy = (float)(ev->y - v->drag_start_y);
        v->cam_yaw -= dx * 0.005f;
        v->cam_pitch += dy * 0.005f;
        if (v->cam_pitch > 1.5f) v->cam_pitch = 1.5f;
        if (v->cam_pitch < -1.5f) v->cam_pitch = -1.5f;
        v->drag_start_x = ev->x;
        v->drag_start_y = ev->y;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean on_scroll_3d(GtkWidget* widget, GdkEventScroll* ev, gpointer data) {
    ViewState* v = (ViewState*)data;
    if (ev->direction == GDK_SCROLL_UP) {
        v->cam_dist *= 0.9f;
        if (v->cam_dist < 10) v->cam_dist = 10;
    } else if (ev->direction == GDK_SCROLL_DOWN) {
        v->cam_dist *= 1.1f;
        if (v->cam_dist > 5000) v->cam_dist = 5000;
    }
    gtk_widget_queue_draw(widget);
    return TRUE;
}

/* ── Keyboard ────────────────────────────────────────────────── */

static gboolean on_key_press(GtkWidget* widget, GdkEventKey* ev, gpointer data) {
    (void)widget; (void)data;

    /* Ctrl combos */
    if (ev->state & GDK_CONTROL_MASK) {
        if (ev->keyval == GDK_KEY_s) { save_map(); return TRUE; }
        if (ev->keyval == GDK_KEY_z) { do_undo(); return TRUE; }
        if (ev->keyval == GDK_KEY_y) { do_redo(); return TRUE; }
        if (ev->keyval == GDK_KEY_d) { duplicate_selected(); return TRUE; }
        return FALSE;
    }

    switch (ev->keyval) {
    case GDK_KEY_b:
        g_tool = TOOL_BRUSH;
        update_status("Tool: Brush — drag in a 2D view to create");
        break;
    case GDK_KEY_s:
        g_tool = TOOL_SELECT;
        update_status("Tool: Select — click to select, Shift+drag to move");
        break;
    case GDK_KEY_e:
        g_tool = TOOL_ENTITY;
        { char msg[128];
          snprintf(msg, sizeof(msg), "Tool: Entity (%s) — click to place, [ ] to change class",
                   g_ent_classes[g_ent_class_idx]);
          update_status(msg); }
        break;
    case GDK_KEY_g:
        g_gizmo = (g_gizmo + 1) % GIZMO_COUNT;
        { const char* names[] = {"Move", "Resize", "Rotate"};
          char msg[64]; snprintf(msg, sizeof(msg), "Gizmo: %s", names[g_gizmo]);
          update_status(msg); }
        redraw_all();
        break;
    case GDK_KEY_x:
    case GDK_KEY_Delete:
        delete_selected();
        break;
    case GDK_KEY_t:
        /* Texture dialog */
        if (g_selected_brush >= 0) {
            GtkWidget* dialog = gtk_dialog_new_with_buttons(
                "Select Texture", GTK_WINDOW(g_window),
                GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                "_OK", GTK_RESPONSE_OK,
                "_Cancel", GTK_RESPONSE_CANCEL, NULL);
            GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
            GtkWidget* combo = gtk_combo_box_text_new();
            for (int i = 0; i < g_tex_count; i++)
                gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(combo), g_tex_names[i]);
            gtk_combo_box_set_active(GTK_COMBO_BOX(combo), 0);
            gtk_container_add(GTK_CONTAINER(content), combo);
            gtk_widget_show_all(dialog);
            if (gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_OK) {
                int sel = gtk_combo_box_get_active(GTK_COMBO_BOX(combo));
                if (sel >= 0 && sel < g_tex_count) {
                    g_current_texture = g_tex_names[sel];
                    int bc; Brush** bs = brush_get_all(&bc);
                    if (g_selected_brush < bc && bs[g_selected_brush]) {
                        for (int f = 0; f < bs[g_selected_brush]->face_count; f++)
                            brush_set_face_texture(bs[g_selected_brush], f, g_current_texture);
                        g_dirty = 1;
                    }
                    char msg[128];
                    snprintf(msg, sizeof(msg), "Texture: %s", g_current_texture);
                    update_status(msg);
                    redraw_all();
                }
            }
            gtk_widget_destroy(dialog);
        }
        break;
    case GDK_KEY_bracketleft:
        g_ent_class_idx = (g_ent_class_idx - 1 + ENT_CLASS_COUNT) % ENT_CLASS_COUNT;
        { char msg[128];
          snprintf(msg, sizeof(msg), "Entity class: %s", g_ent_classes[g_ent_class_idx]);
          update_status(msg); }
        break;
    case GDK_KEY_bracketright:
        g_ent_class_idx = (g_ent_class_idx + 1) % ENT_CLASS_COUNT;
        { char msg[128];
          snprintf(msg, sizeof(msg), "Entity class: %s", g_ent_classes[g_ent_class_idx]);
          update_status(msg); }
        break;
    case GDK_KEY_Escape:
        g_selected_brush = -1;
        g_selected_entity = -1;
        g_tool = TOOL_SELECT;
        update_status("Deselected");
        redraw_all();
        break;
    }
    return FALSE;
}

/* ── Toolbar callbacks ───────────────────────────────────────── */

static void on_tool_select(GtkWidget* w, gpointer d) { (void)w;(void)d; g_tool=TOOL_SELECT; update_status("Tool: Select"); }
static void on_tool_brush(GtkWidget* w, gpointer d)  { (void)w;(void)d; g_tool=TOOL_BRUSH;  update_status("Tool: Brush"); }
static void on_tool_entity(GtkWidget* w, gpointer d) { (void)w;(void)d; g_tool=TOOL_ENTITY; update_status("Tool: Entity"); }
static void on_save(GtkWidget* w, gpointer d)        { (void)w;(void)d; save_map(); }
static void on_undo(GtkWidget* w, gpointer d)        { (void)w;(void)d; do_undo(); }
static void on_redo(GtkWidget* w, gpointer d)        { (void)w;(void)d; do_redo(); }
static void on_delete(GtkWidget* w, gpointer d)      { (void)w;(void)d; delete_selected(); }
static void on_duplicate(GtkWidget* w, gpointer d)   { (void)w;(void)d; duplicate_selected(); }

static void on_grid_changed(GtkWidget* w, gpointer d) {
    (void)d;
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    if (idx >= 0 && idx < GRID_SIZES_COUNT) {
        g_grid_idx = idx;
        g_grid = GRID_SIZES[idx];
        char msg[64]; snprintf(msg, sizeof(msg), "Grid: %d", g_grid);
        update_status(msg);
        redraw_all();
    }
}

static void on_tex_changed(GtkWidget* w, gpointer d) {
    (void)d;
    int idx = gtk_combo_box_get_active(GTK_COMBO_BOX(w));
    if (idx >= 0 && idx < g_tex_count) {
        g_current_texture = g_tex_names[idx];
    }
}

/* ── Create a 2D view widget ─────────────────────────────────── */

static GtkWidget* create_2d_view(ViewState* v) {
    GtkWidget* da = gtk_drawing_area_new();
    v->drawing_area = da;
    gtk_widget_set_can_focus(da, TRUE);
    gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK |
                              GDK_KEY_PRESS_MASK);
    g_signal_connect(da, "draw", G_CALLBACK(on_draw_2d), v);
    g_signal_connect(da, "button-press-event", G_CALLBACK(on_button_press_2d), v);
    g_signal_connect(da, "button-release-event", G_CALLBACK(on_button_release_2d), v);
    g_signal_connect(da, "motion-notify-event", G_CALLBACK(on_motion_2d), v);
    g_signal_connect(da, "scroll-event", G_CALLBACK(on_scroll_2d), v);
    return da;
}

static GtkWidget* create_3d_view(ViewState* v) {
    GtkWidget* da = gtk_drawing_area_new();
    v->drawing_area = da;
    gtk_widget_set_can_focus(da, TRUE);
    gtk_widget_add_events(da, GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
                              GDK_POINTER_MOTION_MASK | GDK_SCROLL_MASK);
    g_signal_connect(da, "draw", G_CALLBACK(on_draw_3d), v);
    g_signal_connect(da, "button-press-event", G_CALLBACK(on_button_press_3d), v);
    g_signal_connect(da, "button-release-event", G_CALLBACK(on_button_release_3d), v);
    g_signal_connect(da, "motion-notify-event", G_CALLBACK(on_motion_3d), v);
    g_signal_connect(da, "scroll-event", G_CALLBACK(on_scroll_3d), v);
    return da;
}

/* ── Main ────────────────────────────────────────────────────── */

int main(int argc, char** argv) {
    printf("╔══════════════════════════════════════╗\n");
    printf("║    Origin Editor (Hammer-style)      ║\n");
    printf("╚══════════════════════════════════════╝\n\n");

    /* Parse args */
    const char* game_folder = "game";
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--game") == 0 && i + 1 < argc)
            game_folder = argv[++i];
        else if (strstr(argv[i], ".oem"))
            strncpy(g_map_path, argv[i], sizeof(g_map_path)-1);
    }

    snprintf(g_tex_folder, sizeof(g_tex_folder), "%s/textures", game_folder);
    if (g_map_path[0] == '\0')
        snprintf(g_map_path, sizeof(g_map_path), "%s/maps/editor.oem", game_folder);

    printf("[editor] game: %s\n", game_folder);
    printf("[editor] map: %s\n", g_map_path);

    /* Init engine data systems (no renderer) */
    console_init();
    entity_system_init();
    brush_system_init();
    trigger_system_init();
    entity_io_init();
    texture_load_all(g_tex_folder);

    /* Build texture list */
    for (int i = 0; i < texture_count(); i++) {
        const TextureInfo* ti = texture_get_info(i);
        if (ti) g_tex_names[g_tex_count++] = ti->name;
    }
    if (g_tex_count > 0) g_current_texture = g_tex_names[0];

    /* Load map */
    MapInfo info;
    if (map_load(g_map_path, &info)) {
        printf("[editor] loaded: %s\n", g_map_path);
    } else {
        printf("[editor] new map\n");
        Brush* fl = brush_create(VEC3(-512,-512,-8), VEC3(512,512,0), "dirt", 1);
        brush_set_face_texture(fl, FACE_TOP, "grass");
        brush_set_face_scale(fl, FACE_TOP, 0.5f, 0.5f);
        Entity* sp = entity_spawn();
        strncpy(sp->classname, "info_player_start", ENT_NAME_LEN-1);
        sp->origin = VEC3(0, -100, 1);
    }

    /* Init view states */
    g_views[0] = (ViewState){.type=VIEW_TOP,   .zoom=0.5, .cam_yaw=0, .cam_pitch=0, .cam_dist=600, .cam_target={0,0,0}};
    g_views[1] = (ViewState){.type=VIEW_FRONT, .zoom=0.5, .cam_yaw=0, .cam_pitch=0, .cam_dist=600, .cam_target={0,0,0}};
    g_views[2] = (ViewState){.type=VIEW_SIDE,  .zoom=0.5, .cam_yaw=0, .cam_pitch=0, .cam_dist=600, .cam_target={0,0,0}};
    g_views[3] = (ViewState){.type=VIEW_3D,    .zoom=1.0, .cam_yaw=2.3f, .cam_pitch=0.5f, .cam_dist=600, .cam_target={0,0,50}};

    /* ── GTK setup ───────────────────────────────────────────── */
    gtk_init(&argc, &argv);

    /* Dark theme */
    GtkSettings* settings = gtk_settings_get_default();
    g_object_set(settings, "gtk-application-prefer-dark-theme", TRUE, NULL);

    /* Main window */
    g_window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    char title[256];
    snprintf(title, sizeof(title), "Origin Editor - %s", g_map_path);
    gtk_window_set_title(GTK_WINDOW(g_window), title);
    gtk_window_set_default_size(GTK_WINDOW(g_window), 1400, 900);
    g_signal_connect(g_window, "destroy", G_CALLBACK(gtk_main_quit), NULL);
    g_signal_connect(g_window, "key-press-event", G_CALLBACK(on_key_press), NULL);

    /* Main vertical box */
    GtkWidget* vbox = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_container_add(GTK_CONTAINER(g_window), vbox);

    /* ── Toolbar ─────────────────────────────────────────────── */
    g_toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_set_margin_start(g_toolbar, 4);
    gtk_widget_set_margin_end(g_toolbar, 4);
    gtk_widget_set_margin_top(g_toolbar, 2);
    gtk_widget_set_margin_bottom(g_toolbar, 2);

    GtkWidget* btn;
    btn = gtk_button_new_with_label("Select(S)"); g_signal_connect(btn, "clicked", G_CALLBACK(on_tool_select), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);
    btn = gtk_button_new_with_label("Brush(B)"); g_signal_connect(btn, "clicked", G_CALLBACK(on_tool_brush), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);
    btn = gtk_button_new_with_label("Entity(E)"); g_signal_connect(btn, "clicked", G_CALLBACK(on_tool_entity), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);

    /* Separator */
    gtk_box_pack_start(GTK_BOX(g_toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);

    btn = gtk_button_new_with_label("Save"); g_signal_connect(btn, "clicked", G_CALLBACK(on_save), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);
    btn = gtk_button_new_with_label("Undo"); g_signal_connect(btn, "clicked", G_CALLBACK(on_undo), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);
    btn = gtk_button_new_with_label("Redo"); g_signal_connect(btn, "clicked", G_CALLBACK(on_redo), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);
    btn = gtk_button_new_with_label("Delete"); g_signal_connect(btn, "clicked", G_CALLBACK(on_delete), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);
    btn = gtk_button_new_with_label("Duplicate"); g_signal_connect(btn, "clicked", G_CALLBACK(on_duplicate), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), btn, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(g_toolbar), gtk_separator_new(GTK_ORIENTATION_VERTICAL), FALSE, FALSE, 4);

    /* Grid combo */
    GtkWidget* grid_label = gtk_label_new("Grid:");
    gtk_box_pack_start(GTK_BOX(g_toolbar), grid_label, FALSE, FALSE, 0);
    GtkWidget* grid_combo = gtk_combo_box_text_new();
    for (int i = 0; i < GRID_SIZES_COUNT; i++) {
        char gs[8]; snprintf(gs, sizeof(gs), "%d", GRID_SIZES[i]);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(grid_combo), gs);
    }
    gtk_combo_box_set_active(GTK_COMBO_BOX(grid_combo), g_grid_idx);
    g_signal_connect(grid_combo, "changed", G_CALLBACK(on_grid_changed), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), grid_combo, FALSE, FALSE, 0);

    /* Texture combo */
    GtkWidget* tex_label = gtk_label_new("Texture:");
    gtk_box_pack_start(GTK_BOX(g_toolbar), tex_label, FALSE, FALSE, 4);
    g_tex_combo = gtk_combo_box_text_new();
    for (int i = 0; i < g_tex_count; i++)
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(g_tex_combo), g_tex_names[i]);
    gtk_combo_box_set_active(GTK_COMBO_BOX(g_tex_combo), 0);
    g_signal_connect(g_tex_combo, "changed", G_CALLBACK(on_tex_changed), NULL);
    gtk_box_pack_start(GTK_BOX(g_toolbar), g_tex_combo, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(vbox), g_toolbar, FALSE, FALSE, 0);

    /* ── 4-view grid ─────────────────────────────────────────── */
    GtkWidget* vpaned = gtk_paned_new(GTK_ORIENTATION_VERTICAL);
    GtkWidget* hpaned_top = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* hpaned_bot = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);

    /* Top-left: 3D view */
    GtkWidget* frame3 = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame3), create_3d_view(&g_views[3]));
    gtk_paned_pack1(GTK_PANED(hpaned_top), frame3, TRUE, TRUE);

    /* Top-right: Top view (XY) */
    GtkWidget* frame0 = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame0), create_2d_view(&g_views[0]));
    gtk_paned_pack2(GTK_PANED(hpaned_top), frame0, TRUE, TRUE);

    /* Bottom-left: Front view (XZ) */
    GtkWidget* frame1 = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame1), create_2d_view(&g_views[1]));
    gtk_paned_pack1(GTK_PANED(hpaned_bot), frame1, TRUE, TRUE);

    /* Bottom-right: Side view (YZ) */
    GtkWidget* frame2 = gtk_frame_new(NULL);
    gtk_container_add(GTK_CONTAINER(frame2), create_2d_view(&g_views[2]));
    gtk_paned_pack2(GTK_PANED(hpaned_bot), frame2, TRUE, TRUE);

    gtk_paned_pack1(GTK_PANED(vpaned), hpaned_top, TRUE, TRUE);
    gtk_paned_pack2(GTK_PANED(vpaned), hpaned_bot, TRUE, TRUE);

    gtk_box_pack_start(GTK_BOX(vbox), vpaned, TRUE, TRUE, 0);

    /* ── Status bar ──────────────────────────────────────────── */
    g_statusbar = gtk_statusbar_new();
    gtk_statusbar_push(GTK_STATUSBAR(g_statusbar), 0, "Ready — S=Select B=Brush E=Entity G=Gizmo T=Texture");
    gtk_box_pack_start(GTK_BOX(vbox), g_statusbar, FALSE, FALSE, 0);

    /* Show and run */
    gtk_widget_show_all(g_window);

    /* Set initial pane positions after show */
    gtk_paned_set_position(GTK_PANED(vpaned), 450);
    gtk_paned_set_position(GTK_PANED(hpaned_top), 700);
    gtk_paned_set_position(GTK_PANED(hpaned_bot), 700);

    printf("[editor] Ready.\n");
    gtk_main();

    /* Auto-save on exit if dirty */
    if (g_dirty) {
        printf("[editor] Auto-saving...\n");
        save_map();
    }

    brush_system_shutdown();
    entity_system_shutdown();
    trigger_system_shutdown();
    texture_free_all();
    console_shutdown();

    printf("[editor] Closed.\n");
    return 0;
}
