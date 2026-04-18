#version 450

layout(push_constant) uniform PushConstants {
    mat4 transform;
} pc;

layout(location = 0) out vec2 fragUV;

/* Hardcoded triangle vertices + UVs */
vec2 positions[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

vec2 uvs[3] = vec2[](
    vec2(0.5, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

void main() {
    gl_Position = pc.transform * vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragUV = uvs[gl_VertexIndex];
}
