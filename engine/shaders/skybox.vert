#version 450

layout(push_constant) uniform PushConstants {
    vec4 forward;  /* camera forward direction */
    vec4 right;    /* camera right direction */
    vec4 up;       /* camera up direction */
    vec4 params;   /* x=fov_tan, y=aspect, z=unused, w=unused */
} pc;

layout(location = 0) out vec2 fragScreenPos;

vec2 positions[3] = vec2[](
    vec2(-1, -1),
    vec2( 3, -1),
    vec2(-1,  3)
);

void main() {
    vec2 pos = positions[gl_VertexIndex];
    gl_Position = vec4(pos, 0.9999, 1.0);
    fragScreenPos = pos;
}
