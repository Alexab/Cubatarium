#version 330 core

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

float hash12(vec2 p)
{
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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
    float d = 0.0;
    float amp = 0.55;
    vec2 p = uv * 1.8;
    for (int i = 0; i < 5; ++i)
    {
        d += amp * (hash12(floor(p * 64.0 + time_shift)) - 0.5);
        p = p * 1.93 + vec2(3.1, 1.7);
        amp *= 0.55;
    }
    return clamp(d + 0.5, 0.0, 1.0);
}

void main()
{
    vec3 skyTop = skyColor.rgb;
    vec3 skyBottom = skyColor.rgb * 1.3;
    vec3 finalColor = mix(skyBottom, skyTop, TexCoord.y);
    vec2 sky_uv = TexCoord * 2.0 - 1.0;
    vec3 view_dir = normalize(vec3(sky_uv.x, max(-0.35, sky_uv.y), 1.25));

    float night_factor = clamp(1.0 - (sin(uTimeOfDay * 6.28318530718) * 0.5 + 0.5), 0.0, 1.0);
    float stars = starField(TexCoord + vec2(0.0, uTimeOfDay * 0.12), uElapsedSec * 0.3);
    finalColor += vec3(stars) * (uStarVisibility * night_factor);

    for (int i = 0; i < 4; ++i)
    {
        if (i >= uCelestialCount)
        {
            break;
        }
        vec3 dir = normalize(uCelestialDir[i]);
        float ang = max(0.05, uCelestialAngularSizeDeg[i]) * 0.0174532925;
        float d = acos(clamp(dot(view_dir, dir), -1.0, 1.0));
        float disc = smoothstep(ang, ang * 0.6, d);
        float halo = smoothstep(ang * 2.8, ang * 0.6, d) * 0.35;
        vec3 body_col = uCelestialColor[i] * uCelestialIntensity[i];
        if (uCelestialType[i] == 1)
        {
            body_col *= vec3(0.75, 0.8, 0.95);
        }
        finalColor += body_col * (disc + halo);
    }

    int steps = clamp(uCloudSteps, 2, 24);
    float acc = 0.0;
    float weight = 0.0;
    vec2 wind = vec2(0.01, 0.004) * uElapsedSec;
    for (int i = 0; i < 24; ++i)
    {
        if (i >= steps)
        {
            break;
        }
        float t = (float(i) + hash12(TexCoord * 4096.0) * uCloudJitter) / float(max(steps, 1));
        vec2 sample_uv = TexCoord + wind + vec2(t * 0.25, t * 0.08);
        float den = cloudDensity(sample_uv, uElapsedSec * 0.03);
        float w = 1.0 - t;
        acc += den * w;
        weight += w;
    }
    float cloud = weight > 0.0 ? acc / weight : 0.0;
    cloud = smoothstep(0.45, 0.85, cloud) * clamp(uCloudCoverage, 0.0, 1.0);
    vec3 cloud_col = mix(vec3(0.55, 0.58, 0.62), vec3(0.9, 0.92, 0.95), clamp(view_dir.y * 0.5 + 0.5, 0.0, 1.0));
    finalColor = mix(finalColor, cloud_col, cloud * 0.75);

    if (uFogHorizonBlend > 0.001) {
        float horizon = 1.0 - smoothstep(0.0, 0.45, TexCoord.y);
        finalColor = mix(finalColor, uFogColor, horizon * uFogHorizonBlend);
    }

    FragColor = vec4(finalColor, 1.0);
}
