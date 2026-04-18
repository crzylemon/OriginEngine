/*
 * brush_entity.c — Brush entity implementation
 */
#include "brush_entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Entity* brush_entity_create(const char* classname, const char* targetname) {
    Entity* ent = entity_spawn();
    if (!ent) return NULL;

    strncpy(ent->classname, classname, ENT_NAME_LEN - 1);
    if (targetname)
        strncpy(ent->targetname, targetname, ENT_NAME_LEN - 1);

    ent->flags = EF_SOLID;

    BrushEntData* bd = (BrushEntData*)calloc(1, sizeof(BrushEntData));
    bd->brush_count = 0;
    bd->spawn_origin = VEC3_ZERO;
    ent->userdata = bd;

    return ent;
}

Brush* brush_entity_add_brush(Entity* ent, Vec3 mins, Vec3 maxs,
                               const char* texture, int solid) {
    BrushEntData* bd = brush_entity_data(ent);
    if (!bd || bd->brush_count >= MAX_BRUSH_ENT_BRUSHES) return NULL;

    /* Create the brush — store with relative coords.
       Mark it non-solid in the brush system since the entity handles collision.
       We'll use a negative solid value (-1) to mark it as entity-owned. */
    Brush* b = brush_create(mins, maxs, texture, solid);
    if (!b) return NULL;
    b->entity_owned = 1;

    bd->brushes[bd->brush_count++] = b;
    return b;
}

BrushEntData* brush_entity_data(Entity* ent) {
    if (!ent || !ent->userdata) return NULL;
    /* Brush entities: func_*, trigger_* */
    if (strncmp(ent->classname, "func_", 5) != 0 &&
        strncmp(ent->classname, "trigger_", 8) != 0) return NULL;
    return (BrushEntData*)ent->userdata;
}

void brush_entity_world_bounds(Entity* ent, Brush* b, Vec3* out_mins, Vec3* out_maxs) {
    BrushEntData* bd = brush_entity_data(ent);
    if (!bd) return;

    /* Offset from spawn to current position */
    Vec3 offset = vec3_sub(ent->origin, bd->spawn_origin);

    *out_mins = vec3_add(b->mins, offset);
    *out_maxs = vec3_add(b->maxs, offset);
}
