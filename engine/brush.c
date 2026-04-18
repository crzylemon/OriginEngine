/*
 * brush.c — Brush system implementation (arbitrary convex polyhedra)
 */
#include "brush.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static Brush* g_brushes[MAX_BRUSHES];
static int    g_brush_count = 0;
static int    g_next_brush_id = 1;

void brush_system_init(void) {
    memset(g_brushes, 0, sizeof(g_brushes));
    g_brush_count = 0;
    g_next_brush_id = 1;
    printf("[brush] system initialized\n");
}

void brush_system_shutdown(void) {
    for (int i = 0; i < g_brush_count; i++) {
        free(g_brushes[i]);
        g_brushes[i] = NULL;
    }
    g_brush_count = 0;
    printf("[brush] system shutdown\n");
}

static Vec3 compute_normal(Vec3 a, Vec3 b, Vec3 c) {
    Vec3 ab = vec3_sub(b, a);
    Vec3 ac = vec3_sub(c, a);
    Vec3 n = VEC3(ab.y*ac.z - ab.z*ac.y,
                  ab.z*ac.x - ab.x*ac.z,
                  ab.x*ac.y - ab.y*ac.x);
    return vec3_normalize(n);
}

void brush_recompute_bounds(Brush* b) {
    b->mins = VEC3(1e9f, 1e9f, 1e9f);
    b->maxs = VEC3(-1e9f, -1e9f, -1e9f);
    for (int f = 0; f < b->face_count; f++) {
        for (int v = 0; v < b->faces[f].vertex_count; v++) {
            Vec3 p = b->faces[f].vertices[v];
            if (p.x < b->mins.x) b->mins.x = p.x;
            if (p.y < b->mins.y) b->mins.y = p.y;
            if (p.z < b->mins.z) b->mins.z = p.z;
            if (p.x > b->maxs.x) b->maxs.x = p.x;
            if (p.y > b->maxs.y) b->maxs.y = p.y;
            if (p.z > b->maxs.z) b->maxs.z = p.z;
        }
    }
}

Brush* brush_create_empty(int solid) {
    if (g_brush_count >= MAX_BRUSHES) return NULL;
    Brush* b = (Brush*)calloc(1, sizeof(Brush));
    if (!b) return NULL;
    b->id = g_next_brush_id++;
    b->solid = solid;
    b->face_count = 0;
    g_brushes[g_brush_count++] = b;
    return b;
}

int brush_add_face(Brush* b, const Vec3* verts, int vert_count,
                   const char* texture) {
    if (!b || b->face_count >= MAX_BRUSH_FACES) return -1;
    if (vert_count < 3 || vert_count > MAX_FACE_VERTS) return -1;

    int fi = b->face_count++;
    BrushFace* face = &b->faces[fi];
    memset(face, 0, sizeof(BrushFace));
    strncpy(face->texture, texture, BRUSH_NAME_LEN - 1);
    face->scale_u = 1.0f;
    face->scale_v = 1.0f;
    face->vertex_count = vert_count;
    for (int i = 0; i < vert_count; i++)
        face->vertices[i] = verts[i];

    /* Compute normal from first 3 vertices */
    face->normal = compute_normal(verts[0], verts[1], verts[2]);

    return fi;
}

/* Create an AABB brush with 6 faces and proper vertices */
Brush* brush_create(Vec3 mins, Vec3 maxs, const char* texture, int solid) {
    Brush* b = brush_create_empty(solid);
    if (!b) return NULL;

    float x0=mins.x, y0=mins.y, z0=mins.z;
    float x1=maxs.x, y1=maxs.y, z1=maxs.z;

    /* 6 faces with 4 vertices each (CW winding from outside) */
    Vec3 face_verts[6][4] = {
        /* Bottom (-Z) */ {{x0,y1,z0},{x1,y1,z0},{x1,y0,z0},{x0,y0,z0}},
        /* Top (+Z) */    {{x0,y0,z1},{x1,y0,z1},{x1,y1,z1},{x0,y1,z1}},
        /* South (-Y) */  {{x0,y0,z0},{x1,y0,z0},{x1,y0,z1},{x0,y0,z1}},
        /* North (+Y) */  {{x1,y1,z0},{x0,y1,z0},{x0,y1,z1},{x1,y1,z1}},
        /* West (-X) */   {{x0,y1,z0},{x0,y0,z0},{x0,y0,z1},{x0,y1,z1}},
        /* East (+X) */   {{x1,y0,z0},{x1,y1,z0},{x1,y1,z1},{x1,y0,z1}},
    };

    for (int i = 0; i < 6; i++) {
        brush_add_face(b, face_verts[i], 4, texture);
    }

    b->mins = mins;
    b->maxs = maxs;
    return b;
}

void brush_set_face_texture(Brush* b, int face, const char* texture) {
    if (!b || face < 0 || face >= b->face_count) return;
    strncpy(b->faces[face].texture, texture, BRUSH_NAME_LEN - 1);
}

