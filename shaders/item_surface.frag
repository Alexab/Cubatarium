#version 330 core
in vec2 vUv;
in vec3 vNormal;
in vec3 vPosition;
out vec4 FragColor;
uniform sampler2D uAlbedo;
uniform vec4 uColor;
uniform vec3 uCamera;
uniform vec3 uLightDirection;
uniform vec3 uFogColor;
uniform float uAmbient;
uniform float uLightIntensity;
uniform float uFog;
uniform float uMetallic;
uniform float uRoughness;
uniform vec3 uEmissive;
uniform int uInspectionMode;
vec3 linearColor(vec3 c)
{
    return mix(c/12.92,pow((c+0.055)/1.055,vec3(2.4)),step(vec3(0.04045),c));
}
vec3 displayColor(vec3 c)
{
    return mix(c*12.92,1.055*pow(max(c,vec3(0.0)),vec3(1.0/2.4))-0.055,step(vec3(0.0031308),c));
}
void main()
{
    vec4 sampleColor=texture(uAlbedo,vUv);
    // The item-only sRGB texture decodes before filtering and mip sampling.
    vec3 albedo=sampleColor.rgb*uColor.rgb;
    vec3 n=normalize(vNormal);
    vec3 l=normalize(uLightDirection);
    vec3 viewDirection=normalize(uCamera-vPosition);
    vec3 halfDirection=normalize(l+viewDirection);
    float diffuse=max(dot(n,l),0.0);
    float roughness=clamp(uRoughness,0.1,1.0);
    float highlight=pow(max(dot(n,halfDirection),0.0),mix(64.0,8.0,roughness));
    vec3 specular=mix(vec3(0.035),albedo*0.16,uMetallic)*highlight*(1.0-roughness*0.65);
    vec3 lit=albedo*(clamp(uAmbient,0.0,1.0)+(1.0-clamp(uAmbient,0.0,1.0))*diffuse)+specular;
    vec3 finalColor=mix(lit*max(uLightIntensity,0.0)+uEmissive,linearColor(uFogColor),clamp(uFog,0.0,1.0));
    if(uInspectionMode==1) finalColor=vec3(0.005);
    if(uInspectionMode==2) finalColor=vec3(0.35)*(clamp(uAmbient,0.0,1.0)+(1.0-clamp(uAmbient,0.0,1.0))*diffuse);
    if(uInspectionMode==3) finalColor=albedo;
    if(uInspectionMode==4) {
        vec2 cell=floor(vUv*32.0);
        finalColor=mod(cell.x+cell.y,2.0)<1.0?vec3(0.8):vec3(0.04);
    }
    if(uInspectionMode==5) { FragColor=vec4(n*0.5+0.5,1.0); return; }
    // The current item material subset is OPAQUE: alpha does not blend.
    FragColor=vec4(displayColor(finalColor),1.0);
}
