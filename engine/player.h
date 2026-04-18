/*
 * player.h — Player entity (Source-style CBasePlayer)
 *
 * The player is a regular Entity with classname "player".
 * Player-specific data (camera, input, etc.) lives in entity->userdata.
 */
#ifndef PLAYER_H
#define PLAYER_H

#include "entity.h"
#include "camera.h"

/* Player-specific data, stored in Entity->userdata */
typedef struct {
    float   yaw;
    float   pitch;
    float   eye_height;
    float   move_speed;
    float   jump_speed;
    int     on_ground;
    int     noclip;
    int     crouching;
    int     wants_crouch;   /* input: player is holding crouch */
    float   crouch_frac;    /* 0.0 = standing, 1.0 = fully crouched */
    float   air_crouch_offset;
    float   fall_velocity;     /* tracks Z vel while airborne for fall damage */
    Camera  camera;
} PlayerData;

/* Heights */
#define PLAYER_HEIGHT_STAND   72.0f
#define PLAYER_HEIGHT_CROUCH  36.0f
#define PLAYER_EYE_STAND      64.0f
#define PLAYER_EYE_CROUCH     28.0f
#define PLAYER_CROUCH_SPEED   8.0f  /* transition speed */

/* Spawn the player entity into the world. Returns the Entity. */
Entity* player_spawn(Vec3 spawn_pos);

/* Get the player entity (finds by classname "player") */
Entity* player_get(void);

/* Get the PlayerData from the player entity */
PlayerData* player_data(Entity* ent);

/* Update player: physics, collision, camera sync */
void    player_update(Entity* ent, float dt);

/* Set movement input for this frame */
void    player_set_input(Entity* ent, float forward, float right, int jump, int crouch);

/* Apply mouse look delta */
void    player_mouse_look(Entity* ent, float dx, float dy);

/* Get the player's camera */
Camera* player_get_camera(Entity* ent);

#endif /* PLAYER_H */
