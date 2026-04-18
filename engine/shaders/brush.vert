#version 450

layout(push_constant) uniform PushConstants {
    mat4 mvp;
} pc;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in float inTexIndex;
layout(location = 3) in float inAlpha;
layout(location = 4) in vec3 inNormal;

layout(location = 0) out vec2 fragUV;
layout(location = 1) out float fragTexIndex;
layout(location = 2) out float fragAlpha;
layout(location = 3) out vec3 fragNormal;

void main() {
    gl_Position = pc.mvp * vec4(inPosition, 1.0);
    fragUV = inUV;
    fragTexIndex = inTexIndex;
    fragAlpha = inAlpha;
    fragNormal = inNormal;
}
