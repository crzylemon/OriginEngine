/*
 * brush_entity.h — Brush entities (Source-style func_ entities)
 *
 * A brush entity is an Entity that owns one or more Brushes.
 * When the entity moves, its brushes move with it.
 * Examples: func_door, func_button, func_rotating, func_wall.
 *
 * Brushes owned by a brush entity are stored with positions
 * RELATIVE to the entity's origin. The renderer adds the
 * entity origin to get world-space positions.
 */
#ifndef BRUSH_ENTITY_H
#define BRUSH_ENTITY_H

#include "entity.h"
#include "brush.h"

#define MAX_BRUSH_ENT_BRUSHES 16

typedef struct {
    Brush*  brushes[MAX_BRUSH_ENT_BRUSHES];
    int     brush_count;
    Vec3    spawn_origin;  /* where the entity started */
} BrushEntData;

/* Create a brush entity. Returns the Entity with BrushEntData attached. */
Entity* brush_entity_create(const char* classname, const char* targetname);

/* Add a brush to a brush entity. The brush mins/maxs should be relative
   to the entity's origin. Returns the Brush. */
Brush* brush_entity_add_brush(Entity* ent, Vec3 mins, Vec3 maxs,
                               const char* texture, int solid);

/* Get the BrushEntData from an entity */
BrushEntData* brush_entity_data(Entity* ent);

/* Get world-space mins/maxs for a brush owned by this entity */
void brush_entity_world_bounds(Entity* ent, Brush* b, Vec3* out_mins, Vec3* out_maxs);

#endif /* BRUSH_ENTITY_H */
