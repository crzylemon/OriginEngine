/*
 * entity.c — Entity system implementation
 */
#include "entity.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Entity* g_entities[MAX_ENTITIES];
static int     g_entity_count = 0;
static int     g_next_id = 1;

void entity_system_init(void) {
    memset(g_entities, 0, sizeof(g_entities));
    g_entity_count = 0;
    g_next_id = 1;
    printf("[entity] system initialized\n");
}

void entity_system_shutdown(void) {
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i]) {
            free(g_entities[i]);
            g_entities[i] = NULL;
        }
    }
    g_entity_count = 0;
    printf("[entity] system shutdown\n");
}

Entity* entity_spawn(void) {
    if (g_entity_count >= MAX_ENTITIES) {
        printf("[entity] ERROR: max entities reached!\n");
        return NULL;
    }

    Entity* ent = (Entity*)calloc(1, sizeof(Entity));
    if (!ent) return NULL;

    ent->id = g_next_id++;
    ent->flags = EF_NONE;
    ent->health = 100;
    ent->max_health = 100;
    ent->origin = VEC3_ZERO;
    ent->angles = VEC3_ZERO;
    ent->velocity = VEC3_ZERO;
    ent->mins = VEC3(-16, -16, 0);
    ent->maxs = VEC3(16, 16, 72);
    ent->next_think = -1;

    g_entities[g_entity_count++] = ent;
    return ent;
}

void entity_remove(Entity* ent) {
    if (!ent) return;
    ent->flags |= EF_DEAD;
}

Entity* entity_find_by_name(const char* targetname) {
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i] && !(g_entities[i]->flags & EF_DEAD)) {
            if (strcmp(g_entities[i]->targetname, targetname) == 0)
                return g_entities[i];
        }
    }
    return NULL;
}

Entity* entity_find_by_class(const char* classname) {
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i] && !(g_entities[i]->flags & EF_DEAD)) {
            if (strcmp(g_entities[i]->classname, classname) == 0)
                return g_entities[i];
        }
    }
    return NULL;
}

Entity* entity_get(int id) {
    for (int i = 0; i < g_entity_count; i++) {
        if (g_entities[i] && g_entities[i]->id == id)
            return g_entities[i];
    }
    return NULL;
}

int entity_count(void) {
    return g_entity_count;
}

Entity** entity_get_all(int* out_count) {
    if (out_count) *out_count = g_entity_count;
    return g_entities;
}
