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

  if (uDepthGuard > 0.5 && uDepthScreenSize.x > 0.0)
  {
    vec2 depth_uv = gl_FragCoord.xy / uDepthScreenSize;
    float scene_depth = texture(uSceneDepth, depth_uv).r;
    if (scene_depth < 0.999)
    {
      discard;
    }
  }
  else if (uv.y < 0.45)
  {
    discard;
  }

  float alpha = 0.0;
  vec3 color = vec3(0.72, 0.8, 0.9);
  float day = clamp(uDayFactor, 0.0, 1.0);
  float t = uTimeSec;
  float wind_bias = clamp(uWind, 0.0, 1.0) * 0.08;

  if (uWeatherKind == 1)
  {
    vec2 p = vec2(uv.x + uv.y * (0.035 + wind_bias), uv.y);
    float lane = hash12(vec2(floor((p.x + t * 0.13) * 160.0), 19.7));
    float phase =
        fract((1.0 - p.y) * 34.0 + t * (4.2 + wind_bias * 2.4) + lane * 7.0);
    float drop = smoothstep(0.88, 1.0, phase);
    float lane_mask = smoothstep(0.38, 0.88, lane);
    alpha = drop * lane_mask * intensity * 0.22;
    color = mix(vec3(0.5, 0.58, 0.68), vec3(0.72, 0.8, 0.9), day);
  }
  else if (uWeatherKind == 2)
  {
    vec2 cell = floor(vec2(uv.x * 90.0 + wind_bias * 20.0,
                           uv.y * 70.0 - t * 0.18));
    vec2 f = fract(vec2(uv.x * 90.0 + wind_bias * 20.0,
                        uv.y * 70.0 - t * 0.18)) - 0.5;
    float seed = hash12(cell);
    float d = length(f - vec2(seed - 0.5, fract(seed * 5.1) - 0.5) * 0.35);
    float flake = smoothstep(0.2, 0.0, d);
    alpha = flake * step(0.56, seed) * intensity * 0.42;
    color = vec3(0.94, 0.96, 1.0);
  }

  if (alpha <= 0.001)
  {
    discard;
  }
  FragColor = vec4(color, clamp(alpha, 0.0, 0.22));
}
