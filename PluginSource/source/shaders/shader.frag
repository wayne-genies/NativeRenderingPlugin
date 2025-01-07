#version 450


layout(location = 0) out vec4 fragColor;
layout(location = 0) in vec4 color;
layout(location = 1) in vec2 ftex;
layout(binding = 0) uniform sampler2D tex1Sampler;
layout(binding = 1) uniform sampler2D tex2Sampler;

void main() { 
    fragColor = (texture(tex1Sampler, ftex) + texture(tex2Sampler, ftex))*0.5;
}