/*
 * entity.h — Base entity system (Source-style)
 *
 * Every object in the world is an entity. Entities have:
 *   - position, velocity, angles
 *   - classname and targetname (for I/O)
 *   - think/touch/use callbacks
 *   - flags and health
 */
#ifndef ENTITY_H
#define ENTITY_H

#include "vec3.h"

#define MAX_ENTITIES    512
#define ENT_NAME_LEN    64

/* Entity flags */
#define EF_NONE         0
#define EF_SOLID        (1 << 0)   /* participates in collision */
#define EF_TRIGGER      (1 << 1)   /* is a trigger volume */
#define EF_DEAD         (1 << 2)   /* marked for removal */
#define EF_NOCLIP       (1 << 3)   /* ignores collision */

typedef struct Entity Entity;

/* Callback types */
typedef void (*ThinkFunc)(Entity* self);
typedef void (*TouchFunc)(Entity* self, Entity* other);
typedef void (*UseFunc)(Entity* self, Entity* activator);

struct Entity {
    int         id;
    int         flags;
    char        classname[ENT_NAME_LEN];
    char        targetname[ENT_NAME_LEN];

    /* Transform */
    Vec3        origin;
    Vec3        angles;
    Vec3        velocity;

    /* Bounding box (mins/maxs relative to origin) */
    Vec3        mins;
    Vec3        maxs;

    /* Gameplay */
    int         health;
    int         max_health;

    /* Callbacks — like Source's Think/Touch/Use */
    ThinkFunc   think;
    TouchFunc   touch;
    UseFunc     use;

    float       next_think;  /* next time think() fires */

    /* For triggers: target to fire */
    char        target[ENT_NAME_LEN];

    /* Generic user data pointer */
    void*       userdata;
};

/* Entity management */
Entity* entity_spawn(void);
void    entity_remove(Entity* ent);
Entity* entity_find_by_name(const char* targetname);
Entity* entity_find_by_class(const char* classname);
Entity* entity_get(int id);
int     entity_count(void);

/* Access the global entity list */
Entity** entity_get_all(int* out_count);

/* Init/shutdown */
void entity_system_init(void);
void entity_system_shutdown(void);

#endif /* ENTITY_H */
