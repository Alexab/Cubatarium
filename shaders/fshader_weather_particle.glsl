#version 330 core

in float vKind;
in float vAlpha;
in vec2 vTexCoord;
out vec4 FragColor;

uniform float uIntensity;

void main()
{
  vec2 p = vTexCoord - vec2(0.5);
  float alpha_mask = 0.0;
  vec3 color = vec3(0.72, 0.8, 0.92);

  if (vKind > 1.5)
  {
    float d = length(p);
    alpha_mask = smoothstep(0.5, 0.05, d) * 0.82;
    color = vec3(0.95, 0.97, 1.0);
  }
  else
  {
    vec2 rp = vec2(p.x * 4.2, p.y * 0.6 + 0.18);
    float body = smoothstep(0.28, 0.0, abs(rp.x)) *
                 smoothstep(0.95, 0.02, abs(rp.y));
    float tip = smoothstep(0.35, 0.0, length(vec2(rp.x * 0.8, rp.y - 0.85)));
    alpha_mask = max(body, tip * 0.8);
    color = vec3(0.62, 0.72, 0.86);
  }

  if (alpha_mask <= 0.001)
  {
    discard;
  }
  float alpha = alpha_mask * vAlpha * clamp(uIntensity, 0.0, 1.0);
  FragColor = vec4(color, clamp(alpha, 0.0, 0.45));
}
