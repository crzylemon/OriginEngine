#version 450

layout(set = 0, binding = 0) uniform sampler2D skyTexture;

layout(push_constant) uniform PushConstants {
    vec4 forward;
    vec4 right;
    vec4 up;
    vec4 params; /* x=fov_tan, y=aspect */
} pc;

layout(location = 0) in vec2 fragScreenPos;
layout(location = 0) out vec4 outColor;

#define PI 3.14159265359

void main() {
    /* Build ray direction from screen position + camera basis */
    float fov_tan = pc.params.x;
    float aspect = pc.params.y;

    vec3 dir = normalize(
        pc.forward.xyz +
        pc.right.xyz * fragScreenPos.x * fov_tan * aspect -
        pc.up.xyz * fragScreenPos.y * fov_tan
    );

    /* Equirectangular: direction -> UV */
    float u = atan(dir.y, dir.x) / (2.0 * PI) + 0.5;
    float v = 1.0 - (asin(clamp(dir.z, -1.0, 1.0)) / PI + 0.5);

    outColor = texture(skyTexture, vec2(u, v));
}
