#version 300 es
precision mediump float;

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D texture0;
uniform mediump int uAnimFrame;
uniform mediump int uAnimFrameCount;

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
    FragColor = vec4(tex.rgb, 1.0);
}
