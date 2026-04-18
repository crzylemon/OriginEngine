/*
 * prop.c — Prop system with basic rigid body physics
 */
#include "prop.h"
#include "brush.h"
#include "brush_entity.h"
#include "entity.h"
#include "player.h"
#include "console.h"
#include "physics.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static Prop  g_props[MAX_PROPS];
static int   g_prop_count = 0;

void prop_system_init(void) {
    memset(g_props, 0, sizeof(g_props));
    g_prop_count = 0;
    printf("[prop] system initialized\n");
}

void prop_system_shutdown(void) {
    g_prop_count = 0;
    printf("[prop] system shutdown\n");
}

/* Compute AABB from mesh vertices (actual min/max, not symmetric) */
static void compute_aabb(Mesh* m, float scale, Vec3* out_mins, Vec3* out_maxs) {
    if (!m || m->vertex_count == 0) {
        *out_mins = VEC3(-8, -8, 0);
        *out_maxs = VEC3(8, 8, 16);
        return;
    }
    float mnx=1e9, mny=1e9, mnz=1e9, mxx=-1e9, mxy=-1e9, mxz=-1e9;
    for (int i = 0; i < m->vertex_count; i++) {
        float x = m->vertices[i].pos[0] * scale;
        float y = m->vertices[i].pos[1] * scale;
        float z = m->vertices[i].pos[2] * scale;
        if (x < mnx) mnx = x; if (x > mxx) mxx = x;
        if (y < mny) mny = y; if (y > mxy) mxy = y;
        if (z < mnz) mnz = z; if (z > mxz) mxz = z;
    }
    *out_mins = VEC3(mnx, mny, mnz);
    *out_maxs = VEC3(mxx, mxy, mxz);
}

static int add_prop_internal(const char* mesh_name, Vec3 origin, Vec3 angles,
                              float scale, int type, float mass) {
    if (g_prop_count >= MAX_PROPS) return -1;
    Prop* p = &g_props[g_prop_count];
    memset(p, 0, sizeof(Prop));
    p->active = 1;
    p->type = type;
    strncpy(p->mesh_name, mesh_name, 63);
    p->mesh = mesh_get(mesh_name);
    p->origin = origin;
    p->velocity = VEC3_ZERO;
    p->angles = angles;
    p->scale = scale > 0 ? scale : 1.0f;
    p->mass = mass > 0 ? mass : 1.0f;
    p->on_ground = 0;
    p->friction = 0.9f;
    p->bounciness = 0.2f;
    p->aabb_mins = VEC3(0,0,0);
    p->aabb_maxs = VEC3(0,0,0);
    compute_aabb(p->mesh, p->scale, &p->aabb_mins, &p->aabb_maxs);
    p->physics_body_id = -1;

    /* Create ODE body for physics props */
    if (type == PROP_PHYSICS && p->mesh) {
        /* Build vertex/index arrays from mesh for trimesh collider.
           Vertices must be relative to the AABB center (body origin). */
        float cx = (p->aabb_mins.x + p->aabb_maxs.x) * 0.5f;
        float cy = (p->aabb_mins.y + p->aabb_maxs.y) * 0.5f;
        float cz = (p->aabb_mins.z + p->aabb_maxs.z) * 0.5f;
        float* mv = (float*)malloc(p->mesh->vertex_count * 3 * sizeof(float));
        for (int v = 0; v < p->mesh->vertex_count; v++) {
            mv[v*3]   = p->mesh->vertices[v].pos[0] * p->scale - cx;
            mv[v*3+1] = p->mesh->vertices[v].pos[1] * p->scale - cy;
            mv[v*3+2] = p->mesh->vertices[v].pos[2] * p->scale - cz;
        }
        Vec3 center = VEC3(origin.x + cx, origin.y + cy, origin.z + cz);
        p->physics_body_id = physics_create_body_mesh(
            center, mv, p->mesh->vertex_count,
            p->mesh->indices, p->mesh->index_count / 3, mass);
        free(mv);
    } else if (type == PROP_PHYSICS) {
        Vec3 half = VEC3(
            (p->aabb_maxs.x - p->aabb_mins.x) * 0.5f,
            (p->aabb_maxs.y - p->aabb_mins.y) * 0.5f,
            (p->aabb_maxs.z - p->aabb_mins.z) * 0.5f
        );
        Vec3 center = VEC3(
            origin.x + (p->aabb_mins.x + p->aabb_maxs.x) * 0.5f,
            origin.y + (p->aabb_mins.y + p->aabb_maxs.y) * 0.5f,
            origin.z + (p->aabb_mins.z + p->aabb_maxs.z) * 0.5f
        );
        p->physics_body_id = physics_create_body_box(center, half, mass);
    }

    if (!p->mesh) printf("[prop] WARNING: mesh '%s' not found\n", mesh_name);
    return g_prop_count++;
}

