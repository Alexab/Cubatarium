#version 300 es
precision mediump int;
precision mediump float;

in vec2 TexCoord;
out vec4 FragColor;

uniform vec4 skyColor;
uniform float uFogHorizonBlend;
uniform vec3 uFogColor;
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
uniform vec3 uCameraPos;

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

float starField(vec2 uv, float twinkle)
{
    vec2 st = uv * 320.0;
    vec2 cell = floor(st);
    vec2 f = fract(st) - 0.5;
    float rnd = hash12(cell);
    float star = smoothstep(0.06, 0.0, length(f));
    float mask = step(0.992, rnd);
    float tw = 0.75 + 0.25 * sin(twinkle + rnd * 17.0);
    return star * mask * tw;
}

float cloudDensity(vec2 uv, float time_shift)
{
    vec2 p = uv + vec2(time_shift * 0.25, -time_shift * 0.08);
    float n0 = valueNoise(p * 4.0);
    float n1 = valueNoise(p * 9.0 + vec2(13.2, -7.1));
    return clamp(n0 * 0.72 + n1 * 0.28, 0.0, 1.0);
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

void main()
{
    vec3 skyTop = skyColor.rgb;
    vec3 skyBottom = skyColor.rgb * 1.3;
    vec3 finalColor = mix(skyBottom, skyTop, TexCoord.y);
    vec2 sky_uv = TexCoord * 2.0 - 1.0;
    vec3 dir_view = normalize(vec3(sky_uv.x, sky_uv.y, -1.0));
    vec3 view_dir = normalize(uInvViewRot * dir_view);
    float night_factor = clamp(1.0 - (sin(uTimeOfDay * 6.28318530718) * 0.5 + 0.5), 0.0, 1.0);
    float stars = starField(TexCoord + vec2(0.0, uTimeOfDay * 0.12), uElapsedSec * 0.3);
    finalColor += vec3(stars) * (uStarVisibility * max(night_factor, 0.35));
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
        finalColor += body_col * (disc + halo);
    }
    float cloud_cov = clamp(uCloudCoverage, 0.0, 1.0);
    if (cloud_cov > 0.001)
    {
        vec2 wind_low = vec2(0.010, 0.004) * uElapsedSec;
        vec2 wind_high = vec2(0.016, -0.003) * uElapsedSec;
        float y_term = max(view_dir.y, 0.15);
        vec2 proj = view_dir.xz / y_term;
        proj = clamp(proj, vec2(-3.0), vec2(3.0));
        vec2 low_uv = uCameraPos.xz * 0.0016 + proj * 0.42 + wind_low * 0.8;
        vec2 high_uv = uCameraPos.xz * 0.0009 + proj * 0.65 + wind_high * 0.9;

        float low_shape = cloudDensity(low_uv, uElapsedSec * 0.022);
        float low_detail = cloudDensity(low_uv * 2.1 + vec2(0.37, -0.22), uElapsedSec * 0.055);
        float low_cov = max(0.0, low_shape * 0.78 + low_detail * 0.30 - 0.40);

        float high_shape = cloudDensity(high_uv, uElapsedSec * 0.015);
        float high_cov = max(0.0, high_shape - 0.52);
        float cloud_low = smoothstep(0.10, 0.35, low_cov) * cloud_cov;
        float cloud_high = smoothstep(0.04, 0.20, high_cov) * cloud_cov;
        float horizon_thick = 1.0 - smoothstep(0.0, 0.42, view_dir.y);
        cloud_low *= mix(1.25, 0.55, clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
        cloud_high *= mix(1.1, 0.75, clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
        vec3 low_col = mix(vec3(0.66, 0.69, 0.74), vec3(0.94, 0.95, 0.98), clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
        vec3 high_col = mix(vec3(0.78, 0.81, 0.87), vec3(0.97, 0.98, 1.0), clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
        finalColor = mix(finalColor, high_col, cloud_high * 0.28);
        finalColor = mix(finalColor, low_col, cloud_low * (0.30 + 0.20 * horizon_thick));
    }

    if (uFogHorizonBlend > 0.001) {
        float horizon = 1.0 - smoothstep(0.0, 0.45, TexCoord.y);
        finalColor = mix(finalColor, uFogColor, horizon * uFogHorizonBlend);
    }

    FragColor = vec4(finalColor, 1.0);
}
