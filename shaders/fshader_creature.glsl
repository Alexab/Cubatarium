#version 330 core

in vec2 TexCoord;
out vec4 FragColor;

uniform sampler2D texture0;
uniform vec4 uTint = vec4(1.0);
uniform int uAnimFrame;
uniform int uAnimFrameCount;

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
    FragColor = vec4(tex.rgb * uTint.rgb, tex.a * uTint.a);
}