int prop_add(const char* mesh_name, Vec3 origin, Vec3 angles, float scale) {
    return add_prop_internal(mesh_name, origin, angles, scale, PROP_STATIC, 0);
}

int prop_add_physics(const char* mesh_name, Vec3 origin, Vec3 angles,
                     float scale, float mass) {
    return add_prop_internal(mesh_name, origin, angles, scale, PROP_PHYSICS, mass);
}

/* ── Mesh-vs-AABB collision (triangle SAT test) ──────────────── */

static inline Vec3 vec3_cross(Vec3 a, Vec3 b) {
    return VEC3(a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x);
}

static inline float fmin3(float a, float b, float c) {
    float m = a < b ? a : b; return m < c ? m : c;
}
static inline float fmax3(float a, float b, float c) {
    float m = a > b ? a : b; return m > c ? m : c;
}

/* Test if triangle (v0,v1,v2) overlaps AABB centered at c with half-extents h.
   Uses Akenine-Möller's triangle-box overlap test. */
static int tri_aabb_overlap(Vec3 v0, Vec3 v1, Vec3 v2, Vec3 c, Vec3 h) {
    /* Move triangle so box is centered at origin */
    v0 = vec3_sub(v0, c); v1 = vec3_sub(v1, c); v2 = vec3_sub(v2, c);

    Vec3 e0 = vec3_sub(v1, v0), e1 = vec3_sub(v2, v1), e2 = vec3_sub(v0, v2);

    /* 9 cross-product axes (edge x AABB axis) */
    Vec3 edges[3] = {e0, e1, e2};
    Vec3 axes_unit[3] = {VEC3(1,0,0), VEC3(0,1,0), VEC3(0,0,1)};
    float hv[3] = {h.x, h.y, h.z};

    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            Vec3 a = vec3_cross(edges[i], axes_unit[j]);
            if (fabsf(a.x) < 1e-6f && fabsf(a.y) < 1e-6f && fabsf(a.z) < 1e-6f) continue;
            float p0 = vec3_dot(a, v0), p1 = vec3_dot(a, v1), p2 = vec3_dot(a, v2);
            float mn = fmin3(p0,p1,p2), mx = fmax3(p0,p1,p2);
            float r = hv[0]*fabsf(a.x) + hv[1]*fabsf(a.y) + hv[2]*fabsf(a.z);
            if (mn > r || mx < -r) return 0;
        }
    }

    /* 3 AABB face normals (X, Y, Z axes) */
    if (fmax3(v0.x,v1.x,v2.x) < -h.x || fmin3(v0.x,v1.x,v2.x) > h.x) return 0;
    if (fmax3(v0.y,v1.y,v2.y) < -h.y || fmin3(v0.y,v1.y,v2.y) > h.y) return 0;
    if (fmax3(v0.z,v1.z,v2.z) < -h.z || fmin3(v0.z,v1.z,v2.z) > h.z) return 0;

    /* Triangle normal */
    Vec3 n = vec3_cross(e0, e1);
    float d = vec3_dot(n, v0);
    float r = hv[0]*fabsf(n.x) + hv[1]*fabsf(n.y) + hv[2]*fabsf(n.z);
    if (d > r || d < -r) return 0;

    return 1;
}

