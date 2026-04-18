/*
 * trigger.h — Trigger volumes (Source-style)
 *
 * Triggers are invisible volumes that fire outputs when
 * entities enter/exit them. Like trigger_once, trigger_multiple, etc.
 */
#ifndef TRIGGER_H
#define TRIGGER_H

#include "entity.h"

/* Trigger types */
#define TRIG_ONCE       0   /* fires once then disables */
#define TRIG_MULTIPLE   1   /* fires every time */

typedef struct Trigger Trigger;

typedef void (*TriggerCallback)(Trigger* trig, Entity* activator);

struct Trigger {
    int             id;
    char            name[ENT_NAME_LEN];
    char            target[ENT_NAME_LEN];
    Vec3            mins;
    Vec3            maxs;
    int             type;
    int             enabled;
    int             fired;
    int             occupied;   /* entity is currently inside */
    float           delay;
    TriggerCallback on_trigger;
};

#define MAX_TRIGGERS 128

Trigger* trigger_create(Vec3 mins, Vec3 maxs, int type, const char* name);
void     trigger_check_entity(Entity* ent);  /* test entity against all triggers */
void     trigger_fire(Trigger* trig, Entity* activator);

Trigger** trigger_get_all(int* out_count);
int       trigger_count(void);

void trigger_system_init(void);
void trigger_system_shutdown(void);

#endif /* TRIGGER_H */
