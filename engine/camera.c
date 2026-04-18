/*
 * camera.c — Free-flying FPS camera implementation
 */
#include "camera.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void camera_init(Camera* cam, Vec3 pos, float yaw, float pitch) {
    cam->position = pos;
    cam->yaw = yaw;
    cam->pitch = pitch;
    cam->speed = 200.0f;
    cam->sensitivity = 0.002f;
    cam->fov = 90.0f;
    cam->near_clip = 0.1f;
    cam->far_clip = 4000.0f;
}

Vec3 camera_forward(const Camera* cam) {
    return VEC3(
        cosf(cam->pitch) * cosf(cam->yaw),
        cosf(cam->pitch) * sinf(cam->yaw),
        sinf(cam->pitch)
    );
}

Vec3 camera_right(const Camera* cam) {
    /* Right = forward x world_up, simplified since no roll */
    return VEC3(
        sinf(cam->yaw),
        -cosf(cam->yaw),
        0.0f
    );
}

Vec3 camera_up(const Camera* cam) {
    (void)cam;
    return VEC3(0, 0, 1); /* world up for noclip cam */
}

void camera_mouse_look(Camera* cam, float dx, float dy) {
    cam->yaw -= dx * cam->sensitivity;
    cam->pitch -= dy * cam->sensitivity;

    /* Clamp pitch to avoid gimbal lock */
    float limit = (float)(89.0 * M_PI / 180.0);
    if (cam->pitch > limit) cam->pitch = limit;
    if (cam->pitch < -limit) cam->pitch = -limit;
}

void camera_move(Camera* cam, float forward, float right, float up, float dt) {
    Vec3 fwd = camera_forward(cam);
    Vec3 rt = camera_right(cam);
    Vec3 movement = VEC3_ZERO;

    movement = vec3_add(movement, vec3_scale(fwd, forward * cam->speed * dt));
    movement = vec3_add(movement, vec3_scale(rt, right * cam->speed * dt));
    movement.z += up * cam->speed * dt;

    cam->position = vec3_add(cam->position, movement);
}

void camera_view_matrix(const Camera* cam, float out[16]) {
    Vec3 f = camera_forward(cam);
    Vec3 r = camera_right(cam);
    /* Recompute up as right x forward for proper orthonormal basis */
    Vec3 u = VEC3(
        r.y * f.z - r.z * f.y,
        r.z * f.x - r.x * f.z,
        r.x * f.y - r.y * f.x
    );

    Vec3 p = cam->position;

    /* Column-major 4x4 look-at matrix */
    out[0]  = r.x;    out[1]  = u.x;    out[2]  = -f.x;   out[3]  = 0;
    out[4]  = r.y;    out[5]  = u.y;    out[6]  = -f.y;   out[7]  = 0;
    out[8]  = r.z;    out[9]  = u.z;    out[10] = -f.z;   out[11] = 0;
    out[12] = -(r.x*p.x + r.y*p.y + r.z*p.z);
    out[13] = -(u.x*p.x + u.y*p.y + u.z*p.z);
    out[14] =  (f.x*p.x + f.y*p.y + f.z*p.z);
    out[15] = 1;
}

void camera_proj_matrix(const Camera* cam, float aspect, float out[16]) {
    float fov_rad = cam->fov * (float)(M_PI / 180.0);
    float tan_half = tanf(fov_rad / 2.0f);
    float n = cam->near_clip;
    float f = cam->far_clip;

    memset(out, 0, 16 * sizeof(float));

    /* Vulkan clip: Y is flipped, Z is [0,1] */
    out[0]  = 1.0f / (aspect * tan_half);
    out[5]  = -1.0f / tan_half;  /* flip Y for Vulkan */
    out[10] = f / (n - f);
    out[11] = -1.0f;
    out[14] = (n * f) / (n - f);
}