/* Test if a prop's actual mesh triangles overlap an AABB (world-space) */
int prop_mesh_overlaps_aabb(const Prop* p, Vec3 aabb_mins, Vec3 aabb_maxs) {
    if (!p || !p->mesh || p->mesh->index_count < 3) {
        /* No mesh — fall back to AABB */
        Vec3 p_mins = vec3_add(p->origin, p->aabb_mins);
        Vec3 p_maxs = vec3_add(p->origin, p->aabb_maxs);
        return (aabb_mins.x < p_maxs.x && aabb_maxs.x > p_mins.x &&
                aabb_mins.y < p_maxs.y && aabb_maxs.y > p_mins.y &&
                aabb_mins.z < p_maxs.z && aabb_maxs.z > p_mins.z);
    }

    /* Broadphase: AABB vs prop AABB */
    Vec3 p_mins = vec3_add(p->origin, p->aabb_mins);
    Vec3 p_maxs = vec3_add(p->origin, p->aabb_maxs);
    if (aabb_mins.x >= p_maxs.x || aabb_maxs.x <= p_mins.x ||
        aabb_mins.y >= p_maxs.y || aabb_maxs.y <= p_mins.y ||
        aabb_mins.z >= p_maxs.z || aabb_maxs.z <= p_mins.z)
        return 0;

    /* AABB center and half-extents */
    Vec3 c = VEC3((aabb_mins.x+aabb_maxs.x)*0.5f,
                  (aabb_mins.y+aabb_maxs.y)*0.5f,
                  (aabb_mins.z+aabb_maxs.z)*0.5f);
    Vec3 h = VEC3((aabb_maxs.x-aabb_mins.x)*0.5f,
                  (aabb_maxs.y-aabb_mins.y)*0.5f,
                  (aabb_maxs.z-aabb_mins.z)*0.5f);

    /* Test each triangle */
    Mesh* m = p->mesh;
    float s = p->scale;
    Vec3 o = p->origin;
    int tri_count = m->index_count / 3;
    for (int t = 0; t < tri_count; t++) {
        unsigned int i0 = m->indices[t*3], i1 = m->indices[t*3+1], i2 = m->indices[t*3+2];
        Vec3 v0 = VEC3(m->vertices[i0].pos[0]*s + o.x,
                       m->vertices[i0].pos[1]*s + o.y,
                       m->vertices[i0].pos[2]*s + o.z);
        Vec3 v1 = VEC3(m->vertices[i1].pos[0]*s + o.x,
                       m->vertices[i1].pos[1]*s + o.y,
                       m->vertices[i1].pos[2]*s + o.z);
        Vec3 v2 = VEC3(m->vertices[i2].pos[0]*s + o.x,
                       m->vertices[i2].pos[1]*s + o.y,
                       m->vertices[i2].pos[2]*s + o.z);
        if (tri_aabb_overlap(v0, v1, v2, c, h)) return 1;
    }
    return 0;
}

