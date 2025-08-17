#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in vec2 inTexCoord;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec2 fragTexCoord;

layout(set = 0, binding = 0) uniform vp {
    mat4 view;
    mat4 projection;
} v_p;

layout(set = 1, binding = 0) uniform mdl {
    mat4 matrix;
} model;

void main() {
    gl_Position = v_p.projection * v_p.view * model.matrix * vec4(inPosition, 1.0f);

    fragColor = inColor;
    fragTexCoord = inTexCoord;
}
