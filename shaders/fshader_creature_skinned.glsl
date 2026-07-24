#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform sampler2D texture0;

void main()
{
  vec4 tex = texture(texture0, uv);
  if (tex.a < 0.05)
    discard;
  FragColor = vec4(tex.rgb, 1.0);
}
