/*
 * entity_io.h — Source-style Entity I/O system
 *
 * Entities have Outputs that fire Inputs on other entities.
 *
 * Output: "OnTrigger", "OnDamaged", "OnDeath", etc.
 * Input:  "Open", "Close", "Toggle", "Kill", "SetHealth", etc.
 *
 * Connection: output -> target_entity -> input -> parameter -> delay
 *
 * Example:
 *   entity_io_connect(trigger, "OnTrigger", "main_door", "Open", "", 0);
 *   entity_io_connect(trigger, "OnTrigger", "alarm", "TurnOn", "", 0.5);
 */
#ifndef ENTITY_IO_H
#define ENTITY_IO_H

#include "entity.h"

#define MAX_IO_CONNECTIONS 16  /* per entity */
#define IO_NAME_LEN 32

/* A single I/O connection */
typedef struct {
    char    output[IO_NAME_LEN];      /* output name on this entity */
    char    target[ENT_NAME_LEN];     /* targetname of receiving entity */
    char    input[IO_NAME_LEN];       /* input name on the target */
    char    parameter[ENT_NAME_LEN];  /* parameter string */
    float   delay;                    /* seconds before firing */
    int     once;                     /* fire only once then remove */
} IOConnection;

/* I/O data attached to an entity via userdata or a parallel array */
typedef struct {
    IOConnection connections[MAX_IO_CONNECTIONS];
    int          count;
} EntityIO;

/* Input handler callback: entity receives an input with a parameter */
typedef void (*InputFunc)(Entity* self, Entity* activator, const char* param);

/* Input registration: maps input names to functions */
typedef struct {
    char      name[IO_NAME_LEN];
    InputFunc func;
} InputEntry;

#define MAX_INPUTS_PER_CLASS 16

typedef struct {
    char        classname[ENT_NAME_LEN];
    InputEntry  inputs[MAX_INPUTS_PER_CLASS];
    int         count;
} InputClass;

/* ── API ─────────────────────────────────────────────────────── */

/* Init/shutdown */
void entity_io_init(void);

/* Get or create I/O data for an entity */
EntityIO* entity_io_get(Entity* ent);

/* Add a connection: when 'output' fires on 'ent', send 'input' to 'target' */
void entity_io_connect(Entity* ent, const char* output,
                       const char* target, const char* input,
                       const char* parameter, float delay, int once);

/* Fire an output on an entity — finds all connections and dispatches inputs */
void entity_io_fire_output(Entity* ent, const char* output, Entity* activator);

/* Register an input handler for a classname */
void entity_io_register_input(const char* classname, const char* input_name, InputFunc func);

/* Dispatch an input to an entity (looks up registered handler) */
void entity_io_send_input(Entity* target, const char* input_name,
                          Entity* activator, const char* parameter);

/* Tick delayed I/O events */
void entity_io_tick(float dt);

#endif /* ENTITY_IO_H */
