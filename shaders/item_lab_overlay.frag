#version 330 core
in vec4 vColor;
in vec2 vUv;
uniform sampler2D uAlbedo;
uniform int uTextured;
out vec4 FragColor;
vec3 displayColor(vec3 c)
{
    return mix(c * 12.92, 1.055 * pow(max(c, vec3(0.0)), vec3(1.0 / 2.4)) - 0.055,
               step(vec3(0.0031308), c));
}
void main()
{
    if (uTextured == 0) {
        FragColor = vColor;
    } else {
        vec4 texel = texture(uAlbedo, vUv);
        float checker = mod(floor(vUv.x * 32.0) + floor(vUv.y * 32.0), 2.0);
        vec3 background = mix(vec3(0.16), vec3(0.23), checker);
        FragColor = vec4(mix(background, displayColor(texel.rgb), texel.a), 1.0);
    }
}
