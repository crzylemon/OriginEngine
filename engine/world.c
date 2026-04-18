/*
 * world.c — World tick implementation
 */
#include "world.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static World g_world;

World* world_get(void) {
    return &g_world;
}

void world_init(void) {
    memset(&g_world, 0, sizeof(World));
    g_world.tick_interval = 1.0f / 66.0f;  /* 66 tick, like Source */
    g_world.gravity = 800.0f;

    console_init();
    entity_system_init();
    brush_system_init();
    trigger_system_init();

    /* Register engine cvars */
    cvar_register("sv_gravity", 800.0f);
    cvar_register("sv_maxspeed", 320.0f);
    cvar_register("host_timescale", 1.0f);

    printf("[world] initialized (tickrate: %.0f)\n",
           1.0f / g_world.tick_interval);
}

/* ── Physics: move entities, collide with brushes ────────────── */

/* Test if entity at position collides with any solid brush */
static int test_position(Entity* e, Vec3 pos) {
    int bcount;
    Brush** brushes = brush_get_all(&bcount);
    Vec3 ent_mins = vec3_add(pos, e->mins);
    Vec3 ent_maxs = vec3_add(pos, e->maxs);

    for (int j = 0; j < bcount; j++) {
        if (brushes[j] && brushes[j]->solid) {
            if (brush_overlaps_aabb(brushes[j], ent_mins, ent_maxs))
                return 1;
        }
    }
    return 0;
}

static void world_physics(void) {
    int count;
    Entity** ents = entity_get_all(&count);
    float dt = g_world.tick_interval;
    float gravity = cvar_get("sv_gravity");

    for (int i = 0; i < count; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        if (e->flags & EF_NOCLIP) {
            e->origin = vec3_add(e->origin, vec3_scale(e->velocity, dt));
            continue;
        }

        /* Apply gravity */
        if (e->flags & EF_SOLID) {
            e->velocity.z -= gravity * dt;
        }

        if (!(e->flags & EF_SOLID)) {
            e->origin = vec3_add(e->origin, vec3_scale(e->velocity, dt));
            continue;
        }

        /* Per-axis collision: try each axis independently (slide along walls) */
        Vec3 try_pos;

        /* X axis */
        try_pos = e->origin;
        try_pos.x += e->velocity.x * dt;
        if (!test_position(e, try_pos)) {
            e->origin.x = try_pos.x;
        } else {
            e->velocity.x = 0;
        }

        /* Y axis */
        try_pos = e->origin;
        try_pos.y += e->velocity.y * dt;
        if (!test_position(e, try_pos)) {
            e->origin.y = try_pos.y;
        } else {
            e->velocity.y = 0;
        }

        /* Z axis */
        try_pos = e->origin;
        try_pos.z += e->velocity.z * dt;
        if (!test_position(e, try_pos)) {
            e->origin.z = try_pos.z;
        } else {
            /* If falling and hit something below, we're on ground */
            if (e->velocity.z < 0) {
                /* on ground — zero out z velocity */
            }
            e->velocity.z = 0;
        }
    }
}

/* ── Think: call entity think functions ──────────────────────── */

static void world_think(void) {
    int count;
    Entity** ents = entity_get_all(&count);

    for (int i = 0; i < count; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;

        if (e->think && e->next_think > 0 && g_world.time >= e->next_think) {
            e->next_think = -1;  /* reset, think can set it again */
            e->think(e);
        }
    }
}

/* ── Triggers: check entities against trigger volumes ────────── */

static void world_triggers(void) {
    int count;
    Entity** ents = entity_get_all(&count);

    for (int i = 0; i < count; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        if (e->flags & EF_TRIGGER) continue;  /* triggers don't trigger triggers */

        trigger_check_entity(e);
    }
}

/* ── Cleanup: remove dead entities ───────────────────────────── */

static void world_cleanup(void) {
    int count;
    Entity** ents = entity_get_all(&count);

    for (int i = 0; i < count; i++) {
        if (ents[i] && (ents[i]->flags & EF_DEAD)) {
            printf("[world] removing dead entity %d (%s)\n",
                   ents[i]->id, ents[i]->classname);
            free(ents[i]);
            /* Shift array */
            for (int j = i; j < count - 1; j++) {
                ents[j] = ents[j + 1];
            }
            ents[count - 1] = NULL;
            /* Update count through entity system — we cheat a bit here */
            count--;
            i--;
        }
    }
}

void world_tick(void) {
    g_world.time += g_world.tick_interval;
    g_world.tick_count++;

    world_physics();
    world_think();
    world_triggers();
    world_cleanup();
}

void world_shutdown(void) {
    trigger_system_shutdown();
    brush_system_shutdown();
    entity_system_shutdown();
    console_shutdown();
    printf("[world] shutdown\n");
}