/* Test if prop AABB at position overlaps any solid world brush, brush entity, or other prop */
static int prop_test_collision_ex(Vec3 pos, Vec3 mins, Vec3 maxs, int skip_prop_idx) {
    Vec3 abs_mins = vec3_add(pos, mins);
    Vec3 abs_maxs = vec3_add(pos, maxs);

    /* World brushes */
    int bcount;
    Brush** brushes = brush_get_all(&bcount);
    for (int i = 0; i < bcount; i++) {
        if (!brushes[i] || !brushes[i]->solid) continue;
        if (brush_overlaps_aabb(brushes[i], abs_mins, abs_maxs))
            return 1;
    }

    /* Brush entity brushes */
    int ecount;
    Entity** ents = entity_get_all(&ecount);
    for (int i = 0; i < ecount; i++) {
        Entity* e = ents[i];
        if (!e || (e->flags & EF_DEAD)) continue;
        BrushEntData* bd = brush_entity_data(e);
        if (!bd) continue;
        Vec3 offset = vec3_sub(e->origin, bd->spawn_origin);
        for (int j = 0; j < bd->brush_count; j++) {
            Brush* b = bd->brushes[j];
            if (!b || !b->solid) continue;
            if (brush_overlaps_aabb(b, vec3_sub(abs_mins, offset), vec3_sub(abs_maxs, offset)))
                return 1;
        }
    }

    /* Other physics props — use actual mesh collision */
    for (int i = 0; i < g_prop_count; i++) {
        if (i == skip_prop_idx) continue;
        Prop* other = &g_props[i];
        if (!other->active || other->type != PROP_PHYSICS) continue;
        if (prop_mesh_overlaps_aabb(other, abs_mins, abs_maxs))
            return 1;
    }

    return 0;
}

static void prop_physics_tick_old(float dt);
static void prop_physics_tick_ode(float dt);

void prop_physics_tick(float dt) {
    if (cvar_get("phy_old") >= 1.0f) {
        prop_physics_tick_old(dt);
    } else {
        prop_physics_tick_ode(dt);
    }
}

/* ODE physics: step ODE world, sync positions back to props */
static void prop_physics_tick_ode(float dt) {
    physics_tick(dt);

    Entity* player = entity_find_by_class("player");

    for (int i = 0; i < g_prop_count; i++) {
        Prop* p = &g_props[i];
        if (!p->active || p->type != PROP_PHYSICS) continue;
        if (p->physics_body_id < 0) continue;

        if (p->held) {
            /* Held props: ODE body is kinematic, position set by prop_update_held */
            continue;
        }

        /* Player push via ODE force */
        if (player) {
            Vec3 body_pos = physics_body_get_pos(p->physics_body_id);
            Vec3 diff = vec3_sub(body_pos, player->origin);
            float hx = (p->aabb_maxs.x - p->aabb_mins.x) * 0.5f;
            float hy = (p->aabb_maxs.y - p->aabb_mins.y) * 0.5f;
            float hz = (p->aabb_maxs.z - p->aabb_mins.z) * 0.5f;
            if (fabsf(diff.x) < 16 + hx && fabsf(diff.y) < 16 + hy && fabsf(diff.z) < 36 + hz) {
                float push = 800.0f;
                Vec3 force = VEC3(
                    player->velocity.x * push * dt,
                    player->velocity.y * push * dt,
                    0
                );
                physics_body_add_force(p->physics_body_id, force);
            }
        }

        /* Sync ODE position back to prop (offset for AABB center vs origin) */
        Vec3 ode_pos = physics_body_get_pos(p->physics_body_id);
        p->origin = VEC3(
            ode_pos.x - (p->aabb_mins.x + p->aabb_maxs.x) * 0.5f,
            ode_pos.y - (p->aabb_mins.y + p->aabb_maxs.y) * 0.5f,
            ode_pos.z - (p->aabb_mins.z + p->aabb_maxs.z) * 0.5f
        );
    }
}

