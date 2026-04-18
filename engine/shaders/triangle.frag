#version 450

layout(location = 0) in vec2 fragUV;
layout(location = 0) out vec4 outColor;

void main() {
    /* UV-colored: R = U, G = V, B = 1-U */
    outColor = vec4(fragUV.x, fragUV.y, 1.0 - fragUV.x, 1.0);
}
