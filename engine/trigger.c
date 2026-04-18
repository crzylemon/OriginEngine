/*
 * trigger.c — Trigger system implementation
 */
#include "trigger.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Trigger* g_triggers[MAX_TRIGGERS];
static int      g_trigger_count = 0;
static int      g_next_trigger_id = 1;

void trigger_system_init(void) {
    memset(g_triggers, 0, sizeof(g_triggers));
    g_trigger_count = 0;
    g_next_trigger_id = 1;
    printf("[trigger] system initialized\n");
}

void trigger_system_shutdown(void) {
    for (int i = 0; i < g_trigger_count; i++) {
        free(g_triggers[i]);
        g_triggers[i] = NULL;
    }
    g_trigger_count = 0;
    printf("[trigger] system shutdown\n");
}

Trigger* trigger_create(Vec3 mins, Vec3 maxs, int type, const char* name) {
    if (g_trigger_count >= MAX_TRIGGERS) {
        printf("[trigger] ERROR: max triggers reached!\n");
        return NULL;
    }

    Trigger* t = (Trigger*)calloc(1, sizeof(Trigger));
    if (!t) return NULL;

    t->id = g_next_trigger_id++;
    t->mins = mins;
    t->maxs = maxs;
    t->type = type;
    t->enabled = 1;
    t->fired = 0;
    t->delay = 0;
    t->on_trigger = NULL;
    strncpy(t->name, name, ENT_NAME_LEN - 1);

    g_triggers[g_trigger_count++] = t;
    return t;
}

static int point_in_trigger(const Trigger* t, Vec3 p) {
    return (p.x >= t->mins.x && p.x <= t->maxs.x &&
            p.y >= t->mins.y && p.y <= t->maxs.y &&
            p.z >= t->mins.z && p.z <= t->maxs.z);
}

void trigger_fire(Trigger* trig, Entity* activator) {
    if (!trig->enabled) return;
    if (trig->type == TRIG_ONCE && trig->fired) return;

    printf("[trigger] '%s' fired by entity %d (%s)\n",
           trig->name, activator->id, activator->classname);

    /* Call custom callback if set */
    if (trig->on_trigger) {
        trig->on_trigger(trig, activator);
    }

    /* Fire target entity's Use function (Source-style I/O) */
    if (trig->target[0] != '\0') {
        Entity* target = entity_find_by_name(trig->target);
        if (target && target->use) {
            printf("[trigger] -> firing Use on '%s'\n", target->targetname);
            target->use(target, activator);
        }
    }

    trig->fired = 1;
    if (trig->type == TRIG_ONCE) {
        trig->enabled = 0;
    }
}

void trigger_check_entity(Entity* ent) {
    if (!ent || (ent->flags & EF_DEAD)) return;

    for (int i = 0; i < g_trigger_count; i++) {
        Trigger* t = g_triggers[i];
        if (!t || !t->enabled) continue;

        int inside = point_in_trigger(t, ent->origin);

        if (inside && !t->occupied) {
            /* Just entered — fire */
            t->occupied = 1;
            trigger_fire(t, ent);
        } else if (!inside && t->occupied) {
            /* Just exited — reset so it can fire again */
            t->occupied = 0;
        }
    }
}

Trigger** trigger_get_all(int* out_count) {
    if (out_count) *out_count = g_trigger_count;
    return g_triggers;
}

int trigger_count(void) {
    return g_trigger_count;
}
