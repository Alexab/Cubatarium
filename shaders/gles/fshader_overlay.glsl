#version 300 es
precision mediump float;
in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D texture0;
uniform int uAnimFrame;
uniform int uAnimFrameCount;
uniform vec3 uTintColor;
uniform float uTintAlpha;

void main()
{
    vec2 uv = TexCoord;
    if (uAnimFrameCount > 1) {
        float frameH = 1.0 / float(uAnimFrameCount);
        uv.y = uv.y * frameH + float(uAnimFrame) * frameH;
    }
    vec4 tex = texture(texture0, uv);
    FragColor = vec4(mix(tex.rgb, uTintColor, uTintAlpha), tex.a * uTintAlpha);
}