/* Old built-in physics (no ODE) */
static void prop_physics_tick_old(float dt) {
    float gravity = cvar_get("sv_gravity");
    Entity* player = entity_find_by_class("player");
    Vec3 player_pos = player ? player->origin : VEC3(0,0,-9999);
    Vec3 player_vel = player ? player->velocity : VEC3_ZERO;
    Vec3 player_half = VEC3(16, 16, 36);

    for (int i = 0; i < g_prop_count; i++) {
        Prop* p = &g_props[i];
        if (!p->active || p->type != PROP_PHYSICS) continue;
        if (p->held) continue;

        if (!p->on_ground) p->velocity.z -= gravity * dt;
        if (p->on_ground) {
            p->velocity.x *= p->friction;
            p->velocity.y *= p->friction;
            if (fabsf(p->velocity.x) < 0.5f) p->velocity.x = 0;
            if (fabsf(p->velocity.y) < 0.5f) p->velocity.y = 0;
        }

        if (player) {
            Vec3 diff = vec3_sub(p->origin, player_pos);
            float ox = (player_half.x + (p->aabb_maxs.x-p->aabb_mins.x)*0.5f) - fabsf(diff.x);
            float oy = (player_half.y + (p->aabb_maxs.y-p->aabb_mins.y)*0.5f) - fabsf(diff.y);
            float oz = (player_half.z + (p->aabb_maxs.z-p->aabb_mins.z)*0.5f) - fabsf(diff.z);
            if (ox > 0 && oy > 0 && oz > 0) {
                float push = 150.0f / p->mass;
                if (fabsf(player_vel.x) > 10) p->velocity.x += (player_vel.x > 0 ? push : -push) * dt * 10;
                if (fabsf(player_vel.y) > 10) p->velocity.y += (player_vel.y > 0 ? push : -push) * dt * 10;
            }
        }

        Vec3 try_pos;
        try_pos = p->origin; try_pos.x += p->velocity.x * dt;
        if (!prop_test_collision_ex(try_pos, p->aabb_mins, p->aabb_maxs, i)) p->origin.x = try_pos.x;
        else p->velocity.x *= -p->bounciness;

        try_pos = p->origin; try_pos.y += p->velocity.y * dt;
        if (!prop_test_collision_ex(try_pos, p->aabb_mins, p->aabb_maxs, i)) p->origin.y = try_pos.y;
        else p->velocity.y *= -p->bounciness;

        try_pos = p->origin; try_pos.z += p->velocity.z * dt;
        if (!prop_test_collision_ex(try_pos, p->aabb_mins, p->aabb_maxs, i)) { p->origin.z = try_pos.z; p->on_ground = 0; }
        else { if (p->velocity.z < 0) { p->on_ground = 1; p->velocity.z = (p->velocity.z < -50) ? p->velocity.z * -p->bounciness : 0; } else p->velocity.z = 0; }

        if (p->on_ground) { Vec3 below = p->origin; below.z -= 0.5f;
            if (!prop_test_collision_ex(below, p->aabb_mins, p->aabb_maxs, i)) p->on_ground = 0; }
    }
}

Prop* prop_get_all(int* out_count) {
    if (out_count) *out_count = g_prop_count;
    return g_props;
}

/* ── Grab/hold system ────────────────────────────────────────── */

static int g_held_prop = -1;
static float g_hold_dist = 60.0f;

int prop_is_holding(void) {
    return g_held_prop >= 0;
}

void prop_try_grab(void) {
    if (g_held_prop >= 0) return; /* already holding */

    Entity* player = entity_find_by_class("player");
    if (!player) return;
    PlayerData* pd = (PlayerData*)player->userdata;
    if (!pd) return;

    Vec3 eye = player->origin;
    eye.z += pd->eye_height;
    Vec3 fwd = camera_forward(&pd->camera);

    /* Raycast: find closest physics prop in front of player */
    float best_dist = 150.0f; /* max grab range */
    int best = -1;

    for (int i = 0; i < g_prop_count; i++) {
        Prop* p = &g_props[i];
        if (!p->active || p->type != PROP_PHYSICS) continue;

        Vec3 to_prop = vec3_sub(p->origin, eye);
        float along = vec3_dot(to_prop, fwd);
        if (along < 0 || along > best_dist) continue;

        /* Check if prop is roughly in front (within cone) */
        Vec3 proj = vec3_add(eye, vec3_scale(fwd, along));
        float perp = vec3_len(vec3_sub(p->origin, proj));
        if (perp < (p->aabb_maxs.x - p->aabb_mins.x) + 20) {
            best_dist = along;
            best = i;
        }
    }

    if (best >= 0) {
        g_held_prop = best;
        g_props[best].held = 1;
        g_props[best].on_ground = 0;
        g_hold_dist = best_dist < 60 ? 60 : best_dist;
        /* Make ODE body kinematic while held */
        if (g_props[best].physics_body_id >= 0 && cvar_get("phy_old") < 1.0f)
            physics_body_set_kinematic(g_props[best].physics_body_id, 1);
        printf("[prop] grabbed '%s'\n", g_props[best].mesh_name);
    }
}

