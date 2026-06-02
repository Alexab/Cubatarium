#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D texture0;
uniform vec4 tint;

void main()
{
    FragColor = texture(texture0, TexCoord) * tint;
}
