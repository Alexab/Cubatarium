#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTimeSec;
uniform float uIntensity;
uniform int uWeatherKind;
uniform float uQuality;
uniform float uDayFactor;
uniform float uWind;
uniform vec2 uWindDirection;
uniform vec3 uCameraRight;
uniform vec3 uCameraUp;
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

float rainLayer(vec2 uv, float scale, float speed, float width,
                float length, float seed_offset, vec2 wind)
{
  vec2 p = uv * vec2(scale * uResolution.x / max(uResolution.y, 1.0), scale);
  p.x += p.y * wind.x * 0.08;
  p += wind * 0.035 * uTimeSec;
  p.y += uTimeSec * speed;

  vec2 cell = floor(p);
  vec2 local = fract(p);
  float seed = hash12(cell + vec2(seed_offset, seed_offset * 1.73));
  float density = smoothstep(0.34, 0.9, hash12(cell + vec2(17.1, 31.7)));
  float streak_x = mix(0.12, 0.88, hash12(cell + vec2(4.7, 9.2)));
  float streak_phase = fract(local.y + seed * 1.7);
  float width_mask = 1.0 - smoothstep(width * 0.55, width, abs(local.x - streak_x));
  float head = smoothstep(0.0, 0.12, streak_phase);
  float tail = 1.0 - smoothstep(length * 0.72, length, streak_phase);
  float noise = 0.78 + 0.22 * hash12(cell + floor(uTimeSec * 3.0));
  return width_mask * head * tail * density * noise;
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
  float wind_strength = clamp(uWind, 0.0, 1.0);
  vec3 wind_world = vec3(uWindDirection.x, 0.0, uWindDirection.y);
  vec2 wind = vec2(dot(wind_world, uCameraRight),
                   dot(wind_world, uCameraUp));
  if (length(wind) > 1.0e-4)
  {
    wind = normalize(wind) * wind_strength;
  }

  if (uWeatherKind == 1)
  {
    float fine = rainLayer(uv, 150.0, 6.2, 0.075, 0.55, 13.7, wind);
    float medium = rainLayer(uv, 82.0, 4.4, 0.12, 0.72, 41.3, wind);
    float broad = rainLayer(uv, 38.0, 3.0, 0.17, 0.88, 77.9, wind);
    float streaks = clamp(fine * 0.64 + medium * 0.48 + broad * 0.28, 0.0, 1.0);
    alpha = streaks * intensity * mix(0.36, 0.52, clamp(uQuality, 0.0, 1.0)) * sky_mask;
    color = mix(vec3(0.5, 0.58, 0.68), vec3(0.72, 0.8, 0.9), day);
  }
  else if (uWeatherKind == 2)
  {
    vec2 drift = vec2(wind.x * t * 0.1, -t * 0.12 + wind.y * t * 0.1);
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
