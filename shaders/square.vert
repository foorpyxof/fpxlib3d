#version 450

layout(location = 0) out vec3 fragColor;

vec3 colors[4] = vec3[](
        vec3(0.5, 1.0, 0.0),
        vec3(0.5, 0.0, 1.0),
        vec3(1.0, 0.0, 0.0),
        vec3(0.6, 0.2, 0.9)
    );

vec2 positions[4] = vec2[](
        vec2(0.8, 0.8),
        vec2(0.5, 0.8),
        vec2(0.5, 0.6),
        vec2(0.8, 0.5)
    );

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
