/*
 * player.c — Player entity implementation
 *
 * The player is an Entity in the world. PlayerData is attached
 * via entity->userdata, just like Source attaches CBasePlayer
 * data to a CBaseEntity.
 */
#include "player.h"
#include "brush.h"
#include "brush_entity.h"
#include "prop.h"
#include "console.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Input state (set each frame before update) */
static float g_input_forward = 0;
static float g_input_right = 0;
static int   g_input_jump = 0;
static int   g_input_crouch = 0;

/* Player think callback — called by the entity system */
static void player_think(Entity* self) {
    /* Re-schedule think for next tick */
    /* (currently unused, but here for future use) */
    (void)self;
}

Entity* player_spawn(Vec3 spawn_pos) {
    Entity* ent = entity_spawn();
    if (!ent) return NULL;

    strcpy(ent->classname, "player");
    strcpy(ent->targetname, "player1");
    ent->origin = spawn_pos;
    ent->flags = EF_SOLID;
    ent->mins = VEC3(-16, -16, 0);
    ent->maxs = VEC3(16, 16, 72);
    ent->health = 100;
    ent->max_health = 100;
    ent->think = player_think;

    /* Allocate player-specific data */
    PlayerData* pd = (PlayerData*)calloc(1, sizeof(PlayerData));
    pd->yaw = 0;
    pd->pitch = 0;
    pd->eye_height = 64.0f;
    pd->move_speed = 250.0f;
    pd->jump_speed = 300.0f;
    pd->on_ground = 0;
    pd->noclip = 0;
    pd->crouching = 0;
    pd->wants_crouch = 0;
    pd->crouch_frac = 0.0f;
    pd->air_crouch_offset = 0.0f;
    camera_init(&pd->camera, spawn_pos, 0, 0);
    pd->camera.fov = 90.0f;

    ent->userdata = pd;

    printf("[player] spawned entity #%d at (%.0f, %.0f, %.0f)\n",
           ent->id, spawn_pos.x, spawn_pos.y, spawn_pos.z);
    return ent;
}

Entity* player_get(void) {
    return entity_find_by_class("player");
}

PlayerData* player_data(Entity* ent) {
    if (!ent) return NULL;
    return (PlayerData*)ent->userdata;
}

void player_set_input(Entity* ent, float forward, float right, int jump, int crouch) {
    (void)ent;
    g_input_forward = forward;
    g_input_right = right;
    g_input_jump = jump;
    g_input_crouch = crouch;
}

void player_mouse_look(Entity* ent, float dx, float dy) {
    PlayerData* pd = player_data(ent);
    if (!pd) return;

    pd->yaw -= dx * pd->camera.sensitivity;
    pd->pitch -= dy * pd->camera.sensitivity;

    float limit = (float)(89.0 * M_PI / 180.0);
    if (pd->pitch > limit) pd->pitch = limit;
    if (pd->pitch < -limit) pd->pitch = -limit;
}

/* Test if entity AABB at position overlaps any solid brush (world + brush entities) */
static int test_solid_collision(Vec3 pos, Vec3 mins, Vec3 maxs) {
    int bcount;
    Brush** brushes = brush_get_all(&bcount);
    Vec3 abs_mins = vec3_add(pos, mins);
    Vec3 abs_maxs = vec3_add(pos, maxs);

    /* World brushes */
    for (int i = 0; i < bcount; i++) {
        Brush* b = brushes[i];
        if (!b || !b->solid) continue;
        if (brush_overlaps_aabb(b, abs_mins, abs_maxs))
            return 1;
    }

    /* Brush entity brushes — offset by entity position */
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
            /* Offset brush bounds to world space */
            Vec3 bmin = vec3_add(b->mins, offset);
            Vec3 bmax = vec3_add(b->maxs, offset);
            /* Quick AABB check first */
            if (bmin.x >= abs_maxs.x || bmax.x <= abs_mins.x ||
                bmin.y >= abs_maxs.y || bmax.y <= abs_mins.y ||
                bmin.z >= abs_maxs.z || bmax.z <= abs_mins.z)
                continue;
            /* Full SAT check with offset vertices */
            if (brush_overlaps_aabb(b, vec3_sub(abs_mins, offset), vec3_sub(abs_maxs, offset)))
                return 1;
        }
    }

    /* Physics props — use actual mesh collision */
    int pcount;
    Prop* props = prop_get_all(&pcount);
    for (int i = 0; i < pcount; i++) {
        Prop* p = &props[i];
        if (!p->active || p->type != PROP_PHYSICS) continue;
        if (p->held) continue;
        if (prop_mesh_overlaps_aabb(p, abs_mins, abs_maxs))
            return 1;
    }

    return 0;
}

