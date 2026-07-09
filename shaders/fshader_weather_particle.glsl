#version 330 core

in float vKind;
in float vAlpha;
in vec2 vTexCoord;
out vec4 FragColor;

uniform float uIntensity;

void main()
{
  vec2 p = vTexCoord - vec2(0.5);
  float d = length(p);
  float soft = smoothstep(0.5, 0.05, d);
  if (soft <= 0.001)
  {
    discard;
  }
  vec3 color = vec3(0.72, 0.8, 0.92);
  if (vKind > 1.5)
  {
    color = vec3(0.95, 0.97, 1.0);
    soft *= 0.85;
  }
  float alpha = soft * vAlpha * clamp(uIntensity, 0.0, 1.0);
  FragColor = vec4(color, clamp(alpha, 0.0, 0.45));
}
