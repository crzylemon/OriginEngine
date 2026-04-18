/*
 * vec3.h — Simple 3D vector math
 */
#ifndef VEC3_H
#define VEC3_H

#include <math.h>

typedef struct {
    float x, y, z;
} Vec3;

#define VEC3(x, y, z) ((Vec3){(x), (y), (z)})
#define VEC3_ZERO VEC3(0, 0, 0)

static inline Vec3 vec3_add(Vec3 a, Vec3 b) {
    return VEC3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline Vec3 vec3_sub(Vec3 a, Vec3 b) {
    return VEC3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline Vec3 vec3_scale(Vec3 v, float s) {
    return VEC3(v.x * s, v.y * s, v.z * s);
}

static inline float vec3_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline float vec3_len(Vec3 v) {
    return sqrtf(vec3_dot(v, v));
}

static inline Vec3 vec3_normalize(Vec3 v) {
    float l = vec3_len(v);
    if (l < 0.0001f) return VEC3_ZERO;
    return vec3_scale(v, 1.0f / l);
}

#endif /* VEC3_H */
