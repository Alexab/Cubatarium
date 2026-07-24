#version 300 es
precision mediump int;
precision mediump float;

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
uniform int uCloudSteps;
uniform float uCloudJitter;
uniform int uCelestialCount;
uniform vec3 uCelestialDir[4];
uniform vec3 uCelestialColor[4];
uniform float uCelestialIntensity[4];
uniform float uCelestialAngularSizeDeg[4];
uniform int uCelestialType[4];
uniform mat3 uInvViewRot;
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

vec2 octahedralEncode(vec3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    if (n.y >= 0.0)
    {
        return n.xz;
    }
    // Avoid GLSL sign(0)==0 collapsing the octahedral unwrap.
    vec2 s = vec2(n.x < 0.0 ? -1.0 : 1.0, n.z < 0.0 ? -1.0 : 1.0);
    return (vec2(1.0) - abs(n.zx)) * s;
}

vec3 starLayer(vec2 uv, float cell_scale, float threshold, float size,
               float mag_boost)
{
    vec2 st = uv * cell_scale + vec2(19.17, 78.3);
    vec2 cell = floor(st);
    vec2 f = fract(st) - 0.5;
    float rnd = hash12(cell);
    float rnd2 = hash12(cell + vec2(31.7, 7.1));
    float rnd3 = hash12(cell + vec2(4.2, 53.9));
    float mask = step(threshold, rnd);
    // Soft core + wider glow reduces temporal aliasing of sub-pixel dots.
    float d = length(f);
    float core = smoothstep(size, size * 0.25, d);
    float glow = smoothstep(size * 2.4, size * 0.85, d) * 0.40;
    float star = core + glow;
    float mag = pow(rnd2, 7.0) * mag_boost;
    vec3 cool = vec3(0.75, 0.82, 1.0);
    vec3 warm = vec3(1.0, 0.92, 0.78);
    vec3 col = mix(cool, warm, rnd3);
    return col * (star * mask * mag);
}

vec3 starFieldColor(vec3 dir_star)
{
    vec2 uv = octahedralEncode(dir_star);
    // Fewer, larger stars stay stable under camera / sidereal motion.
    vec3 dense = starLayer(uv, 160.0, 0.991, 0.085, 1.05);
    vec3 bright = starLayer(uv, 72.0, 0.9975, 0.12, 2.2);
    return dense + bright;
}

float cloudDensity(vec2 uv, float time_shift)
{
    vec2 p = uv + vec2(time_shift * 0.25, -time_shift * 0.08);
    float n0 = valueNoise(p * 4.0);
    float n1 = valueNoise(p * 9.0 + vec2(13.2, -7.1));
    return clamp(n0 * 0.72 + n1 * 0.28, 0.0, 1.0);
}

vec2 cloudHorizonUv(vec3 dir, vec2 camera_xz, vec2 wind, float ring_scale,
                    float elev_scale)
{
    vec2 horiz = normalize(dir.xz + vec2(1e-4));
    float elev = clamp(dir.y, 0.0, 1.0);
    return horiz * ring_scale + vec2(0.0, elev * elev_scale) +
           camera_xz * 0.0008 + wind * 0.55;
}

vec2 cloudWorldUv(vec3 dir, vec2 camera_xz, float world_scale, float parallax,
                  vec2 wind, float elev_w)
{
    float y_safe = mix(0.50, max(dir.y, 0.24), elev_w);
    vec2 proj = dir.xz / y_safe;
    float r2 = dot(proj, proj);
    proj *= inversesqrt(1.0 + r2 * 0.28);
    return camera_xz * world_scale + proj * parallax * elev_w + wind;
}

vec2 blendCloudUv(vec2 horizon_uv, vec2 world_uv, float elev_w)
{
    return mix(horizon_uv, world_uv, elev_w);
}

float cloudDensityWorld(vec3 world_pos, float time_shift)
{
    return cloudDensity(world_pos.xz * 0.0012, time_shift);
}

float layerProfile(float y, float center, float half_thickness)
{
    float dy = abs(y - center) / max(half_thickness, 1.0);
    return clamp(1.0 - dy, 0.0, 1.0);
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
    vec3 dir_view = normalize(vec3(sky_uv.x, sky_uv.y, -1.0));
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
    finalColor += stars * star_vis * horizon_mask * elev_fade * (1.0 - below_waterline);

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
        finalColor += body_col * (disc + halo) * (1.0 - below_waterline);
    }

    float cloud_cov = clamp(uCloudCoverage, 0.0, 1.0);
    if (cloud_cov > 0.001)
    {
        float elev = clamp(view_dir.y, 0.0, 1.0);
        float elev_w = smoothstep(0.22, 0.52, elev);
        float horizon_fade = smoothstep(0.06, 0.42, elev);

        vec2 wind_low = vec2(0.010, 0.004) * uElapsedSec;
        vec2 wind_high = vec2(0.016, -0.003) * uElapsedSec;
        vec2 horizon_low_uv =
            cloudHorizonUv(view_dir, uCameraPos.xz, wind_low, 5.5, 3.2);
        vec2 horizon_high_uv =
            cloudHorizonUv(view_dir, uCameraPos.xz, wind_high, 3.6, 2.6);
        vec2 world_low_uv =
            cloudWorldUv(view_dir, uCameraPos.xz, 0.0016, 0.42, wind_low, elev_w);
        vec2 world_high_uv =
            cloudWorldUv(view_dir, uCameraPos.xz, 0.0009, 0.65, wind_high, elev_w);
        vec2 low_uv = blendCloudUv(horizon_low_uv, world_low_uv, elev_w);
        vec2 high_uv = blendCloudUv(horizon_high_uv, world_high_uv, elev_w);

        float low_shape = cloudDensity(low_uv, uElapsedSec * 0.022);
        float low_detail =
            cloudDensity(low_uv * 2.1 + vec2(0.37, -0.22), uElapsedSec * 0.055) *
            elev_w;
        float low_cov =
            max(0.0, low_shape * 0.78 + low_detail * 0.30 - 0.40);

        float high_shape = cloudDensity(high_uv, uElapsedSec * 0.015);
        float high_cov = max(0.0, high_shape - 0.52);
        float cloud_low = smoothstep(0.10, 0.35, low_cov) * cloud_cov * horizon_fade;
        float cloud_high = smoothstep(0.04, 0.20, high_cov) * cloud_cov * horizon_fade;
        cloud_low *= mix(0.45, 1.0, elev_w);
        cloud_high *= mix(0.65, 1.0, elev_w);
        vec3 low_col = mix(vec3(0.66, 0.69, 0.74), vec3(0.94, 0.95, 0.98), clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
        vec3 high_col = mix(vec3(0.78, 0.81, 0.87), vec3(0.97, 0.98, 1.0), clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
        float cloud_mask = (1.0 - below_waterline);
        finalColor = mix(finalColor, high_col, cloud_high * 0.40 * cloud_mask);
        finalColor = mix(finalColor, low_col, cloud_low * 0.45 * cloud_mask);
    }

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