void prop_release(int throw_it) {
    if (g_held_prop < 0) return;
    Prop* p = &g_props[g_held_prop];
    p->held = 0;

    /* Restore ODE body to dynamic */
    if (p->physics_body_id >= 0 && cvar_get("phy_old") < 1.0f) {
        physics_body_set_kinematic(p->physics_body_id, 0);
    }

    if (throw_it) {
        /* Throw in camera direction */
        Entity* player = entity_find_by_class("player");
        if (player) {
            PlayerData* pd = (PlayerData*)player->userdata;
            if (pd) {
                Vec3 fwd = camera_forward(&pd->camera);
                float throw_speed = 200.0f / p->mass;
                p->velocity = vec3_scale(fwd, throw_speed);
                if (p->physics_body_id >= 0 && cvar_get("phy_old") < 1.0f)
                    physics_body_set_vel(p->physics_body_id, p->velocity);
            }
        }
        printf("[prop] threw '%s'\n", p->mesh_name);
    } else {
        printf("[prop] dropped '%s'\n", p->mesh_name);
    }

    g_held_prop = -1;
}

void prop_update_held(Vec3 eye, Vec3 forward) {
    if (g_held_prop < 0) return;
    Prop* p = &g_props[g_held_prop];

    /* Target position: in front of camera */
    Vec3 target = vec3_add(eye, vec3_scale(forward, g_hold_dist));

    /* Move toward target, but check collision each axis */
    Vec3 diff = vec3_sub(target, p->origin);
    Vec3 move = vec3_scale(diff, 0.3f);

    /* Try X */
    Vec3 try_pos = p->origin;
    try_pos.x += move.x;
    if (!prop_test_collision_ex(try_pos, p->aabb_mins, p->aabb_maxs, g_held_prop))
        p->origin.x = try_pos.x;

    /* Try Y */
    try_pos = p->origin;
    try_pos.y += move.y;
    if (!prop_test_collision_ex(try_pos, p->aabb_mins, p->aabb_maxs, g_held_prop))
        p->origin.y = try_pos.y;

    /* Try Z */
    try_pos = p->origin;
    try_pos.z += move.z;
    if (!prop_test_collision_ex(try_pos, p->aabb_mins, p->aabb_maxs, g_held_prop))
        p->origin.z = try_pos.z;

    p->velocity = VEC3_ZERO;
    p->on_ground = 0;

    /* Sync ODE body position */
    if (p->physics_body_id >= 0 && cvar_get("phy_old") < 1.0f) {
        Vec3 center = VEC3(
            p->origin.x + (p->aabb_mins.x + p->aabb_maxs.x) * 0.5f,
            p->origin.y + (p->aabb_mins.y + p->aabb_maxs.y) * 0.5f,
            p->origin.z + (p->aabb_mins.z + p->aabb_maxs.z) * 0.5f
        );
        physics_body_set_pos(p->physics_body_id, center);
        physics_body_set_vel(p->physics_body_id, VEC3_ZERO);
    }

    /* If prop is stuck (too far from target), drop it */
    float dist = vec3_len(vec3_sub(target, p->origin));
    if (dist > 200.0f) {
        prop_release(0);
    }
}