void brush_set_face_scale(Brush* b, int face, float u, float v) {
    if (!b || face < 0 || face >= b->face_count) return;
    b->faces[face].scale_u = u > 0.0f ? u : 1.0f;
    b->faces[face].scale_v = v > 0.0f ? v : 1.0f;
}

void brush_set_face_offset(Brush* b, int face, float u, float v) {
    if (!b || face < 0 || face >= b->face_count) return;
    b->faces[face].offset_u = u;
    b->faces[face].offset_v = v;
}

Brush* brush_get(int id) {
    for (int i = 0; i < g_brush_count; i++)
        if (g_brushes[i] && g_brushes[i]->id == id) return g_brushes[i];
    return NULL;
}

Brush** brush_get_all(int* out_count) {
    if (out_count) *out_count = g_brush_count;
    return g_brushes;
}

int brush_count(void) { return g_brush_count; }

int brush_contains_point(const Brush* b, Vec3 p) {
    /* Point is inside convex brush if it's behind all face planes */
    for (int f = 0; f < b->face_count; f++) {
        Vec3 n = b->faces[f].normal;
        Vec3 v0 = b->faces[f].vertices[0];
        /* Distance from point to plane */
        float d = vec3_dot(n, vec3_sub(p, v0));
        if (d > 0.01f) return 0; /* point is in front of this face = outside */
    }
    return 1;
}

/*
 * Test if an AABB overlaps a convex brush.
 * Uses SAT: test separation along each face normal and the 3 AABB axes.
 * For each axis, project both shapes and check for overlap.
 */
static void aabb_project_onto_axis(Vec3 mins, Vec3 maxs, Vec3 axis,
                                    float* out_min, float* out_max) {
    /* 8 corners of the AABB */
    float dots[8];
    dots[0] = vec3_dot(axis, VEC3(mins.x, mins.y, mins.z));
    dots[1] = vec3_dot(axis, VEC3(maxs.x, mins.y, mins.z));
    dots[2] = vec3_dot(axis, VEC3(mins.x, maxs.y, mins.z));
    dots[3] = vec3_dot(axis, VEC3(maxs.x, maxs.y, mins.z));
    dots[4] = vec3_dot(axis, VEC3(mins.x, mins.y, maxs.z));
    dots[5] = vec3_dot(axis, VEC3(maxs.x, mins.y, maxs.z));
    dots[6] = vec3_dot(axis, VEC3(mins.x, maxs.y, maxs.z));
    dots[7] = vec3_dot(axis, VEC3(maxs.x, maxs.y, maxs.z));
    *out_min = dots[0]; *out_max = dots[0];
    for (int i = 1; i < 8; i++) {
        if (dots[i] < *out_min) *out_min = dots[i];
        if (dots[i] > *out_max) *out_max = dots[i];
    }
}

static void brush_project_onto_axis(const Brush* b, Vec3 axis,
                                     float* out_min, float* out_max) {
    int first = 1;
    *out_min = 0; *out_max = 0;
    for (int f = 0; f < b->face_count; f++) {
        for (int v = 0; v < b->faces[f].vertex_count; v++) {
            float d = vec3_dot(axis, b->faces[f].vertices[v]);
            if (first) { *out_min = d; *out_max = d; first = 0; }
            else { if (d < *out_min) *out_min = d; if (d > *out_max) *out_max = d; }
        }
    }
}

int brush_overlaps_aabb(const Brush* b, Vec3 mins, Vec3 maxs) {
    /* Broadphase: AABB vs AABB */
    if (b->mins.x >= maxs.x || b->maxs.x <= mins.x ||
        b->mins.y >= maxs.y || b->maxs.y <= mins.y ||
        b->mins.z >= maxs.z || b->maxs.z <= mins.z)
        return 0;

    /* SAT: test each brush face normal as a separating axis */
    for (int f = 0; f < b->face_count; f++) {
        Vec3 axis = b->faces[f].normal;
        if (vec3_len(axis) < 0.001f) continue;

        float bmin, bmax, amin, amax;
        brush_project_onto_axis(b, axis, &bmin, &bmax);
        aabb_project_onto_axis(mins, maxs, axis, &amin, &amax);

        if (amax <= bmin || bmax <= amin)
            return 0; /* separating axis found */
    }

    /* SAT: test the 3 AABB axes (X, Y, Z) */
    Vec3 aabb_axes[3] = { VEC3(1,0,0), VEC3(0,1,0), VEC3(0,0,1) };
    for (int a = 0; a < 3; a++) {
        float bmin, bmax, amin, amax;
        brush_project_onto_axis(b, aabb_axes[a], &bmin, &bmax);
        aabb_project_onto_axis(mins, maxs, aabb_axes[a], &amin, &amax);

        if (amax <= bmin || bmax <= amin)
            return 0;
    }

    return 1; /* no separating axis found = overlapping */
}
