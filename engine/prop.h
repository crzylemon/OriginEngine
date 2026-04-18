/*
 * prop.h — Static and dynamic props with basic physics
 */
#ifndef PROP_H
#define PROP_H

#include "vec3.h"
#include "mesh.h"

#define MAX_PROPS 128
#define PROP_STATIC  0
#define PROP_PHYSICS 1

typedef struct {
    int     active;
    int     type;
    char    mesh_name[64];
    Mesh*   mesh;
    Vec3    origin;
    Vec3    velocity;
    Vec3    angles;
    float   scale;
    Vec3    aabb_mins;
    Vec3    aabb_maxs;
    float   mass;
    int     on_ground;
    float   friction;
    float   bounciness;
    int     held;
    int     physics_body_id; /* ODE body ID, -1 if none */
} Prop;

int   prop_add(const char* mesh_name, Vec3 origin, Vec3 angles, float scale);
int   prop_add_physics(const char* mesh_name, Vec3 origin, Vec3 angles,
                       float scale, float mass);

/* Test if an AABB overlaps a prop's actual mesh geometry */
int   prop_mesh_overlaps_aabb(const Prop* p, Vec3 aabb_mins, Vec3 aabb_maxs);
void  prop_physics_tick(float dt);
Prop* prop_get_all(int* out_count);
void  prop_system_init(void);
void  prop_system_shutdown(void);
void  prop_try_grab(void);
void  prop_release(int throw_it);
void  prop_update_held(Vec3 eye, Vec3 forward);
int   prop_is_holding(void);

#endif /* PROP_H */
