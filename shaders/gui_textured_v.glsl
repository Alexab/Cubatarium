#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 screenSize;
uniform vec4 rectPx;

void main()
{
    vec2 pixelPos = rectPx.xy + aPos * rectPx.zw;
    vec2 normalizedPos = (pixelPos / screenSize) * 2.0 - 1.0;
    gl_Position = vec4(normalizedPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
