/*
 * brush.h — Brush geometry (Source-style world solids)
 *
 * Brushes can be AABB boxes OR arbitrary convex polyhedra.
 * Each face has its own texture, vertex list, and UV properties.
 *
 * AABB convenience: FACE_BOTTOM..FACE_EAST indices still work for
 * brushes created with brush_create() (6-face AABBs).
 */
#ifndef BRUSH_H
#define BRUSH_H

#include "vec3.h"

#define MAX_BRUSHES       256
#define BRUSH_NAME_LEN    64
#define MAX_FACE_VERTS    32   /* max vertices per face */
#define MAX_BRUSH_FACES   32   /* max faces per brush */

/* AABB face indices (for brush_create convenience) */
#define FACE_BOTTOM 0
#define FACE_TOP    1
#define FACE_SOUTH  2
#define FACE_NORTH  3
#define FACE_WEST   4
#define FACE_EAST   5

typedef struct {
    char    texture[BRUSH_NAME_LEN];
    float   scale_u;
    float   scale_v;
    float   offset_u;
    float   offset_v;
    Vec3    vertices[MAX_FACE_VERTS];
    int     vertex_count;
    Vec3    normal;  /* outward-facing normal */
} BrushFace;

typedef struct {
    int         id;
    BrushFace   faces[MAX_BRUSH_FACES];
    int         face_count;
    Vec3        mins;       /* AABB bounds (computed from all vertices) */
    Vec3        maxs;
    int         solid;
    int         entity_owned;
} Brush;

/* Create an AABB brush (6 faces, auto-generates vertices) */
Brush*  brush_create(Vec3 mins, Vec3 maxs, const char* texture, int solid);

/* Create an empty brush — add faces manually */
Brush*  brush_create_empty(int solid);

/* Add a face with vertices to a brush. Returns face index or -1. */
int     brush_add_face(Brush* b, const Vec3* verts, int vert_count,
                       const char* texture);

/* Recompute AABB bounds from all face vertices */
void    brush_recompute_bounds(Brush* b);

/* Set a specific face's texture */
void    brush_set_face_texture(Brush* b, int face, const char* texture);
void    brush_set_face_scale(Brush* b, int face, float u, float v);
void    brush_set_face_offset(Brush* b, int face, float u, float v);

Brush*  brush_get(int id);
Brush** brush_get_all(int* out_count);
int     brush_count(void);

/* Collision: uses AABB bounds */
int brush_contains_point(const Brush* b, Vec3 point);
int brush_overlaps_aabb(const Brush* b, Vec3 mins, Vec3 maxs);

void brush_system_init(void);
void brush_system_shutdown(void);

#endif /* BRUSH_H */
