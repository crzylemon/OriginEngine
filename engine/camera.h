/*
 * camera.h — Free-flying FPS camera
 */
#ifndef CAMERA_H
#define CAMERA_H

#include "vec3.h"

typedef struct {
    Vec3  position;
    float yaw;       /* radians, 0 = looking down +X */
    float pitch;     /* radians, clamped to ~+-89 deg */
    float speed;     /* units per second */
    float sensitivity;
    float fov;       /* vertical FOV in degrees */
    float near_clip;
    float far_clip;
} Camera;

void camera_init(Camera* cam, Vec3 pos, float yaw, float pitch);

/* Get forward/right vectors from yaw/pitch */
Vec3 camera_forward(const Camera* cam);
Vec3 camera_right(const Camera* cam);
Vec3 camera_up(const Camera* cam);

/* Apply mouse delta */
void camera_mouse_look(Camera* cam, float dx, float dy);

/* Move relative to facing direction */
void camera_move(Camera* cam, float forward, float right, float up, float dt);

/* Build 4x4 view matrix (column-major for Vulkan) */
void camera_view_matrix(const Camera* cam, float out[16]);

/* Build 4x4 perspective projection (Vulkan clip space: Y flipped, Z 0..1) */
void camera_proj_matrix(const Camera* cam, float aspect, float out[16]);

#endif /* CAMERA_H */
