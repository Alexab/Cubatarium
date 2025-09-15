#version 330 core

layout (location = 0) in vec2 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 screenSize;

void main()
{
    // Преобразуем координаты из пикселей в нормализованные координаты (-1 до 1)
    vec2 normalizedPos = (aPos / screenSize) * 2.0 - 1.0;
    gl_Position = vec4(normalizedPos, 0.0, 1.0);
    TexCoord = aTexCoord;
}
