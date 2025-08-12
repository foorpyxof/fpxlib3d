#version 450

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

layout(set = 0, binding = 0) uniform vp {
    mat4 view;
    mat4 projection;
} v_p;

layout(set = 1, binding = 0) uniform mdl {
    mat4 matrix;
} model;

void main() {
    gl_Position = v_p.projection * v_p.view * model.matrix * vec4(inPosition, 0.0f, 1.0f);
    fragColor = inColor.yxy;
}
