#version 300 es
precision mediump float;

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D texture0;
uniform mediump int uAnimFrame;
uniform mediump int uAnimFrameCount;

// Same sky/block light curve as the block shaders (fshader_greedy.glsl).
// uCreatureLitEnabled stays 0 for previews and effects: those draw unlit.
uniform float uCreatureLitEnabled = 0.0;
uniform vec2 uCreatureLight = vec2(1.0, 0.0); // x = sky 0..1, y = block 0..1
uniform float uEnvMinAmbient = 0.12;
uniform float uEnvDayFactor = 1.0;
uniform float uEnvNightFactor = 0.0;
uniform float uEnvSkyLightScale = 1.0;

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
    vec2 uv = TexCoord;
    if (uAnimFrameCount > 1) {
        float frameH = 1.0 / float(uAnimFrameCount);
        uv.y = uv.y * frameH + float(uAnimFrame) * frameH;
    }
    vec4 tex = texture(texture0, uv);
    if (tex.a < 0.05)
        discard;
    FragColor = vec4(tex.rgb * creatureLit(), 1.0);
}
