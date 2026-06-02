#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in mat4 instanceMVP;

out vec2 TexCoord;

void main()
{
    gl_Position = instanceMVP * vec4(aPos, 1.0);
    TexCoord = aTexCoord;
}
