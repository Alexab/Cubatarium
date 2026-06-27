#ifdef GL_ES
// Set default precision to medium
precision mediump int;
precision mediump float;
#endif

varying vec2 v_texcoord;

uniform vec4 skyColor;
uniform float uFogHorizonBlend;
uniform vec3 uFogColor;

void main()
{
    vec3 skyTop = skyColor.rgb;
    vec3 skyBottom = skyColor.rgb * 1.3;
    vec3 finalColor = mix(skyBottom, skyTop, v_texcoord.y);

    if (uFogHorizonBlend > 0.001) {
        float horizon = 1.0 - smoothstep(0.0, 0.45, v_texcoord.y);
        finalColor = mix(finalColor, uFogColor, horizon * uFogHorizonBlend);
    }

    gl_FragColor = vec4(finalColor, 1.0);
}
