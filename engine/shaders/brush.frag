#version 450

layout(set = 0, binding = 0) uniform sampler2D textures[64];

layout(location = 0) in vec2 fragUV;
layout(location = 1) in float fragTexIndex;
layout(location = 2) in float fragAlpha;
layout(location = 3) in vec3 fragNormal;

layout(location = 0) out vec4 outColor;

void main() {
    int idx = int(fragTexIndex + 0.5);
    vec4 texColor = texture(textures[idx], fragUV);

    /* Face-based lighting: sun from above-right */
    vec3 lightDir = normalize(vec3(0.3, 0.2, 1.0));
    float ambient = 0.45;
    float diffuse = max(dot(normalize(fragNormal), lightDir), 0.0) * 0.55;
    float light = ambient + diffuse;

    outColor = vec4(texColor.rgb * light, texColor.a * fragAlpha);
    if (outColor.a < 0.01) discard;
}
