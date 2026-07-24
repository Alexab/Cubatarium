#version 300 es
precision mediump float;

in vec2 TexCoord;
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTimeSec;
uniform float uIntensity;
uniform int uWeatherKind;
uniform float uQuality;
uniform float uDayFactor;
uniform float uWind;
uniform sampler2D uSceneDepth;
uniform float uDepthGuard;
uniform vec2 uDepthScreenSize;
uniform float uDebugMode;

float hash12(vec2 p)
{
  vec3 p3 = fract(vec3(p.xyx) * 0.1031);
  p3 += dot(p3, p3.yzx + 33.33);
  return fract((p3.x + p3.y) * p3.z);
}

void main()
{
  vec2 uv = TexCoord;
  if (uDebugMode > 0.5)
  {
    FragColor = vec4(0.25, 0.55, 0.95, 0.28);
    return;
  }

  float intensity = clamp(uIntensity * uQuality, 0.0, 1.0);
  if (intensity <= 0.001)
  {
    discard;
  }

  float sky_mask = smoothstep(0.52, 0.9, uv.y);
  if (sky_mask <= 0.001)
  {
    discard;
  }

  if (uDepthGuard > 0.5 && uDepthScreenSize.x > 0.0)
  {
    vec2 depth_uv = gl_FragCoord.xy / uDepthScreenSize;
    float scene_depth = texture(uSceneDepth, depth_uv).r;
    if (scene_depth < 0.999)
    {
      discard;
    }
  }
  float alpha = 0.0;
  vec3 color = vec3(0.72, 0.8, 0.9);
  float day = clamp(uDayFactor, 0.0, 1.0);
  float t = uTimeSec;
  float wind_bias = clamp(uWind, 0.0, 1.0) * 0.08;

  if (uWeatherKind == 1)
  {
    vec2 p = vec2(uv.x + uv.y * (0.025 + wind_bias), uv.y);
    float lane_seed = hash12(vec2(floor((p.x + t * 0.07) * 220.0), floor(uv.y * 10.0)));
    float lane = smoothstep(0.62, 0.96, lane_seed);
    float run = fract((1.0 - p.y) * 52.0 + t * (5.8 + wind_bias * 3.0) + lane_seed * 11.0);
    float streak = smoothstep(0.93, 1.0, run);
    float turbulence = 0.7 + 0.3 * hash12(vec2(p.x * 120.0, t * 0.5));
    alpha = streak * lane * turbulence * intensity * 0.26 * sky_mask;
    color = mix(vec3(0.5, 0.58, 0.68), vec3(0.72, 0.8, 0.9), day);
  }
  else if (uWeatherKind == 2)
  {
    vec2 drift = vec2(t * (0.06 + wind_bias * 0.25), -t * 0.12);
    vec2 cell = floor((uv + drift) * vec2(70.0, 52.0));
    vec2 f = fract((uv + drift) * vec2(70.0, 52.0)) - 0.5;
    float seed = hash12(cell);
    vec2 jitter = vec2(seed - 0.5, fract(seed * 5.1) - 0.5) * 0.28;
    float d = length(f - jitter);
    float flake = smoothstep(0.23, 0.0, d);
    alpha = flake * step(0.44, seed) * intensity * 0.48 * sky_mask;
    color = vec3(0.94, 0.96, 1.0);
  }

  if (alpha <= 0.001)
  {
    discard;
  }
  FragColor = vec4(color, clamp(alpha, 0.0, 0.18));
}
