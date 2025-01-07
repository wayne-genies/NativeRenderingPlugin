#version 450

layout(location = 0) in vec3 vpos;
layout(location = 1) in vec4 vcol;
layout(location = 2) in vec2 vtex;
layout(location = 0) out vec4 color;
layout(location = 1) out vec2 ftex;
layout(push_constant) uniform PushConstants { mat4 matrix; };

vec2 texCoords[6] = vec2[](
    vec2(0.0, 0.0),
    vec2(1.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 0.0),
    vec2(1.0, 1.0),
    vec2(0.0, 1.0)
);

void main() {
    gl_Position = matrix * vec4(vpos, 1.0);
    color = vcol;
    ftex = vtex;
}