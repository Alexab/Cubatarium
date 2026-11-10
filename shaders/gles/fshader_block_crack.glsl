#version 300 es
precision mediump float;

in vec2 vTexCoord;
out vec4 FragColor;

uniform sampler2D uCrackTex;
uniform float uAlpha;

void main()
{
  vec4 crack = texture(uCrackTex, vTexCoord);
  float alpha = crack.a * clamp(uAlpha, 0.0, 1.0);
  if (alpha <= 0.01)
  {
    discard;
  }
  FragColor = vec4(crack.rgb, alpha);
}
