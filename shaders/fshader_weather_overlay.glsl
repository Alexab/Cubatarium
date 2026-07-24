#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTimeSec;
uniform float uIntensity;
uniform int uWeatherKind; // 1 rain/storm, 2 snow
uniform float uQuality;
uniform float uDayFactor;

float hash12(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

void main()
{
  vec2 uv = TexCoord;
  vec2 screen = uv * uResolution;
  float intensity = clamp(uIntensity * uQuality, 0.0, 1.0);
  if (intensity <= 0.001)
  {
    discard;
  }

  float alpha = 0.0;
  vec3 tint = vec3(0.8, 0.85, 0.9);
  if (uWeatherKind == 1)
  {
    vec2 motion = vec2(-0.22, -1.4) * uTimeSec;
    vec2 grid = floor((screen + motion * 220.0) / 10.0);
    float seed = hash12(grid);
    float drop = fract((screen.y + motion.y * 800.0 + seed * 180.0) / 28.0);
    float line = smoothstep(0.98, 1.0, drop) * smoothstep(0.2, 1.0, seed);
    alpha = line * intensity * 0.23;
    tint = mix(vec3(0.62, 0.70, 0.78), vec3(0.78, 0.84, 0.9), clamp(uDayFactor, 0.0, 1.0));
  }
  else if (uWeatherKind == 2)
  {
    vec2 motion = vec2(sin(uTimeSec * 0.23), -0.35) * uTimeSec;
    vec2 grid = floor((screen + motion * 140.0) / 14.0);
    float seed = hash12(grid);
    vec2 local = fract((screen + motion * 140.0) / 14.0) - 0.5;
    float flake = smoothstep(0.24, 0.0, length(local + vec2(seed - 0.5) * 0.25));
    alpha = flake * intensity * (0.16 + seed * 0.08);
    tint = mix(vec3(0.68, 0.74, 0.82), vec3(0.92, 0.95, 1.0), clamp(uDayFactor, 0.0, 1.0));
  }

  alpha = clamp(alpha, 0.0, 0.35);
  if (alpha <= 0.001)
  {
    discard;
  }
  FragColor = vec4(tint, alpha);
}