static int check_ground(Vec3 pos, Vec3 mins, Vec3 maxs) {
    Vec3 test = pos;
    test.z -= 0.1f;  /* small epsilon check */
    return test_solid_collision(test, mins, maxs);
}

static void update_noclip(Entity* ent, PlayerData* pd, float dt) {
    Vec3 fwd = camera_forward(&pd->camera);
    Vec3 rt = camera_right(&pd->camera);

    Vec3 move = VEC3_ZERO;
    move = vec3_add(move, vec3_scale(fwd, g_input_forward * pd->move_speed * dt));
    move = vec3_add(move, vec3_scale(rt, g_input_right * pd->move_speed * dt));
    if (g_input_jump > 0) move.z += pd->move_speed * dt;
    if (g_input_jump < 0) move.z -= pd->move_speed * dt;

    ent->origin = vec3_add(ent->origin, move);
    ent->flags |= EF_NOCLIP;

    /* Keep bbox at standing size in noclip */
    ent->mins = VEC3(-16, -16, 0);
    ent->maxs = VEC3(16, 16, PLAYER_HEIGHT_STAND);
    pd->eye_height = PLAYER_EYE_STAND;
    pd->crouch_frac = 0.0f;
    pd->crouching = 0;
    pd->on_ground = 0;
    ent->velocity = VEC3_ZERO;
}

