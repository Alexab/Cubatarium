#version 330 core

in vec4 vColor;
in vec2 vLocal;
out vec4 FragColor;

void main()
{
  if (vColor.a <= 0.01)
  {
    discard;
  }
  float edge = max(abs(vLocal.x), abs(vLocal.y));
  float shade = 1.0 - 0.3 * edge;
  FragColor = vec4(vColor.rgb * shade, vColor.a);
}
