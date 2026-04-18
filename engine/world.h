/*
 * world.h — Game world / tick system
 *
 * Runs the game loop: physics, think, triggers, cleanup.
 */
#ifndef WORLD_H
#define WORLD_H

#include "entity.h"
#include "brush.h"
#include "trigger.h"
#include "console.h"

/* World state */
typedef struct {
    float   time;           /* current world time */
    float   tick_interval;  /* seconds per tick */
    int     tick_count;
    float   gravity;
} World;

/* Get the global world */
World* world_get(void);

/* Initialize all systems */
void world_init(void);

/* Run one tick: physics -> think -> triggers -> cleanup */
void world_tick(void);

/* Shutdown everything */
void world_shutdown(void);

#endif /* WORLD_H */
