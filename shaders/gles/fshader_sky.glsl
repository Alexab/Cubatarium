#version 300 es
precision highp float;
precision highp int;

in vec2 TexCoord;
out vec4 FragColor;

uniform vec4 skyColor;
uniform float uFogHorizonBlend;
uniform vec3 uFogColor;
uniform float uHorizonFogRadial;
uniform float uHorizonFogCelestialTint;
uniform float uFogHorizonElevation;
uniform float uTimeOfDay;
uniform float uStarVisibility;
uniform float uCloudCoverage;
uniform float uElapsedSec;
uniform vec2 uWindDirection;
uniform float uWindStrength;
uniform sampler2D uWeatherField;
uniform float uWeatherFieldValid;
uniform vec2 uWeatherFieldCenter;
uniform float uWeatherFieldWorldSize;
uniform int uCelestialCount;
uniform vec3 uCelestialDir[4];
uniform vec3 uCelestialColor[4];
uniform float uCelestialIntensity[4];
uniform float uCelestialAngularSizeDeg[4];
uniform int uCelestialType[4];
uniform mat3 uInvViewRot;
uniform mat4 uInvProjection;
uniform float uPerspective;
uniform mat3 uStarCelestialInv;
uniform vec3 uCameraPos;
uniform float uUnderwaterSkyAmount;
uniform float uScreenWaterlineNdc;
uniform vec3 uUnderwaterFogColor;

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash12(i);
    float b = hash12(i + vec2(1.0, 0.0));
    float c = hash12(i + vec2(0.0, 1.0));
    float d = hash12(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}

float hash31(vec3 p)
{
    p = fract(p * 0.1031);
    p += dot(p, p.yxz + 33.33);
    return fract((p.x + p.y) * p.z);
}

vec3 starLayer(vec3 direction, float cell_scale, float threshold, float size,
               float mag_boost)
{
    vec3 st = (direction * 0.5 + 0.5) * cell_scale + vec3(19.17, 78.3, 41.6);
    vec3 cell = floor(st);
    vec3 jitter = vec3(hash31(cell + vec3(31.7, 7.1, 4.2)),
                       hash31(cell + vec3(4.2, 53.9, 31.7)),
                       hash31(cell + vec3(7.1, 4.2, 53.9))) * 0.6 + 0.2;
    vec3 star_direction = normalize((cell + jitter - vec3(19.17, 78.3, 41.6)) /
                                    cell_scale * 2.0 - 1.0);
    float rnd = hash31(cell);
    float rnd2 = hash31(cell + vec3(31.7, 7.1, 4.2));
    float rnd3 = hash31(cell + vec3(4.2, 53.9, 31.7));
    float mask = step(threshold, rnd);
    // Soft core + wider glow reduces temporal aliasing of sub-pixel dots.
    float d = length(direction - star_direction);
    float core = 1.0 - smoothstep(size * 0.25, size, d);
    float glow = (1.0 - smoothstep(size * 0.85, size * 2.4, d)) * 0.40;
    float star = core + glow;
    float mag = pow(rnd2, 7.0) * mag_boost;
    vec3 cool = vec3(0.75, 0.82, 1.0);
    vec3 warm = vec3(1.0, 0.92, 0.78);
    vec3 col = mix(cool, warm, rnd3);
    return col * (star * mask * mag);
}

vec3 starFieldColor(vec3 dir_star)
{
    // A jittered 3D direction lattice avoids projection seams and polar
    // crowding while remaining deterministic under sidereal rotation.
    vec3 dense = starLayer(dir_star, 160.0, 0.991, 0.0018, 1.05);
    vec3 bright = starLayer(dir_star, 72.0, 0.9975, 0.0034, 2.2);
    return dense + bright;
}

float cloudDensity(vec2 world_xz, float time_shift, float scale)
{
    vec2 wind = normalize(uWindDirection + vec2(1.0e-4)) *
                (0.25 + clamp(uWindStrength, 0.0, 1.0));
    vec2 p = (world_xz + wind * time_shift) * scale;
    float broad = valueNoise(p);
    float medium = valueNoise(p * 2.7 + vec2(13.2, -7.1));
    float detail = valueNoise(p * 8.0 + vec2(-4.1, 22.7));
    return clamp(broad * 0.55 + medium * 0.32 + detail * 0.13, 0.0, 1.0);
}

