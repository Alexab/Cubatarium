#version 300 es
precision mediump float;
in vec2 uv;
out vec4 FragColor;

uniform sampler2D texture0;
// ShaderManager initializes these file defaults before publishing the program.
uniform vec4 uTint;

// Same sky/block light curve as the block shaders (fshader_greedy.glsl).
// uCreatureLitEnabled stays 0 for previews and effects: those draw unlit.
uniform float uCreatureLitEnabled;
uniform vec2 uCreatureLight; // x = sky 0..1, y = block 0..1
uniform float uEnvMinAmbient;
uniform float uEnvDayFactor;
uniform float uEnvNightFactor;
uniform float uEnvSkyLightScale;

// Distance fog matching the terrain block shaders (fshader_greedy.glsl):
// the fog factor is precomputed per creature on the CPU from the same fog
// range the world renders with, so mobs fade into the horizon instead of
// popping crisp beyond the fog wall. uFogEnabled stays 0 for previews and
// when distance fog is disabled in settings.
uniform float uCreatureFogFactor;
uniform vec3 uFogColor;
uniform float uFogEnabled;

vec3 applyCreatureFog(vec3 color)
{
    float fog = clamp(uCreatureFogFactor, 0.0, 1.0) * uFogEnabled;
    return mix(color, uFogColor, fog);
}

float creatureLit()
{
    if (uCreatureLitEnabled < 0.5) {
        return 1.0;
    }
    float sky01 = clamp(uCreatureLight.x, 0.0, 1.0);
    float block01 = clamp(uCreatureLight.y, 0.0, 1.0);
    float daySky = clamp(uEnvDayFactor * uEnvSkyLightScale, 0.0, 1.0);
    float nightSky = clamp(uEnvNightFactor * (1.0 - daySky), 0.0, 1.0);
    float dayAmbient = uEnvMinAmbient * (0.42 + 0.38 * daySky);
    float nightAmbient = uEnvMinAmbient * (0.18 + 0.32 * nightSky);
    float skyAmbient = mix(nightAmbient, dayAmbient, daySky);
    float skyMix = clamp(sky01 * daySky + sky01 * 0.18 * (1.0 - daySky), 0.0, 1.0);
    float skyLit = mix(skyAmbient, 1.0, skyMix);
    float blockAmbientFloor = clamp(uEnvMinAmbient * 0.04, 0.005, 0.1);
    float blockStrength = mix(0.9, 0.35, daySky);
    float blockLit = mix(blockAmbientFloor, blockStrength, block01);
    return clamp(max(skyLit, blockLit), 0.0, 1.0);
}

void main()
{
  vec4 tex = texture(texture0, uv);
  if (tex.a < 0.05)
    discard;
  FragColor = vec4(applyCreatureFog(tex.rgb * uTint.rgb * creatureLit()), 1.0);
}
