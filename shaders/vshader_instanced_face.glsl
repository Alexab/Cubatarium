#version 330 core

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;
layout (location = 2) in mat4 instanceMVP;
layout (location = 6) in vec4 instanceAtlasUV;
layout (location = 7) in vec2 instanceQuadSize;

out vec2 TexCoord;

void main()
{
    gl_Position = instanceMVP * vec4(aPos, 1.0);
    vec2 quadSize = max(instanceQuadSize, vec2(1.0));
    vec2 tiled = fract(aTexCoord * quadSize);
    TexCoord = mix(instanceAtlasUV.xy, instanceAtlasUV.zw, tiled);
}