float sampleWeatherCoverage(vec2 world_xz)
{
    if (uWeatherFieldValid < 0.5)
    {
        return clamp(uCloudCoverage, 0.0, 1.0);
    }
    vec2 uv = (world_xz - uWeatherFieldCenter) /
              max(uWeatherFieldWorldSize, 1.0) + vec2(0.5);
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
    {
        return clamp(uCloudCoverage, 0.0, 1.0);
    }
    float edge_distance = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    float edge_fade = smoothstep(0.0, 0.12, edge_distance);
    float regional = texture(uWeatherField, uv).r;
    return mix(clamp(uCloudCoverage, 0.0, 1.0), regional, edge_fade);
}

float cloudLayerOpticalDepth(vec3 ray, float layer_base, float thickness,
                             float coverage, float time_sec, out vec3 cloud_pos)
{
    cloud_pos = vec3(0.0);
  if (abs(ray.y) <= 0.001)
    {
        return 0.0;
    }
  float distance_a = (layer_base - uCameraPos.y) / ray.y;
  float distance_b = (layer_base + thickness - uCameraPos.y) / ray.y;
  float segment_start = max(0.0, min(distance_a, distance_b));
  float segment_end = max(distance_a, distance_b);
  if (segment_end <= segment_start)
    {
        return 0.0;
    }
  float distance_to_layer = 0.5 * (segment_start + segment_end);
  if (distance_to_layer > 18000.0)
  {
    return 0.0;
  }
    cloud_pos = uCameraPos + ray * distance_to_layer;
    coverage = sampleWeatherCoverage(cloud_pos.xz);
    if (coverage <= 0.001)
    {
        return 0.0;
    }
    float shape = cloudDensity(cloud_pos.xz, time_sec * 0.012, 0.0014);
    float threshold = mix(0.78, 0.36, clamp(coverage, 0.0, 1.0));
    float density = smoothstep(threshold, threshold + 0.22, shape);
  float optical_path = min((segment_end - segment_start) /
                               max(thickness, 1.0),
                           3.5);
  return density * coverage * 1.8 * optical_path;
}

vec3 horizonFogColor(vec3 view_dir)
{
    vec3 fog_col = uFogColor;
    if (uHorizonFogCelestialTint < 0.5)
    {
        return fog_col;
    }
    for (int i = 0; i < 4; ++i)
    {
        if (i >= uCelestialCount)
        {
            break;
        }
        vec3 dir = normalize(uCelestialDir[i]);
        if (dir.y <= 0.0)
        {
            continue;
        }
        float nd = clamp(dot(view_dir, dir), -1.0, 1.0);
        float cone = cos(max(1.5, uCelestialAngularSizeDeg[i]) * 0.35 * 0.0174532925);
        float prox = smoothstep(cone, 1.0, nd);
        vec3 body_col = uCelestialColor[i] * uCelestialIntensity[i];
        float tint_strength = (uCelestialType[i] == 1) ? 0.28 : 0.45;
        fog_col = mix(fog_col, body_col, prox * tint_strength);
    }
    return fog_col;
}

