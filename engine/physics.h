/*
 * physics.h — ODE-based physics system for Origin Engine
 */
#ifndef PHYSICS_H
#define PHYSICS_H

#include "vec3.h"

/* Init/shutdown */
int  physics_init(void);
void physics_shutdown(void);

/* Step the simulation */
void physics_tick(float dt);

/* Create a static box collider (world geometry) */
void physics_add_static_box(Vec3 mins, Vec3 maxs);

/* Add static trimesh collider (for slopes, wedges, etc.) */
void physics_add_static_trimesh(const float* verts, int vert_count,
                                 const int* indices, int tri_count);

/* Create a dynamic rigid body box. Returns body ID. */
int  physics_create_body_box(Vec3 pos, Vec3 half_size, float mass);

/* Create a dynamic rigid body cylinder. Returns body ID. */
int  physics_create_body_cylinder(Vec3 pos, float radius, float height, float mass);

/* Create a dynamic rigid body with trimesh collider. Returns body ID. */
int  physics_create_body_mesh(Vec3 pos, const float* verts, int vert_count,
                               const unsigned int* indices, int tri_count, float mass);

/* Get body position and rotation */
Vec3 physics_body_get_pos(int body_id);
void physics_body_get_rotation(int body_id, float out_mat[12]);

/* Set body position/velocity */
void physics_body_set_pos(int body_id, Vec3 pos);
void physics_body_set_vel(int body_id, Vec3 vel);
Vec3 physics_body_get_vel(int body_id);

/* Apply force/impulse */
void physics_body_add_force(int body_id, Vec3 force);

/* Enable/disable body (for held props) */
void physics_body_set_kinematic(int body_id, int kinematic);

/* Get gravity */
void physics_set_gravity(float gz);

#endif /* PHYSICS_H */
