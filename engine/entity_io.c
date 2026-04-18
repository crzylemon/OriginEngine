/*
 * entity_io.c — Entity I/O implementation
 */
#include "entity_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Per-entity I/O storage ──────────────────────────────────── */

#define MAX_IO_ENTITIES 512
static EntityIO  g_io[MAX_IO_ENTITIES];
static Entity*   g_io_owners[MAX_IO_ENTITIES];
static int       g_io_count = 0;

/* ── Input class registry ────────────────────────────────────── */

#define MAX_INPUT_CLASSES 32
static InputClass g_input_classes[MAX_INPUT_CLASSES];
static int        g_input_class_count = 0;

/* ── Delayed events ──────────────────────────────────────────── */

typedef struct {
    char    target[ENT_NAME_LEN];
    char    input[IO_NAME_LEN];
    char    parameter[ENT_NAME_LEN];
    int     activator_id;
    float   delay;
} DelayedEvent;

#define MAX_DELAYED 64
static DelayedEvent g_delayed[MAX_DELAYED];
static int          g_delayed_count = 0;

void entity_io_init(void) {
    g_io_count = 0;
    g_input_class_count = 0;
    g_delayed_count = 0;
    memset(g_io, 0, sizeof(g_io));
    memset(g_io_owners, 0, sizeof(g_io_owners));
    printf("[entity_io] initialized\n");
}

EntityIO* entity_io_get(Entity* ent) {
    /* Find existing */
    for (int i = 0; i < g_io_count; i++) {
        if (g_io_owners[i] == ent) return &g_io[i];
    }
    /* Create new */
    if (g_io_count >= MAX_IO_ENTITIES) return NULL;
    int idx = g_io_count++;
    g_io_owners[idx] = ent;
    memset(&g_io[idx], 0, sizeof(EntityIO));
    return &g_io[idx];
}

void entity_io_connect(Entity* ent, const char* output,
                       const char* target, const char* input,
                       const char* parameter, float delay, int once) {
    EntityIO* io = entity_io_get(ent);
    if (!io || io->count >= MAX_IO_CONNECTIONS) return;

    IOConnection* c = &io->connections[io->count++];
    strncpy(c->output, output, IO_NAME_LEN - 1);
    strncpy(c->target, target, ENT_NAME_LEN - 1);
    strncpy(c->input, input, IO_NAME_LEN - 1);
    strncpy(c->parameter, parameter ? parameter : "", ENT_NAME_LEN - 1);
    c->delay = delay;
    c->once = once;
}

void entity_io_fire_output(Entity* ent, const char* output, Entity* activator) {
    EntityIO* io = entity_io_get(ent);
    if (!io) return;

    for (int i = 0; i < io->count; i++) {
        IOConnection* c = &io->connections[i];
        if (strcmp(c->output, output) != 0) continue;

        printf("[io] %s.%s -> %s.%s", ent->targetname, output, c->target, c->input);
        if (c->delay > 0) printf(" (delay %.1fs)", c->delay);
        printf("\n");

        if (c->delay > 0) {
            /* Queue delayed event */
            if (g_delayed_count < MAX_DELAYED) {
                DelayedEvent* de = &g_delayed[g_delayed_count++];
                strncpy(de->target, c->target, ENT_NAME_LEN - 1);
                strncpy(de->input, c->input, IO_NAME_LEN - 1);
                strncpy(de->parameter, c->parameter, ENT_NAME_LEN - 1);
                de->activator_id = activator ? activator->id : 0;
                de->delay = c->delay;
            }
        } else {
            /* Fire immediately */
            Entity* tgt = entity_find_by_name(c->target);
            if (tgt) {
                entity_io_send_input(tgt, c->input, activator, c->parameter);
            }
        }

        /* Remove if fire-once */
        if (c->once) {
            io->connections[i] = io->connections[--io->count];
            i--;
        }
    }
}

/* ── Input registry ──────────────────────────────────────────── */

void entity_io_register_input(const char* classname, const char* input_name, InputFunc func) {
    /* Find or create class entry */
    InputClass* ic = NULL;
    for (int i = 0; i < g_input_class_count; i++) {
        if (strcmp(g_input_classes[i].classname, classname) == 0) {
            ic = &g_input_classes[i];
            break;
        }
    }
    if (!ic && g_input_class_count < MAX_INPUT_CLASSES) {
        ic = &g_input_classes[g_input_class_count++];
        strncpy(ic->classname, classname, ENT_NAME_LEN - 1);
        ic->count = 0;
    }
    if (!ic || ic->count >= MAX_INPUTS_PER_CLASS) return;

    InputEntry* ie = &ic->inputs[ic->count++];
    strncpy(ie->name, input_name, IO_NAME_LEN - 1);
    ie->func = func;
}

void entity_io_send_input(Entity* target, const char* input_name,
                          Entity* activator, const char* parameter) {
    if (!target) return;

    /* Look up handler by classname */
    for (int i = 0; i < g_input_class_count; i++) {
        if (strcmp(g_input_classes[i].classname, target->classname) != 0) continue;
        for (int j = 0; j < g_input_classes[i].count; j++) {
            if (strcmp(g_input_classes[i].inputs[j].name, input_name) == 0) {
                g_input_classes[i].inputs[j].func(target, activator, parameter);
                return;
            }
        }
    }

    /* Fallback: "Use" input calls the entity's use callback */
    if (strcmp(input_name, "Use") == 0 && target->use) {
        target->use(target, activator);
        return;
    }

    /* Built-in inputs */
    if (strcmp(input_name, "Kill") == 0) {
        entity_remove(target);
    } else if (strcmp(input_name, "SetHealth") == 0 && parameter) {
        target->health = atoi(parameter);
    } else {
        printf("[io] WARNING: no handler for %s.%s\n", target->classname, input_name);
    }
}

/* ── Tick delayed events ─────────────────────────────────────── */

void entity_io_tick(float dt) {
    for (int i = 0; i < g_delayed_count; i++) {
        g_delayed[i].delay -= dt;
        if (g_delayed[i].delay <= 0) {
            Entity* tgt = entity_find_by_name(g_delayed[i].target);
            Entity* act = entity_get(g_delayed[i].activator_id);
            if (tgt) {
                entity_io_send_input(tgt, g_delayed[i].input, act, g_delayed[i].parameter);
            }
            /* Remove by swapping with last */
            g_delayed[i] = g_delayed[--g_delayed_count];
            i--;
        }
    }
}