void main()
{
    if (uUnderwaterSkyAmount > 0.99) {
        FragColor = vec4(uUnderwaterFogColor, 1.0);
        return;
    }

    vec2 sky_uv = TexCoord * 2.0 - 1.0;
    vec4 view_near_h = uInvProjection * vec4(sky_uv, -1.0, 1.0);
    vec4 view_far_h = uInvProjection * vec4(sky_uv, 1.0, 1.0);
    vec3 view_near = view_near_h.xyz / max(abs(view_near_h.w), 1e-7);
    vec3 view_far = view_far_h.xyz / max(abs(view_far_h.w), 1e-7);
    vec3 dir_view = normalize(uPerspective > 0.5 ? view_near
                                                : view_far - view_near);
    vec3 view_dir = normalize(uInvViewRot * dir_view);

    vec3 skyTop = skyColor.rgb;
    vec3 skyBottom = skyColor.rgb * 1.3;
    float sky_t = clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 finalColor = mix(skyBottom, skyTop, sky_t);

    float below_waterline = 0.0;
    if (uScreenWaterlineNdc > -1.5) {
        below_waterline = step(TexCoord.y, uScreenWaterlineNdc);
    }

    vec3 dir_star = normalize(uStarCelestialInv * view_dir);
    float horizon_mask = smoothstep(-0.02, 0.10, view_dir.y);
    float elev_fade = smoothstep(0.00, 0.22, view_dir.y);
    float star_vis = pow(clamp(uStarVisibility, 0.0, 1.0), 1.2);
    vec3 stars = starFieldColor(dir_star);
    vec3 celestial_color = vec3(0.0);

    for (int i = 0; i < 4; ++i)
    {
        if (i >= uCelestialCount)
        {
            break;
        }
        vec3 dir = normalize(uCelestialDir[i]);
        float ang = max(1.5, uCelestialAngularSizeDeg[i]) * 0.0174532925;
        float nd = clamp(dot(view_dir, dir), -1.0, 1.0);
        float cos_ang = cos(ang);
        float cos_inner = cos(ang * 0.35);
        float disc = smoothstep(cos_ang, cos_inner, nd);
        float halo = smoothstep(cos(ang * 3.5), cos_inner, nd) * 0.55;
        vec3 body_col = uCelestialColor[i] * uCelestialIntensity[i];
        if (uCelestialType[i] == 1)
        {
            body_col *= vec3(0.75, 0.8, 0.95);
        }
        celestial_color += body_col * (disc + halo);
    }

    float cloud_cov = clamp(uCloudCoverage, 0.0, 1.0);
    float cloud_transmittance = 1.0;
    vec3 cloud_radiance = vec3(0.0);
    if (uWeatherFieldValid > 0.5 || cloud_cov > 0.001)
    {
        vec3 low_pos;
        vec3 high_pos;
        float low_tau = cloudLayerOpticalDepth(view_dir, 112.0, 46.0,
                                               cloud_cov, uElapsedSec, low_pos);
        float high_tau = cloudLayerOpticalDepth(view_dir, 205.0, 58.0,
                                                cloud_cov * 0.65,
                                                uElapsedSec * 0.83, high_pos);
        float low_alpha = 1.0 - exp(-low_tau);
        float high_alpha = 1.0 - exp(-high_tau);
        float sun_lighting = 0.0;
        vec3 sun_color = vec3(1.0, 0.95, 0.84);
        for (int i = 0; i < 4; ++i)
        {
            if (i >= uCelestialCount) break;
            if (uCelestialType[i] == 0)
            {
                sun_lighting = max(0.0, uCelestialDir[i].y) *
                               clamp(uCelestialIntensity[i], 0.0, 1.0);
                sun_color = uCelestialColor[i];
                break;
            }
        }
        vec3 cloud_day = mix(vec3(0.34, 0.38, 0.46),
                             vec3(0.78, 0.81, 0.84), sun_lighting);
        vec3 cloud_night = vec3(0.045, 0.055, 0.085);
        vec3 low_color = mix(cloud_night, cloud_day * sun_color, sun_lighting);
        vec3 high_color = mix(cloud_night * 1.2,
                              mix(vec3(0.62, 0.67, 0.75), cloud_day, sun_lighting),
                              sun_lighting);
        float cloud_mask = 1.0 - below_waterline;
        cloud_radiance += low_color * low_alpha * cloud_mask;
        cloud_transmittance *= 1.0 - low_alpha * cloud_mask;
        cloud_radiance += cloud_transmittance * high_color * high_alpha * cloud_mask;
        cloud_transmittance *= 1.0 - high_alpha * cloud_mask;
    }

    finalColor += (stars * star_vis * horizon_mask * elev_fade +
                   celestial_color) * (1.0 - below_waterline);
    finalColor = cloud_radiance + finalColor * cloud_transmittance;

    if (below_waterline > 0.5) {
        finalColor = uUnderwaterFogColor;
    }

    if (uFogHorizonBlend > 0.001) {
        float elev = clamp(view_dir.y, 0.0, 1.0);
        float horizon = uHorizonFogRadial > 0.5
            ? smoothstep(uFogHorizonElevation, 0.02, elev)
            : (1.0 - smoothstep(0.0, 0.45, TexCoord.y));
        vec3 fog_col = horizonFogColor(view_dir);
        finalColor = mix(finalColor, fog_col, horizon * uFogHorizonBlend);
    }

    FragColor = vec4(finalColor, 1.0);
}
