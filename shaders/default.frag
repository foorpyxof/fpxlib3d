#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec2 fragTexCoord;

layout(set = 1, binding = 1) uniform sampler2D texSampler[2];

layout(location = 0) out vec4 outColor;

void main() {
    if (fragTexCoord.x < 0.5 && fragTexCoord.y < 0.5 || fragTexCoord.x > 0.5 && fragTexCoord.y > 0.5)
        outColor = vec4(texture(texSampler[0], fragTexCoord * 2.0).rgb, 1.0);
    else
        outColor = vec4(texture(texSampler[1], fragTexCoord * 2.0).rgb, 1.0);
}