static void update_normal(Entity* ent, PlayerData* pd, float dt) {
    float gravity = cvar_get("sv_gravity");
    ent->flags &= ~EF_NOCLIP;

    /* ── Crouch ────────────────────────────────────────────── */
    pd->wants_crouch = g_input_crouch;

    if (pd->wants_crouch) {
        pd->crouch_frac += PLAYER_CROUCH_SPEED * dt;
        if (pd->crouch_frac > 1.0f) pd->crouch_frac = 1.0f;
        pd->crouching = 1;
    } else if (pd->crouching) {
        Vec3 stand_mins = VEC3(-16, -16, 0);
        Vec3 stand_maxs = VEC3(16, 16, PLAYER_HEIGHT_STAND);
        if (!test_solid_collision(ent->origin, stand_mins, stand_maxs)) {
            pd->crouch_frac -= PLAYER_CROUCH_SPEED * dt;
            if (pd->crouch_frac <= 0.0f) {
                pd->crouch_frac = 0.0f;
                pd->crouching = 0;
            }
        }
    }

    float lerp = pd->crouch_frac;
    float height = PLAYER_HEIGHT_STAND + (PLAYER_HEIGHT_CROUCH - PLAYER_HEIGHT_STAND) * lerp;
    pd->eye_height = PLAYER_EYE_STAND + (PLAYER_EYE_CROUCH - PLAYER_EYE_STAND) * lerp;

    /* Bbox: mins.z always 0, crouch shrinks from top.
       Crouch-jumping works because the shorter bbox clears obstacles
       that the standing bbox wouldn't — same as Source. */
    ent->mins = VEC3(-16, -16, 0);
    ent->maxs = VEC3(16, 16, height);

    float speed_mult = pd->crouching ? 0.5f : 1.0f;

    /* ── Movement ──────────────────────────────────────────── */
    float cy = cosf(pd->yaw), sy = sinf(pd->yaw);
    Vec3 fwd = VEC3(cy, sy, 0);
    Vec3 rt = VEC3(sy, -cy, 0);

    Vec3 wish = VEC3_ZERO;
    wish = vec3_add(wish, vec3_scale(fwd, g_input_forward * pd->move_speed * speed_mult));
    wish = vec3_add(wish, vec3_scale(rt, g_input_right * pd->move_speed * speed_mult));

    ent->velocity.x = wish.x;
    ent->velocity.y = wish.y;

    if (!pd->on_ground) {
        ent->velocity.z -= gravity * dt;
    } else {
        if (ent->velocity.z < 0) ent->velocity.z = 0;
    }

    if (g_input_jump > 0 && pd->on_ground) {
        ent->velocity.z = pd->jump_speed;
        pd->on_ground = 0;
    }

    /* ── Collision with slope step-up ─────────────────────────── */
    #define STEP_HEIGHT 18.0f
    Vec3 try_pos;

    /* Try full horizontal move */
    try_pos = ent->origin;
    try_pos.x += ent->velocity.x * dt;
    try_pos.y += ent->velocity.y * dt;

    if (!test_solid_collision(try_pos, ent->mins, ent->maxs)) {
        ent->origin.x = try_pos.x;
        ent->origin.y = try_pos.y;
    } else if (pd->on_ground) {
        /* Blocked — try stepping up */
        int stepped = 0;
        for (float step = 1.0f; step <= STEP_HEIGHT; step += 1.0f) {
            Vec3 step_pos = try_pos;
            step_pos.z += step;
            if (!test_solid_collision(step_pos, ent->mins, ent->maxs)) {
                ent->origin = step_pos;
                stepped = 1;
                break;
            }
        }
        if (!stepped) {
            /* Try X only */
            Vec3 try_x = ent->origin;
            try_x.x += ent->velocity.x * dt;
            if (!test_solid_collision(try_x, ent->mins, ent->maxs))
                ent->origin.x = try_x.x;
            else
                ent->velocity.x = 0;
            /* Try Y only */
            Vec3 try_y = ent->origin;
            try_y.y += ent->velocity.y * dt;
            if (!test_solid_collision(try_y, ent->mins, ent->maxs))
                ent->origin.y = try_y.y;
            else
                ent->velocity.y = 0;
        }
    } else {
        /* In air — per-axis sliding */
        Vec3 try_x = ent->origin;
        try_x.x += ent->velocity.x * dt;
        if (!test_solid_collision(try_x, ent->mins, ent->maxs))
            ent->origin.x = try_x.x;
        else
            ent->velocity.x = 0;
        Vec3 try_y = ent->origin;
        try_y.y += ent->velocity.y * dt;
        if (!test_solid_collision(try_y, ent->mins, ent->maxs))
            ent->origin.y = try_y.y;
        else
            ent->velocity.y = 0;
    }

    if (ent->velocity.z != 0) {
        try_pos = ent->origin;
        try_pos.z += ent->velocity.z * dt;
        if (!test_solid_collision(try_pos, ent->mins, ent->maxs)) {
            ent->origin.z = try_pos.z;
            pd->on_ground = 0;
            /* Track fall speed */
            if (ent->velocity.z < pd->fall_velocity)
                pd->fall_velocity = ent->velocity.z;
        } else {
            if (ent->velocity.z <= 0) {
                pd->on_ground = 1;
                /* Fall damage: threshold ~300 units/s, scales above that */
                float impact = -pd->fall_velocity;
                if (impact > 300.0f) {
                    int dmg = (int)((impact - 300.0f) * 0.1f);
                    if (dmg > 0) {
                        ent->health -= dmg;
                        if (ent->health < 0) ent->health = 0;
                        printf("[player] fall damage: %d (impact %.0f)\n", dmg, impact);
                    }
                }
                pd->fall_velocity = 0;
            }
            ent->velocity.z = 0;
        }
    }

    /* Edge check + ground snap for slopes */
    if (pd->on_ground) {
        /* Try to snap down to stay on slopes when walking downhill */
        Vec3 snap = ent->origin;
        Vec3 best = ent->origin;
        int found_ground = 0;
        for (float drop = 0.25f; drop <= STEP_HEIGHT + 2.0f; drop += 0.25f) {
            snap.z = ent->origin.z - drop;
            if (test_solid_collision(snap, ent->mins, ent->maxs)) {
                found_ground = 1;
                break;
            }
            best = snap;
        }
        if (found_ground) {
            ent->origin = best;
        } else {
            /* No ground below — we walked off an edge */
            pd->on_ground = 0;
        }
    }
}

void player_update(Entity* ent, float dt) {
    PlayerData* pd = player_data(ent);
    if (!pd) return;

    if (pd->noclip)
        update_noclip(ent, pd, dt);
    else
        update_normal(ent, pd, dt);

    /* Sync camera to entity position */
    Vec3 eye = ent->origin;
    eye.z += pd->eye_height;
    pd->camera.position = eye;
    pd->camera.yaw = pd->yaw;
    pd->camera.pitch = pd->pitch;

    /* Sync entity angles for other systems */
    ent->angles = VEC3(pd->pitch, pd->yaw, 0);
}

Camera* player_get_camera(Entity* ent) {
    PlayerData* pd = player_data(ent);
    return pd ? &pd->camera : NULL;
}
